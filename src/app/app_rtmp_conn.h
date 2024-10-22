#ifndef RTMP_CONN_H
#define RTMP_CONN_H

#include <iostream>
#include "util/util.h"
#include "util/util_pdu.h"
#include "util/dlog.h"
#include "protocol/rtmp_handshake.h"

#include "network/netlib.h"
#include "thread/thread_pool.h"

//rtmp
#include "protocol/rtmp_handshake.h"
#include "protocol/rtmp_internal.h"
#include "protocol/rtmp_msgtypeid.h"
#include "protocol/rtmp_handshake.h"
#include "protocol/rtmp_netstream.h"
#include "protocol/rtmp_netconnection.h"
#include "protocol/rtmp_control_message.h"
#include "protocol/rtmp_event.h"

#include <list>
#include <mutex>
#include <memory>



using namespace longkit;
class LiveSource;
class LiveConsumer;
// 具体的RMTP连接，推流拉流都在此
typedef struct {
    uint32_t conn_uuid; // 用于查找connection
    string resp_data;   // 要回发的数据
} ResponsePdu_t;

#define RTMP_FMSVER				"FMS/3,0,1,123"
#define RTMP_CAPABILITIES		31
#define RTMP_OUTPUT_CHUNK_SIZE	4096
#define RTMP_SERVER_ASYNC_START 0x12345678 // magic number, user call rtmp_server_start

enum { RTMP_SERVER_ONPLAY = 1, RTMP_SERVER_ONPUBLISH = 2};


typedef struct {
	uint8_t *data;
	uint32_t bytes;
	uint32_t timestamp;
}TagBuffer;

class RtmpConn : public CRefObject 
{
public:
    /* data */
	TagBuffer *audio_buf_ = nullptr;
	bool is_send_audio_config_ = false;
	TagBuffer *video_buf_ = nullptr;
	bool is_send_video_config_ = false;
	TagBuffer *script_buf_  = nullptr;
	bool is_send_script_metadata_ = false;
	
public:
    RtmpConn(/* args */);
    virtual ~RtmpConn();
    uint32_t GetConnHandle() { return conn_handle_; }
    char *GetPeerIP() { return (char *)peer_ip_.c_str(); }
    int Send(void *data, int len);
    void Close();
    virtual void OnConnect(net_handle_t handle);
    virtual void OnRead();
    virtual void OnWrite();
    virtual void OnClose();

    static void AddResponseData(uint32_t conn_uuid,
                                string &resp_data); // 工作线程调用
    static void SendResponseDataList();             // 主线程调用

public:
	int rtmp_server_send_onstatus(double transaction, int r, const char* success, const char* fail, const char* description);
	int rtmp_server_send_handshake();
	int rtmp_server_send_set_chunk_size();
	int rtmp_server_send_acknowledgement(size_t size);
	int rtmp_server_send_server_bandwidth();
	int rtmp_server_send_client_bandwidth();
	int rtmp_server_send_stream_is_record();
	int rtmp_server_send_stream_begin();
	int rtmp_server_rtmp_sample_access();
	void rtmp_server_destroy();
	int rtmp_server_getstate();
	int rtmp_server_input(const uint8_t* data, size_t bytes);
	int rtmp_server_start( int r, const char* msg);
	int rtmp_server_check_send_audio_config();
	int rtmp_server_check_send_video_config();
	int rtmp_server_check_send_script_metadata();
	int rtmp_server_send_audio(const void* data, size_t bytes, uint32_t timestamp);
	int rtmp_server_send_video(const void* data, size_t bytes, uint32_t timestamp);
	int rtmp_server_send_script(const void* data, size_t bytes, uint32_t timestamp);

	std::shared_ptr<LiveSource> rtmp_source_ = nullptr;
	LiveConsumer *consumer_ = nullptr;
	struct rtmp_t  rtmp;
	uint32_t recv_bytes[2]; // for rtmp_acknowledgement

	void* param;
	// struct rtmp_server_handler_t handler;

	uint8_t payload[2 * 1024];
	uint8_t handshake[2 * RTMP_HANDSHAKE_SIZE + 1]; // only for handshake
	size_t handshake_bytes;
	int handshake_state; // RTMP_HANDSHAKE_XXX

	struct rtmp_connect_t info; // Server application name, e.g.: testapp
	char stream_name[256]; // Play/Publishing stream name, flv:sample, mp3:sample, H.264/AAC: mp4:sample.m4v
	char stream_type[18]; // Publishing type: live/record/append
	uint32_t stream_id; // createStream/deleteStream
	uint8_t receiveAudio; // 1-enable audio, 0-no audio
	uint8_t receiveVideo; // 1-enable video, 0-no video

	// onpublish/onplay only
	struct
	{
		double transaction;
		int reset;
		int play; // RTMP_SERVER_ONPLAY/RTMP_SERVER_ONPUBLISH
	} start;

 protected:
    net_handle_t m_sock_handle;
    uint32_t conn_handle_;
    bool busy_;

    uint32_t state_;
    std::string peer_ip_;
    uint16_t peer_port_;
    CSimpleBuffer in_buf_;
    CSimpleBuffer out_buf_;

    uint64_t last_send_tick_;
    uint64_t last_recv_tick_;

  
 
    uint32_t uuid_;                  // 自己的uuid

    static std::mutex s_resp_mutex;
    static std::list<ResponsePdu_t *> s_response_pdu_list; // 主线程发送回复消息
};

int RtmpInitListen(std::string listen_ip, uint16_t listen_port, uint32_t thread_num);

#endif
