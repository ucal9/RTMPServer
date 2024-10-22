// OS依赖的类型定义
#ifndef __OS_TYPE_H__
#define __OS_TYPE_H__

#ifdef _WIN32
// Windows平台特定头文件
#include <WinSock2.h>
#include <WinBase.h>
#include <Windows.h>
#include <direct.h>
#else
#ifdef __APPLE__
// macOS平台特定头文件
#include <sys/event.h>
#include <sys/time.h>
#include <sys/syscall.h> // 用于syscall(SYS_gettid)
#else
// Linux平台特定头文件
#include <sys/epoll.h>
#endif
// POSIX兼容系统通用头文件
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h> // 定义int8_t等类型
#include <signal.h>
#include <unistd.h>
#define closesocket close
#define ioctlsocket ioctl
#endif

#include <stdexcept>

#ifdef __GNUC__
// GCC编译器特定的hash_map实现
#include <unordered_map>
using namespace std;
#else
// 其他编译器的hash_map实现
#include <hash_map>
using namespace stdext;
#endif

#ifdef _WIN32
// Windows平台类型定义
typedef char int8_t;
typedef short int16_t;
typedef int int32_t;
typedef long long int64_t;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef int socklen_t;
#else
// POSIX兼容系统类型定义
typedef int SOCKET;
typedef int BOOL;
#ifndef __APPLE__
const int TRUE = 1;
const int FALSE = 0;
#endif
const int SOCKET_ERROR = -1;
const int INVALID_SOCKET = -1;
#endif

// 通用类型定义
typedef unsigned char uchar_t;
typedef int net_handle_t;
typedef int conn_handle_t;

// 网络库返回值枚举
enum
{
    NETLIB_OK = 0,
    NETLIB_ERROR = -1
};

#define NETLIB_INVALID_HANDLE -1

// 网络消息类型枚举
enum
{
    NETLIB_MSG_CONNECT = 1,
    NETLIB_MSG_CONFIRM,
    NETLIB_MSG_READ,
    NETLIB_MSG_WRITE,
    NETLIB_MSG_CLOSE,
    NETLIB_MSG_TIMER,
    NETLIB_MSG_LOOP
};

// 常量定义
const uint32_t INVALID_UINT32 = (uint32_t)-1;
const uint32_t INVALID_VALUE = 0;

// 回调函数类型定义
typedef void (*callback_t)(void *callback_data, uint8_t msg, uint32_t handle, void *pParam);

#endif
