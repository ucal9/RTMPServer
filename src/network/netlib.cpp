// 包含必要的头文件
#include "netlib.h"
#include "base_socket.h"
#include "event_dispatch.h"
#include "util/dlog.h"

// 初始化网络库
// 返回值: NETLIB_OK 表示成功，NETLIB_ERROR 表示失败
int netlib_init() {
    int ret = NETLIB_OK;
#ifdef _WIN32
    // Windows平台特定的初始化
    WSADATA wsaData;
    WORD wReqest = MAKEWORD(1, 1);  // 请求使用的WinSock版本
    if (WSAStartup(wReqest, &wsaData) != 0) {
        ret = NETLIB_ERROR;
    }
#endif

    return ret;
}

// 销毁网络库，释放资源
// 返回值: NETLIB_OK 表示成功，NETLIB_ERROR 表示失败
int netlib_destroy() {
    int ret = NETLIB_OK;
#ifdef _WIN32
    // Windows平台特定的清理
    if (WSACleanup() != 0) {
        ret = NETLIB_ERROR;
    }
#endif

    return ret;
}

// 在指定IP和端口上监听连接
// 参数:
//   server_ip: 服务器IP地址
//   port: 监听端口
//   callback: 连接回调函数
//   callback_data: 回调函数的用户数据
// 返回值: NETLIB_OK 表示成功，NETLIB_ERROR 表示失败
int netlib_listen(const char *server_ip, uint16_t port, callback_t callback,
                  void *callback_data) {
    CBaseSocket *pSocket = new CBaseSocket();
    if (!pSocket)
    {
        LogError("Failed to create CBaseSocket object");
        return NETLIB_ERROR;
    }

    int ret = pSocket->Listen(server_ip, port, callback, callback_data);
    if (ret == NETLIB_ERROR)
    {
        LogError("CBaseSocket::Listen failed, errno: {}, error: {}", errno, strerror(errno));
        delete pSocket;
    }
    return ret;
}

// 连接到指定IP和端口
// 参数:
//   server_ip: 服务器IP地址
//   port: 服务器端口
//   callback: 连接回调函数
//   callback_data: 回调函数的用户数据
// 返回值: 成功返回有效的网络句柄，失败返回NETLIB_INVALID_HANDLE
net_handle_t netlib_connect(const char *server_ip, uint16_t port,
                            callback_t callback, void *callback_data) {
    CBaseSocket *pSocket = new CBaseSocket();
    if (!pSocket)
        return NETLIB_INVALID_HANDLE;

    net_handle_t handle =
        pSocket->Connect(server_ip, port, callback, callback_data);
    if (handle == NETLIB_INVALID_HANDLE)
        delete pSocket;
    return handle;
}

// 发送数据
// 参数:
//   handle: 网络句柄
//   buf: 发送数据缓冲区
//   len: 要发送的数据长度
// 返回值: 成功发送的字节数，失败返回NETLIB_ERROR
int netlib_send(net_handle_t handle, void *buf, int len) {
    CBaseSocket *pSocket = FindBaseSocket(handle);
    if (!pSocket) {
        return NETLIB_ERROR;
    }
    int ret = pSocket->Send(buf, len);
    pSocket->ReleaseRef();
    return ret;
}

// 接收数据
// 参数:
//   handle: 网络句柄
//   buf: 接收数据缓冲区
//   len: 缓冲区大小
// 返回值: 实际接收的字节数，失败返回NETLIB_ERROR
int netlib_recv(net_handle_t handle, void *buf, int len) {
    CBaseSocket *pSocket = FindBaseSocket(handle);
    if (!pSocket)
        return NETLIB_ERROR;

    int ret = pSocket->Recv(buf, len);
    pSocket->ReleaseRef();
    return ret;
}

// 关闭连接
// 参数:
//   handle: 要关闭的网络句柄
// 返回值: NETLIB_OK 表示成功，NETLIB_ERROR 表示失败
int netlib_close(net_handle_t handle) {
    CBaseSocket *pSocket = FindBaseSocket(handle);
    if (!pSocket)
        return NETLIB_ERROR;

    int ret = pSocket->Close();
    pSocket->ReleaseRef();
    return ret;
}

// 设置或获取socket选项
// 参数:
//   handle: 网络句柄
//   opt: 选项类型（如NETLIB_OPT_SET_CALLBACK, NETLIB_OPT_GET_REMOTE_IP等）
//   optval: 选项值的指针
// 返回值: NETLIB_OK 表示成功，NETLIB_ERROR 表示失败
int netlib_option(net_handle_t handle, int opt, void *optval) {
    CBaseSocket *pSocket = FindBaseSocket(handle);
    if (!pSocket)
        return NETLIB_ERROR;

    if ((opt >= NETLIB_OPT_GET_REMOTE_IP) && !optval)
        return NETLIB_ERROR;

    switch (opt) {
    case NETLIB_OPT_SET_CALLBACK:
        pSocket->SetCallback((callback_t)optval);
        break;
    case NETLIB_OPT_SET_CALLBACK_DATA:
        pSocket->SetCallbackData(optval);
        break;
    case NETLIB_OPT_GET_REMOTE_IP:
        *(string *)optval = pSocket->GetRemoteIP();
        break;
    case NETLIB_OPT_GET_REMOTE_PORT:
        *(uint16_t *)optval = pSocket->GetRemotePort();
        break;
    case NETLIB_OPT_GET_LOCAL_IP:
        *(string *)optval = pSocket->GetLocalIP();
        break;
    case NETLIB_OPT_GET_LOCAL_PORT:
        *(uint16_t *)optval = pSocket->GetLocalPort();
        break;
    case NETLIB_OPT_SET_SEND_BUF_SIZE:
        pSocket->SetSendBufSize(*(uint32_t *)optval);
        break;
    case NETLIB_OPT_SET_RECV_BUF_SIZE:
        pSocket->SetRecvBufSize(*(uint32_t *)optval);
        break;
    }

    pSocket->ReleaseRef();
    return NETLIB_OK;
}

// 注册定时器
// 参数:
//   callback: 定时器回调函数
//   user_data: 回调函数的用户数据
//   interval: 定时器间隔（毫秒）
// 返回值: 0 表示成功
int netlib_register_timer(callback_t callback, void *user_data,
                          uint64_t interval) {
    CEventDispatch::Instance()->AddTimer(callback, user_data, interval);
    return 0;
}

// 删除定时器
// 参数:
//   callback: 要删除的定时器回调函数
//   user_data: 回调函数的用户数据
// 返回值: 0 表示成功
int netlib_delete_timer(callback_t callback, void *user_data) {
    CEventDispatch::Instance()->RemoveTimer(callback, user_data);
    return 0;
}

// 添加循环任务
// 参数:
//   callback: 循环任务回调函数
//   user_data: 回调函数的用户数据
// 返回值: 0 表示成功
int netlib_add_loop(callback_t callback, void *user_data) {
    CEventDispatch::Instance()->AddLoop(callback, user_data);
    return 0;
}

// 启动事件循环
// 参数:
//   wait_timeout: 等待超时时间（毫秒）
void netlib_eventloop(uint32_t wait_timeout) {
    CEventDispatch::Instance()->StartDispatch(wait_timeout);
}

// 停止事件循环
void netlib_stop_event() { CEventDispatch::Instance()->StopDispatch(); }

// 检查事件循环是否正在运行
// 返回值: true 表示正在运行，false 表示未运行
bool netlib_is_running() { return CEventDispatch::Instance()->IsRunning(); }
