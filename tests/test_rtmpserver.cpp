/**
 * 支持rtmp多路推流、rtmp多路拉流
*/
 
#include "util/util.h"
#include "util/util_pdu.h"
#include "util/dlog.h"
#include "network/netlib.h"
#include "app/app_rtmp_conn.h"
#include <list>
#include <mutex>
#include <stdexcept>
#include <errno.h>

int main(int argc, char *argv[]) 
{
    try {
        LogInfo("程序开始执行");
        
        // 初始化网络库
        if (netlib_init() != NETLIB_OK) {
            LogError("网络库初始化失败");
            return -1;
        }
        LogInfo("网络库初始化成功");

        signal(SIGPIPE, SIG_IGN);
        LogInfo("SIGPIPE 信号已忽略");
        
        int ret = 0;
        
        // 设置日志级别
        if(argc == 1) {
            DLog::SetLevel("warn");
            LogInfo("日志级别设置为 warn");
        } else {
            DLog::SetLevel(argv[1]);
            LogInfo("日志级别设置为 {}", argv[1]);
        }

        LogInfo("准备初始化RTMP监听器");
        int port = 1936;
        ret = RtmpInitListen("0.0.0.0", port, 4);
        if(ret != NETLIB_OK) {
            LogError("RtmpInitListen {} 初始化失败, 错误代码: {}, 错误信息: {}", 
                     port, ret, strerror(errno));
            netlib_destroy();
            return -1;
        }
        LogInfo("RTMP监听器初始化成功");

        LogInfo("准备写入PID");
        WritePid();
        LogInfo("PID写入完成");

        LogInfo("准备开始事件循环");
        netlib_eventloop(1);

        LogInfo("事件循环结束，程序正常退出");
        netlib_destroy();
        return 0;
    } catch (const std::exception& e) {
        LogError("主程序中捕获到未处理的异常: {}", e.what());
        netlib_destroy();
        return -1;
    } catch (...) {
        LogError("主程序中捕获到未知异常");
        netlib_destroy();
        return -1;
    }
}
