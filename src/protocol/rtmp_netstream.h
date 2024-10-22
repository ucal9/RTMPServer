#ifndef _rtmp_netstream_h_
#define _rtmp_netstream_h_

#include <stdint.h>
#include <stddef.h>

// 播放流
// @param out 输出缓冲区
// @param bytes 缓冲区大小
// @param transactionId 事务ID
// @param stream_name 流名称
// @param start 开始位置 (-2:直播/点播, -1:仅直播, >=0:seek位置)
// @param duration 持续时间 (<=-1:全部, 0:单帧, >0:指定时长)
// @param reset 是否重置 (1:清除之前的播放列表, 0:不清除)
uint8_t* rtmp_netstream_play(uint8_t* out, size_t bytes, double transactionId, const char* stream_name, double start, double duration, int reset);

// 暂停/恢复流
// @param pause 1:暂停, 0:恢复
// @param ms 暂停/恢复的时间点
uint8_t* rtmp_netstream_pause(uint8_t* out, size_t bytes, double transactionId, int pause, double ms);

// 跳转到指定时间点
// @param ms 目标时间点(毫秒)
uint8_t* rtmp_netstream_seek(uint8_t* out, size_t bytes, double transactionId, double ms);

// 控制是否接收音频
// @param enable 1:接收, 0:不接收
uint8_t* rtmp_netstream_receive_audio(uint8_t* out, size_t bytes, double transactionId, int enable);

// 控制是否接收视频
// @param enable 1:接收, 0:不接收
uint8_t* rtmp_netstream_receive_video(uint8_t* out, size_t bytes, double transactionId, int enable);

// 发布流
// @param stream_name 流名称
// @param stream_type 流类型 ("live", "record", "append")
uint8_t* rtmp_netstream_publish(uint8_t* out, size_t bytes, double transactionId, const char* stream_name, const char* stream_type);

// 删除流
// @param stream_id 流ID
uint8_t* rtmp_netstream_delete_stream(uint8_t* out, size_t bytes, double transactionId, double stream_id);

// 关闭流
// @param stream_id 流ID
uint8_t* rtmp_netconnection_close_stream(uint8_t* out, size_t bytes, double transactionId, double stream_id);

// 释放流
// @param stream_name 流名称
uint8_t* rtmp_netstream_release_stream(uint8_t* out, size_t bytes, double transactionId, const char* stream_name);

// FCPublish (Flash Communication Publish)
// @param stream_name 流名称
uint8_t* rtmp_netstream_fcpublish(uint8_t* out, size_t bytes, double transactionId, const char* stream_name);

// FCUnpublish (Flash Communication Unpublish)
// @param stream_name 流名称
uint8_t* rtmp_netstream_fcunpublish(uint8_t* out, size_t bytes, double transactionId, const char* stream_name);

// FCSubscribe (Flash Communication Subscribe)
// @param stream_name 流名称
uint8_t* rtmp_netstream_fcsubscribe(uint8_t* out, size_t bytes, double transactionId, const char* stream_name);

// FCUnsubscribe (Flash Communication Unsubscribe)
// @param stream_name 流名称
uint8_t* rtmp_netstream_fcunsubscribe(uint8_t* out, size_t bytes, double transactionId, const char* stream_name);

// 发送状态消息
// @param level 级别 ("status", "warning", "error")
// @param code 状态码
// @param description 描述信息
uint8_t* rtmp_netstream_onstatus(uint8_t* out, size_t bytes, double transactionId, const char* level, const char* code, const char* description);

// RTMP Sample Access
uint8_t* rtmp_netstream_rtmpsampleaccess(uint8_t* out, size_t bytes);

#endif /* !_rtmp_netstream_h_ */
