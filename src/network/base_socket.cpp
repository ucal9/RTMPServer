// 事件驱动模型：Reactor 使用 I/O 多路复用技术（如 epoll 或 kqueue），可以同时监控大量的文件描述符，
// 而不是为每个连接创建一个线程或进程。
// 这避免了创建大量线程或进程带来的系统开销，尤其是在高并发场景中，Reactor 可以更有效地利用系统资源。
// 非阻塞 I/O：Reactor 模型依赖于非阻塞 I/O，
// 因此即使某个连接暂时没有数据可处理，也不会阻塞整个服务器。它能够继续处理其他连接的请求，提高了 I/O 的利用率。
// 2. 可扩展性好
// Reactor 非常适合高并发场景，因为它允许在一个线程或少量线程中处理大量连接。
// 与传统的“一请求一线程”模型相比，Reactor 避免了线程数量过多带来的性能瓶颈，比如上下文切换和内存占用问题。
// 通过引入 多线程模型（如 Reactor + 线程池），Reactor 模型能够在保持高并发连接的同时，
// 利用线程池来处理具体的业务逻辑。这种方式保证了核心网络层的高效，同时业务处理部分也能得到扩展。
// 3. 资源占用低
// 减少线程或进程的创建和管理开销：在传统的阻塞式模型中，每个连接通常会分配一个独立的线程来处理，但线程的创建、销毁和调度都带有一定的系统开销。而 Reactor 模型中，所有 I/O 都在少量线程内处理，极大地减少了资源的浪费。
// 良好的内存利用率：由于不需要为每个连接创建独立的线程或进程，Reactor 模型在内存消耗上表现得更为出色，尤其是当有成千上万的连接时。
// 4. 延迟低、响应快
// Reactor 模型通过事件驱动机制，可以在事件到达的瞬间立即触发处理程序，保证了低延迟的处理过程。与传统的阻塞式模型相比，Reactor 的非阻塞机制减少了等待时间，使得响应速度更快。
// 5. 更灵活的架构
#include "base_socket.h"
#include "event_dispatch.h"
#include "util/dlog.h"

#include <map>
#include <utility>
using std::map;
using std::make_pair;
typedef std::unordered_map<net_handle_t, CBaseSocket *> SocketMap;
SocketMap g_socket_map;

// 添加一个基础套接字到全局映射中
// 参数:
// - pSocket: 指向要添加的CBaseSocket对象的指针
void AddBaseSocket(CBaseSocket *pSocket) {
    g_socket_map.insert(make_pair((net_handle_t)pSocket->GetSocket(), pSocket));
}

// 从全局映射中移除一个基础套接字
// 参数:
// - pSocket: 指向要移除的CBaseSocket象的指针
void RemoveBaseSocket(CBaseSocket *pSocket) {
    g_socket_map.erase((net_handle_t)pSocket->GetSocket());
}

// 根据文件描述符查找对应的基础套接字
// 参数:
// - fd: 要查找的套接字文件描述符
// 返回值:
// - 如果找到，返回对应的CBaseSocket指针；否则返回NULL
CBaseSocket *FindBaseSocket(net_handle_t fd) {
    CBaseSocket *pSocket = NULL;
    SocketMap::iterator iter = g_socket_map.find(fd);
    if (iter != g_socket_map.end()) {
        pSocket = iter->second;
        pSocket->AddRef();
    }

    return pSocket;
}

//////////////////////////////

// CBaseSocket类的构造函数
CBaseSocket::CBaseSocket(): CRefObject(0) {
    // printf("CBaseSocket::CBaseSocket\n");
    socket_ = INVALID_SOCKET;
    state_ = SOCKET_STATE_IDLE;
}

// CBaseSocket类的析构函数
CBaseSocket::~CBaseSocket() {
    // printf("CBaseSocket::~CBaseSocket, socket=%d\n", m_socket);
}

// 监听指定IP和端口
// 参数:
// - server_ip: 服务器IP地址
// - port: 监听端口
// - callback: 回调函数
// - callback_data: 回调函数的用户数据
// 返回值:
// - 成功返回NETLIB_OK，失败返回NETLIB_ERROR
int CBaseSocket::Listen(const char *server_ip, uint16_t port,
                        callback_t callback, void *callback_data) {
    // 保存地IP地址和端口号
    local_ip_ = server_ip;
    local_port_ = port;
    // 保存回调函数和回调数据
    callback_ = callback;
    callback_data_ = callback_data;

    // 创建套接字
    socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_ == INVALID_SOCKET) {
        LogError("socket failed, err_code={}, server_ip={}, port={}, error: {}", 
                 _GetErrorCode(), server_ip, port, strerror(errno));
        return NETLIB_ERROR;
    }

    // 设置地址重用和非阻塞模式
    _SetReuseAddr(socket_);
    _SetNonblock(socket_);

    // 绑定地址和端口
    sockaddr_in serv_addr;
    _SetAddr(server_ip, port, &serv_addr);
    int ret = ::bind(socket_, (sockaddr *)&serv_addr, sizeof(serv_addr));
    if (ret == SOCKET_ERROR) {
        LogError("bind failed, err_code={}, server_ip={}, port={}, error: {}", 
                 _GetErrorCode(), server_ip, port, strerror(errno));
        closesocket(socket_);
        return NETLIB_ERROR;
    }

    // 开始监听
    ret = listen(socket_, 64);
    if (ret == SOCKET_ERROR) {
        LogError("listen failed, err_code={}, server_ip={}, port={}, error: {}", 
                 _GetErrorCode(), server_ip, port, strerror(errno));
        closesocket(socket_);
        return NETLIB_ERROR;
    }

    // 设置套接字状态为监听中
    state_ = SOCKET_STATE_LISTENING;

    printf("CBaseSocket::Listen 正在监听 %s:%d\n", server_ip, port);

    // 将套接字添加到全局映射中
    AddBaseSocket(this);
    // 添加读取和异常事件到事件分发器
    CEventDispatch::Instance()->AddEvent(socket_, SOCKET_READ | SOCKET_EXCEP);
    return NETLIB_OK;
}

// 连接到指定的服务器
// 参数:
// - server_ip: 服务器IP地址
// - port: 服务器端口
// - callback: 回调函数
// - callback_data: 回调函数的用户数据
// 返回值:
// - 成功返回套接字句柄，失败返回NETLIB_INVALID_HANDLE
net_handle_t CBaseSocket::Connect(const char *server_ip, uint16_t port,
                                  callback_t callback, void *callback_data) {
    printf("CBaseSocket::Connect, server_ip=%s, port=%d", server_ip, port);

    remote_ip_ = server_ip;
    remote_port_ = port;
    callback_ = callback;
    callback_data_ = callback_data;

    socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_ == INVALID_SOCKET) {
        printf("socket failed, err_code=%d, server_ip=%s, port=%u",
               _GetErrorCode(), server_ip, port);
        return NETLIB_INVALID_HANDLE;
    }

    _SetNonblock(socket_);
    _SetNoDelay(socket_);
    sockaddr_in serv_addr;
    _SetAddr(server_ip, port, &serv_addr);
    int ret = connect(socket_, (sockaddr *)&serv_addr, sizeof(serv_addr));
    if ((ret == SOCKET_ERROR) && (!_IsBlock(_GetErrorCode()))) {
        printf("connect failed, err_code=%d, server_ip=%s, port=%u",
               _GetErrorCode(), server_ip, port);
        closesocket(socket_);
        return NETLIB_INVALID_HANDLE;
    }
    state_ = SOCKET_STATE_CONNECTING;
    AddBaseSocket(this);
    CEventDispatch::Instance()->AddEvent(socket_, SOCKET_ALL);

    return (net_handle_t)socket_;
}

// 发送数据
// 参数:
// - buf: 要发送的数据缓冲区
// - len: 要发送的数据长度
// 返回值:
// - 成功发送的字节数，失败返回NETLIB_ERROR
int CBaseSocket::Send(void *buf, int len) {
    if (state_ != SOCKET_STATE_CONNECTED)
        return NETLIB_ERROR;

    int ret = send(socket_, (char *)buf, len, 0);
    if (ret == SOCKET_ERROR) {
        int err_code = _GetErrorCode();
        if (_IsBlock(err_code)) {
#if ((defined _WIN32) || (defined __APPLE__))
            CEventDispatch::Instance()->AddEvent(socket_, SOCKET_WRITE);
#endif
            ret = 0;
            // printf("socket send block fd=%d", m_socket);
        } else {
            printf("send failed, err_code=%d, len=%d", err_code, len);
        }
    }

    return ret;
}

// 接收数据
// 参数:
// - buf: 接收数据的缓冲区
// - len: 缓区大小
// 返回值:
// - 实际接收的字节数
int CBaseSocket::Recv(void *buf, int len) {
    return recv(socket_, (char *)buf, len, 0);
}

// 关闭套接字连接
// 返回值:
// - 总是返回0
int CBaseSocket::Close() {
    CEventDispatch::Instance()->RemoveEvent(socket_, SOCKET_ALL);
    RemoveBaseSocket(this);
    // printf("close socket fd:%d\n", socket_);
    closesocket(socket_);
    ReleaseRef();

    return 0;
}

// 处理可读事件
void CBaseSocket::OnRead() {
    if (state_ == SOCKET_STATE_LISTENING) {
        _AcceptNewSocket();
    } else {
        u_long avail = 0;
        int ret = ioctlsocket(socket_, FIONREAD, &avail);
        if ((SOCKET_ERROR == ret) || (avail == 0)) {
            callback_(callback_data_, NETLIB_MSG_CLOSE, (net_handle_t)socket_,
                      NULL);
        } else {
            callback_(callback_data_, NETLIB_MSG_READ, (net_handle_t)socket_,
                      NULL);
        }
    }
}

// 处理可写事件
void CBaseSocket::OnWrite() {
#if ((defined _WIN32) || (defined __APPLE__))
    CEventDispatch::Instance()->RemoveEvent(socket_, SOCKET_WRITE);
#endif

    if (state_ == SOCKET_STATE_CONNECTING) {
        int error = 0;
        socklen_t len = sizeof(error);
#ifdef _WIN32

        getsockopt(socket_, SOL_SOCKET, SO_ERROR, (char *)&error, &len);
#else
        getsockopt(socket_, SOL_SOCKET, SO_ERROR, (void *)&error, &len);
#endif
        if (error) {
            callback_(callback_data_, NETLIB_MSG_CLOSE, (net_handle_t)socket_,
                      NULL);
        } else {
            state_ = SOCKET_STATE_CONNECTED;
            callback_(callback_data_, NETLIB_MSG_CONFIRM, (net_handle_t)socket_,
                      NULL);
        }
    } else {
        callback_(callback_data_, NETLIB_MSG_WRITE, (net_handle_t)socket_,
                  NULL);
    }
}

// 处理关闭事件
void CBaseSocket::OnClose() {
    state_ = SOCKET_STATE_CLOSING;
    callback_(callback_data_, NETLIB_MSG_CLOSE, (net_handle_t)socket_, NULL);
}

// 设置发送缓冲区大小
// 参数:
// - send_size: 要设置的发送缓冲区大小
void CBaseSocket::SetSendBufSize(uint32_t send_size) {
    int ret = setsockopt(socket_, SOL_SOCKET, SO_SNDBUF, &send_size, 4);
    if (ret == SOCKET_ERROR) {
        printf("set SO_SNDBUF failed for fd=%d", socket_);
    }

    socklen_t len = 4;
    int size = 0;
    getsockopt(socket_, SOL_SOCKET, SO_SNDBUF, &size, &len);
    printf("socket=%d send_buf_size=%d", socket_, size);
}

// 设置接收缓冲区大小
// 参数:
// - recv_size: 要设置的接收缓冲区大小
void CBaseSocket::SetRecvBufSize(uint32_t recv_size) {
    int ret = setsockopt(socket_, SOL_SOCKET, SO_RCVBUF, &recv_size, 4);
    if (ret == SOCKET_ERROR) {
        printf("set SO_RCVBUF failed for fd=%d", socket_);
    }

    socklen_t len = 4;
    int size = 0;
    getsockopt(socket_, SOL_SOCKET, SO_RCVBUF, &size, &len);
    printf("socket=%d recv_buf_size=%d", socket_, size);
}

// 获取错误码
// 返回值:
// - 当前的错误码
int CBaseSocket::_GetErrorCode() {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

// 判断是否为阻塞错误
// 参数:
// - error_code: 要判断的错误码
// 返回值:
// - 如果是阻塞错误返回true，否则返回false
bool CBaseSocket::_IsBlock(int error_code) {
#ifdef _WIN32
    return ((error_code == WSAEINPROGRESS) || (error_code == WSAEWOULDBLOCK));
#else
    return ((error_code == EINPROGRESS) || (error_code == EWOULDBLOCK));
#endif
}

// 设置套接字为非阻塞模式
// 参数:
// - fd: 要设置的套接字文件描述符
void CBaseSocket::_SetNonblock(SOCKET fd) {
#ifdef _WIN32
    u_long nonblock = 1;
    int ret = ioctlsocket(fd, FIONBIO, &nonblock);
#else
    int ret = fcntl(fd, F_SETFL, O_NONBLOCK | fcntl(fd, F_GETFL));
#endif
    if (ret == SOCKET_ERROR) {
        printf("_SetNonblock failed, err_code=%d, fd=%d", _GetErrorCode(), fd);
    }
}

// 设置套接地址可重用
// 参数:
// - fd: 要设置的套接字文件描述符
void CBaseSocket::_SetReuseAddr(SOCKET fd) {
    int reuse = 1;
    int ret =
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse));
    if (ret == SOCKET_ERROR) {
        printf("_SetReuseAddr failed, err_code=%d, fd=%d", _GetErrorCode(), fd);
    }
}

// 设置套接字为无延迟模式（禁用Nagle算法）
// 参数:
// - fd: 要设置的套接字文件描述符
void CBaseSocket::_SetNoDelay(SOCKET fd) {
    int nodelay = 1;
    int ret = setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char *)&nodelay,
                         sizeof(nodelay));
    if (ret == SOCKET_ERROR) {
        printf("_SetNoDelay failed, err_code=%d, fd=%d", _GetErrorCode(), fd);
    }
}

// 设置套接字地址结构
// 参数:
// - ip: IP地址字符串
// - port: 端口号
// - addr: 指向要设置的sockaddr_in结构的指针
void CBaseSocket::_SetAddr(const char *ip, const uint16_t port,
                           sockaddr_in *addr) {
    memset(addr, 0, sizeof(sockaddr_in));
    addr->sin_family = AF_INET;
    addr->sin_port = htons(port);
    addr->sin_addr.s_addr = inet_addr(ip);
    if (addr->sin_addr.s_addr == INADDR_NONE) {
        hostent *host = gethostbyname(ip);
        if (host == NULL) {
            printf("gethostbyname failed, ip=%s, port=%u", ip, port);
            return;
        }

        addr->sin_addr.s_addr = *(uint32_t *)host->h_addr;
    }
}

// 接受新的连接
void CBaseSocket::_AcceptNewSocket() {
    SOCKET fd = 0;
    sockaddr_in peer_addr;
    socklen_t addr_len = sizeof(sockaddr_in);
    char ip_str[64];
    while ((fd = accept(socket_, (sockaddr *)&peer_addr, &addr_len)) !=
           INVALID_SOCKET) {
        CBaseSocket *pSocket = new CBaseSocket();
        uint32_t ip = ntohl(peer_addr.sin_addr.s_addr);
        uint16_t port = ntohs(peer_addr.sin_port);

        snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d", ip >> 24,
                 (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);

        // printf("AcceptNewSocket, socket=%d from %s:%d\n", fd, ip_str, port);

        pSocket->SetSocket(fd);
        pSocket->SetCallback(callback_);
        pSocket->SetCallbackData(callback_data_);
        pSocket->SetState(SOCKET_STATE_CONNECTED);
        pSocket->SetRemoteIP(ip_str);
        pSocket->SetRemotePort(port);

        _SetNoDelay(fd);
        _SetNonblock(fd);
        AddBaseSocket(pSocket);
        CEventDispatch::Instance()->AddEvent(fd, SOCKET_READ | SOCKET_EXCEP);
        callback_(callback_data_, NETLIB_MSG_CONNECT, (net_handle_t)fd, NULL);
    }
}



