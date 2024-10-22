/**
 * 头文件包含规范
 * 1.本类的声明（第一个包含本类h文件，有效减少依赖）
 * 2.C系统文件
 * 3.C++系统文件
 * 4.其他库头文件
 * 5.本项目内头文件
 */

// 项目内头文件
#include "util/util.h"
#include "util/util_pdu.h"
#include "util/dlog.h"
#include "protocol/http_parser_wrapper.h"
#include "network/netlib.h"
#include "thread/thread_pool.h"

// C系统文件
#ifdef __APPLE__
#include <sys/socket.h>
#else
#include <sys/sendfile.h>  // 用于高效传输文件
#endif

// C++系统文件
#include <list>
#include <mutex>
#include <filesystem>
#include <iostream>
#include <unordered_map>

// 使用longkit命名空间
using namespace longkit;

// 定义HTTP连接超时时间（毫秒）
#define HTTP_CONN_TIMEOUT 60000

// 定义读取缓冲区大小
#define READ_BUF_SIZE 2048

// 定义HTTP JSON响应格式
#define HTTP_RESPONSE_JSON                                                     \
    "HTTP/1.1 200 OK\r\n"                                                      \
    "Connection:close\r\n"                                                     \
    "Content-Length:%d\r\n"                                                    \
    "Content-Type:application/json;charset=utf-8\r\n\r\n%s"

// 定义HTTP JSON响应最大长度
#define HTTP_RESPONSE_JSON_MAX 4096

// 定义HTTP HTML响应格式
#define HTTP_RESPONSE_HTML                                                   \
    "HTTP/1.1 200 OK\r\n"                                                      \
    "Connection:close\r\n"                                                     \
    "Content-Length:%d\r\n"                                                    \
    "Content-Type:text/html;charset=utf-8\r\n\r\n%s"

// 定义HTTP HTML响应最大长度
#define HTTP_RESPONSE_HTML_MAX 4096

// 静态HTML缓冲区
static char s_html_buf[HTTP_RESPONSE_HTML_MAX - 200] = {0};
// 静态HTML格式化缓冲区
static char s_html_sprintf_buf[HTTP_RESPONSE_HTML_MAX] = {0};

// 定义连接状态枚举
enum {
    CONN_STATE_IDLE,        // 空闲状态
    CONN_STATE_CONNECTED,   // 已连接状态
    CONN_STATE_OPEN,        // 打开状态
    CONN_STATE_CLOSED,      // 关闭状态
};

// 前向声明CHttpConn类
class CHttpConn;

// conn_handle 从0开始递增，可以防止因socket handle重用引起的一些冲突
static uint32_t g_conn_handle_generator = 0;

// 定义UUID到CHttpConn指针的映射类型
typedef std::unordered_map<uint32_t, CHttpConn *> UserMap_t;
// 全局UUID到CHttpConn指针的映射
static UserMap_t g_uuid_conn_map;

// UUID分配器
uint32_t  s_uuid_alloctor = 0;

// 定义连接句柄到CHttpConn指针的映射类型
typedef std::unordered_map<uint32_t, CHttpConn *> HttpConnMap_t;

// 全局连接句柄到CHttpConn指针的映射
static HttpConnMap_t g_http_conn_map;
// 全局线程池
ThreadPool g_thread_pool;

// HTTP连接回调函数声明
void httpconn_callback(void *callback_data, uint8_t msg, uint32_t handle,
                       uint32_t uParam, void *pParam);

// 根据句柄查找HTTP连接
CHttpConn *FindHttpConnByHandle(uint32_t handle);
// HTTP连接定时器回调函数声明
void http_conn_timer_callback(void *callback_data, uint8_t msg, uint32_t handle,
                              void *pParam);
// 根据UUID查找HTTP连接
CHttpConn *GetHttpConnByUuid(uint32_t uuid);

// 定义响应PDU结构体
typedef struct 
{
    uint32_t conn_uuid; // 用于查找connection
    string resp_data;   // 要回发的数据
} ResponsePdu_t;

// HTTP连接类定义
class CHttpConn : public CRefObject 
{
  public:
    // 构造函数
    CHttpConn(): CRefObject(2){
        busy_ = false;
        m_sock_handle = NETLIB_INVALID_HANDLE;
        state_ = CONN_STATE_IDLE;

        last_send_tick_ = last_recv_tick_ = GetTickCount();
        conn_handle_ = ++g_conn_handle_generator;
        if (conn_handle_ == 0) {
            conn_handle_ = ++g_conn_handle_generator;
        }

        uuid_ = ++s_uuid_alloctor; // 单线程里用的，不需要加锁
        if (uuid_ == 0) {
            uuid_ = ++s_uuid_alloctor;
        }
        g_uuid_conn_map.insert(make_pair(uuid_, this)); // 单线程里用的，不需要加锁
        LogDebug("conn_uuid: {}, conn_handle_: {:X}", uuid_, conn_handle_);
    }

    // 析构函数
    virtual ~CHttpConn(){
        LogDebug("~CHttpConn, conn_handle_ = {}", conn_handle_);
    }
    
    // 获取连接句柄
    uint32_t GetConnHandle() { return conn_handle_; }
    // 获取对端IP
    char *GetPeerIP() { return (char *)peer_ip_.c_str(); }

    // 发送数据
    int Send(void *data, int len){
        last_send_tick_ = GetTickCount();

        if (busy_) {
            out_buf_.Write(data, len);
            return len;
        }

        int ret = netlib_send(m_sock_handle, data, len);
        if (ret < 0)
            ret = 0;

        if (ret < len) {
            out_buf_.Write(
                (char *)data + ret,
                len - ret); // 保存buffer里面，下次reactor write触发后再发送
            busy_ = true;
            LogDebug("not send all, remain= {}", out_buf_.GetWriteOffset());
        } else {
            OnWriteComlete();
        }

        return len;
    }

    // 关闭连接
    void Close() {
        state_ = CONN_STATE_CLOSED;

        g_http_conn_map.erase(conn_handle_);
        g_uuid_conn_map.erase(uuid_); // 移除uuid
        netlib_close(m_sock_handle);

        ReleaseRef();
    }

    // 连接建立时的处理
    void OnConnect(net_handle_t handle){
        LogDebug("CHttpConn, handle = {}", handle);
        m_sock_handle = handle;
        state_ = CONN_STATE_CONNECTED;
        g_http_conn_map.insert(make_pair(conn_handle_, this));

        netlib_option(handle, NETLIB_OPT_SET_CALLBACK, (void *)httpconn_callback);
        netlib_option(handle, NETLIB_OPT_SET_CALLBACK_DATA,
                    reinterpret_cast<void *>(conn_handle_));
        netlib_option(handle, NETLIB_OPT_GET_REMOTE_IP, (void *)&peer_ip_);
    }

    // 读取数据
    void OnRead(){
        for (;;) {
            uint32_t free_buf_len =
                in_buf_.GetAllocSize() - in_buf_.GetWriteOffset();
            if (free_buf_len < READ_BUF_SIZE + 1)
                in_buf_.Extend(READ_BUF_SIZE + 1);

            int ret = netlib_recv(m_sock_handle,
                                in_buf_.GetBuffer() + in_buf_.GetWriteOffset(),
                                READ_BUF_SIZE);
            if (ret <= 0)
                break;

            in_buf_.IncWriteOffset(ret);

            last_recv_tick_ = GetTickCount();
        }

        // 每次请求对应一个HTTP连接，所以读完数据后，不用在同一个连接里面准备读取下个请求
        char *in_buf = (char *)in_buf_.GetBuffer();
        uint32_t buf_len = in_buf_.GetWriteOffset();
        in_buf[buf_len] = '\0';

        // 如果buf_len 过长可能是受到攻击，则断开连接
        // 正常的url最大长度为2048，我们接受的所有数据长度不得大于2K
        if (buf_len > 2048) {
            LogError("get too much data: {}", in_buf);
            Close();
            return;
        }

        LogDebug("buf_len: {}, conn_handle_: {}, in_buf: {}", buf_len, conn_handle_, in_buf);
        // 解析http数据
        http_parser_.ParseHttpContent(in_buf, buf_len);
        if (http_parser_.IsReadAll()) {
            string url = http_parser_.GetUrl();
            string content = http_parser_.GetBodyContent();
            LogInfo("url: {}", url);                     // for debug
            if (strncmp(url.c_str(), "/json", 5) == 0) { // 返回json字符串
                // 回复网页
                string  str_json =  "{\"darren\":\"king\"}";
                char *str_content = new char[HTTP_RESPONSE_JSON_MAX];
                uint32_t ulen = str_json.length();
                snprintf(str_content, HTTP_RESPONSE_JSON_MAX, HTTP_RESPONSE_JSON, ulen,
                        str_json.c_str());
                str_json = str_content;
                int ret = Send((void *)str_content, strlen(str_content));
            } else  if (strncmp(url.c_str(), "/html2", 6) == 0) {
                g_thread_pool.exec(std::bind(&CHttpConn::_HandleHtmlRequest2, this, std::placeholders::_1), uuid_);
            }else  if (strncmp(url.c_str(), "/html", 5) == 0) {
                g_thread_pool.exec(std::bind(&CHttpConn::_HandleHtmlRequest, this, std::placeholders::_1), uuid_);                
            } 
            else {
                // LogInfo("no handle {}", url.c_str);
            }  
        }
    }

    // 写入数据
    void OnWrite(){
        if (!busy_)
            return;

        int ret = netlib_send(m_sock_handle, out_buf_.GetBuffer(),
                            out_buf_.GetWriteOffset());
        if (ret < 0)
            ret = 0;

        int out_buf_size = (int)out_buf_.GetWriteOffset();

        out_buf_.Read(NULL, ret);

        if (ret < out_buf_size) {
            busy_ = true;
            LogInfo("not send all, remain = {}", out_buf_.GetWriteOffset());
        } else {
            OnWriteComlete();
            busy_ = false;
        }
    }

    // 关闭连接
    void OnClose() { Close(); }

    // 定时器处理
    void OnTimer(uint64_t curr_tick) {
        if (curr_tick > last_recv_tick_ + HTTP_CONN_TIMEOUT) {
            LogWarn("HttpConn timeout, handle={}", conn_handle_);
            Close();
        }
    }

    // 写入完成处理
    void OnWriteComlete() {
        LogDebug("write complete");
        Close();
    }

    // 处理HTML请求
    void _HandleHtmlRequest(uint32_t uuid) {
        char *str_content = new char[HTTP_RESPONSE_HTML_MAX];
        uint32_t ulen = strlen(s_html_buf);
        snprintf(str_content, HTTP_RESPONSE_HTML_MAX, HTTP_RESPONSE_HTML, ulen,
                s_html_buf);
        std::string str_html = str_content;
        CHttpConn::AddResponseData(uuid, str_html);
        delete[] str_content;
     }

    // 处理HTML请求（优化版）
    void _HandleHtmlRequest2(uint32_t uuid) {
        std::string str_html = s_html_sprintf_buf;
        CHttpConn::AddResponseData(uuid, str_html);
     }

    // 添加响应数据（静态方法，工作线程调用）
    static void AddResponseData(uint32_t conn_uuid,
                                string &resp_data);
    // 发送响应数据列表（静态方法，主线程调用）
    static void SendResponseDataList();

  private:
     

  protected:
    net_handle_t m_sock_handle;     // 网络句柄
    uint32_t conn_handle_;          // 连接句柄
    bool busy_;                     // 是否忙碌

    uint32_t state_;                // 连接状态
    std::string peer_ip_;           // 对端IP
    uint16_t peer_port_;            // 对端端口
    CSimpleBuffer in_buf_;          // 输入缓冲区
    CSimpleBuffer out_buf_;         // 输出缓冲区

    uint64_t last_send_tick_;       // 上次发送��间
    uint64_t last_recv_tick_;       // 上次接收时间

    CHttpParserWrapper http_parser_; // HTTP解析器
 
    uint32_t uuid_;                  // 自己的uuid

    static std::mutex s_resp_mutex;
    static std::list<ResponsePdu_t *> s_response_pdu_list; // 主线程发送回复消息
};

// 静态成员初始化
std::list<ResponsePdu_t *> CHttpConn::s_response_pdu_list;
std::mutex CHttpConn::s_resp_mutex;

// 添加响应数据
void CHttpConn::AddResponseData(uint32_t conn_uuid, string &resp_data) {
    LogDebug("into");
    ResponsePdu_t *pResp = new ResponsePdu_t;
    pResp->conn_uuid = conn_uuid;
    pResp->resp_data = std::move(resp_data);

    s_resp_mutex.lock();
    s_response_pdu_list.push_back(pResp);
    s_resp_mutex.unlock();
}

// 发送响应数据列表
void CHttpConn::SendResponseDataList() {
    LogDebug("into");
    // 发送数据
    s_resp_mutex.lock();
    while (!s_response_pdu_list.empty()) {
        ResponsePdu_t *pResp = s_response_pdu_list.front();
        s_response_pdu_list.pop_front();
        s_resp_mutex.unlock();
        CHttpConn *pConn = GetHttpConnByUuid(
            pResp->conn_uuid); // 该连接有可能已经被释放，如果被释放则返回NULL
        if (pConn) {
            pConn->Send((void *)pResp->resp_data.c_str(),
                        pResp->resp_data.size());  // 最终socket send
        }
        delete pResp;

        s_resp_mutex.lock();
    }

    s_resp_mutex.unlock();
}

// HTTP连接回调函数
void httpconn_callback(void *callback_data, uint8_t msg, uint32_t handle,
                       uint32_t uParam, void *pParam) {
    NOTUSED_ARG(uParam);
    NOTUSED_ARG(pParam);

    // convert void* to uint32_t, oops
    uint32_t conn_handle = *((uint32_t *)(&callback_data));
    CHttpConn *pConn = FindHttpConnByHandle(conn_handle);
    if (!pConn) {
        return;
    }

    switch (msg) {
    case NETLIB_MSG_READ:
        pConn->OnRead();
        break;
    case NETLIB_MSG_WRITE:
        pConn->OnWrite();
        break;
    case NETLIB_MSG_CLOSE:
        pConn->OnClose();
        break;
    default:
        LogError("!!!httpconn_callback error msg:{}", msg);
        
        break;
    }
}
 
// 根据句柄查找HTTP连接
CHttpConn *FindHttpConnByHandle(uint32_t handle) {
     CHttpConn *pConn = NULL;
    HttpConnMap_t::iterator it = g_http_conn_map.find(handle);
    if (it != g_http_conn_map.end()) {
        pConn = it->second;
    }

    return pConn;
}

// HTTP连接定时器回调函数
void http_conn_timer_callback(void *callback_data, uint8_t msg, uint32_t handle,
                              void *pParam) {
    UNUSED(pParam);
    CHttpConn *pConn = NULL;
    HttpConnMap_t::iterator it, it_old;
    uint64_t cur_time = GetTickCount();

    for (it = g_http_conn_map.begin(); it != g_http_conn_map.end();) {
        it_old = it;
        it++;

        pConn = it_old->second;
        pConn->OnTimer(cur_time);
    }
}

// 初始化HTTP连接
void InitHttpConn() {
    netlib_register_timer(http_conn_timer_callback, NULL, 1000);
}

// 根据UUID获取HTTP连接
CHttpConn *GetHttpConnByUuid(uint32_t uuid) {
    CHttpConn *pConn = NULL;
    UserMap_t::iterator it = g_uuid_conn_map.find(uuid);
    if (it != g_uuid_conn_map.end()) {
        pConn = (CHttpConn *)it->second;
    }

    return pConn;
}

// HTTP回调函数
void http_callback(void *callback_data, uint8_t msg, uint32_t handle,
                   void *pParam) {
     
    if (msg == NETLIB_MSG_CONNECT) {
        // 这里是不是觉得很奇怪,为什么new了对象却没有释放?
        // 实际上对象在被Close时使用delete this的方式释放自己
        CHttpConn *pConn = new CHttpConn();
        pConn->OnConnect(handle);
    } else {
        LogError("!!!error msg:{}", msg);
    }
}

// HTTP循环回调函数
void http_loop_callback(void *callback_data, uint8_t msg, uint32_t handle,
                        void *pParam) {
    UNUSED(callback_data);
    UNUSED(msg);
    UNUSED(handle);
    UNUSED(pParam);
    CHttpConn::SendResponseDataList(); // 静态函数, 将要发送的数据循环发给客户端
}

// 初始化HTTP连接
int initHttpConn(uint32_t thread_num) {
    g_thread_pool.init(thread_num); // 初始化线程数量
    g_thread_pool.start();          // 启动多线程
    netlib_add_loop(http_loop_callback,NULL); // http_loop_callback被epoll所在线程循环调用
    return 0;
}

// 主函数
int main(int argc, char *argv[]) {
    signal(SIGPIPE, SIG_IGN);
    int ret = 0;
    if(argc == 1)
        DLog::SetLevel("warn");
    else 
        DLog::SetLevel(argv[1]);

    LogInfo("main into"); //单例模式 日志库 spdlog
    LogInfo("main into2 {}", "darren"); // 只是为了测试日志

    std::cout << "Current working directory: " << std::filesystem::current_path() << std::endl;
    std::cout << "Attempting to open file: " << "hello.html" << std::endl;

    // 读取HTML文件
    int fd = open("hello.html", O_RDONLY);
    if (fd < 0) {
        std::cerr << "Failed to open hello.html. Error: " << strerror(errno) << std::endl;
        exit(1);
    }

    ret = read(fd, s_html_buf, sizeof(s_html_buf) - 1);
    if (ret < 0) {
        std::cerr << "Failed to read from hello.html. Error: " << strerror(errno) << std::endl;
        close(fd);
        exit(1);
    } else if (ret == 0) {
        std::cerr << "hello.html is empty" << std::endl;
        close(fd);
        exit(1);
    } else {
        s_html_buf[ret] = '\0';  // Null-terminate the string
        std::cout << "Successfully read " << ret << " bytes from hello.html" << std::endl;
    }
    close(fd);

    uint32_t ulen = strlen(s_html_buf);
    snprintf(s_html_sprintf_buf, HTTP_RESPONSE_HTML_MAX, HTTP_RESPONSE_HTML, ulen,
            s_html_buf);
    
    // 设置线程数
    int thread_num = 2;
    if(argc >= 3) {
        thread_num = atoi(argv[2]);
    }

    // 设置HTTP服务器端口和IP
    uint16_t http_port = 8080;
    string http_listen_ip = "0.0.0.0";
    ret = netlib_init();
    if (ret == NETLIB_ERROR) {
        LogError("netlib_init failed");
        return ret;
    }

    // 监听HTTP端口
    ret = netlib_listen(http_listen_ip.c_str(), http_port, http_callback, NULL);
    if (ret == NETLIB_ERROR)
        return ret;
   
    initHttpConn(thread_num);

    LogInfo("server start listen on:For http://{}:{}", http_listen_ip, http_port);

    LogInfo("now enter the event loop...");

    WritePid();
    // 超时参数影响回发客户端的时间
    netlib_eventloop(1);

    return 0;
}
