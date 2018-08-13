#include <gss_transport.h>
#include <pjnath/errno.h>
#include <pjnath/p2p_global.h>
#include <pjnath/gss_conn.h>
#include <common/gss_protocol.h>

//client audio and video pull stream connection 
typedef struct gss_client_pull_conn
{
	gss_conn* gssc;

	gss_pull_conn_cb cb; /*call functions*/

	void *user_data;
	
	//video or audio frame cache
	char* frame_buf;
	unsigned int frame_len;
	unsigned int frame_capacity;
}gss_client_pull_conn;


//callback to free memory of gss_client_pull_conn
static void client_pull_on_destroy(void *conn, void *user_data)
{
	gss_client_pull_conn *pull_conn = (gss_client_pull_conn*)user_data;
	if(!conn)
		return;

	if(pull_conn->frame_buf)
	{
		p2p_free(pull_conn->frame_buf);
		pull_conn->frame_buf = NULL;
	}
}

static void client_pull_on_disconnect(void *conn, void* user_data, int status)
{
	gss_client_pull_conn *pull_conn = (gss_client_pull_conn*)user_data;
	PJ_UNUSED_ARG(conn);

	if(pull_conn->cb.on_disconnect)
		(*pull_conn->cb.on_disconnect)(pull_conn, pull_conn->user_data, status);

	PJ_LOG(4,(pull_conn->gssc->obj_name, "client_pull_on_disconnect %p, status=%d", pull_conn, status));
}

static void client_pull_connect_result(void *conn, void* user_data, int status)
{
	gss_client_pull_conn *pull_conn = (gss_client_pull_conn*)user_data;
	PJ_UNUSED_ARG(conn);

	PJ_LOG(4,(pull_conn->gssc->obj_name, "client_pull_connect_result %p, status=%d", pull_conn, status));

	if(status == PJ_SUCCESS)
	{
		//send login command to server
		GSS_LOGIN_CMD login_cmd;
		login_cmd.type = GSS_CLIENT_PULL_LOGIN_CMD;
		strcpy(login_cmd.uid, pull_conn->gssc->uid.ptr);
		gss_conn_send(pull_conn->gssc, 
			(char*)&login_cmd, 
			sizeof(GSS_LOGIN_CMD), 
			NULL, 
			0,
			GSS_CLIENT_PULL_LOGIN_CMD,
			P2P_SEND_BLOCK);
	}
	else
	{
		if(pull_conn->cb.on_connect_result)
			(*pull_conn->cb.on_connect_result)(pull_conn, pull_conn->user_data, status);
	}
}

static void client_pull_realloc_frame(gss_client_pull_conn *pull_conn, int add_len)
{
	unsigned int capacity;

	capacity = pull_conn->frame_len + add_len;

	if(capacity > pull_conn->frame_capacity)
	{
		char* frame_buf;
		unsigned int frame_capacity = pull_conn->frame_capacity;
		
		frame_capacity *= 2;
		while(frame_capacity < capacity)
			frame_capacity *= 2;

		if(frame_capacity > MAX_AV_FRAME_LEN)
		{
			PJ_LOG(1,(pull_conn->gssc->obj_name, "client_pull_realloc_frame %p, frame too long", pull_conn));
			pull_conn->frame_len = 0;
			return;
		}
		
		pull_conn->frame_capacity = frame_capacity;
		PJ_LOG(4,(pull_conn->gssc->obj_name, "client_pull_realloc_frame %p, capacity %d", pull_conn, frame_capacity));

		frame_buf = (char*)p2p_malloc(frame_capacity);
		memcpy(frame_buf, pull_conn->frame_buf, pull_conn->frame_len);
		p2p_free(pull_conn->frame_buf);
		pull_conn->frame_buf = frame_buf;
	}
}

static void gss_client_pull_on_recv(gss_client_pull_conn *pull_conn, char* data, int len)
{
	int av_data_len;
	unsigned int* time_stamp;
	char* av_data;
	GSS_DATA_HEADER* header;
	char av_type = GSS_CUSTOM_BASE;

	if(!pull_conn->cb.on_recv)
		return;

	//sizeof(unsigned int) is time stamp length
	av_data_len = len-sizeof(GSS_DATA_HEADER)-sizeof(unsigned int);

	header = (GSS_DATA_HEADER*)data;
	if(header->cmd == GSS_PUSH_VIDEO || header->cmd == GSS_PUSH_KEY_VIDEO)
		av_type = GSS_VIDEO_DATA;
	else if(header->cmd == GSS_PUSH_AUDIO)
		av_type = GSS_AUDIO_DATA;
	else if(header->cmd >= GSS_CUSTOM_BASE && header->cmd <= GSS_CUSTOM_MAX)
		av_type = header->cmd;

	time_stamp = (unsigned int*)(header+1);
	av_data = (char*)(time_stamp+1);

	if(header->data_seq == LAST_DATA_SEQ)
	{
		if(pull_conn->frame_len == 0) //alone audio or video frame
		{
			(*pull_conn->cb.on_recv)(pull_conn, pull_conn->user_data, av_data, av_data_len, av_type, pj_ntohl(*time_stamp)); 
		}
		else
		{
			client_pull_realloc_frame(pull_conn, av_data_len);

			//copy and merge audio or video frame
			memcpy(pull_conn->frame_buf+pull_conn->frame_len, av_data, av_data_len);
			pull_conn->frame_len += av_data_len;
			
			(*pull_conn->cb.on_recv)(pull_conn, pull_conn->user_data, pull_conn->frame_buf, pull_conn->frame_len, av_type, pj_ntohl(*time_stamp)); 
			pull_conn->frame_len = 0;
		}
	}
	else
	{
		client_pull_realloc_frame(pull_conn, av_data_len);

		//copy and merge audio or video frame
		memcpy(pull_conn->frame_buf+pull_conn->frame_len, av_data, av_data_len);
		pull_conn->frame_len += av_data_len;
	}
}

static void client_pull_on_recv(void *conn, void *user_data, char* data, int len)
{
	gss_client_pull_conn *pull_conn = (gss_client_pull_conn*)user_data;
	GSS_DATA_HEADER* header = (GSS_DATA_HEADER*)data;
	PJ_UNUSED_ARG(conn);

	switch(header->cmd)
	{
	case GSS_CONNECT_RESULT:
		{
			int result = pj_ntohl(*(int*)(header+1));
			if(pull_conn->cb.on_connect_result)
				(*pull_conn->cb.on_connect_result)(pull_conn, pull_conn->user_data, result);
		}
		break;
	case GSS_PUSH_DISCONNECTED:
		{
			if(pull_conn->cb.on_device_disconnect)
				(*pull_conn->cb.on_device_disconnect)(pull_conn, pull_conn->user_data);
		}
		break;
	case GSS_PUSH_VIDEO:
	case GSS_PUSH_KEY_VIDEO:
	case GSS_PUSH_AUDIO:
		gss_client_pull_on_recv(pull_conn, data, len);
		break;
	default:
		if(header->cmd >= GSS_CUSTOM_BASE && header->cmd <= GSS_CUSTOM_MAX)
			gss_client_pull_on_recv(pull_conn, data, len);
		break;
	}
}

//client audio and video stream connection connect server
P2P_DECL(int) gss_client_pull_connect(gss_pull_conn_cfg* cfg, void** transport)
{
	pj_status_t status;
	gss_conn* gssc = NULL;
	gss_client_pull_conn* conn;
	gss_conn_cb callback;

	if(cfg == NULL || transport == NULL || cfg->server == NULL || cfg->uid == NULL)
		return PJ_EINVAL;

	check_pj_thread();

	callback.on_recv = client_pull_on_recv;
	callback.on_destroy = client_pull_on_destroy;
	callback.on_connect_result = client_pull_connect_result;
	callback.on_disconnect = client_pull_on_disconnect;
	status = gss_conn_create(cfg->uid, cfg->server, cfg->port, NULL, &callback, &gssc);
	if(status != PJ_SUCCESS)
		return status;

	conn = PJ_POOL_ZALLOC_T(gssc->pool, gss_client_pull_conn);
	conn->gssc = gssc;
	conn->user_data = cfg->user_data;
	gssc->user_data = conn;

	conn->frame_buf = p2p_malloc(GSS_MAX_CMD_LEN);
	conn->frame_len = 0;
	conn->frame_capacity = GSS_MAX_CMD_LEN;

	pj_memcpy(&conn->cb, cfg->cb, sizeof(gss_pull_conn_cb));

	status = gss_conn_connect_server(gssc);
	if(status != PJ_SUCCESS)
	{
		gss_conn_destroy(gssc);
		return status;
	}

	*transport = conn;
	return PJ_SUCCESS;
}

//destroy client audio and video stream connection
P2P_DECL(void) gss_client_pull_destroy(void* transport)
{ 
	gss_client_pull_conn *conn = (gss_client_pull_conn *)transport;
	if(!conn)
		return;

	check_pj_thread();

	gss_conn_destroy(conn->gssc);
}