#ifndef _rtmp_internal_h_
#define _rtmp_internal_h_

#include "rtmp_chunk_header.h"
#include "rtmp_netconnection.h"
#include "rtmp_netstream.h"

// 定义最大块流数量
#define N_CHUNK_STREAM	8 // maximum chunk stream count
// 定义最大流名称长度
#define N_STREAM_NAME	256

// 定义RTMP流类型
#define RTMP_STREAM_LIVE	"live"
#define RTMP_STREAM_RECORD	"record"
#define RTMP_STREAM_APPEND	"append"

// 定义RTMP日志级别
#define RTMP_LEVEL_WARNING	"warning"
#define RTMP_LEVEL_STATUS	"status"
#define RTMP_LEVEL_ERROR	"error"
#define RTMP_LEVEL_FINISH	"finish" // ksyun cdn

// 定义RTMP通道ID
enum rtmp_channel_t
{
	RTMP_CHANNEL_PROTOCOL = 2,	// 协议控制消息 (1,2,3,5,6) 和用户控制消息事件 (4)
	RTMP_CHANNEL_INVOKE,		// RTMP_TYPE_INVOKE (20) 和 RTMP_TYPE_FLEX_MESSAGE (17)
	RTMP_CHANNEL_AUDIO,			// RTMP_TYPE_AUDIO (8)
	RTMP_CHANNEL_VIDEO,			// RTMP_TYPE_VIDEO (9)
	RTMP_CHANNEL_DATA,			// RTMP_TYPE_DATA (18) 和 RTMP_TYPE_FLEX_STREAM (15)

	RTMP_CHANNEL_MAX = 65599,	// 协议支持最多65597个流，ID范围为3-65599(65535 + 64)
};

// 定义RTMP状态
enum rtmp_state_t
{
	RTMP_STATE_UNINIT = 0,
	RTMP_STATE_HANDSHAKE,
	RTMP_STATE_CONNECTED,
	RTMP_STATE_CREATE_STREAM,
	RTMP_STATE_START,
	RTMP_STATE_STOP,
	RTMP_STATE_DELETE_STREAM,
};

// 定义RTMP事务ID
enum rtmp_transaction_id_t
{
	RTMP_TRANSACTION_CONNECT = 1,
	RTMP_TRANSACTION_CREATE_STREAM,
	RTMP_TRANSACTION_GET_STREAM_LENGTH,
};

// 定义RTMP通知类型
enum rtmp_notify_t
{
	RTMP_NOTIFY_START = 1,
	RTMP_NOTIFY_STOP,
	RTMP_NOTIFY_PAUSE,
	RTMP_NOTIFY_SEEK,
};

// 定义RTMP数据包结构
struct rtmp_packet_t
{
	struct rtmp_chunk_header_t header;
	uint32_t delta; // 时间增量
	uint32_t clock; // 时间戳

	uint8_t* payload;
	size_t capacity; // 仅用于网络读取
	size_t bytes; // 仅用于网络读取
};

// 5.3.1. 块格式 (p11)
/* 3字节基本头 + 11字节消息头 + 4字节扩展时间戳 */
#define MAX_CHUNK_HEADER 18

// 定义RTMP解析器状态
enum rtmp_parser_state_t
{
	RTMP_PARSE_INIT = 0,
	RTMP_PARSE_BASIC_HEADER,
	RTMP_PARSE_MESSAGE_HEADER,
	RTMP_PARSE_EXTENDED_TIMESTAMP,
	RTMP_PARSE_PAYLOAD,
};

// 定义RTMP解析器结构
struct rtmp_parser_t
{
	uint8_t buffer[MAX_CHUNK_HEADER];
	uint32_t basic_bytes; // 基本头长度
	uint32_t bytes;

	enum rtmp_parser_state_t state;

	struct rtmp_packet_t* pkt;
};

// 定义RTMP主结构
struct rtmp_t
{
	uint32_t in_chunk_size; // 从网络读取的块大小
	uint32_t out_chunk_size; // 写入网络的块大小

	uint32_t sequence_number; // 字节读取报告
	uint32_t window_size; // 服务器带宽 (2500000)
	uint32_t peer_bandwidth; // 客户端带宽
	
	uint32_t buffer_length_ms; // 服务器到客户端

	uint8_t limit_type; // 客户端带宽限制类型
	
	// 块头
	struct rtmp_packet_t in_packets[N_CHUNK_STREAM]; // 从网络接收
	struct rtmp_packet_t out_packets[N_CHUNK_STREAM]; // 发送到网络
	struct rtmp_parser_t parser;

	void* param;

	/// @return 0-成功, 其他-错误
	int (*send)(void* param, const uint8_t* header, uint32_t headerBytes, const uint8_t* payload, uint32_t payloadBytes);
	
	int (*onaudio)(void* param, const uint8_t* data, size_t bytes, uint32_t timestamp);
	int (*onvideo)(void* param, const uint8_t* data, size_t bytes, uint32_t timestamp);
	int (*onscript)(void* param, const uint8_t* data, size_t bytes, uint32_t timestamp);

	void (*onabort)(void* param, uint32_t chunk_stream_id);

	struct
	{
		// 服务器端回调函数
		int (*onconnect)(void* param, int r, double transaction, const struct rtmp_connect_t* connect);
		int (*oncreate_stream)(void* param, int r, double transaction);
		int (*onplay)(void* param, int r, double transaction, const char* stream_name, double start, double duration, uint8_t reset);
		int (*ondelete_stream)(void* param, int r, double transaction, double stream_id);
		int (*onreceive_audio)(void* param, int r, double transaction, uint8_t audio);
		int (*onreceive_video)(void* param, int r, double transaction, uint8_t video);
		int (*onpublish)(void* param, int r, double transaction, const char* stream_name, const char* stream_type);
		int (*onseek)(void* param, int r, double transaction, double milliSeconds);
		int (*onpause)(void* param, int r, double transaction, uint8_t pause, double milliSeconds);
		int (*onget_stream_length)(void* param, int r, double transaction, const char* stream_name);
	} server;

	struct
	{
		// 客户端回调函数
		int (*onconnect)(void* param);
		int (*oncreate_stream)(void* param, double stream_id);
		int (*onnotify)(void* param, enum rtmp_notify_t notify);
        int (*oneof)(void* param, uint32_t stream_id); // EOF事件
		int (*onping)(void* param, uint32_t stream_id); // 发送pong
		int (*onbandwidth)(void* param); // 发送窗口确认大小
	} client;
};

/// @return 0-成功, 其他-错误
int rtmp_chunk_read(struct rtmp_t* rtmp, const uint8_t* data, size_t bytes);
/// @return 0-成功, 其他-错误
int rtmp_chunk_write(struct rtmp_t* rtmp, const struct rtmp_chunk_header_t* header, const uint8_t* payload);

int rtmp_handler(struct rtmp_t* rtmp, struct rtmp_chunk_header_t* header, const uint8_t* payload);
int rtmp_event_handler(struct rtmp_t* rtmp, const struct rtmp_chunk_header_t* header, const uint8_t* data);
int rtmp_invoke_handler(struct rtmp_t* rtmp, const struct rtmp_chunk_header_t* header, const uint8_t* data);
/// @return >0-成功, 0-错误
int rtmp_control_handler(struct rtmp_t* rtmp, const struct rtmp_chunk_header_t* header, const uint8_t* data);

#endif /* !_rtmp_internal_h_ */
