#include <pjnath/gss_conn.h>
#include <pjnath/errno.h>
#include <pj/compat/socket.h>
#include <pjnath/p2p_global.h>
#include <pjnath/p2p_tcp_proxy.h>

#ifndef GSS_CONN_POOL_LEN
#define GSS_CONN_POOL_LEN (1024)
#endif

//callback to free memory of gss_conn
static void gss_conn_on_destroy(void *obj)
{
	gss_conn *conn = (gss_conn*)obj;
	PJ_LOG(4,(conn->obj_name, "gss connection %p destroyed", conn));

	if(conn->cb.on_destroy)
		(*conn->cb.on_destroy)(conn, conn->user_data);

	if(conn->destroy_event)
	{
		pj_event_destroy(conn->destroy_event);
		conn->destroy_event = NULL;
	}
	
	if(conn->send_event)
	{
		pj_event_destroy(conn->send_event);
		conn->send_event = NULL;
	}

	if(conn->send_mutex)
	{
		pj_mutex_destroy(conn->send_mutex);
		conn->send_mutex = NULL;
	}
	
	delay_destroy_pool(conn->pool);
}

//connection connect server
int gss_conn_create(char* uid, char* server, unsigned short port, void* user_data, gss_conn_cb* cb, gss_conn** gssc)  
{
	pj_status_t status;
	pj_pool_t *pool;
	gss_conn *conn;

	//create gss_conn object
	pool = pj_pool_create(&get_p2p_global()->caching_pool.factory, 
		"gssc%p", 
		GSS_CONN_POOL_LEN+sizeof(gss_conn),
		GSS_CONN_POOL_LEN, 
		NULL);

	conn = PJ_POOL_ZALLOC_T(pool, gss_conn);
	pj_bzero(conn, sizeof(gss_conn));
	conn->pool = pool;
	conn->obj_name = pool->obj_name;
	conn->user_data = user_data;
	conn->port = port;
	conn->pause_recv_status = GSS_CONN_PAUSE_NONE;

	conn->conn_status = GSS_CONN_DISCONNECT;

	pj_strdup2_with_null(pool, &conn->server, server);
	pj_strdup2_with_null(pool, &conn->uid, uid);

	pj_memcpy(&conn->cb, cb, sizeof(gss_conn_cb));
	
	status = pj_grp_lock_create(pool, NULL, &conn->grp_lock);
	if (status != PJ_SUCCESS)
	{
		pj_pool_release(pool);
		return status;
	}

	status = pj_mutex_create_recursive(pool, NULL, &conn->send_mutex);
	if (status != PJ_SUCCESS)
	{
		pj_pool_release(pool);
		return status;
	}

	status = pj_event_create(pool, "gss_conn_send_event", PJ_FALSE, PJ_FALSE, &conn->send_event);
	if (status != PJ_SUCCESS)
	{
		pj_pool_release(pool);
		return status;
	}

	status = pj_event_create(conn->pool, "gss_conn_destory_event", PJ_FALSE, PJ_FALSE, &conn->destroy_event);
	if (status != PJ_SUCCESS)
	{
		pj_pool_release(pool);
		return status;
	}

	//add self reference count
	pj_grp_lock_add_ref(conn->grp_lock);
	pj_grp_lock_add_handler(conn->grp_lock, pool, conn, &gss_conn_on_destroy);

	PJ_LOG(4, (conn->obj_name, "gss connection %p created", conn));

	*gssc = conn;
	return PJ_SUCCESS;
}

static void async_gss_conn_destroy(void *arg)
{
	gss_conn *conn = (gss_conn *)arg;
	pj_activesock_t *activesock = NULL;
	pj_sock_t sock = PJ_INVALID_SOCKET;

	struct p2p_tcp_data* send_data;

	PJ_LOG(4,(conn->obj_name, "gss connection %p destroy start", conn));

	if(!conn || conn->destroy_req)
		return;

	pj_grp_lock_acquire(conn->grp_lock);

	if (conn->destroy_req) { //already destroy, so return
		pj_grp_lock_release(conn->grp_lock);
		return;
	}
	conn->destroy_req = PJ_TRUE;

	activesock = conn->activesock;
	sock = conn->sock;

	conn->activesock = NULL;
	conn->sock = PJ_INVALID_SOCKET;

	conn->conn_status = GSS_CONN_DISCONNECT;

	while(conn->send_data_first)
	{
		send_data = conn->send_data_first;
		conn->send_data_first = send_data->next;
		p2p_free(send_data);
	}
	conn->send_data_last = NULL;
	conn->send_cache_count = 0;

	pj_grp_lock_release(conn->grp_lock);

	if (activesock != NULL) 
	{
		pj_activesock_close(activesock);
	} 
	else if (sock != PJ_INVALID_SOCKET)
	{
		pj_sock_close(sock);
	}

	if(conn->send_event)
		pj_event_set(conn->send_event);

	if(conn->destroy_event)
	{
		PJ_LOG(4,(conn->obj_name, "gss connection %p pj_event_set", conn->destroy_event));
		pj_event_set(conn->destroy_event);
	}

	pj_timer_heap_cancel_if_active(get_p2p_global()->timer_heap, &conn->heart_timer, 0);

	//add in gss_conn_destroy
	if(conn->destroy_in_net_thread)
		pj_grp_lock_dec_ref(conn->grp_lock);

	//free reference count, trigger free memory function "gss_conn_destroy", add in gss_conn_create
	pj_grp_lock_dec_ref(conn->grp_lock);

	PJ_LOG(4,("async_gss_conn_destroy", "gss connection %p destroy end", conn));
	return ;
}

//destroy connection
void gss_conn_destroy(gss_conn *conn)
{
	pj_time_val delay = {0, 0};

	if(conn == 0 || conn->destroy_req )
		return ;

	PJ_LOG(4,(conn->obj_name, "gss_conn_destroy %p begin", conn));

	conn->destroy_in_net_thread = get_p2p_global()->thread == pj_thread_this();
	pj_grp_lock_add_ref(conn->grp_lock);

	//prevent call on_disconnect
	conn->conn_status = GSS_CONN_DISCONNECT;

	//async call async_gss_conn_destroy
	p2p_global_set_timer(delay, conn, async_gss_conn_destroy);

	if(conn->destroy_in_net_thread)
	{
		PJ_LOG(4,(conn->obj_name, "gss_conn_destroy 1 %p end", conn));
	}
	else
	{
		PJ_LOG(4,(conn->obj_name, "gss_conn_destroy 2 %p end", conn));
		pj_event_wait(conn->destroy_event);		
		pj_grp_lock_dec_ref(conn->grp_lock);
		PJ_LOG(4,("gss_conn_destroy", "gss_conn_destroy 3 %p end", conn));
	}

	PJ_LOG(4,("gss_conn_destroy", "gss_conn_destroy %p end", conn));
}

//receive tcp data
static pj_bool_t gss_conn_on_read(pj_activesock_t *asock,
								   void *data,
								   pj_size_t size,
								   pj_status_t status,
								   pj_size_t *remainder)
{
	gss_conn *conn = (gss_conn*)pj_activesock_get_user_data(asock);
	if(conn->destroy_req)
		return PJ_TRUE;

	//PJ_LOG(4,(conn->obj_name, "gss_conn_on_read %p status %d, size %d, *remainder %d", conn, status, size, *remainder));
	if(status == PJ_SUCCESS)
	{
		pj_bool_t ret = PJ_TRUE;
		pj_size_t pos = 0;

		pj_gettickcount(&conn->last_recv_time);
		
		while(pos < size)
		{
			GSS_DATA_HEADER* header = (GSS_DATA_HEADER*)((char*)data+pos);
			unsigned short cmd_len = ntohs(header->len)+sizeof(GSS_DATA_HEADER);
			if(cmd_len <= (size-pos)) //got a full command
			{
				header->len = cmd_len - sizeof(GSS_DATA_HEADER);
				if(conn->cb.on_recv)
					(*conn->cb.on_recv)(conn, conn->user_data, (char*)header, cmd_len);

				pos += cmd_len;
			}
			else
			{
				*remainder = size-pos;
				if(pos != 0)
					memmove(data, (char*)data+pos, *remainder);
				break;
			}
		}		
		//PJ_LOG(4,(conn->obj_name, "gss_conn_on_read return PJ_TRUE %p remainder %d", conn, *remainder));

		pj_grp_lock_acquire(conn->grp_lock);
		if(conn->pause_recv_status == GSS_CONN_PAUSE_READY)
		{
			PJ_LOG(4,(conn->obj_name, "gss_conn_on_read GSS_CONN_PAUSE_COMPLETED remainder %d", *remainder));
			conn->pause_recv_status = GSS_CONN_PAUSE_COMPLETED;
			ret = PJ_FALSE;
		}
		pj_grp_lock_release(conn->grp_lock);
		return ret;
	}
	else
	{
		//tcp connection is closed by user or server
		PJ_LOG(4,(conn->obj_name, "gss_conn_on_read closed, %p status %d", conn, status));

		if(conn->conn_status != GSS_CONN_DISCONNECT)
		{
			conn->conn_status = GSS_CONN_DISCONNECT;

			//wake up block send
			if(conn->send_event)
				pj_event_set(conn->send_event);

			if(conn->cb.on_disconnect)
				(*conn->cb.on_disconnect)(conn, conn->user_data, status);
		}

		return PJ_FALSE;
	}
}

static pj_bool_t gss_conn_on_send(pj_activesock_t *asock,
								   pj_ioqueue_op_key_t *op_key,
								   pj_ssize_t bytes_sent)
{
	gss_conn *conn = (gss_conn*)pj_activesock_get_user_data(asock);
	PJ_UNUSED_ARG(op_key);
	/* Check for error/closure */
	if (bytes_sent <= 0) //tcp connection has exception,so close it
	{
		pj_status_t status;

		PJ_LOG(4,(conn->obj_name, "gss_conn_on_send error, %p sent=%d", conn, bytes_sent));

		status = (bytes_sent == 0) ? PJ_RETURN_OS_ERROR(OSERR_ENOTCONN) : -bytes_sent;

		if(conn->conn_status != GSS_CONN_DISCONNECT)
		{
			conn->conn_status = GSS_CONN_DISCONNECT;

			//wake up block send
			if(conn->send_event)
				pj_event_set(conn->send_event);

			if(conn->cb.on_disconnect)
				(*conn->cb.on_disconnect)(conn, conn->user_data, status);
		}

		return PJ_FALSE;
	}
	else //continue send cache data
	{
		p2p_tcp_data* data;

		pj_mutex_lock(conn->send_mutex);
		PJ_LOG(5,(conn->obj_name, "gss_conn_on_send begin send_cache_count=%d", conn->send_cache_count) );

		while(conn->send_data_first)
		{
			data = conn->send_data_first;
			conn->send_data_first = data->next;
			p2p_free(data);//the data is sent, free it
			conn->send_cache_count--;
			if(conn->send_data_first)//try send next data
			{
				if(conn->sock != PJ_INVALID_SOCKET)
				{
					pj_ssize_t size = conn->send_data_first->buffer_len;
					pj_status_t status = pj_activesock_send(conn->activesock, 
						&conn->send_key, 
						conn->send_data_first->buffer,
						&size, 
						0);
					//the activesock is whole data sent,please see pj_activesock_t.whole_data
					if(status != PJ_SUCCESS)
						break;
				}
			}
			else
			{
				conn->send_data_last = NULL; //all cache data be sent
			}
		}
		PJ_LOG(5,(conn->obj_name, "gss_conn_on_send end send_cache_count=%d", conn->send_cache_count) );

		pj_mutex_unlock(conn->send_mutex);

		if(conn->send_event)
			pj_event_set(conn->send_event);
		return PJ_TRUE;
	}
}

PJ_INLINE(p2p_tcp_data*)  gss_conn_malloc_send_data(char* buf, int buffer_len, char* prefix, int prefix_len, unsigned char cmd, unsigned char data_seq)
{
	p2p_tcp_data* data;
	GSS_DATA_HEADER* header;
	unsigned char* p ;
	int total_data_len = buffer_len + prefix_len;

	data =  (p2p_tcp_data*)p2p_malloc(sizeof(p2p_tcp_data)+sizeof(GSS_DATA_HEADER)+total_data_len);
	memset(data, 0, sizeof(p2p_tcp_data)+sizeof(GSS_DATA_HEADER)+total_data_len);

	data->buffer = (char*)data + sizeof(p2p_tcp_data);
	data->buffer_len = sizeof(GSS_DATA_HEADER)+total_data_len;
	data->next = NULL;
	data->pos = 0;

	header = (GSS_DATA_HEADER*)(data+1);
	header->cmd = cmd;
	header->data_seq = data_seq;
	header->len = htons((unsigned short)total_data_len);

	//fill prefix 
	p = (unsigned char*)(header+1);
	if(prefix_len)
	{
		memcpy(p, prefix, prefix_len);
		p += prefix_len;
	}

	//copy data
	if(buffer_len)
		memcpy(p, buf, buffer_len);

	return data;
}

static void gss_conn_send_data(gss_conn *conn, p2p_tcp_data* data)
{
	if(conn->send_data_first)//the socket connection's send queue already has data, so cache it
	{
		conn->send_data_last->next = data;
		conn->send_data_last = data;
		conn->send_cache_count++;
		PJ_LOG(5,(conn->obj_name, "gss_conn_send_cmd cache size %d, count %d", data->buffer_len, conn->send_cache_count)); 
	}
	else
	{
		pj_ssize_t size = data->buffer_len;
		pj_status_t status = PJ_SUCCESS;
		pj_activesock_t *activesock = conn->activesock;
		pj_ioqueue_op_key_t* send_key = &conn->send_key;

		if(activesock)
			status = pj_activesock_send(activesock, send_key, data->buffer, &size, 0);

		//the activesock is whole data sent,please see pj_activesock_t.whole_data
		if(status == PJ_SUCCESS)
		{
			p2p_free(data);
		}
		else
		{			
			//the socket connection's send queue already has data, so cache it
			if(conn->send_data_first)
			{
				conn->send_data_last->next = data;
				conn->send_data_last = data;
				conn->send_cache_count++;
				PJ_LOG(5,(conn->obj_name, "gss_conn_send_cmd cache status %d, size %d, count %d",status, size, conn->send_cache_count)); 
			}
			else
			{
				conn->send_cache_count++;
				conn->send_data_first = conn->send_data_last = data;
				PJ_LOG(5,(conn->obj_name, "gss_conn_send_cmd status %d, size %d, count %d", status, size, conn->send_cache_count));
			}			
		}
	}
}

static void gss_conn_send_cmd(gss_conn *conn, char* buf, int buffer_len, char* prefix, int prefix_len, unsigned char cmd, unsigned char data_seq)  
{
	p2p_tcp_data* data = gss_conn_malloc_send_data(buf, buffer_len, prefix, prefix_len, cmd, data_seq);  

	gss_conn_send_data(conn, data);
}

int gss_conn_send_custom_data(gss_conn* conn, char* buf, int buffer_len)
{
	p2p_tcp_data* data;
	if(conn == 0  || conn->destroy_req || buffer_len > GSS_MAX_CMD_LEN)
		return PJ_EINVAL;

	if(conn->conn_status != GSS_CONN_CONNECTED
		|| conn->activesock == NULL 
		|| conn->sock == PJ_INVALID_SOCKET)
	{
		return PJ_EGONE;
	}

	data = (p2p_tcp_data*)p2p_malloc(sizeof(p2p_tcp_data)+buffer_len);

	data->buffer = (char*)data + sizeof(p2p_tcp_data);
	data->next = NULL;
	data->pos = 0;
	data->buffer_len = buffer_len;
	memcpy(data->buffer, buf, buffer_len);

	pj_mutex_lock(conn->send_mutex);
	gss_conn_send_data(conn, data);
	pj_mutex_unlock(conn->send_mutex);

	return PJ_SUCCESS;
}

static void gss_conn_heart_timer(pj_timer_heap_t *th, pj_timer_entry *e)
{
	gss_conn *conn = (gss_conn*)e->user_data;
	pj_time_val delay = { GSS_HEART_SPAN, 0 };
	pj_time_val now;
	long span;

	PJ_UNUSED_ARG(th);

	//send heart command
	pj_mutex_lock(conn->send_mutex);
	gss_conn_send_cmd(conn, NULL, 0, NULL, 0, GSS_HEART_CMD, LAST_DATA_SEQ);
	pj_mutex_unlock(conn->send_mutex);

	//if pause receive data, do not check heart
	if(conn->pause_recv_status == GSS_CONN_PAUSE_NONE)
	{
		//if now time subtract last send time greater than or equal to GSS_HEART_SPAN*2, server disconnect
		pj_gettickcount(&now);
		span = now.sec - conn->last_recv_time.sec;
		if(span >= GSS_HEART_SPAN*2)
		{
			PJ_LOG(4,(conn->obj_name, "gss_conn_heart_timer disconnect %p,send_cache_count %d,now.sec %d, last_recv_time.sec %d, uid %s", 
				conn, conn->send_cache_count, now.sec, conn->last_recv_time.sec, conn->uid.ptr));

			if(conn->conn_status != GSS_CONN_DISCONNECT)
			{
				conn->conn_status = GSS_CONN_DISCONNECT;

				//wake up block send
				if(conn->send_event)
					pj_event_set(conn->send_event);

				if(conn->cb.on_disconnect)
					(*conn->cb.on_disconnect)(conn, conn->user_data, PJ_EEOF);
			}
			return;
		}
	}
	
	pj_timer_heap_schedule_w_grp_lock(get_p2p_global()->timer_heap, &conn->heart_timer,
		&delay, P2P_TIMER_NONE, conn->grp_lock);
}

static pj_bool_t gss_conn_on_connect(pj_activesock_t *asock,  pj_status_t status)
{
	gss_conn *conn = (gss_conn*)pj_activesock_get_user_data(asock);

	PJ_LOG(4,(conn->obj_name, "gss_conn_on_connect %p, status=%d", conn, status));

	if(status == PJ_SUCCESS)
	{
		void *readbuf[1];
		pj_time_val delay = { GSS_HEART_SPAN, 0 };

		conn->conn_status = GSS_CONN_CONNECTED;

		/*start read socket data, callback in gss_conn_on_read*/
		readbuf[0] = conn->read_buffer;
		status = pj_activesock_start_read2(conn->activesock, conn->pool, sizeof(conn->read_buffer), readbuf, 0);

		//create and schedule send heart command timer
		pj_timer_entry_init(&conn->heart_timer, GSS_TIMER_NONE, conn, &gss_conn_heart_timer);
		pj_timer_heap_schedule_w_grp_lock(get_p2p_global()->timer_heap, &conn->heart_timer,
			&delay, GSS_CONN_HEART_TIMER_ID, conn->grp_lock);

		pj_gettickcount(&conn->last_recv_time);
	}
	else
		conn->conn_status = GSS_CONN_DISCONNECT;

	if(conn->cb.on_connect_result)
		(*conn->cb.on_connect_result)(conn, conn->user_data, status);

	return status == PJ_SUCCESS;
}

int gss_conn_connect_server(gss_conn *conn)
{
	pj_status_t status;
	pj_activesock_cfg asock_cfg;
	pj_activesock_cb tcp_callback;
	pj_sockaddr rem_addr;
	char seps[] = "\n";
	char *token = 0;
	int family = pj_AF_INET();
	char* server;
	pj_str_t pj_addr;

	if(conn == 0 )
		return PJ_EINVAL;

	pj_grp_lock_acquire(conn->grp_lock);
	if(conn->destroy_req )
	{
		pj_grp_lock_release(conn->grp_lock);
		return PJ_EGONE;
	}

	if( conn->sock != 0 )
	{
		pj_grp_lock_release(conn->grp_lock);
		return PJ_EEXISTS;
	}

	//check server family, support ipv6
	server = strdup(conn->server.ptr);
	token = strtok(server, seps);
	token = strtok(NULL, seps);
	if(token)
	{
		family = atoi(token);
		pj_addr = pj_str(server);
	}
	else
		pj_addr = conn->server;

	/* Create socket */
	status = pj_sock_socket(family, pj_SOCK_STREAM(), 0, &conn->sock);
	if (status != PJ_SUCCESS)
	{
		free(server);
		pj_grp_lock_release(conn->grp_lock);
		return status;
	}

	/* Init send_key */
	pj_ioqueue_op_key_init(&conn->send_key, sizeof(conn->send_key));

	/*create pjlib active socket*/
	pj_activesock_cfg_default(&asock_cfg);
	asock_cfg.grp_lock = conn->grp_lock;

	pj_bzero(&tcp_callback, sizeof(tcp_callback));
	tcp_callback.on_data_read = &gss_conn_on_read;
	tcp_callback.on_data_sent = &gss_conn_on_send;
	tcp_callback.on_connect_complete = &gss_conn_on_connect;

	status = pj_activesock_create(conn->pool, conn->sock, pj_SOCK_STREAM(), &asock_cfg,
		get_p2p_global()->ioqueue, &tcp_callback, conn, &conn->activesock);
	if (status != PJ_SUCCESS) 
	{
		free(server);
		pj_grp_lock_release(conn->grp_lock);
		return status;
	}

	//connect server
	pj_sockaddr_init(pj_AF_INET(), &rem_addr, &pj_addr, conn->port);
	status = pj_activesock_start_connect(conn->activesock, conn->pool, &rem_addr, pj_sockaddr_get_len(&rem_addr));
	if (status == PJ_SUCCESS)   //connect successful immediately
	{
		conn->conn_status = GSS_CONN_CONNECTED;

		if(conn->cb.on_connect_result)
			(*conn->cb.on_connect_result)(conn, conn->user_data, PJ_SUCCESS);

		free(server);
		pj_grp_lock_release(conn->grp_lock);
		return PJ_SUCCESS;
	}
	else if (status != PJ_EPENDING) 
	{
		free(server);
		pj_grp_lock_release(conn->grp_lock);
		return status;
	}
	conn->conn_status = GSS_CONN_CONNECTTING;
	free(server);
	pj_grp_lock_release(conn->grp_lock);
	return PJ_SUCCESS;
}

//close socket, disconnect from server
void gss_conn_disconnect_server(gss_conn *conn)
{
	pj_activesock_t *activesock = NULL;
	pj_sock_t sock = PJ_INVALID_SOCKET;

	if(conn == 0)
		return;

	pj_grp_lock_acquire(conn->grp_lock);
	if(conn->destroy_req || conn->sock == 0 )
	{
		pj_grp_lock_release(conn->grp_lock);
		return;
	}

	activesock = conn->activesock;
	sock = conn->sock;

	conn->activesock = NULL;
	conn->sock = PJ_INVALID_SOCKET;

	conn->conn_status = GSS_CONN_DISCONNECT;

	pj_grp_lock_release(conn->grp_lock);

	pj_timer_heap_cancel_if_active(get_p2p_global()->timer_heap, &conn->heart_timer, 0);

	if (activesock != NULL) 
	{
		pj_activesock_close(activesock);
	} 
	else if (sock != PJ_INVALID_SOCKET)
	{
		pj_sock_close(sock);
	}

	if(conn->send_event)
		pj_event_set(conn->send_event);
}

int gss_conn_send(gss_conn* conn, char* buf, int buffer_len, char* prefix, int prefix_len, unsigned char cmd, p2p_send_model model)
{	
	int sended = 0;
	unsigned char data_seq = 0;
	int data_count;

	if(conn == 0  || conn->destroy_req || prefix_len > GSS_MAX_PREFIX_LEN)
		return PJ_EINVAL;

	if(conn->conn_status != GSS_CONN_CONNECTED
		|| conn->activesock == NULL 
		|| conn->sock == PJ_INVALID_SOCKET)
	{
		return PJ_EGONE;
	}

	//nonblock model,cache full
	if(model == P2P_SEND_NONBLOCK && conn->send_cache_count >= GSS_MAX_CACHE_LEN)
	{
		return PJ_CACHE_FULL;
	}
	data_count = (buffer_len+GSS_MAX_DATA_LEN-1) / GSS_MAX_DATA_LEN;

	pj_grp_lock_add_ref(conn->grp_lock); //for multithread, other thread maybe call gss_conn_destroy, so add reference 
	pj_mutex_lock(conn->send_mutex);
		
	while(sended < buffer_len)
	{
		int data_length;

		if(conn->destroy_req || conn->conn_status != GSS_CONN_CONNECTED
			|| conn->activesock == NULL || conn->sock == PJ_INVALID_SOCKET)
		{
			pj_mutex_unlock(conn->send_mutex);
			pj_grp_lock_dec_ref(conn->grp_lock); //for multithread, decrease reference 
			return PJ_EGONE;
		}

		//block model, wait for cache data be sended
		if(model == P2P_SEND_BLOCK && conn->send_cache_count >= GSS_MAX_CACHE_LEN)
		{
			int ret;
			pj_mutex_unlock(conn->send_mutex);

			ret = run_global_loop();
			if(ret == GLOBAL_THREAD_EXIT)
			{
				pj_grp_lock_dec_ref(conn->grp_lock); //for multithread, decrease reference 
				return PJ_EGONE;
			}
			if(ret == NO_GLOBAL_THREAD)
			{
				PJ_LOG(4,(conn->obj_name, "gss_conn_send pj_event_wait begin,send_cache_count %d",	conn->send_cache_count));
				pj_event_wait(conn->send_event);
				PJ_LOG(4,(conn->obj_name, "gss_conn_send pj_event_wait end,send_cache_count %d", conn->send_cache_count));
			}

			if(conn->destroy_req || conn->conn_status != GSS_CONN_CONNECTED
				|| conn->activesock == NULL || conn->sock == PJ_INVALID_SOCKET)
			{
				pj_grp_lock_dec_ref(conn->grp_lock); //for multithread, decrease reference 
				return PJ_EGONE;
			}
			pj_mutex_lock(conn->send_mutex);
			continue;
		}

		data_length	= buffer_len-sended;
		if(data_length > GSS_MAX_DATA_LEN)
			data_length = GSS_MAX_DATA_LEN;		

		if(data_seq == data_count-1)
			data_seq = LAST_DATA_SEQ;
		
		gss_conn_send_cmd(conn, buf+sended, data_length, prefix, prefix_len, cmd, data_seq);
		
		data_seq++;
		sended += data_length;
	}

	pj_mutex_unlock(conn->send_mutex);
	pj_grp_lock_dec_ref(conn->grp_lock); //for multithread, decrease reference 
	
	return PJ_SUCCESS;
}

void gss_conn_pause_recv(gss_conn* conn, int is_pause)
{
	pj_bool_t post_read = PJ_FALSE;
	if(conn == 0 || conn->destroy_req)
		return ;

	PJ_LOG(4,(conn->obj_name, "gss_conn_pause_recv is_pause %d, pause_recv_status %d ", is_pause, conn->pause_recv_status));

	pj_grp_lock_acquire(conn->grp_lock);
	if(is_pause)
	{
		if(conn->pause_recv_status == GSS_CONN_PAUSE_NONE)
			conn->pause_recv_status = GSS_CONN_PAUSE_READY;
	}
	else
	{
		if(conn->pause_recv_status == GSS_CONN_PAUSE_READY)
		{
			conn->pause_recv_status = GSS_CONN_PAUSE_NONE;
		}
		else if(conn->pause_recv_status == GSS_CONN_PAUSE_COMPLETED)
		{
			post_read = PJ_TRUE;
			conn->pause_recv_status = GSS_CONN_PAUSE_NONE;
		}
	}
	
	pj_grp_lock_release(conn->grp_lock);

	if(post_read)
	{
		void *readbuf[1];
		readbuf[0] = conn->read_buffer;

		PJ_LOG(4,(conn->obj_name, "gss_conn_pause_recv pj_activesock_post_read"));

		pj_activesock_post_read(conn->activesock, sizeof(conn->read_buffer), readbuf, 0);
	}
}

//clean all send buffer data
void gss_conn_clean_send_buf(gss_conn* conn)
{
	struct p2p_tcp_data* send_data;
	struct p2p_tcp_data* next_data;

	if(conn == 0 || conn->destroy_req || conn->send_cache_count == 0)
		return ;

	//for multithread, other thread maybe call gss_conn_destroy, so add reference 
	pj_grp_lock_add_ref(conn->grp_lock);

	pj_mutex_lock(conn->send_mutex);

	//send_data_first already call pj_activesock_send, so the send_data_first can not free
	conn->send_data_last = conn->send_data_first;
	conn->send_cache_count = 1;
	send_data = conn->send_data_first->next;
	conn->send_data_first->next = NULL;

	while(send_data)
	{
		next_data = send_data->next;
		p2p_free(send_data);
		send_data = next_data;
	}
	

	pj_mutex_unlock(conn->send_mutex);

	//for multithread, decrease reference 
	pj_grp_lock_dec_ref(conn->grp_lock); 
}