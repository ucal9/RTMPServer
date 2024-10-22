// 包含必要的头文件
#include "event_dispatch.h"
#include "base_socket.h"

// 定义最小定时器持续时间为100毫秒
#define MIN_TIMER_DURATION 100 // 100 miliseconds

// 静态成员变量初始化
CEventDispatch *CEventDispatch::event_dispatch_ = NULL;

// CEventDispatch构造函数
CEventDispatch::CEventDispatch() {
    running_ = false;
#ifdef _WIN32
    // Windows平台初始化
    FD_ZERO(&m_read_set);
    FD_ZERO(&m_write_set);
    FD_ZERO(&m_excep_set);
#elif __APPLE__
    // MacOS平台初始化
    m_kqfd = kqueue();
    if (m_kqfd == -1) {
        printf("kqueue failed");
    }
#else
    // Linux平台初始化
    epfd_ = epoll_create(1024);
    if (epfd_ == -1) {
        printf("epoll_create failed");
    }
#endif
}

// CEventDispatch析构函数
CEventDispatch::~CEventDispatch() {
#ifdef _WIN32
    // Windows平台不需要特殊清理
#elif __APPLE__
    // MacOS平台关闭kqueue
    close(m_kqfd);
#else
    // Linux平台关闭epoll
    close(epfd_);
#endif
}

// 添加定时器
void CEventDispatch::AddTimer(callback_t callback, void *user_data,
                              uint64_t interval) {
    list<TimerItem *>::iterator it;
    // 检查是否已存在相同的定时器
    for (it = timer_list_.begin(); it != timer_list_.end(); it++) {
        TimerItem *pItem = *it;
        if (pItem->callback == callback && pItem->user_data == user_data) {
            // 如果存在，更新间隔和下次触发时间
            pItem->interval = interval;
            pItem->next_tick = GetTickCount() + interval;
            return;
        }
    }

    // 创建新的定时器项
    TimerItem *pItem = new TimerItem;
    pItem->callback = callback;
    pItem->user_data = user_data;
    pItem->interval = interval;
    pItem->next_tick = GetTickCount() + interval;
    timer_list_.push_back(pItem);
}

// 移除定时器
void CEventDispatch::RemoveTimer(callback_t callback, void *user_data) {
    list<TimerItem *>::iterator it;
    for (it = timer_list_.begin(); it != timer_list_.end(); it++) {
        TimerItem *pItem = *it;
        if (pItem->callback == callback && pItem->user_data == user_data) {
            timer_list_.erase(it);
            delete pItem;
            return;
        }
    }
}

// 检查并触发定时器
void CEventDispatch::_CheckTimer() {
    uint64_t curr_tick = GetTickCount();
    list<TimerItem *>::iterator it;

    for (it = timer_list_.begin(); it != timer_list_.end();) {
        TimerItem *pItem = *it;
        it++; // 提前递增迭代器，因为回调可能会删除当前项
        if (curr_tick >= pItem->next_tick) {
            pItem->next_tick += pItem->interval;
            pItem->callback(pItem->user_data, NETLIB_MSG_TIMER, 0, NULL);
        }
    }
}

// 添加循环任务
void CEventDispatch::AddLoop(callback_t callback, void *user_data) {
    TimerItem *pItem = new TimerItem;
    pItem->callback = callback;
    pItem->user_data = user_data;
    loop_list_.push_back(pItem);
}

// 检查并执行循环任务
void CEventDispatch::_CheckLoop() {
    for (list<TimerItem *>::iterator it = loop_list_.begin();
         it != loop_list_.end(); it++) {
        TimerItem *pItem = *it;
        pItem->callback(pItem->user_data, NETLIB_MSG_LOOP, 0, NULL);
    }
}

// 获取CEventDispatch单例
CEventDispatch *CEventDispatch::Instance() {
    if (event_dispatch_ == NULL) {
        event_dispatch_ = new CEventDispatch();
    }

    return event_dispatch_;
}

#ifdef _WIN32

// Windows平台：添加事件
void CEventDispatch::AddEvent(SOCKET fd, uint8_t socket_event) {
    CAutoLock func_lock(&m_lock);

    if ((socket_event & SOCKET_READ) != 0) {
        FD_SET(fd, &m_read_set);
    }

    if ((socket_event & SOCKET_WRITE) != 0) {
        FD_SET(fd, &m_write_set);
    }

    if ((socket_event & SOCKET_EXCEP) != 0) {
        FD_SET(fd, &m_excep_set);
    }
}

// Windows平台：移除事件
void CEventDispatch::RemoveEvent(SOCKET fd, uint8_t socket_event) {
    CAutoLock func_lock(&m_lock);

    if ((socket_event & SOCKET_READ) != 0) {
        FD_CLR(fd, &m_read_set);
    }

    if ((socket_event & SOCKET_WRITE) != 0) {
        FD_CLR(fd, &m_write_set);
    }

    if ((socket_event & SOCKET_EXCEP) != 0) {
        FD_CLR(fd, &m_excep_set);
    }
}

// Windows平台：开始事件分发
void CEventDispatch::StartDispatch(uint32_t wait_timeout) {
    fd_set read_set, write_set, excep_set;
    timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = wait_timeout * 1000; // 10 millisecond

    if (running_)
        return;
    running_ = true;

    while (running_) {
        _CheckTimer();
        _CheckLoop();

        if (!m_read_set.fd_count && !m_write_set.fd_count &&
            !m_excep_set.fd_count) {
            Sleep(MIN_TIMER_DURATION);
            continue;
        }

        m_lock.lock();
        memcpy(&read_set, &m_read_set, sizeof(fd_set));
        memcpy(&write_set, &m_write_set, sizeof(fd_set));
        memcpy(&excep_set, &m_excep_set, sizeof(fd_set));
        m_lock.unlock();

        int nfds = select(0, &read_set, &write_set, &excep_set, &timeout);

        if (nfds == SOCKET_ERROR) {
            printf("select failed, error code: %d", GetLastError());
            Sleep(MIN_TIMER_DURATION);
            continue; // select again
        }

        if (nfds == 0) {
            continue;
        }

        for (u_int i = 0; i < read_set.fd_count; i++) {
            // printf("select return read count=%d\n", read_set.fd_count);
            SOCKET fd = read_set.fd_array[i];
            CBaseSocket *pSocket = FindBaseSocket((net_handle_t)fd);
            if (pSocket) {
                pSocket->OnRead();
                pSocket->ReleaseRef();
            }
        }

        for (u_int i = 0; i < write_set.fd_count; i++) {
            // printf("select return write count=%d\n", write_set.fd_count);
            SOCKET fd = write_set.fd_array[i];
            CBaseSocket *pSocket = FindBaseSocket((net_handle_t)fd);
            if (pSocket) {
                pSocket->OnWrite();
                pSocket->ReleaseRef();
            }
        }

        for (u_int i = 0; i < excep_set.fd_count; i++) {
            // printf("select return exception count=%d\n", excep_set.fd_count);
            SOCKET fd = excep_set.fd_array[i];
            CBaseSocket *pSocket = FindBaseSocket((net_handle_t)fd);
            if (pSocket) {
                pSocket->OnClose();
                pSocket->ReleaseRef();
            }
        }
    }
}

// Windows平台：停止事件分发
void CEventDispatch::StopDispatch() { running_ = false; }

#elif __APPLE__

// MacOS平台：添加事件
void CEventDispatch::AddEvent(SOCKET fd, uint8_t socket_event) {
    struct kevent ke;

    if ((socket_event & SOCKET_READ) != 0) {
        EV_SET(&ke, fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
        kevent(m_kqfd, &ke, 1, NULL, 0, NULL);
    }

    if ((socket_event & SOCKET_WRITE) != 0) {
        EV_SET(&ke, fd, EVFILT_WRITE, EV_ADD, 0, 0, NULL);
        kevent(m_kqfd, &ke, 1, NULL, 0, NULL);
    }
}

// MacOS平台：移除事件
void CEventDispatch::RemoveEvent(SOCKET fd, uint8_t socket_event) {
    struct kevent ke;

    if ((socket_event & SOCKET_READ) != 0) {
        EV_SET(&ke, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
        kevent(m_kqfd, &ke, 1, NULL, 0, NULL);
    }

    if ((socket_event & SOCKET_WRITE) != 0) {
        EV_SET(&ke, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
        kevent(m_kqfd, &ke, 1, NULL, 0, NULL);
    }
}

// MacOS平台：开始事件分发
void CEventDispatch::StartDispatch(uint32_t wait_timeout) {
    // 定义事件数组和计数器
    struct kevent events[1024];
    int nfds = 0;
    // 设置超时结构���
    struct timespec timeout;
    timeout.tv_sec = 0;
    timeout.tv_nsec = wait_timeout * 1000000;

    // 如果已经在运行，直接返回
    if (running_)
        return;
    running_ = true;

    // 主事件循环
    while (running_) {
        // 等待事件发生
        nfds = kevent(m_kqfd, NULL, 0, events, 1024, &timeout);

        // 处理所有发生的事件
        for (int i = 0; i < nfds; i++) {
            int ev_fd = events[i].ident;
            CBaseSocket *pSocket = FindBaseSocket(ev_fd);
            if (!pSocket)
                continue;

            // 处理读事件
            if (events[i].filter == EVFILT_READ) {
                // printf("OnRead, socket=%d\n", ev_fd);
                pSocket->OnRead();
            }

            // 处理写事件
            if (events[i].filter == EVFILT_WRITE) {
                // printf("OnWrite, socket=%d\n", ev_fd);
                pSocket->OnWrite();
            }

            // 释放socket引用
            pSocket->ReleaseRef();
        }

        // 检查定时器
        _CheckTimer();
        // 检查循环
        _CheckLoop();
    }
}

// MacOS平台：停止事件分发
void CEventDispatch::StopDispatch() { running_ = false; }

#else

// Linux平台：添加事件
void CEventDispatch::AddEvent(SOCKET fd, uint8_t socket_event) {
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET | EPOLLPRI | EPOLLERR | EPOLLHUP;
    ev.data.fd = fd;
    if (epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ev) != 0) {
        printf("epoll_ctl() failed, errno=%d", errno);
    }
}

// Linux平台：移除事件
void CEventDispatch::RemoveEvent(SOCKET fd, uint8_t socket_event) {
    if (epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, NULL) != 0) {
        printf("epoll_ctl failed, errno=%d", errno);
    }
}

// Linux平台：开始事件分发
void CEventDispatch::StartDispatch(uint32_t wait_timeout) {
    struct epoll_event events[1024];
    int nfds = 0;

    if (running_)
        return;
    running_ = true;

    while (running_) {
        nfds = epoll_wait(epfd_, events, 1024, wait_timeout);
        for (int i = 0; i < nfds; i++) {
            int ev_fd = events[i].data.fd;
            CBaseSocket *pSocket = FindBaseSocket(ev_fd);
            if (!pSocket)
                continue;

// Commit  2023-02-28
#ifdef EPOLLRDHUP
            if (events[i].events & EPOLLRDHUP) {
                // printf("On Peer Close, socket=%d, ev_fd);
                pSocket->OnClose();
            }
#endif
            // Commit End

            if (events[i].events & EPOLLIN) {
                // printf("OnRead, socket=%d\n", ev_fd);
                pSocket->OnRead();
            }

            if (events[i].events & EPOLLOUT) {
                // printf("OnWrite, socket=%d\n", ev_fd);
                pSocket->OnWrite();
            }

            if (events[i].events & (EPOLLPRI | EPOLLERR | EPOLLHUP)) {
                // printf("OnClose, socket=%d\n", ev_fd);
                pSocket->OnClose();
            }

            pSocket->ReleaseRef();
        }

        _CheckTimer();
        _CheckLoop();
    }
}

// Linux平台：停止事件分发
void CEventDispatch::StopDispatch() { running_ = false; }

#endif
