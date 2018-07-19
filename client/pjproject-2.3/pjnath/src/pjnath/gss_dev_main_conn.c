#include <gss_transport.h>
#include <pjnath/errno.h>
#include <pjnath/p2p_global.h>
#include <pjnath/gss_conn.h>
#include <common/gss_protocol.h>

//device main connection 
typedef struct gss_dev_main_conn
{
	gss_conn* gssc;
	
	gss_dev_main_cb cb; /*call functions*/
	
	void *user_data;

	pj_timer_entry reconnect_timer; /*reconnect timer*/ 
	int reconnect_times;
}gss_dev_main_conn;

#define DEVICE_MAIN_RECONNECT_SPAN (30)

//callback to free memory of gss_dev_main_conn
static void dev_main_on_destroy(void *conn, void *user_data)
{
	gss_dev_main_conn *main_conn = (gss_dev_main_conn*)user_data;
	PJ_UNUSED_ARG(conn);
	PJ_UNUSED_ARG(main_conn);
}

static void dev_main_connect_result(void *conn, void* user_data, int status)
{
	gss_dev_main_conn *main_conn = (gss_dev_main_conn*)user_data;
	PJ_UNUSED_ARG(conn);

	PJ_LOG(4,(main_conn->gssc->obj_name, "dev_main_connect_result %p, status=%d", main_conn, status));

	if(status == PJ_SUCCESS)
	{
		//send login command to server
		GSS_LOGIN_CMD login_cmd;

		main_conn->reconnect_times = 0;

		login_cmd.type = GSS_DEV_LOGIN_CMD;
		strcpy(login_cmd.uid, main_conn->gssc->uid.ptr);
		gss_conn_send(main_conn->gssc, 
			(char*)&login_cmd,
			sizeof(GSS_LOGIN_CMD),
			NULL, 
			0,
			GSS_DEV_LOGIN_CMD,
			P2P_SEND_BLOCK);
	}
	else
	{
		pj_time_val delay = {DEVICE_MAIN_RECONNECT_SPAN , 0 };
		if(main_conn->reconnect_times == 0 && main_conn->cb.on_connect_result)
			(*main_conn->cb.on_connect_result)(main_conn, main_conn->user_data, status);

		main_conn->reconnect_times++;

		pj_timer_heap_schedule_w_grp_lock(get_p2p_global()->timer_heap, &main_conn->reconnect_timer,
			&delay, GSS_RECONNECT_TIMER_ID, main_conn->gssc->grp_lock);
	}
}

static void dev_main_on_disconnect(void *conn, void* user_data, int status)
{
	gss_dev_main_conn *main_conn = (gss_dev_main_conn*)user_data;
	PJ_UNUSED_ARG(conn);

	if(main_conn->cb.on_disconnect)
		(*main_conn->cb.on_disconnect)(main_conn, main_conn->user_data, status);

	//try reconnect
	PJ_LOG(4,(main_conn->gssc->obj_name, "dev_main_on_disconnect %p, status=%d", main_conn, status));

//	if(status != GSS_DEV_KICKOUT && main_conn->gssc->destroy_in_net_thread == 0) //kick out by other connection,do not reconnect
//	{
//		gss_conn_disconnect_server(main_conn->gssc);
//		status = gss_conn_connect_server(main_conn->gssc);
//		if(status != PJ_SUCCESS)
//		{
//			pj_time_val delay = {DEVICE_MAIN_RECONNECT_SPAN , 0 };
//			pj_timer_heap_schedule_w_grp_lock(get_p2p_global()->timer_heap, &main_conn->reconnect_timer,
//				&delay, GSS_RECONNECT_TIMER_ID, main_conn->gssc->grp_lock);
//		}
//	}
}

static void dev_main_on_recv(void *conn, void *user_data, char* data, int len)
{
	gss_dev_main_conn *main_conn = (gss_dev_main_conn*)user_data;
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

			if(main_conn->cb.on_connect_result)
				(*main_conn->cb.on_connect_result)(main_conn, main_conn->user_data, result);
		}
		break;
	case GSS_KICKOUT: //kick out by other connection
		gss_conn_disconnect_server(main_conn->gssc);
		dev_main_on_disconnect(main_conn->gssc, main_conn, GSS_DEV_KICKOUT);
		break;

	case GSS_SIGNALING_CONNECTED: //client connected 

		if(main_conn->cb.on_accept_signaling_client)
		{
			unsigned short client_idx;
			//in arm platform, 4 byte alignment, can not cast
			memcpy(&client_idx, header+1, sizeof(unsigned short));
			client_idx = pj_ntohs(client_idx);

			PJ_LOG(4,(main_conn->gssc->obj_name, "main accept signaling connection index %d", client_idx));
			(*main_conn->cb.on_accept_signaling_client)(main_conn, main_conn->user_data, client_idx);
		}
		break;

	case GSS_SIGNALING_DISCONNECTED: //client disconnected
		if(main_conn->cb.on_disconnect_signaling_client)
		{
			unsigned short client_idx;
			//in arm platform, 4 byte alignment, can not cast
			memcpy(&client_idx, header+1, sizeof(unsigned short));
			client_idx = pj_ntohs(client_idx);

			PJ_LOG(4,(main_conn->gssc->obj_name, "main signaling disconnect, index %d", client_idx));
			(*main_conn->cb.on_disconnect_signaling_client)(main_conn, main_conn->user_data, client_idx);
		}
		break;

	case GSS_SIGNALING_DATA: //signal data
		if(main_conn->cb.on_recv)
		{
			unsigned short client_idx;
			//in arm platform, 4 byte alignment, can not cast
			memcpy(&client_idx, header+1, sizeof(unsigned short));
			client_idx = pj_ntohs(client_idx);

			(*main_conn->cb.on_recv)(main_conn, 
				main_conn->user_data, 
				client_idx, 
				(char*)(header+1)+sizeof(unsigned short),
				len-sizeof(GSS_DATA_HEADER)-sizeof(unsigned short));
		}
		break;
	case GSS_CLIENT_AV_LOGIN_CMD:
		if(main_conn->cb.on_recv_av_request)
		{
			int client_conn;
			//in arm platform, 4 byte alignment, can not cast
			memcpy(&client_conn, header+1, sizeof(int));
			client_conn = pj_ntohl(client_conn);

			(*main_conn->cb.on_recv_av_request)(main_conn, main_conn->user_data, client_conn);
		}
		break;
	default:
		break;
	}
}

static void gss_dev_main_reconnect_timer(pj_timer_heap_t *th, pj_timer_entry *e)
{
	gss_dev_main_conn *main_conn = (gss_dev_main_conn*) (gss_conn*)e->user_data;
	int status;

	PJ_UNUSED_ARG(th);

	gss_conn_disconnect_server(main_conn->gssc);
	status = gss_conn_connect_server(main_conn->gssc);
	if(status != PJ_SUCCESS)
	{
		pj_time_val delay = {DEVICE_MAIN_RECONNECT_SPAN , 0 };
		pj_timer_heap_schedule_w_grp_lock(get_p2p_global()->timer_heap, &main_conn->reconnect_timer,
			&delay, GSS_RECONNECT_TIMER_ID, main_conn->gssc->grp_lock);
	}
}

//device main connection connect server
P2P_DECL(int) gss_dev_main_connect(gss_dev_main_cfg* cfg, void** transport)
{
	pj_status_t status;
	gss_conn* gssc = NULL;
	gss_dev_main_conn* conn;
	gss_conn_cb callback;

	if(cfg == NULL || transport == NULL || cfg->server == NULL || cfg->uid == NULL)
		return PJ_EINVAL;

	check_pj_thread();

	callback.on_recv = dev_main_on_recv;
	callback.on_destroy = dev_main_on_destroy;
	callback.on_connect_result = dev_main_connect_result;
	callback.on_disconnect = dev_main_on_disconnect;
	status = gss_conn_create(cfg->uid, cfg->server, cfg->port, NULL, &callback, &gssc);
	if(status != PJ_SUCCESS)
		return status;

	conn = PJ_POOL_ZALLOC_T(gssc->pool, gss_dev_main_conn);
	conn->gssc = gssc;
	conn->user_data = cfg->user_data;
	gssc->user_data = conn;

	pj_memcpy(&conn->cb, cfg->cb, sizeof(gss_dev_main_cb));

	//create reconnect timer
	pj_timer_entry_init(&conn->reconnect_timer, GSS_TIMER_NONE, conn, &gss_dev_main_reconnect_timer);

	status = gss_conn_connect_server(gssc);
	if(status != PJ_SUCCESS)
	{
		gss_conn_destroy(gssc);
		return status;
	}

	*transport = conn;
	return PJ_SUCCESS;
}

//send data to client
P2P_DECL(int) gss_dev_main_send(void *transport, unsigned short client_conn, char* buf, int buffer_len, p2p_send_model model)
{
	gss_dev_main_conn *conn = (gss_dev_main_conn *)transport;
	if(!conn)
		return PJ_EINVAL;

	check_pj_thread();

	client_conn = pj_htons(client_conn);

	return gss_conn_send(conn->gssc,
		buf, 
		buffer_len, 
		(char*)&client_conn, 
		sizeof(unsigned short), 
		GSS_SIGNALING_DATA,
		model);
}

//destroy device main connection
P2P_DECL(void) gss_dev_main_destroy(void* transport)
{
	gss_dev_main_conn *conn = (gss_dev_main_conn *)transport;
	if(!conn)
		return;

	check_pj_thread();

	pj_timer_heap_cancel_if_active(get_p2p_global()->timer_heap, &conn->reconnect_timer, 0);

	gss_conn_destroy(conn->gssc);
}