#include <gss_transport.h>
#include <pjnath/errno.h>
#include <pjnath/p2p_global.h>
#include <pjnath/gss_conn.h>
#include <common/gss_protocol.h>


//device audio and video push connection 
typedef struct gss_dev_push_conn
{
	gss_conn* gssc;

	gss_dev_push_cb cb; /*call functions*/

	void *user_data;

}gss_dev_push_conn;


//callback to free memory of gss_dev_push_conn
static void dev_push_on_destroy(void *conn, void *user_data)
{
	gss_dev_push_conn *push_conn = (gss_dev_push_conn*)user_data;
	PJ_UNUSED_ARG(conn);
	PJ_UNUSED_ARG(push_conn);
}

static void dev_push_on_disconnect(void *conn, void* user_data, int status)
{
	gss_dev_push_conn *push_conn = (gss_dev_push_conn*)user_data;
	PJ_UNUSED_ARG(conn);

	if(push_conn->cb.on_disconnect)
		(*push_conn->cb.on_disconnect)(push_conn, push_conn->user_data, status);

	PJ_LOG(4,(push_conn->gssc->obj_name, "dev_push_on_disconnect %p, status=%d", push_conn, status));
}

static void dev_push_connect_result(void *conn, void* user_data, int status)
{
	gss_dev_push_conn *push_conn = (gss_dev_push_conn*)user_data;
	PJ_UNUSED_ARG(conn);

	PJ_LOG(4,(push_conn->gssc->obj_name, "dev_push_connect_result %p, status=%d", push_conn, status));

	if(status == PJ_SUCCESS)
	{
		//send login command to server
		GSS_LOGIN_CMD login_cmd;
		strcpy(login_cmd.uid, push_conn->gssc->uid.ptr);
		login_cmd.type = GSS_DEV_PUSH_LOGIN_CMD;
		gss_conn_send(push_conn->gssc, 
			(char*)&login_cmd, 
			sizeof(GSS_DEVICE_AV_LOGIN_CMD), 
			NULL, 
			0,
			GSS_DEV_PUSH_LOGIN_CMD,
			P2P_SEND_BLOCK);
	}
	else
	{
		if(push_conn->cb.on_connect_result)
			(*push_conn->cb.on_connect_result)(push_conn, push_conn->user_data, status);
	}
}

static void dev_push_on_recv(void *conn, void *user_data, char* data, int len)
{
	gss_dev_push_conn *push_conn = (gss_dev_push_conn*)user_data;
	GSS_DATA_HEADER* header = (GSS_DATA_HEADER*)data;

	PJ_UNUSED_ARG(conn);
	PJ_UNUSED_ARG(len);

	switch(header->cmd)
	{
	case GSS_CONNECT_RESULT:
		{
			int result;
			//in arm platform, 4 byte alignment, can not cast
			memcpy(&result, header+1, sizeof(int));
			result = pj_ntohl(result);
			if(push_conn->cb.on_connect_result)
				(*push_conn->cb.on_connect_result)(push_conn, push_conn->user_data, result);
		}
		break;
	case GSS_KICKOUT: //kick out by other connection
		gss_conn_disconnect_server(push_conn->gssc);
		dev_push_on_disconnect(push_conn->gssc, push_conn, GSS_DEV_KICKOUT);
		break;
	case GSS_RTMP_CONNECT_RESULT:
		{
			int result;
			//in arm platform, 4 byte alignment, can not cast
			memcpy(&result, header+1, sizeof(int));
			result = pj_ntohl(result);
			
			PJ_LOG(4,(push_conn->gssc->obj_name, "GSS_RTMP_CONNECT_RESULT %p, status=%d", push_conn, result));

			if(push_conn->cb.on_rtmp_event)
			{
				if(result == 0)
					(*push_conn->cb.on_rtmp_event)(push_conn, push_conn->user_data, RTMP_EVENT_CONNECT_SUCCESS);
				else
					(*push_conn->cb.on_rtmp_event)(push_conn, push_conn->user_data, RTMP_EVENT_CONNECT_FAIL);
			}
			
		}
		break;
	case GSS_RTMP_DISCONNECT:
		{
			PJ_LOG(4,(push_conn->gssc->obj_name, "GSS_RTMP_DISCONNECT %p", push_conn));
			if(push_conn->cb.on_rtmp_event)
				(*push_conn->cb.on_rtmp_event)(push_conn, push_conn->user_data, RTMP_EVENT_DISCONNECT);
		}
		break;

	case GSS_PULL_STATUS_CHANGED:
		{
			unsigned int count;
			//in arm platform, 4 byte alignment, can not cast
			memcpy(&count, header+1, sizeof(unsigned int));
			count = pj_ntohl(count);
			if(push_conn->cb.on_pull_count_changed)
				(*push_conn->cb.on_pull_count_changed)(push_conn, push_conn->user_data, count);
		}
		break;
	default:
		break;
	}
}

//device audio and video push stream connection connect server
int gss_dev_push_connect(gss_dev_push_conn_cfg* cfg, void** transport)
{
	pj_status_t status;
	gss_conn* gssc = NULL;
	gss_dev_push_conn* conn;
	gss_conn_cb callback;

	if(transport == NULL)
		return PJ_EINVAL;

	check_pj_thread();

	callback.on_recv = dev_push_on_recv;
	callback.on_destroy = dev_push_on_destroy;
	callback.on_connect_result = dev_push_connect_result;
	callback.on_disconnect = dev_push_on_disconnect;
	status = gss_conn_create(cfg->uid, cfg->server, cfg->port, NULL, &callback, &gssc);
	if(status != PJ_SUCCESS)
		return status;

	conn = PJ_POOL_ZALLOC_T(gssc->pool, gss_dev_push_conn);
	conn->gssc = gssc;
	conn->user_data = cfg->user_data;
	gssc->user_data = conn;

	pj_memcpy(&conn->cb, cfg->cb, sizeof(gss_dev_push_cb));

	status = gss_conn_connect_server(gssc);
	if(status != PJ_SUCCESS)
	{
		gss_conn_destroy(gssc);
		return status;
	}

	*transport = conn;
	return PJ_SUCCESS;
}

//send audio and video response data to device
P2P_DECL(int) gss_dev_push_send(void *transport, char* buf, int buffer_len, unsigned char type, unsigned int time_stamp, char is_key, p2p_send_model model)
{
	gss_dev_push_conn *conn = (gss_dev_push_conn *)transport;
	unsigned char cmd;
	if(!conn)
		return PJ_EINVAL;

	check_pj_thread();
	
	if(type == GSS_AUDIO_DATA)
	{
		cmd = GSS_PUSH_AUDIO;
	}
	else if(type == GSS_VIDEO_DATA)
	{
		if(is_key)
			cmd = GSS_PUSH_KEY_VIDEO;
		else
			cmd = GSS_PUSH_VIDEO;
	}
	else if(type >= GSS_CUSTOM_DATA && type <= GSS_CUSTOM_MAX_DATA)
	{
		cmd = type;
	}
	else
		return PJ_EINVAL;

	time_stamp = pj_htonl(time_stamp);

	return gss_conn_send(conn->gssc, buf, buffer_len, (char*)&time_stamp, sizeof(unsigned int), cmd, model);
}

//destroy device audio and video push stream connection
P2P_DECL(void) gss_dev_push_destroy(void* transport)
{ 
	gss_dev_push_conn *conn = (gss_dev_push_conn *)transport;
	if(!conn)
		return;

	check_pj_thread();

	gss_conn_destroy(conn->gssc);
}

//push audio and video to third rtmp server
P2P_DECL(int) gss_dev_push_rtmp(void *transport, char* url)
{
	gss_dev_push_conn *conn = (gss_dev_push_conn *)transport;
	GSS_PUSH_RMTP_CMD rtmp_cmd;

	check_pj_thread();

	if(!conn || !url)
		return PJ_EINVAL;

	if(strlen(url) >= MAX_RTMP_URL_LEN)
		return PJ_ENAMETOOLONG;

	strcpy(rtmp_cmd.url, url);

	return gss_conn_send(conn->gssc, 
		(char*)&rtmp_cmd, 
		sizeof(GSS_PUSH_RMTP_CMD), 
		NULL, 
		0,
		GSS_PUSH_RTMP,
		P2P_SEND_BLOCK);
}