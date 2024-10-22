#include "app_rtmp_conn.h"
#include "thread/thread_pool.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include "util/dlog.h"
#include <map>


#define READ_BUF_SIZE 200000 // 每次尝试读取200K

static uint32_t g_conn_handle_generator = 0;
static uint32_t  s_uuid_alloctor = 0;
typedef unordered_map<uint32_t, RtmpConn *> UserMap_t;
typedef unordered_map<uint32_t, RtmpConn *> RtmpConnMap_t;

static UserMap_t s_uuid_conn_map;
static RtmpConnMap_t s_rtmp_conn_map;

static ThreadPool s_rtmp_thread_pool;

std::list<ResponsePdu_t *> RtmpConn::s_response_pdu_list;
std::mutex RtmpConn::s_resp_mutex;



// FLV Tag Type
#define FLV_TYPE_AUDIO		8
#define FLV_TYPE_VIDEO		9
#define FLV_TYPE_SCRIPT		18

class LiveConsumer
{
public:
    // TODO: add packet queue
    RtmpConn* rtmp_ = nullptr;
    struct flv_muxer_t* muxer;

    LiveConsumer(RtmpConn* rtmp) : rtmp_(rtmp)
    {
       
    }

    ~LiveConsumer()
    {
 
    }

    static int handler(void* param, int type, const void* data, size_t bytes, uint32_t timestamp)
    {
        LiveConsumer* consumer = (LiveConsumer*)param;
		int ret = 0;
		//   LogInfo("rtmp_ conn:  {}", (void *)player->rtmp_);
        switch (type)
        {
        case FLV_TYPE_SCRIPT:
			ret = consumer->rtmp_->rtmp_server_check_send_script_metadata();
            return consumer->rtmp_->rtmp_server_send_script(data, bytes, timestamp);
        case FLV_TYPE_AUDIO:
		 	ret = consumer->rtmp_->rtmp_server_check_send_audio_config();
            return consumer->rtmp_->rtmp_server_send_audio(data, bytes, timestamp);
        case FLV_TYPE_VIDEO:
			ret = consumer->rtmp_->rtmp_server_check_send_video_config();
            return consumer->rtmp_->rtmp_server_send_video(data, bytes, timestamp);		
        default:
            assert(0);
            return -1;
        }
    }
};


class LiveSource
{ 
public:
    std::list<std::shared_ptr<LiveConsumer> > players;
	RtmpConn *rtmp_conn_ = nullptr;
	std::string app_stream_;
    LiveSource(RtmpConn *rtmp_conn, std::string app_stream)
		: rtmp_conn_(rtmp_conn), app_stream_(app_stream)
    {
		
    }

    ~LiveSource()
    {
        
    }
	void update_rtmp_conn(RtmpConn *rtmp_conn) {
		rtmp_conn_ = rtmp_conn;
	}

    static int handler(void* param, int type, const uint8_t* data, size_t bytes, uint32_t timestamp)
    {
        int r = 0;
        LiveSource* s = (LiveSource*)param;
		if(s->rtmp_conn_) {		//有推流的情况下才调用player
			for (auto it = s->players.begin(); it != s->players.end(); ++it)
			{
				// TODO: push to packet queue
				// rtmp_player_t::handler()
				LiveConsumer::handler(it->get(), type, data, bytes,  timestamp);
			}
		}
        return 0; // ignore error
    }
};

static std::map<std::string, std::shared_ptr<LiveSource> > s_lives;
enum {
    CONN_STATE_IDLE,
    CONN_STATE_CONNECTED,
    CONN_STATE_OPEN,
    CONN_STATE_CLOSED,
};

void rtmp_server_onabort(void* param, uint32_t chunk_stream_id);
int rtmp_server_onaudio(void* param, const uint8_t* data, size_t bytes, uint32_t timestamp);
int rtmp_server_onvideo(void* param, const uint8_t* data, size_t bytes, uint32_t timestamp);
int rtmp_server_onscript(void* param, const uint8_t* data, size_t bytes, uint32_t timestamp);
int rtmp_server_onconnect(void* param, int r, double transaction, const struct rtmp_connect_t* connect);
int rtmp_server_oncreate_stream(void* param, int r, double transaction);
int rtmp_server_ondelete_stream(void* param, int r, double transaction, double stream_id);
int rtmp_server_onget_stream_length(void* param, int r, double transaction, const char* stream_name);
int rtmp_server_onpublish(void* param, int r, double transaction, const char* stream_name, const char* stream_type);
int rtmp_server_onplay(void* param, int r, double transaction, const char* stream_name, double start, double duration, uint8_t reset);
int rtmp_server_onpause(void* param, int r, double transaction, uint8_t pause, double milliSeconds);
int rtmp_server_onseek(void* param, int r, double transaction, double milliSeconds);
int rtmp_server_onreceive_audio(void* param, int r, double transaction, uint8_t audio);
int rtmp_server_onreceive_video(void* param, int r, double transaction, uint8_t video);
int rtmp_server_send(void* param, const uint8_t* header, uint32_t headerBytes, const uint8_t* payload, uint32_t payloadBytes);
int rtmp_server_onclose(void* param); // 传入的实际是rtmpconn

RtmpConn *FindHttpConnByHandle(uint32_t handle) {
    RtmpConn *pConn = NULL;
    RtmpConnMap_t::iterator it = s_rtmp_conn_map.find(handle);
    if (it != s_rtmp_conn_map.end()) {
        pConn = it->second;
	//	pConn->AddRef();	//添加引用计数
    }
    return pConn;
}

RtmpConn *GetRtmpConnByUuid(uint32_t uuid) {
    RtmpConn *pConn = NULL;
    UserMap_t::iterator it = s_uuid_conn_map.find(uuid);
    if (it != s_uuid_conn_map.end()) {
        pConn = (RtmpConn *)it->second;
    }
    return pConn;
}

void rtmp_conn_callback(void *callback_data, uint8_t msg, uint32_t handle,
                       uint32_t uParam, void *pParam) {
    NOTUSED_ARG(uParam);
    NOTUSED_ARG(pParam);

    // convert void* to uint32_t, oops
    uint32_t conn_handle = *((uint32_t *)(&callback_data));
    RtmpConn *pConn = FindHttpConnByHandle(conn_handle);
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
        LogError("!!!rtmp_conn_callback error msg:{}", msg);
        break;
    }
	// pConn->ReleaseRef();// 但线程的时候不需要搞引用计数
}

RtmpConn::RtmpConn(/* args */):
	CRefObject(1)
{
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
    s_uuid_conn_map.insert(make_pair(uuid_, this)); // 单线程里用的，不需要加锁
    LogInfo("conn_uuid: {}, conn_handle_: {:X}", uuid_, conn_handle_);

    stream_id = 0;
	receiveAudio = 1;
	receiveVideo = 1;
	handshake_state = RTMP_HANDSHAKE_UNINIT;

	rtmp.parser.state = RTMP_PARSE_INIT;
	rtmp.in_chunk_size = RTMP_CHUNK_SIZE;
	rtmp.out_chunk_size = RTMP_CHUNK_SIZE;
	rtmp.window_size = 5000000;
	rtmp.peer_bandwidth = 5000000;
	rtmp.buffer_length_ms = 30000;

    rtmp.param = this;
	rtmp.send = rtmp_server_send;
	rtmp.onaudio = rtmp_server_onaudio;
	rtmp.onvideo = rtmp_server_onvideo;
	rtmp.onabort = rtmp_server_onabort;
	rtmp.onscript = rtmp_server_onscript;
	rtmp.server.onconnect = rtmp_server_onconnect;
	rtmp.server.oncreate_stream = rtmp_server_oncreate_stream;
	rtmp.server.ondelete_stream = rtmp_server_ondelete_stream;
	rtmp.server.onget_stream_length = rtmp_server_onget_stream_length;
	rtmp.server.onpublish = rtmp_server_onpublish;
	rtmp.server.onplay = rtmp_server_onplay;
	rtmp.server.onpause = rtmp_server_onpause;
	rtmp.server.onseek = rtmp_server_onseek;
	rtmp.server.onreceive_audio = rtmp_server_onreceive_audio;
	rtmp.server.onreceive_video = rtmp_server_onreceive_video;

	memset(&rtmp.in_packets, 0, sizeof(struct rtmp_packet_t) * N_CHUNK_STREAM);

	rtmp.out_packets[RTMP_CHANNEL_PROTOCOL].header.cid = RTMP_CHANNEL_PROTOCOL;
	rtmp.out_packets[RTMP_CHANNEL_INVOKE].header.cid = RTMP_CHANNEL_INVOKE;
	rtmp.out_packets[RTMP_CHANNEL_AUDIO].header.cid = RTMP_CHANNEL_AUDIO;
	rtmp.out_packets[RTMP_CHANNEL_VIDEO].header.cid = RTMP_CHANNEL_VIDEO;
	rtmp.out_packets[RTMP_CHANNEL_DATA].header.cid = RTMP_CHANNEL_DATA;
}

RtmpConn::~RtmpConn()
{
	LogInfo("~RtmpConn, m_sock_handle = {}, conn_handle_ = {}", m_sock_handle , conn_handle_);
}

// 线程安全的问题，如果多个线程调用会怎么样？
// 如果send只在epoll线程被调用，out_buf_只保留还没有send的buffer
// 既然这里我们是用了缓存，那调用Send意味着数据都已经被处理了
int RtmpConn::Send(void *data, int len) 
{ 
    last_send_tick_ = GetTickCount();

    if (busy_) {
        out_buf_.Write(data, len);
        return 0;
    }

    int ret = netlib_send(m_sock_handle, data, len);
    if (ret < 0) {
		LogError("m_sock_handle: {}, send failed, ret = {}", m_sock_handle, ret);
        ret = 0;
	}

    if (ret < len) {
        out_buf_.Write(
            (char *)data + ret,
            len - ret); // 保存buffer里面，下次reactor write触发后再发送
        busy_ = true;
        LogInfo("not send all={}, remain= {}", len, out_buf_.GetWriteOffset());
    } else {
            // 已经发送完毕了
        LogDebug("send all size:{}", ret);
    }

    return 0;
}

void RtmpConn::Close() 
{
	rtmp_server_onclose(this);
 
    LogInfo("Close handle = {}", conn_handle_);
    state_ = CONN_STATE_CLOSED;
    s_rtmp_conn_map.erase(conn_handle_);
    s_uuid_conn_map.erase(uuid_); // 移除uuid
    netlib_close(m_sock_handle);

    ReleaseRef();
}

//这个是虚函数
void RtmpConn::OnConnect(net_handle_t handle) 
{
    m_sock_handle = handle;
	LogInfo("RtmpConn, m_sock_handle = {}, conn_handle_ = {}", m_sock_handle , conn_handle_);
    state_ = CONN_STATE_CONNECTED;
    s_rtmp_conn_map.insert(make_pair(conn_handle_, this));

    netlib_option(handle, NETLIB_OPT_SET_CALLBACK, (void *)rtmp_conn_callback);
    netlib_option(handle, NETLIB_OPT_SET_CALLBACK_DATA,
                reinterpret_cast<void *>(conn_handle_));
    netlib_option(handle, NETLIB_OPT_GET_REMOTE_IP, (void *)&peer_ip_);
}

void RtmpConn::OnRead() 
{
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

    // 分析是否符号rtmp的需求，不断进行解析
    uint8_t *in_buf =  in_buf_.GetBuffer();
    uint32_t buf_len = in_buf_.GetWriteOffset();
	LogDebug("recv: {}", buf_len);
    // 第一步 握手
    int remain =  rtmp_server_input(in_buf, buf_len);
    in_buf_.Skip(buf_len - remain);

    // 第二步

    // 第三步
}

void RtmpConn::OnWrite() {
	// LogInfo("busy_: {}", busy_);
    if (!busy_)
        return; // 没有数据可写
    //发送数据
    int ret = netlib_send(m_sock_handle, out_buf_.GetBuffer(),
                        out_buf_.GetWriteOffset());
    if (ret < 0)
        ret = 0;
    //缓存总长
    int out_buf_size = (int)out_buf_.GetWriteOffset();
    // 跳过已经发送的数据
    out_buf_.Read(NULL, ret);

    if (ret < out_buf_size) {   // 还没有发送完毕
        busy_ = true;
        LogInfo("not send all, remain = {}", out_buf_.GetWriteOffset());
    } else {
        // 已经发送完毕
        busy_ = false;
    }
}

void RtmpConn::OnClose() 
{
    Close();
}

void RtmpConn::AddResponseData(uint32_t conn_uuid, string &resp_data) 
{
    LogDebug("into");
    ResponsePdu_t *pResp = new ResponsePdu_t;
    pResp->conn_uuid = conn_uuid;
    pResp->resp_data = std::move(resp_data);

    s_resp_mutex.lock();
    s_response_pdu_list.push_back(pResp);
    s_resp_mutex.unlock();
}

void RtmpConn::SendResponseDataList() 
{
     LogDebug("into");
    // 发送数据
    s_resp_mutex.lock();
    while (!s_response_pdu_list.empty()) {
        ResponsePdu_t *pResp = s_response_pdu_list.front();
        s_response_pdu_list.pop_front();
        s_resp_mutex.unlock();
        RtmpConn *pConn = GetRtmpConnByUuid(pResp->conn_uuid); // 该连接有可能已经被释放，如果被释放则返回NULL
        // LogInfo("conn_uuid: {}", pResp->conn_uuid); //{0:x}
        if (pConn) {
            // LogInfo("send: {}", pResp->resp_data);
            pConn->Send((void *)pResp->resp_data.c_str(),
                        pResp->resp_data.size());  // 最终socket send
        }
        delete pResp;

        s_resp_mutex.lock();
    }

    s_resp_mutex.unlock();
}

void rtmp_callback(void *callback_data, uint8_t msg, uint32_t handle, void *pParam) 
{
    if (msg == NETLIB_MSG_CONNECT) {
        // 这里是不是觉得很奇怪,为什么new了对象却没有释放?
        // 实际上对象在被Close时使用delete this的方式释放自己
        RtmpConn *pConn = new RtmpConn();
        pConn->OnConnect(handle);
    } else {
        LogError("!!!error msg:{}", msg);
    }
}

void rtmp_loop_callback(void *callback_data, uint8_t msg, uint32_t handle,
                        void *pParam) {
    UNUSED(callback_data);
    UNUSED(msg);
    UNUSED(handle);
    UNUSED(pParam);
    RtmpConn::SendResponseDataList(); // 静态函数, 将要发送的数据循环发给客户端
}

// 每个业务有自己的线程池，定时器保活后续再添加
int RtmpInitListen(std::string listen_ip, uint16_t listen_port, uint32_t thread_num)
{
    s_rtmp_thread_pool.init(thread_num);
    s_rtmp_thread_pool.start();
    netlib_add_loop(rtmp_loop_callback,NULL);

    int ret = netlib_listen(listen_ip.c_str(), listen_port, rtmp_callback, NULL);
    if (ret == NETLIB_ERROR)
    {
        LogError("netlib_listen failed, errno: {}, error: {}", errno, strerror(errno));
        return ret;
    }
    return NETLIB_OK;
}





int rtmp_server_send_control(struct rtmp_t* rtmp, const uint8_t* payload, uint32_t bytes, uint32_t stream_id)
{
	struct rtmp_chunk_header_t header;
	header.fmt = RTMP_CHUNK_TYPE_0; // disable compact header
	header.cid = RTMP_CHANNEL_INVOKE;
	header.timestamp = 0;
	header.length = bytes;
	header.type = RTMP_TYPE_INVOKE;
	header.stream_id = stream_id; /* default 0 */
	return rtmp_chunk_write(rtmp, &header, payload);
}

int RtmpConn::rtmp_server_send_onstatus(double transaction, int r, const char* success, const char* fail, const char* description)
{
	r = (int)(rtmp_netstream_onstatus(this->payload, sizeof(this->payload), transaction, 
        0==r ? RTMP_LEVEL_STATUS : RTMP_LEVEL_ERROR, 0==r ? success : fail, description) - this->payload);
	return rtmp_server_send_control(&this->rtmp, this->payload, r, this->stream_id);
}

// handshake
int RtmpConn::rtmp_server_send_handshake()
{
    LogDebug("Send rtmp_handshake_s0 s1 s2");
	int n, r;
	n = rtmp_handshake_s0(this->handshake, RTMP_VERSION);
	n += rtmp_handshake_s1(this->handshake + n, (uint32_t)time(NULL), this->payload, RTMP_HANDSHAKE_SIZE);
	n += rtmp_handshake_s2(this->handshake + n, (uint32_t)time(NULL), this->payload, RTMP_HANDSHAKE_SIZE);
	assert(n == 1 + RTMP_HANDSHAKE_SIZE + RTMP_HANDSHAKE_SIZE);
    r = this->Send(this->handshake, n);
	return n == r ? 0 : r;      // 返回0是正常
}

/// 5.4.1. Set Chunk Size (1)
int RtmpConn::rtmp_server_send_set_chunk_size()
{
	int n, r;
	n = rtmp_set_chunk_size(this->payload, sizeof(this->payload), RTMP_OUTPUT_CHUNK_SIZE);
	r =this->Send(this->payload, n);
	this->rtmp.out_chunk_size = RTMP_OUTPUT_CHUNK_SIZE;
	return n == r ? 0 : r;
}

/// 5.4.3. Acknowledgement (3)
int RtmpConn::rtmp_server_send_acknowledgement(size_t size)
{
	int n, r;
	this->recv_bytes[0] += (uint32_t)size;
	if (this->rtmp.window_size && this->recv_bytes[0] - this->recv_bytes[1] > this->rtmp.window_size)
	{
		n = rtmp_acknowledgement(this->payload, sizeof(this->payload), this->recv_bytes[0]);
		r = this->Send(this->payload, n);
		this->recv_bytes[1] = this->recv_bytes[0];
		return n == r ? 0 : r;
	}
	return 0;
}

/// 5.4.4. Window Acknowledgement Size (5)
int RtmpConn::rtmp_server_send_server_bandwidth()
{
	int n, r;
	n = rtmp_window_acknowledgement_size(this->payload, sizeof(this->payload), this->rtmp.window_size);
	r = this->Send(this->payload, n);
	return n == r ? 0 : r;
}

/// 5.4.5. Set Peer Bandwidth (6)
int RtmpConn::rtmp_server_send_client_bandwidth()
{
	int n, r;
	n = rtmp_set_peer_bandwidth(this->payload, sizeof(this->payload), this->rtmp.peer_bandwidth, RTMP_BANDWIDTH_LIMIT_DYNAMIC);
	r = this->Send(this->payload, n);
	return n == r ? 0 : r;
}

int RtmpConn::rtmp_server_send_stream_is_record()
{
	int n, r;
	n = rtmp_event_stream_is_record(this->payload, sizeof(this->payload), this->stream_id);
	r = this->Send(this->payload, n);
	return n == r ? 0 : r;
}

int RtmpConn::rtmp_server_send_stream_begin()
{
	int n, r;
	n = rtmp_event_stream_begin(this->payload, sizeof(this->payload), this->stream_id);
	r = this->Send(this->payload, n);
	return n == r ? 0 : r;
}

int RtmpConn::rtmp_server_rtmp_sample_access()
{
	int n;
	struct rtmp_chunk_header_t header;
	
	n = (int)(rtmp_netstream_rtmpsampleaccess(this->payload, sizeof(this->payload)) - this->payload);

	header.fmt = RTMP_CHUNK_TYPE_0; // disable compact header
	header.cid = RTMP_CHANNEL_INVOKE;
	header.timestamp = 0;
	header.length = n;
	header.type = RTMP_TYPE_DATA;
	header.stream_id = this->stream_id;
	return rtmp_chunk_write(&this->rtmp, &header, this->payload);
}

void  rtmp_server_onabort(void *param, uint32_t chunk_stream_id)
{
    LogInfo("into");
	RtmpConn *ctx = (RtmpConn*)param;
	// (void)(void)chunk_stream_id;
//	this->handler.onerror(-1, "client abort");
}
// 第一个音频帧
int rtmp_server_onaudio(void* param, const uint8_t* data, size_t bytes, uint32_t timestamp)
{
    LogDebug("into, bytes: {}", bytes);
    RtmpConn *ctx = (RtmpConn*)param;
	if(!ctx->audio_buf_) {
		ctx->audio_buf_ = new TagBuffer;
		ctx->audio_buf_->bytes = bytes;
		ctx->audio_buf_->timestamp = timestamp;
		ctx->audio_buf_->data = new uint8_t[bytes];
		memcpy(ctx->audio_buf_->data, data, bytes);
		LogInfo("get audio_specific_config, bytes: {}", bytes);
	}
	// 先找到对应的source

	LiveSource::handler(ctx->rtmp_source_.get(), FLV_TYPE_AUDIO, data, bytes, timestamp);
	// return this->handler.onaudio(data, bytes, timestamp);
    return 0;
}
// 第一个视频帧这些都要先存在
int rtmp_server_onvideo(void* param, const uint8_t* data, size_t bytes, uint32_t timestamp)
{
    LogDebug("into, bytes: {}", bytes);
    RtmpConn *ctx = (RtmpConn*)param;
	if(!ctx->video_buf_) {
		ctx->video_buf_ = new TagBuffer;
		ctx->video_buf_->bytes = bytes;
		ctx->video_buf_->timestamp = timestamp;
		ctx->video_buf_->data = new uint8_t[bytes];
		memcpy(ctx->video_buf_->data, data, bytes);
		LogInfo("get avc_decoder_configuration_record, bytes: {}", bytes);
	}

	LiveSource::handler(ctx->rtmp_source_.get(), FLV_TYPE_VIDEO, data, bytes, timestamp);
	// return this->handler.onvideo(data, bytes, timestamp);
    return 0;
}

int rtmp_server_onscript(void* param, const uint8_t* data, size_t bytes, uint32_t timestamp)
{
    LogInfo("into");
    RtmpConn *ctx = (RtmpConn*)param;
	if(!ctx->script_buf_) {
		ctx->script_buf_ = new TagBuffer;
		ctx->script_buf_->bytes = bytes;
		ctx->script_buf_->timestamp = timestamp;
		ctx->script_buf_->data = new uint8_t[bytes];
		memcpy(ctx->script_buf_->data, data, bytes);
		LogInfo("get script metadata, bytes: {}", bytes);
	}
	LiveSource::handler(ctx->rtmp_source_.get(), FLV_TYPE_SCRIPT, data, bytes, timestamp);
	// return this->handler.onscript(data, bytes, timestamp);
    return 0;
}

// 7.2.1.1. connect (p29)
// _result/_error
int  rtmp_server_onconnect(void* param, int r, double transaction, const struct rtmp_connect_t* connect)
{
	int n;
	assert((double)RTMP_ENCODING_AMF_0 == connect->encoding || (double)RTMP_ENCODING_AMF_3 == connect->encoding);
    RtmpConn *ctx = (RtmpConn*)param;
	if (0 == r)
	{
		assert(1 == (int)transaction);
		memcpy(&ctx->info, connect, sizeof(ctx->info));
		r = ctx->rtmp_server_send_server_bandwidth();
		r = 0 == r ? ctx->rtmp_server_send_client_bandwidth() : r;
		r = 0 == r ? ctx->rtmp_server_send_set_chunk_size() : r;
	}

	if(0 == r)
	{
		n = (int)(rtmp_netconnection_connect_reply(ctx->payload, sizeof(ctx->payload), transaction, 
                RTMP_FMSVER, RTMP_CAPABILITIES, "NetConnection.Connect.Success", RTMP_LEVEL_STATUS, 
                "Connection Succeeded.", connect->encoding) - ctx->payload);
		r = rtmp_server_send_control(&ctx->rtmp, ctx->payload, n, 0);
	}

	return r;
}

// 7.2.1.3. createStream (p36)
// _result/_error
int rtmp_server_oncreate_stream(void* param, int r, double transaction)
{
    RtmpConn *ctx = (RtmpConn*)param;
	if (0 == r)
	{
		ctx->stream_id = 1;
		//r = this->handler.oncreate_stream(&this->stream_id);
		if (0 == r)
			r = (int)(rtmp_netconnection_create_stream_reply(ctx->payload, sizeof(ctx->payload),
                transaction, ctx->stream_id) - ctx->payload);
		else
			r = (int)(rtmp_netconnection_error(ctx->payload, sizeof(ctx->payload), transaction, 
                "NetConnection.CreateStream.Failed", RTMP_LEVEL_ERROR, "createStream failed.") - ctx->payload);
		r = rtmp_server_send_control(&ctx->rtmp, ctx->payload, r, 0/*this->stream_id*/); // must be 0
	}

	return r;
}

// 7.2.2.3. deleteStream (p43)
// The server does not send any response
int rtmp_server_ondelete_stream(void* param, int r, double transaction, double stream_id)
{
    RtmpConn *ctx = (RtmpConn*)param;
	if (0 == r)
	{
		stream_id = ctx->stream_id = 0; // clear stream id
		//r = this->handler.ondelete_stream((uint32_t)stream_id);
		r = ctx->rtmp_server_send_onstatus(transaction, r, "NetStream.DeleteStream.Suceess", "NetStream.DeleteStream.Failed", "");
	}

	return r;
}

int rtmp_server_onget_stream_length(void* param, int r, double transaction, const char* stream_name)
{
	double duration = -1;
    RtmpConn *ctx = (RtmpConn*)param; 

	if (0 == r )
    // && this->handler.ongetduration)
	{
		// get duration (seconds)
		r = 0;// this->handler.ongetduration(this->info.app, stream_name, &duration);
		if (0 == r)
		{
			r = (int)(rtmp_netconnection_get_stream_length_reply(ctx->payload, sizeof(ctx->payload), transaction, duration) - ctx->payload);
			r = rtmp_server_send_control(&ctx->rtmp, ctx->payload, r, ctx->stream_id);
		}
	}

	return r;
}

// 7.2.2.6. publish (p45)
// The server responds with the onStatus command
// 推流
int rtmp_server_onpublish(void* param, int r, double transaction, const char* stream_name, const char* stream_type)
{
    RtmpConn *ctx = (RtmpConn*)param; 
	std::string key(ctx->info.app);
    key += "/";
    key += stream_name;
	LogWarn("into, key: {}, stream_type: {}", key, stream_type);
	if (0 == r)
	{
		ctx->start.play = RTMP_SERVER_ONPUBLISH;
		ctx->start.transaction = transaction;
		snprintf(ctx->stream_name, sizeof(ctx->stream_name) - 1, "%s", stream_name ? stream_name : "");
		snprintf(ctx->stream_type, sizeof(ctx->stream_type) - 1, "%s", stream_type ? stream_type : "");

		r = 0; // this->handler.onpublish(this->info.app, stream_name, stream_type);
		if (RTMP_SERVER_ASYNC_START == r || 0 == ctx->start.play)
			return RTMP_SERVER_ASYNC_START == r ? 0 : r;

		r = ctx->rtmp_server_start(r, NULL);
	}
	std::shared_ptr<LiveSource> source = nullptr;
	auto it = s_lives.find(key);
	if(it == s_lives.end()) {		// source没有找到
    	std::shared_ptr<LiveSource> source(new LiveSource(ctx, key));
			// assert();  // 之前的如果不删除会有这个问题
		s_lives[key] = source;
		ctx->rtmp_source_ = source;
	} else {	// source还存在的情况下
		ctx->rtmp_source_ = it->second;				// source已经创建过了
		ctx->rtmp_source_->update_rtmp_conn(ctx);	// 将rtmp source的rtmp conn更新为当前连接
	}
    
	return r;
}

// 7.2.2.1. play (p38)
// reply onStatus NetStream.Play.Start & NetStream.Play.Reset
int rtmp_server_onplay(void* param, int r, double transaction, const char* stream_name, double start, double duration, uint8_t reset)
{
    RtmpConn *ctx = (RtmpConn*)param; 
	// LogInfo("%s, %s, %f, %f, %d)\n", app, stream, start, duration, (int)reset);
    std::string key(ctx->info.app);
    key += "/";
    key += stream_name;

	LogInfo("into");
	if (0 == r)
	{
		ctx->start.play = RTMP_SERVER_ONPLAY;
		ctx->start.reset = reset;
		ctx->start.transaction = transaction;
		snprintf(ctx->stream_name, sizeof(ctx->stream_name) - 1, "%s", stream_name ? stream_name : "");
		snprintf(ctx->stream_type, sizeof(ctx->stream_type) - 1, "%s", -1 == start ? RTMP_STREAM_LIVE : RTMP_STREAM_RECORD);

		r =  0; //this->handler.onplay(this->info.app, stream_name, start, duration, reset);
		if (RTMP_SERVER_ASYNC_START == r || 0 == ctx->start.play)
			return RTMP_SERVER_ASYNC_START == r ? 0 : r;

		r = ctx->rtmp_server_start(r, NULL);
	}

	std::shared_ptr<class LiveSource> s;
	auto it = s_lives.find(key);
	if (it == s_lives.end())
	{
		LogWarn("source({}) not found, we create it and wait publisher\n", key);
		std::shared_ptr<LiveSource> source(new LiveSource(ctx, key));
	 	s = source;
		s_lives[key] = s;
	} else {
		s = it->second;
	}
	for (auto j = s->players.begin(); j != s->players.end(); ++j)
	{
		if (j->get()->rtmp_ == ctx)
		{
			LogError("source({}), rtmp conn({}) repeat join\n", key, param );
			return  -1;
		}
	}
	std::shared_ptr<LiveConsumer> player(new LiveConsumer(ctx));
	s->players.push_back(player);
	LogWarn("source app stream: {},  players: {}", key, s->players.size());
	//   LogInfo("rtmp_ conn 2:  {}",  (void *)player->rtmp_);
	ctx->rtmp_source_ = s;			// 保留source
	ctx->consumer_ = player.get();		// 通过裸指针判断
	// 这里发？ 还是在收到新的audio、video再发？
	// 发送metadata
	// 发送 avc_decoder_configuration_record
	// 发送 audio_specific_config
	return r;
}

// 7.2.2.8. pause (p47)
// sucessful: NetStream.Pause.Notify/NetStream.Unpause.Notify
// failure:  _error message
int rtmp_server_onpause(void* param, int r, double transaction, uint8_t pause, double milliSeconds)
{
    RtmpConn *ctx = (RtmpConn*)param; 
	if (0 == r)
	{
		// r = this->handler.onpause(pause, (uint32_t)milliSeconds);
		r = ctx->rtmp_server_send_onstatus(transaction, r, pause ? "NetStream.Pause.Notify" : "NetStream.Unpause.Notify", "NetStream.Pause.Failed", "");
	}

	return r;
}

// 7.2.2.7. seek (p46)
// successful : NetStream.Seek.Notify
// failure:  _error message
int rtmp_server_onseek(void* param, int r, double transaction, double milliSeconds)
{
    RtmpConn *ctx = (RtmpConn*)param; 
	if (0 == r)
	{
		// r = this->handler.onseek((uint32_t)milliSeconds);
		r = ctx->rtmp_server_send_onstatus(transaction, r, "NetStream.Seek.Notify", "NetStream.Seek.Failed", "");
	}

	return r;
}

// 7.2.2.4. receiveAudio (p44)
// false: The server does not send any response,
// true: server responds with status messages NetStream.Seek.Notify and NetStream.Play.Start
int rtmp_server_onreceive_audio(void* param, int r, double transaction, uint8_t audio)
{
    RtmpConn *ctx = (RtmpConn*)param; 
	if(0 == r)
	{
		ctx->receiveAudio = audio;
		if (audio)
		{
			r = ctx->rtmp_server_send_onstatus(transaction, r, "NetStream.Seek.Notify", "NetStream.Seek.Failed", "");
			r = ctx->rtmp_server_send_onstatus(transaction, r, "NetStream.Play.Start", "NetStream.Play.Failed", "");
		}
	}

	return r;
}

int rtmp_server_onreceive_video(void* param, int r, double transaction, uint8_t video)
{
    RtmpConn *ctx = (RtmpConn*)param; 
	if(0 == r)
	{
		ctx->receiveVideo = video;
		if (video)
		{
			r = ctx->rtmp_server_send_onstatus(transaction, r, "NetStream.Seek.Notify", "NetStream.Seek.Failed", "");
			r = ctx->rtmp_server_send_onstatus(transaction, r, "NetStream.Play.Start", "NetStream.Play.Failed", "");
		}
	}

	return r;
}

int  rtmp_server_send(void* param, const uint8_t* header, uint32_t headerBytes, const uint8_t* payload, uint32_t payloadBytes)
{
	
    RtmpConn *ctx = (RtmpConn *) param;
	LogDebug("headerBytes: {}, payloadBytes: {}", headerBytes, payloadBytes);
	int r;
	r = ctx->Send((void*)header, headerBytes);
    r = ctx->Send((void*)payload, payloadBytes);
	return  0; //(r == (int)(payloadBytes + headerBytes)) ? 0 : -1;
}

int rtmp_server_onclose(void* param)
{
	RtmpConn *ctx = (RtmpConn *) param;

	if(ctx->consumer_) {
		//如果是拉流play
		for (auto j = ctx->rtmp_source_->players.begin(); j != ctx->rtmp_source_->players.end(); ++j)
		{
			if (j->get() == ctx->consumer_)
			{
				ctx->rtmp_source_->players.erase(j);
				break;
			}
		}
	} else { // 如果是推流
		// 先分析当前player，如果当前source还有player则则不能释放source
		if(ctx->rtmp_source_->players.size() > 0)  {
			// 只释放链接
			ctx->rtmp_source_->rtmp_conn_ = nullptr;	// 设置为空
		} else {
			for (auto j = s_lives.begin(); j != s_lives.end(); ++j)
			{
				if (j->second.get()  == ctx->rtmp_source_.get())
				{
					LogWarn("release rtmp source: {}\n", ctx->info.app);
					s_lives.erase(j);	// 移除这个source
				}
			}
		}
	}
}

void RtmpConn::rtmp_server_destroy()
{
	size_t i;
	assert(sizeof(this->rtmp.in_packets) == sizeof(this->rtmp.out_packets));
	for (i = 0; i < N_CHUNK_STREAM; i++)
	{
		assert(NULL == this->rtmp.out_packets[i].payload);
		if (this->rtmp.in_packets[i].payload)
			free(this->rtmp.in_packets[i].payload);
	}
}

int RtmpConn::rtmp_server_getstate()
{
	return this->handshake_state;
}

//分析数据
// 一般c0和c1一起发送，s0和s1一起发送
int RtmpConn::rtmp_server_input(const uint8_t* data, size_t bytes)
{
	int r;
	size_t n;
	const uint8_t* p;

	p = data;
	while (bytes > 0)
	{
		switch (this->handshake_state)
		{
		case RTMP_HANDSHAKE_UNINIT: // C0: version
            LogDebug("RTMP_HANDSHAKE_UNINIT -> RTMP_HANDSHAKE_0");
			this->handshake_state = RTMP_HANDSHAKE_0;
			this->handshake_bytes = 0; // clear buffer
			assert(*p <= RTMP_VERSION);
			bytes -= 1;
			p += 1;  // C0 和 S0 包由一个字节组成 , 所以这里跳过1字节
			break;

		case RTMP_HANDSHAKE_0: // C1: 4-time + 4-zero + 1528-random
            LogDebug("RTMP_HANDSHAKE_0 -> RTMP_HANDSHAKE_1");
			assert(RTMP_HANDSHAKE_SIZE > this->handshake_bytes);
			n = RTMP_HANDSHAKE_SIZE - this->handshake_bytes;
			n = n <= bytes ? n : bytes;
			memcpy(this->payload + this->handshake_bytes, p, n);    // 缓存握手数据
			this->handshake_bytes += n;
			bytes -= n;
			p += n;

			if (this->handshake_bytes == RTMP_HANDSHAKE_SIZE)       // 解析到完整的c1
			{
				this->handshake_state = RTMP_HANDSHAKE_1;
				this->handshake_bytes = 0; // clear buffer
				r = rtmp_server_send_handshake();
				if(0 != r) return r;
			}
			break;

		case RTMP_HANDSHAKE_1: // C2: 4-time + 4-time2 + 1528-echo
            LogDebug("RTMP_HANDSHAKE_1 -> RTMP_HANDSHAKE_2");
			assert(RTMP_HANDSHAKE_SIZE > this->handshake_bytes);
			n = RTMP_HANDSHAKE_SIZE - this->handshake_bytes;
			n = n <= bytes ? n : bytes;
			memcpy(this->payload + this->handshake_bytes, p, n);
			this->handshake_bytes += n;
			bytes -= n;
			p += n;

			if (this->handshake_bytes == RTMP_HANDSHAKE_SIZE)
			{
				this->handshake_state = RTMP_HANDSHAKE_2;   //握手已经成功
				this->handshake_bytes = 0; // clear buffer
			}
			break;

		case RTMP_HANDSHAKE_2:  // 握手成功后
		default:
			rtmp_server_send_acknowledgement(bytes);        // 内部计算累计数据，如果需要ack则ack
			return rtmp_chunk_read(&this->rtmp, (const uint8_t*)p, bytes);  // 重点是这个函数的进一步处理
		}
	}

	return 0;       // 返回0说明数据都已经处理完毕了
}

int RtmpConn::rtmp_server_start( int r, const char* msg)
{
	if (RTMP_SERVER_ONPLAY == this->start.play)
	{
		if (0 == r)
		{
			// User Control (StreamBegin)
			r = 0 == r ? rtmp_server_send_stream_begin() : r;

			// NetStream.Play.Reset
			if (this->start.reset) r = 0 == r ? rtmp_server_send_onstatus(this->start.transaction, 0, "NetStream.Play.Reset", "NetStream.Play.Failed", "") : r;

			if (0 != r)
				return r;
		}

		r = rtmp_server_send_onstatus(this->start.transaction, r, "NetStream.Play.Start", msg && *msg ? msg : "NetStream.Play.Failed", "Start video on demand");

		// User Control (StreamIsRecorded)
		r = 0 == r ? rtmp_server_send_stream_is_record() : r;
		r = 0 == r ? rtmp_server_rtmp_sample_access() : r;
	}
	else if(RTMP_SERVER_ONPUBLISH == this->start.play)
	{
		if (0 == r)
		{
			// User Control (StreamBegin)
			r = rtmp_server_send_stream_begin();
			if (0 != r)
				return r;
		}

		r = rtmp_server_send_onstatus( this->start.transaction, r, "NetStream.Publish.Start", msg && *msg ? msg : "NetStream.Publish.BadName", "");
	}

	this->start.play = 0;
	return r;
}

int RtmpConn::rtmp_server_check_send_audio_config() 
{ 
	if(rtmp_source_->rtmp_conn_->audio_buf_ && !is_send_audio_config_) {
		rtmp_server_send_audio(rtmp_source_->rtmp_conn_->audio_buf_->data, 
							rtmp_source_->rtmp_conn_->audio_buf_->bytes, 
							rtmp_source_->rtmp_conn_->audio_buf_->timestamp);
		is_send_audio_config_ = true;
	}
	return 0; 
}

int RtmpConn::rtmp_server_check_send_video_config() 
{
	if(rtmp_source_->rtmp_conn_->video_buf_ && !is_send_video_config_) {
		rtmp_server_send_video(rtmp_source_->rtmp_conn_->video_buf_->data, 
				rtmp_source_->rtmp_conn_->video_buf_->bytes, rtmp_source_->rtmp_conn_->video_buf_->timestamp);
		is_send_video_config_ = true;
	}
	return 0;  
}

int RtmpConn::rtmp_server_check_send_script_metadata() 
{
	if(rtmp_source_->rtmp_conn_->script_buf_ && !is_send_script_metadata_) {
		rtmp_server_send_script(rtmp_source_->rtmp_conn_->script_buf_->data,
				 rtmp_source_->rtmp_conn_->script_buf_->bytes, rtmp_source_->rtmp_conn_->script_buf_->timestamp);
		is_send_script_metadata_ = true;
	}
	return 0;  
}

int RtmpConn::rtmp_server_send_audio(const void* data, size_t bytes, uint32_t timestamp)
{
	struct rtmp_chunk_header_t header;
	if (0 == this->receiveAudio)
		return 0; // client don't want receive audio

	header.fmt = RTMP_CHUNK_TYPE_1; // enable compact header
	header.cid = RTMP_CHANNEL_AUDIO;
	header.timestamp = timestamp;
	header.length = (uint32_t)bytes;
	header.type = RTMP_TYPE_AUDIO;
	header.stream_id = this->stream_id;

	return rtmp_chunk_write(&this->rtmp, &header, (const uint8_t*)data);
}

// 推流者调用拉流者的接口发送视频
// 可以考虑gop缓存的问题
int RtmpConn::rtmp_server_send_video(const void* data, size_t bytes, uint32_t timestamp)
{
	struct rtmp_chunk_header_t header;
	if (0 == this->receiveVideo)
		return 0; // client don't want receive video

	header.fmt = RTMP_CHUNK_TYPE_1; // enable compact header
	header.cid = RTMP_CHANNEL_VIDEO;
	header.timestamp = timestamp;
	header.length = (uint32_t)bytes;
	header.type = RTMP_TYPE_VIDEO;
	header.stream_id = this->stream_id;

	return rtmp_chunk_write(&this->rtmp, &header, (const uint8_t*)data);
}

int RtmpConn::rtmp_server_send_script(const void* data, size_t bytes, uint32_t timestamp)
{
	struct rtmp_chunk_header_t header;
	header.fmt = RTMP_CHUNK_TYPE_1; // enable compact header
	header.cid = RTMP_CHANNEL_INVOKE;
	header.timestamp = timestamp;
	header.length = (uint32_t)bytes;
	header.type = RTMP_TYPE_DATA;
	header.stream_id = this->stream_id;

	return rtmp_chunk_write(&this->rtmp, &header, (const uint8_t*)data);
}
