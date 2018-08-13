#include <gss_transport.h>
#include <pjnath/errno.h>
#include <pjnath/p2p_global.h>
#include <pjnath/gss_conn.h>
#include <common/gss_protocol.h>


//device audio and video connection 
typedef struct gss_dev_av_conn
{
	gss_conn* gssc;

	gss_dev_av_cb cb; /*call functions*/

	void *user_data;

	unsigned int client_conn; //client av connection index in server

	/*device send fast and mobile phone receive slow
	server tcp send cache too many, so must limit device send
	*/
	unsigned char is_svr_send_limit;
	//block send event
	pj_event_t *send_limit_event;

}gss_dev_av_conn;


//callback to free memory of gss_dev_av_conn
static void dev_av_on_destroy(void *conn, void *user_data)
{
	gss_dev_av_conn *av_conn = (gss_dev_av_conn*)user_data;

	PJ_UNUSED_ARG(conn);

	if(av_conn->send_limit_event)
	{
		pj_event_destroy(av_conn->send_limit_event);
		av_conn->send_limit_event = NULL;
	}
}

static void dev_av_on_disconnect(void *conn, void* user_data, int status)
{
	gss_dev_av_conn *av_conn = (gss_dev_av_conn*)user_data;
	PJ_UNUSED_ARG(conn);

	//wake up block send
	if(av_conn->is_svr_send_limit && av_conn->send_limit_event)
	{
		av_conn->is_svr_send_limit = 0;
		pj_event_set(av_conn->send_limit_event);
	}
	if(av_conn->cb.on_disconnect)
		(*av_conn->cb.on_disconnect)(av_conn, av_conn->user_data, status);

	PJ_LOG(4,(av_conn->gssc->obj_name, "dev_av_on_disconnect %p, status=%d", av_conn, status));
}

static void dev_av_connect_result(void *conn, void* user_data, int status)
{
	gss_dev_av_conn *av_conn = (gss_dev_av_conn*)user_data;
	PJ_UNUSED_ARG(conn);

	PJ_LOG(4,(av_conn->gssc->obj_name, "dev_av_connect_result %p, status=%d", av_conn, status));

	if(status == PJ_SUCCESS)
	{
		//send login command to server
		GSS_DEVICE_AV_LOGIN_CMD login_cmd;
		strcpy(login_cmd.uid, av_conn->gssc->uid.ptr);
		login_cmd.index = pj_htonl(av_conn->client_conn);
		gss_conn_send(av_conn->gssc, 
			(char*)&login_cmd,
			sizeof(GSS_DEVICE_AV_LOGIN_CMD), 
			NULL, 
			0, 
			GSS_DEV_AV_LOGIN_CMD,
			P2P_SEND_BLOCK);
	}
	else
	{
		if(av_conn->cb.on_connect_result)
			(*av_conn->cb.on_connect_result)(av_conn, av_conn->user_data, status);
	}
}

static void dev_av_on_recv(void *conn, void *user_data, char* data, int len)
{
	gss_dev_av_conn *av_conn = (gss_dev_av_conn*)user_data;

	GSS_DATA_HEADER* header = (GSS_DATA_HEADER*)data;
	PJ_UNUSED_ARG(conn);

	switch(header->cmd)
	{
	case GSS_CONNECT_RESULT:
		{
			int result;
			//in arm platform, can not cast
			memcpy(&result, header+1, sizeof(int));
			result = pj_ntohl(result);

			av_conn->is_svr_send_limit = 0;
			if(av_conn->cb.on_connect_result)
				(*av_conn->cb.on_connect_result)(av_conn, av_conn->user_data, result);
		}
		break;
	case GSS_AV_DISCONNECTED:
		{
			PJ_LOG(4,(av_conn->gssc->obj_name, "dev_av_on_recv on_device_disconnect %s",
				av_conn->gssc->uid.ptr));

			//wake up block send
			if(av_conn->is_svr_send_limit && av_conn->send_limit_event)
			{
				av_conn->is_svr_send_limit = 0;
				pj_event_set(av_conn->send_limit_event);
			}

			if(av_conn->cb.on_client_disconnect)
				(*av_conn->cb.on_client_disconnect)(av_conn, av_conn->user_data);
		}
		break;
	case GSS_AV_DATA:
		{
			(*av_conn->cb.on_recv)(av_conn, av_conn->user_data, (char*)(header+1), len-sizeof(GSS_DATA_HEADER));
		}
		break;
	case GSS_SEND_LIMIT:
		{
			av_conn->is_svr_send_limit = *(unsigned char*)(header+1);
			//wake up block send
			if(av_conn->is_svr_send_limit == 0)
			{
				if(av_conn->send_limit_event)
					pj_event_set(av_conn->send_limit_event);
			}
		}
		break;
	default:
		break;
	}
}

//device audio and video stream connection connect server
int gss_dev_av_connect(gss_dev_av_cfg* cfg, void** transport)
{
	pj_status_t status;
	gss_conn* gssc = NULL;
	gss_dev_av_conn* conn;
	gss_conn_cb callback;

	if(transport == NULL)
		return PJ_EINVAL;

	check_pj_thread();

	callback.on_recv = dev_av_on_recv;
	callback.on_destroy = dev_av_on_destroy;
	callback.on_connect_result = dev_av_connect_result;
	callback.on_disconnect = dev_av_on_disconnect;
	status = gss_conn_create(cfg->uid, cfg->server, cfg->port, NULL, &callback, &gssc);
	if(status != PJ_SUCCESS)
		return status;

	conn = PJ_POOL_ZALLOC_T(gssc->pool, gss_dev_av_conn);
	conn->gssc = gssc;
	conn->client_conn = cfg->client_conn;
	conn->user_data = cfg->user_data;
	conn->is_svr_send_limit = 0;

	status = pj_event_create(gssc->pool, "send_limit_event", PJ_FALSE, PJ_FALSE, &conn->send_limit_event);
	if (status != PJ_SUCCESS)
	{
		gss_conn_destroy(gssc);
		return status;
	}

	gssc->user_data = conn;

	pj_memcpy(&conn->cb, cfg->cb, sizeof(gss_client_conn_cb));

	status = gss_conn_connect_server(gssc);
	if(status != PJ_SUCCESS)
	{
		gss_conn_destroy(gssc);
		return status;
	}

	*transport = conn;
	return PJ_SUCCESS;
}

int gss_dev_av_send_limit(gss_dev_av_conn *conn, int model)
{
	//nonblock model,server send cache full
	if(conn->is_svr_send_limit)
	{
		if(model == P2P_SEND_NONBLOCK)
		{
			return PJ_CACHE_FULL;
		}
		else
		{
			//for multithread, other thread maybe call gss_conn_destroy, so add reference
			pj_grp_lock_add_ref(conn->gssc->grp_lock); 

			while(conn->is_svr_send_limit)
			{
				int ret = run_global_loop();
				if(ret == GLOBAL_THREAD_EXIT)
				{
					//for multithread, decrease reference 
					pj_grp_lock_dec_ref(conn->gssc->grp_lock); 
					return PJ_EGONE;
				}
				//wait disconnect or receive GSS_SEND_LIMIT command
				if(ret == NO_GLOBAL_THREAD)
				{
					PJ_LOG(4,(conn->gssc->obj_name, "gss_dev_av_send_limit pj_event_wait begin"));
					pj_event_wait(conn->send_limit_event);
					PJ_LOG(4,(conn->gssc->obj_name, "gss_dev_av_send_limit pj_event_wait end"));
				}

				if(conn->gssc->destroy_req || conn->gssc->conn_status != GSS_CONN_CONNECTED
					|| conn->gssc->activesock == NULL || conn->gssc->sock == PJ_INVALID_SOCKET)
				{
					//for multithread, decrease reference 
					pj_grp_lock_dec_ref(conn->gssc->grp_lock); 
					return PJ_EGONE;
				}
			}
			//for multithread, decrease reference 
			pj_grp_lock_dec_ref(conn->gssc->grp_lock); 

		}
	}	
	return PJ_SUCCESS;
}

//send audio and video response data to device
P2P_DECL(int) gss_dev_av_send(void *transport, char* buf, int buffer_len, p2p_send_model model, int type)
{
	int result;
	gss_dev_av_conn *conn = (gss_dev_av_conn *)transport;
	if(!conn)
		return PJ_EINVAL;

	check_pj_thread();
	
	result = gss_dev_av_send_limit(conn, model);
	if(result != PJ_SUCCESS)
		return result;

	type = pj_htonl(type);

	return gss_conn_send(conn->gssc, buf, buffer_len, (char*)&type, sizeof(type), GSS_AV_DATA, model);
}

//destroy device audio and video stream connection
P2P_DECL(void) gss_dev_av_destroy(void* transport)
{ 
	gss_dev_av_conn *conn = (gss_dev_av_conn *)transport;
	if(!conn)
		return;

	check_pj_thread();

	//wake up block send
	if(conn->is_svr_send_limit && conn->send_limit_event)
	{
		conn->is_svr_send_limit = 0;
		pj_event_set(conn->send_limit_event);
	}
	gss_conn_destroy(conn->gssc);

}

//clean all send buffer data
P2P_DECL(void) gss_dev_av_clean_buf(void* transport)
{
	int dummy = 0;

	gss_dev_av_conn *conn = (gss_dev_av_conn *)transport;
	if(!conn)
		return;

	check_pj_thread();

	gss_conn_clean_send_buf(conn->gssc);

	gss_conn_send(conn->gssc, (char*)&dummy, sizeof(int), NULL, 0, GSS_CLEAN_BUFFER, P2P_SEND_BLOCK);
}