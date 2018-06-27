#include <gss_transport.h>
#include <pjnath/errno.h>
#include <pjnath/p2p_global.h>
#include <pjnath/gss_conn.h>
#include <common/gss_protocol.h>
#include <pjnath/p2p_smooth.h>

//client audio and video connection 
typedef struct gss_client_av_conn
{
	gss_conn* gssc;

	gss_client_conn_cb cb; /*call functions*/

	void *user_data;

	//video or audio frame cache
	char* frame_buf;
	unsigned int frame_len;
	unsigned int frame_capacity;

	struct p2p_smooth* smooth;
}gss_client_av_conn;


//callback to free memory of gss_client_av_conn
static void client_av_on_destroy(void *conn, void *user_data)
{
	gss_client_av_conn *av_conn = (gss_client_av_conn*)user_data;
	if(!conn)
		return;

	PJ_LOG(4,(av_conn->gssc->obj_name, "client_av_on_destroy %p", av_conn));


	if(av_conn->frame_buf)
	{
		p2p_free(av_conn->frame_buf);
		av_conn->frame_buf = NULL;
	}
}

static void client_av_on_disconnect(void *conn, void* user_data, int status)
{
	gss_client_av_conn *av_conn = (gss_client_av_conn*)user_data;
	PJ_UNUSED_ARG(conn);

	if(av_conn->cb.on_disconnect)
		(*av_conn->cb.on_disconnect)(av_conn, av_conn->user_data, status);

	PJ_LOG(4,(av_conn->gssc->obj_name, "client_av_on_disconnect %p, status=%d", av_conn, status));
}

static void client_av_connect_result(void *conn, void* user_data, int status)
{
	gss_client_av_conn *av_conn = (gss_client_av_conn*)user_data;
	PJ_UNUSED_ARG(conn);

	PJ_LOG(4,(av_conn->gssc->obj_name, "client_av_connect_result %p, status=%d", av_conn, status));

	if(status == PJ_SUCCESS)
	{
		//send login command to server
		GSS_LOGIN_CMD login_cmd;
		login_cmd.type = GSS_CLIENT_AV_LOGIN_CMD;
		strcpy(login_cmd.uid, av_conn->gssc->uid.ptr);
		gss_conn_send(av_conn->gssc,
			(char*)&login_cmd, 
			sizeof(GSS_LOGIN_CMD), 
			NULL, 
			0,
			GSS_CLIENT_AV_LOGIN_CMD,
			P2P_SEND_BLOCK);
	}
	else
	{
		if(av_conn->cb.on_connect_result)
			(*av_conn->cb.on_connect_result)(av_conn, av_conn->user_data, status);
	}
}

static void client_av_realloc_frame(gss_client_av_conn *av_conn, int add_len)
{
	unsigned int capacity;

	capacity = av_conn->frame_len + add_len;

	if(capacity > av_conn->frame_capacity)
	{
		char* frame_buf;
		unsigned int frame_capacity = av_conn->frame_capacity;

		frame_capacity *= 2;
		while(frame_capacity < capacity)
			frame_capacity *= 2;

		if(frame_capacity > MAX_AV_FRAME_LEN)
		{
			PJ_LOG(1,(av_conn->gssc->obj_name, "client_av_realloc_frame %p, frame too long", av_conn));
			av_conn->frame_len = 0;
			return;
		}

		av_conn->frame_capacity = frame_capacity;
		PJ_LOG(4,(av_conn->gssc->obj_name, "client_av_realloc_frame %p, capacity %d", av_conn, frame_capacity));

		frame_buf = (char*)p2p_malloc(frame_capacity);
		memcpy(frame_buf, av_conn->frame_buf, av_conn->frame_len);
		p2p_free(av_conn->frame_buf);
		av_conn->frame_buf = frame_buf;
	}
}

//#pragma   pack(1)
//typedef struct P2pHead_t
//{
//	int  			flag;		//消息开始标识
//	unsigned int 	size;		//接收发送消息大小(不包括消息头)
//	char 			type;		//协议类型1 json 2 json 加密
//	char			protoType;	//消息类型1 请求2应答3通知
//	int 			msgType;	//IOTYPE消息类型
//	char 			reserve[6];	//保留
//}P2pHead;
//#pragma   pack()
//
//typedef struct _gos_frame_head
//{
//	unsigned int	nFrameNo;			// 帧号
//	unsigned int	nFrameType;			// 帧类型	gos_frame_type_t
//	unsigned int	nCodeType;			// 编码类型 gos_codec_type_t
//	unsigned int	nFrameRate;			// 视频帧率，音频采样率
//	unsigned int	nTimestamp;			// 时间戳
//	unsigned short	sWidth;				// 视频宽
//	unsigned short	sHeight;			// 视频高
//	unsigned int	reserved;			// 预留
//	unsigned int	nDataSize;			// data数据长度
//	char			data[0];
//}gos_frame_head;

static void gss_client_av_smooth_data(gss_client_av_conn *av_conn, int type, char* data, int len)
{
	/*gos_frame_head* head = (gos_frame_head*)(data+sizeof(P2pHead));

	PJ_LOG(4,(av_conn->gssc->obj_name, "gss_client_av_smooth_data %p,len=%d,nFrameType=%d,nTimestamp=%d", av_conn, len, head->nFrameType, head->nTimestamp));*/

	if(av_conn->smooth && type == GSS_REALPLAY_DATA)
	{
		p2p_smooth_push(av_conn->smooth, data, len);
	}
	else
	{
		if(av_conn->cb.on_recv)
			(*av_conn->cb.on_recv)(av_conn, av_conn->user_data, data, len); 
	}
}

static void gss_client_av_on_recv(gss_client_av_conn *av_conn, char* data, int len)
{
	int av_data_len;
	char* av_data;
	GSS_DATA_HEADER* header;
	int type;

	if(!av_conn->cb.on_recv)
		return;

	av_data_len = len-sizeof(GSS_DATA_HEADER)-sizeof(int);
	header = (GSS_DATA_HEADER*)data;
	type = *(int*)(header+1);
	type = pj_ntohl(type);
	av_data = (char*)(header+1)+sizeof(int);

	//PJ_LOG(4,(av_conn->gssc->obj_name, "gss_client_av_on_recv %p,len=%d,data_seq %d", av_conn, len, header->data_seq));

	if(header->data_seq == LAST_DATA_SEQ)
	{
		if(av_conn->frame_len == 0) //alone audio or video frame
		{
			gss_client_av_smooth_data(av_conn, type, av_data, av_data_len);
		}
		else
		{
			client_av_realloc_frame(av_conn, av_data_len);

			//copy and merge audio or video frame
			memcpy(av_conn->frame_buf+av_conn->frame_len, av_data, av_data_len);
			av_conn->frame_len += av_data_len;

			gss_client_av_smooth_data(av_conn, type, av_conn->frame_buf, av_conn->frame_len);

			av_conn->frame_len = 0;
		}
	}
	else
	{
		client_av_realloc_frame(av_conn, av_data_len);

		//copy and merge audio or video frame
		memcpy(av_conn->frame_buf+av_conn->frame_len, av_data, av_data_len);
		av_conn->frame_len += av_data_len;
	}
}

static void client_av_on_recv(void *conn, void *user_data, char* data, int len)
{
	gss_client_av_conn *av_conn = (gss_client_av_conn*)user_data;

	GSS_DATA_HEADER* header = (GSS_DATA_HEADER*)data;
	PJ_UNUSED_ARG(conn);

	switch(header->cmd)
	{
	case GSS_CONNECT_RESULT:
		{
			int result = pj_ntohl(*(int*)(header+1));
			PJ_LOG(4,(av_conn->gssc->obj_name, "client_av_on_recv on_connect_result %s %d",
				av_conn->gssc->uid.ptr, result));

			if(av_conn->cb.on_connect_result)
				(*av_conn->cb.on_connect_result)(av_conn, av_conn->user_data, result);
		}
		break;
	case GSS_AV_DISCONNECTED:
		{
			PJ_LOG(4,(av_conn->gssc->obj_name, "client_av_on_recv on_device_disconnect %s",
				av_conn->gssc->uid.ptr));
			if(av_conn->cb.on_device_disconnect)
				(*av_conn->cb.on_device_disconnect)(av_conn, av_conn->user_data);
		}
		break;
	case GSS_AV_DATA:
		{
			gss_client_av_on_recv(av_conn, data, len);
		}
		break;
	case GSS_CLEAN_BUFFER:
		{
			av_conn->frame_len = 0; //clean receive frame buffer
		}
		break;
	default:
		break;
	}
}

static void gss_client_av_smooth_cb(const char* buffer, unsigned int len, void* user_data)
{
	gss_client_av_conn *conn = (gss_client_av_conn *)user_data;
	if(conn && conn->cb.on_recv)
		(*conn->cb.on_recv)(conn, conn->user_data, (char*)buffer, len);
}

//client audio and video stream connection connect server
P2P_DECL(int) gss_client_av_connect(gss_client_conn_cfg* cfg, void** transport)
{
	pj_status_t status;
	gss_conn* gssc = NULL;
	gss_client_av_conn* conn;
	gss_conn_cb callback;

	if(cfg == NULL || transport == NULL || cfg->server == NULL || cfg->uid == NULL)
		return PJ_EINVAL;

	check_pj_thread();

	PJ_LOG(4,("client_av", "gss_client_av_connect begin"));

	callback.on_recv = client_av_on_recv;
	callback.on_destroy = client_av_on_destroy;
	callback.on_connect_result = client_av_connect_result;
	callback.on_disconnect = client_av_on_disconnect;
	status = gss_conn_create(cfg->uid, cfg->server, cfg->port, NULL, &callback, &gssc);
	if(status != PJ_SUCCESS)
	{
		PJ_LOG(4,("client_av", "gss_client_av_connect gss_conn_create return %d", status));
		return status;
	}
	conn = PJ_POOL_ZALLOC_T(gssc->pool, gss_client_av_conn);
	conn->gssc = gssc;
	conn->user_data = cfg->user_data;
	gssc->user_data = conn;

	conn->frame_buf = p2p_malloc(GSS_MAX_CMD_LEN);
	conn->frame_len = 0;
	conn->frame_capacity = GSS_MAX_CMD_LEN;

	pj_memcpy(&conn->cb, cfg->cb, sizeof(gss_client_conn_cb));

	if(get_p2p_global()->smooth_span)
		conn->smooth = p2p_create_smooth(gss_client_av_smooth_cb, conn);
	else
		conn->smooth = NULL;

	status = gss_conn_connect_server(gssc);
	if(status != PJ_SUCCESS)
	{
		PJ_LOG(4,("client_av", "gss_client_av_connect gss_conn_connect_server return %d", status));
		gss_conn_destroy(gssc);
		return status;
	}

	*transport = conn;

	PJ_LOG(4,("client_av", "gss_client_av_connect end %p", transport));
	return PJ_SUCCESS;
}

//send audio and video response data to device
P2P_DECL(int) gss_client_av_send(void *transport, char* buf, int buffer_len, p2p_send_model model)
{
	gss_client_av_conn *conn = (gss_client_av_conn *)transport;
	int result;
	if(!conn)
		return PJ_EINVAL;

	check_pj_thread();

	//PJ_LOG(4,(conn->gssc->obj_name, "gss_client_av_send begin buffer_len %d", buffer_len));
	result = gss_conn_send(conn->gssc, buf, buffer_len, NULL, 0, GSS_AV_DATA, model);
	//PJ_LOG(4,(conn->gssc->obj_name, "gss_client_av_send end buffer_len %d", buffer_len));

	return result;
}

//destroy client audio and video stream connection
P2P_DECL(void) gss_client_av_destroy(void* transport)
{ 
	gss_client_av_conn *conn = (gss_client_av_conn *)transport;
	if(!conn)
		return;

	check_pj_thread();

	if(conn->smooth)
		p2p_destroy_smooth(conn->smooth);
	gss_conn_destroy(conn->gssc);
}

//pause receive device data
P2P_DECL(void) gss_client_av_pause_recv(void* transport, int is_pause)
{
	gss_client_av_conn *conn = (gss_client_av_conn *)transport;
	if(!conn)
		return;

	check_pj_thread();

	gss_conn_pause_recv(conn->gssc, is_pause);
}

//clean all receive buffer data
P2P_DECL(void) gss_client_av_clean_buf(void* transport)
{
	gss_client_av_conn *conn = (gss_client_av_conn *)transport;

	check_pj_thread();

	if(!conn || !conn->gssc || conn->gssc->destroy_req)
		return;

	PJ_LOG(4,(conn->gssc->obj_name, "gss_client_av_clean_buf"));

	pj_grp_lock_acquire(conn->gssc->grp_lock);

	if (conn->gssc->destroy_req)
	{ //already destroy, so return
		pj_grp_lock_release(conn->gssc->grp_lock);
		return;
	}
	
	if(conn->smooth)
		p2p_smooth_reset(conn->smooth);

	conn->frame_len = 0;

	pj_grp_lock_release(conn->gssc->grp_lock);
}