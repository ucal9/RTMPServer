#include "rtmp_debug.h"
#include "rtmp_internal.h"
#include "amf0.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "rtmp_client_invoke_handler.h"

// 连接请求解析器
static int rtmp_command_onconnect(struct rtmp_t* rtmp, double transaction, const uint8_t* data, uint32_t bytes)
{
	RTMP_TRACE_INTO
	int r;
	struct rtmp_connect_t connect;
	struct amf_object_item_t items[1];
	struct amf_object_item_t commands[8];

	memset(&connect, 0, sizeof(connect));
	connect.encoding = (double)RTMP_ENCODING_AMF_0;
	// 设置连接参数
	AMF_OBJECT_ITEM_VALUE(commands[0], AMF_STRING, "app", connect.app, sizeof(connect.app));
	AMF_OBJECT_ITEM_VALUE(commands[1], AMF_STRING, "flashver", connect.flashver, sizeof(connect.flashver));
	AMF_OBJECT_ITEM_VALUE(commands[2], AMF_STRING, "tcUrl", connect.tcUrl, sizeof(connect.tcUrl));
	AMF_OBJECT_ITEM_VALUE(commands[3], AMF_BOOLEAN, "fpad", &connect.fpad, 1);
	AMF_OBJECT_ITEM_VALUE(commands[4], AMF_NUMBER, "audioCodecs", &connect.audioCodecs, 8);
	AMF_OBJECT_ITEM_VALUE(commands[5], AMF_NUMBER, "videoCodecs", &connect.videoCodecs, 8);
	AMF_OBJECT_ITEM_VALUE(commands[6], AMF_NUMBER, "videoFunction", &connect.videoFunction, 8);
	AMF_OBJECT_ITEM_VALUE(commands[7], AMF_NUMBER, "objectEncoding", &connect.encoding, 8);

	AMF_OBJECT_ITEM_VALUE(items[0], AMF_OBJECT, "command", commands, sizeof(commands) / sizeof(commands[0]));

	// 解析AMF数据
	r = amf_read_items(data, data + bytes, items, sizeof(items) / sizeof(items[0])) ? 0 : -1;
	// 调用连接回调函数
	return rtmp->server.onconnect ? rtmp->server.onconnect(rtmp->param, r, transaction, &connect) : -1;
}

// 创建流请求解析器
static int rtmp_command_oncreate_stream(struct rtmp_t* rtmp, double transaction, const uint8_t* data, uint32_t bytes)
{
	RTMP_TRACE_INTO
	int r;
	struct amf_object_item_t items[1];
	AMF_OBJECT_ITEM_VALUE(items[0], AMF_OBJECT, "command", NULL, 0);

	// 解析AMF数据
	r = amf_read_items(data, data + bytes, items, sizeof(items) / sizeof(items[0])) ? 0 : -1;
	// 调用创建流回调函数
	return rtmp->server.oncreate_stream ? rtmp->server.oncreate_stream(rtmp->param, r, transaction) : -1;
}

// 7.2.2.1. 播放命令解析器 (p38)
static int rtmp_command_onplay(struct rtmp_t* rtmp, double transaction, const uint8_t* data, uint32_t bytes)
{
	RTMP_TRACE_INTO
	int r;
	uint8_t reset = 0;
	double start = -2; // 开始时间（秒），[默认] -2-直播/点播，-1-仅直播，>=0-定位位置
	double duration = -1; // 播放持续时间（秒），[默认] -1-直播/录制结束，0-单帧，>0-播放持续时间
	char stream_name[N_STREAM_NAME] = { 0 };

	struct amf_object_item_t items[5];
	AMF_OBJECT_ITEM_VALUE(items[0], AMF_OBJECT, "command", NULL, 0);
	AMF_OBJECT_ITEM_VALUE(items[1], AMF_STRING, "stream", stream_name, sizeof(stream_name));
	AMF_OBJECT_ITEM_VALUE(items[2], AMF_NUMBER, "start", &start, 8);
	AMF_OBJECT_ITEM_VALUE(items[3], AMF_NUMBER, "duration", &duration, 8);
	AMF_OBJECT_ITEM_VALUE(items[4], AMF_BOOLEAN, "reset", &reset, 1);

	// 解析AMF数据
	r = amf_read_items(data, data + bytes, items, sizeof(items) / sizeof(items[0])) ? 0 : -1;
	// 调用播放回调函数
	return rtmp->server.onplay ? rtmp->server.onplay(rtmp->param, r, transaction, stream_name, start, duration, reset) : -1;
}

// 7.2.2.3. 删除流命令解析器 (p43)
static int rtmp_command_ondelete_stream(struct rtmp_t* rtmp, double transaction, const uint8_t* data, uint32_t bytes)
{
	RTMP_TRACE_INTO
	int r;
	double stream_id = 0;
	struct amf_object_item_t items[2];
	AMF_OBJECT_ITEM_VALUE(items[0], AMF_OBJECT, "command", NULL, 0);
	AMF_OBJECT_ITEM_VALUE(items[1], AMF_NUMBER, "streamId", &stream_id, 8);

	// 解析AMF数据
	r = amf_read_items(data, data + bytes, items, sizeof(items) / sizeof(items[0])) ? 0 : -1;
	// 调用删除流回调函数
	return rtmp->server.ondelete_stream ? rtmp->server.ondelete_stream(rtmp->param, r, transaction, stream_id) : -1;
}

// 7.2.2.4. 接收音频命令解析器 (p44)
static int rtmp_command_onreceive_audio(struct rtmp_t* rtmp, double transaction, const uint8_t* data, uint32_t bytes)
{
	RTMP_TRACE_INTO
	int r;
	uint8_t receiveAudio = 1; // 1-接收音频，0-不接收音频
	struct amf_object_item_t items[2];
	AMF_OBJECT_ITEM_VALUE(items[0], AMF_OBJECT, "command", NULL, 0);
	AMF_OBJECT_ITEM_VALUE(items[1], AMF_BOOLEAN, "receiveAudio", &receiveAudio, 1);

	// 解析AMF数据
	r = amf_read_items(data, data + bytes, items, sizeof(items) / sizeof(items[0])) ? 0 : -1;
	// 调用接收音频回调函数
	return rtmp->server.onreceive_audio ? rtmp->server.onreceive_audio(rtmp->param, r, transaction, receiveAudio) : -1;
}

// 7.2.2.5. 接收视频命令解析器 (p45)
static int rtmp_command_onreceive_video(struct rtmp_t* rtmp, double transaction, const uint8_t* data, uint32_t bytes)
{
	RTMP_TRACE_INTO
	int r;
	uint8_t receiveVideo = 1; // 1-接收视频，0-不接收视频
	struct amf_object_item_t items[2];
	AMF_OBJECT_ITEM_VALUE(items[0], AMF_OBJECT, "command", NULL, 0);
	AMF_OBJECT_ITEM_VALUE(items[1], AMF_BOOLEAN, "receiveVideo", &receiveVideo, 1);

	// 解析AMF数据
	r = amf_read_items(data, data + bytes, items, sizeof(items) / sizeof(items[0])) ? 0 : -1;
	// 调用接收视频回调函数
	return rtmp->server.onreceive_video ? rtmp->server.onreceive_video(rtmp->param, r, transaction, receiveVideo) : -1;
}

// 7.2.2.6. 发布命令解析器 (p45)
static int rtmp_command_onpublish(struct rtmp_t* rtmp, double transaction, const uint8_t* data, uint32_t bytes)
{
	RTMP_TRACE_INTO
	int r;
	char stream_name[N_STREAM_NAME] = { 0 };
	char stream_type[18] = { 0 }; // 发布类型：live/record/append

	struct amf_object_item_t items[3];
	AMF_OBJECT_ITEM_VALUE(items[0], AMF_OBJECT, "command", NULL, 0);
	AMF_OBJECT_ITEM_VALUE(items[1], AMF_STRING, "name", stream_name, sizeof(stream_name));
	AMF_OBJECT_ITEM_VALUE(items[2], AMF_STRING, "type", stream_type, sizeof(stream_type));

	// 解析AMF数据
	r = amf_read_items(data, data + bytes, items, sizeof(items) / sizeof(items[0])) ? 0 : -1;
	// 调用发布回调函数
	return rtmp->server.onpublish ? rtmp->server.onpublish(rtmp->param, r, transaction, stream_name, stream_type) : -1;
}

// 7.2.2.7. 定位命令解析器 (p46)
static int rtmp_command_onseek(struct rtmp_t* rtmp, double transaction, const uint8_t* data, uint32_t bytes)
{
	RTMP_TRACE_INTO
	int r;
	double milliSeconds = 0;
	struct amf_object_item_t items[2];
	AMF_OBJECT_ITEM_VALUE(items[0], AMF_OBJECT, "command", NULL, 0);
	AMF_OBJECT_ITEM_VALUE(items[1], AMF_NUMBER, "milliSeconds", &milliSeconds, 8);

	// 解析AMF数据
	r = amf_read_items(data, data + bytes, items, sizeof(items) / sizeof(items[0])) ? 0 : -1;
	// 调用定位回调函数
	return rtmp->server.onseek ? rtmp->server.onseek(rtmp->param, r, transaction, milliSeconds) : -1;
}

// 暂停请求解析器
static int rtmp_command_onpause(struct rtmp_t* rtmp, double transaction, const uint8_t* data, uint32_t bytes)
{
	RTMP_TRACE_INTO
	int r;
	uint8_t pause = 0; // 1-暂停，0-恢复播放
	double milliSeconds = 0;
	struct amf_object_item_t items[3];
	AMF_OBJECT_ITEM_VALUE(items[0], AMF_OBJECT, "command", NULL, 0);
	AMF_OBJECT_ITEM_VALUE(items[1], AMF_BOOLEAN, "pause", &pause, 1);
	AMF_OBJECT_ITEM_VALUE(items[2], AMF_NUMBER, "milliSeconds", &milliSeconds, 8);

	// 解析AMF数据
	r = amf_read_items(data, data + bytes, items, sizeof(items) / sizeof(items[0])) ? 0 : -1;
	// 调用暂停回调函数
	return rtmp->server.onpause ? rtmp->server.onpause(rtmp->param, r, transaction, pause, milliSeconds) : -1;
}

static int rtmp_command_onget_stream_length(struct rtmp_t* rtmp, double transaction, const uint8_t* data, uint32_t bytes)
{
	RTMP_TRACE_INTO
	int r;
	char stream_name[N_STREAM_NAME] = { 0 };
	struct amf_object_item_t items[3];
	AMF_OBJECT_ITEM_VALUE(items[0], AMF_OBJECT, "command", NULL, 0);
	AMF_OBJECT_ITEM_VALUE(items[1], AMF_STRING, "playpath", stream_name, sizeof(stream_name));

	// 解析AMF数据
	r = amf_read_items(data, data + bytes, items, sizeof(items) / sizeof(items[0])) ? 0 : -1;
	// 调用获取流长度回调函数
	return rtmp->server.onget_stream_length ? rtmp->server.onget_stream_length(rtmp->param, r, transaction, stream_name) : -1;
}

// 命令处理器结构体
struct rtmp_command_handler_t
{
	const char* name;
	int (*handler)(struct rtmp_t* rtmp, double transaction, const uint8_t* data, uint32_t bytes);
};

// 命令处理器数组
const static struct rtmp_command_handler_t s_command_handler[] = {
	// 客户端命令
	{ "_result",		rtmp_command_onresult },
	{ "_error",			rtmp_command_onerror },
	{ "onStatus",		rtmp_command_onstatus },

	// 服务器端命令
	{ "connect",		rtmp_command_onconnect },
	{ "createStream",	rtmp_command_oncreate_stream },
	{ "play",			rtmp_command_onplay },
	{ "deleteStream",	rtmp_command_ondelete_stream },
	{ "receiveAudio",	rtmp_command_onreceive_audio },
	{ "receiveVideo",	rtmp_command_onreceive_video },
	{ "publish",		rtmp_command_onpublish },
	{ "seek",			rtmp_command_onseek },
	{ "pause",			rtmp_command_onpause },
	{ "getStreamLength",rtmp_command_onget_stream_length },
};

// RTMP调用处理函数
int rtmp_invoke_handler(struct rtmp_t* rtmp, const struct rtmp_chunk_header_t* header, const uint8_t* data)
{
	RTMP_TRACE_INTO
	int i;
	char command[64] = { 0 };
	double transaction = -1;
	const uint8_t *end = data + header->length;

	struct amf_object_item_t items[2];
	AMF_OBJECT_ITEM_VALUE(items[0], AMF_STRING, "command", command, sizeof(command));
	AMF_OBJECT_ITEM_VALUE(items[1], AMF_NUMBER, "transactionId", &transaction, sizeof(double));

	// 解析命令名称和事务ID
	data = amf_read_items(data, end, items, sizeof(items) / sizeof(items[0]));
	if (!data)
		return -EINVAL; // 无效数据
	if (-1.0 == transaction)
		return 0; // 修复：onFCPublish没有事务ID

	// 查找并执行对应的命令处理函数
	for (i = 0; i < sizeof(s_command_handler) / sizeof(s_command_handler[0]); i++)
	{
		if (0 == strcmp(command, s_command_handler[i].name))
		{
			return s_command_handler[i].handler(rtmp, transaction, data, (int)(end - data));
		}
	}

	// 未找到对应的命令处理函数
	return 0;
}
