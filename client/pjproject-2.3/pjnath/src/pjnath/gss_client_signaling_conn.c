#include <gss_transport.h>
#include <pjnath/errno.h>
#include <pjnath/p2p_global.h>
#include <pjnath/gss_conn.h>
#include <common/gss_protocol.h>

//client signaling connection 
typedef struct gss_client_signaling_conn
{
	gss_conn* gssc;

	gss_client_conn_cb cb; /*call functions*/

	void *user_data;

	unsigned short client_index;
}gss_client_signaling_conn;

//callback to free memory of gss_client_signaling_conn
static void client_signaling_on_destroy(void *conn, void *user_data)
{
	gss_client_signaling_conn *client_conn = (gss_client_signaling_conn*)user_data;
	PJ_UNUSED_ARG(conn);
	PJ_UNUSED_ARG(client_conn);
}

static void client_signaling_connect_result(void *conn, void* user_data, int status)
{
	gss_client_signaling_conn *client_conn = (gss_client_signaling_conn*)user_data;
	PJ_UNUSED_ARG(conn);
	if(status != PJ_SUCCESS) //failed to connect server
	{
		if(client_conn->cb.on_connect_result)
			(*client_conn->cb.on_connect_result)(client_conn, client_conn->user_data, status);
	}
	else
	{
		//send login command to server
		GSS_LOGIN_CMD login_cmd;
		login_cmd.type = GSS_CLIENT_LOGIN_CMD;
		strcpy(login_cmd.uid, client_conn->gssc->uid.ptr);
		gss_conn_send(client_conn->gssc, 
			(char*)&login_cmd, 
			sizeof(GSS_LOGIN_CMD), 
			NULL, 
			0,
			GSS_CLIENT_LOGIN_CMD,
			P2P_SEND_BLOCK);
	}
}

static void client_signaling_on_disconnect(void *conn, void* user_data, int status)
{
	gss_client_signaling_conn *client_conn = (gss_client_signaling_conn*)user_data;
	PJ_UNUSED_ARG(conn);
	if(client_conn->cb.on_disconnect)
		(*client_conn->cb.on_disconnect)(client_conn, client_conn->user_data, status);
}

static void client_signaling_on_recv(void *conn, void *user_data, char* data, int len)
{
	gss_client_signaling_conn *client_conn = (gss_client_signaling_conn*)user_data;
	GSS_DATA_HEADER* header = (GSS_DATA_HEADER*)data;
	PJ_UNUSED_ARG(conn);
	switch(header->cmd)
	{
	case GSS_SIGNALING_DATA: //signal data
		if(client_conn->cb.on_recv)
		{
			unsigned short* client_idx =(unsigned short*)(header+1);
			*client_idx = pj_ntohs(*client_idx);

			if(*client_idx != client_conn->client_index)
			{
				PJ_LOG(1,("client_signaling", "client index error!, command index %d,connection index %d", *client_idx, client_conn->client_index));
				return;
			}
			(*client_conn->cb.on_recv)(client_conn, 
				client_conn->user_data, 
				(char*)(client_idx+1),
				len-sizeof(GSS_DATA_HEADER)-sizeof(unsigned short));
		}
		break;
	case GSS_MAIN_DISCONNECTED: //device disconnect

		PJ_LOG(4,(client_conn->gssc->obj_name, "signaling device disconnect"));

		if(client_conn->cb.on_device_disconnect)
		{
			(*client_conn->cb.on_device_disconnect)(client_conn, client_conn->user_data);
		}
		break;

	case GSS_CONNECT_RESULT: //connect result
		{
			GSS_SIGNALING_CONNECT_RESULT_CMD* conn_result = (GSS_SIGNALING_CONNECT_RESULT_CMD*)(header+1); 

			conn_result->result = pj_ntohl(conn_result->result);
			conn_result->index = pj_ntohs(conn_result->index);

			PJ_LOG(4,(client_conn->gssc->obj_name, "signaling connect result %d,index %d", conn_result->result, conn_result->index));

			if(conn_result->result == 0) //connect ok,save client index
				client_conn->client_index = conn_result->index;
			if(client_conn->cb.on_connect_result)
			{
				(*client_conn->cb.on_connect_result)(client_conn, client_conn->user_data, conn_result->result);
			}
		}
		break;
	}
}

//client signaling connection connect server
P2P_DECL(int) gss_client_signaling_connect(gss_client_conn_cfg* cfg, void** transport)
{
	pj_status_t status;
	gss_conn* gssc = NULL;
	gss_client_signaling_conn* conn;
	gss_conn_cb callback;

	if(cfg == NULL || transport == NULL || cfg->server == NULL || cfg->uid == NULL)
		return PJ_EINVAL;

	check_pj_thread();

	callback.on_recv = client_signaling_on_recv;
	callback.on_disconnect = client_signaling_on_disconnect;
	callback.on_connect_result = client_signaling_connect_result;
	callback.on_destroy = client_signaling_on_destroy;
	status = gss_conn_create(cfg->uid, cfg->server, cfg->port, cfg->user_data, &callback, &gssc);
	if(status != PJ_SUCCESS)
		return status;

	conn = PJ_POOL_ZALLOC_T(gssc->pool, gss_client_signaling_conn);
	conn->gssc = gssc;
	conn->user_data = cfg->user_data;
	conn->client_index = INVALID_CLINET_INDEX;
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

//send data to client
P2P_DECL(int) gss_client_signaling_send(void *transport, char* buf, int buffer_len, p2p_send_model model)
{
	gss_client_signaling_conn *conn = (gss_client_signaling_conn *)transport;
	unsigned short client_index;
	if(!conn)
		return PJ_EINVAL;

	check_pj_thread();

	client_index = pj_htons(conn->client_index);
	return gss_conn_send(conn->gssc,
		buf, 
		buffer_len, 
		(char*)&client_index, 
		sizeof(unsigned short), 
		GSS_SIGNALING_DATA,
		model); 
}


//destroy client signaling connection
P2P_DECL(void) gss_client_signaling_destroy(void* transport)
{
	gss_client_signaling_conn *conn = (gss_client_signaling_conn *)transport;
	if(!conn)
		return;

	check_pj_thread();

	gss_conn_destroy(conn->gssc);

}