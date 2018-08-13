#include <pjnath/p2p_global.h>
#include <pjnath/p2p_tcp_proxy.h>
#include <pj/compat/socket.h>



typedef struct p2p_tcp_connect_sock 
{
	pj_int32_t id; /*Unique id,high two bytes is remote listen port, low two bytes is remote sock id*/
	pj_uint32_t hash_value; /*cache hash value for connection id, only calculate once*/

	pj_activesock_t *activesock;
	pj_sock_t		sock;

	pj_pool_t *pool; /*memory manage poll*/

	pj_grp_lock_t  *grp_lock;  /**< Group lock.*/

	pj_bool_t	 destroy_req;//To prevent duplicate destroy

	char read_buffer[sizeof(p2p_proxy_header) + PROXY_SOCK_PACKAGE_SIZE];/*receive data buffer*/

	pj_ioqueue_op_key_t send_key; /*pj io send key*/
	p2p_tcp_data* p2p_send_data_first;/*fist cache data for pending send*/
	p2p_tcp_data* p2p_send_data_last;/*last cache data for pending send*/

	pj_timer_entry timer;	
	pj_uint8_t delay_destroy_times; /*if has cache send data, delay destroy*/

	p2p_tcp_connect_proxy* connect_proxy;

	int pause_send_status;

}p2p_tcp_connect_sock;

static void p2p_tcp_connect_sock_destroy(p2p_tcp_connect_sock* tcp_sock);

PJ_DECL(pj_status_t) init_p2p_tcp_connect_proxy(p2p_tcp_connect_proxy* proxy,
												pj_str_t* proxy_addr,
												pj_pool_t *pool,
												p2p_tcp_connect_proxy_cb* cb,
												void* user_data)
{
	PJ_LOG(4,("p2p_tcp_c_p", "init_p2p_tcp_connect_proxy %p", proxy));
	proxy->pool = pool;
	pj_memcpy(&proxy->cb , cb, sizeof(p2p_tcp_connect_proxy_cb));
	proxy->user_data = user_data;
	proxy->tcp_sock_proxys = pj_hash_create(proxy->pool, SOCK_HASH_TABLE_SIZE);
	pj_strdup_with_null(pool, &proxy->proxy_addr, proxy_addr);
	pj_mutex_create_recursive(proxy->pool, NULL, &proxy->sock_mutex);
	proxy->data_package_size = PROXY_SOCK_PACKAGE_SIZE;
	return PJ_SUCCESS;
}

PJ_DECL(void) uninit_p2p_tcp_connect_proxy(p2p_tcp_connect_proxy* proxy)
{
	p2p_tcp_connect_sock **tcp_sock = NULL;
	unsigned sock_count = 0;
	pj_hash_iterator_t itbuf, *it;
	unsigned i;

	PJ_LOG(4,("p2p_tcp_c_p", "uninit_p2p_tcp_connect_proxy %p", proxy));

	pj_mutex_lock(proxy->sock_mutex);
	//prevent deadlock, get items in hash table, then clean hash table
	sock_count = pj_hash_count(proxy->tcp_sock_proxys);
	if(sock_count)
	{
		p2p_tcp_connect_sock** sock;
		tcp_sock = sock = (p2p_tcp_connect_sock**)p2p_malloc(sizeof(p2p_tcp_connect_sock*)*sock_count);
		it = pj_hash_first(proxy->tcp_sock_proxys, &itbuf);
		while (it) 
		{
			*sock = (p2p_tcp_connect_sock*) pj_hash_this(proxy->tcp_sock_proxys, it);
			//use pj_hash_set NULL, remove from hash table
			pj_hash_set(NULL, proxy->tcp_sock_proxys, &(*sock)->id, sizeof(pj_int32_t), (*sock)->hash_value, NULL);
			it = pj_hash_first(proxy->tcp_sock_proxys, &itbuf);
			sock++;
		}
	}
	pj_mutex_unlock(proxy->sock_mutex);

	for(i=0; i<sock_count; i++)
	{
		p2p_tcp_connect_sock_destroy(tcp_sock[i]);
	}
	if(tcp_sock)
		p2p_free(tcp_sock);

	pj_mutex_destroy(proxy->sock_mutex);
	PJ_LOG(4,("p2p_tcp_c_p", "uninit_p2p_tcp_connect_proxy %p end", proxy));
}

static void send_empty_cmd(p2p_tcp_connect_sock *tcp_sock, pj_int16_t cmd)
{
	//cross-platform, All the fields are in network byte order
	p2p_proxy_header header;
	PJ_LOG(4,("p2p_tcp_c_s", "send_empty_cmd %p %d %d", tcp_sock, tcp_sock->id, cmd));
	header.listen_port = pj_htons(ID_TO_LISTEN_PORT(tcp_sock->id));
	header.sock_id = pj_htons(ID_TO_SOCK_ID(tcp_sock->id));
	header.command = pj_htons(cmd);
	header.data_length = 0;
	if(tcp_sock->connect_proxy->cb.send_tcp_data)
		(*tcp_sock->connect_proxy->cb.send_tcp_data)(tcp_sock->connect_proxy, 
		(const char*)&header,
		sizeof(p2p_proxy_header));
}

static void add_sock_to_connect_proxy(p2p_tcp_connect_proxy* connect_proxy, p2p_tcp_connect_sock* tcp_sock)
{
	pj_uint32_t hval=0;
	pj_mutex_lock(connect_proxy->sock_mutex);

	if (pj_hash_get(connect_proxy->tcp_sock_proxys, &tcp_sock->id, sizeof(pj_int32_t), &hval) == NULL) 
	{		
		pj_hash_set(connect_proxy->pool, connect_proxy->tcp_sock_proxys, &tcp_sock->id, sizeof(pj_int32_t), hval, tcp_sock);
		tcp_sock->hash_value = hval;
	}

	pj_mutex_unlock(connect_proxy->sock_mutex);

	if(connect_proxy->cb.add_ref) //release it in remove_sock_from_connect_proxy
		(*connect_proxy->cb.add_ref)(connect_proxy);
}

static void remove_sock_from_connect_proxy(p2p_tcp_connect_proxy* connect_proxy, p2p_tcp_connect_sock* tcp_sock)
{
	pj_mutex_lock(connect_proxy->sock_mutex);
	//use pj_hash_set NULL, remove from hash table
	if(pj_hash_get(connect_proxy->tcp_sock_proxys, &tcp_sock->id, sizeof(pj_int32_t), &tcp_sock->hash_value)) 
		pj_hash_set(NULL, connect_proxy->tcp_sock_proxys, &tcp_sock->id, sizeof(pj_int32_t), tcp_sock->hash_value, NULL);

	pj_mutex_unlock(connect_proxy->sock_mutex);

	if(connect_proxy->cb.release_ref)
		(*connect_proxy->cb.release_ref)(connect_proxy);
}

static void p2p_tcp_connect_sock_destroy(p2p_tcp_connect_sock* tcp_sock)
{
	p2p_tcp_data* p2p_data, *next_data;
	pj_activesock_t *activesock;
	pj_sock_t sock;
	if(tcp_sock == NULL || tcp_sock->destroy_req)
		return;

	PJ_LOG(4,("p2p_tcp_c_s", "p2p_tcp_connect_sock_destroy  %p %d", tcp_sock, tcp_sock->id));

	pj_grp_lock_acquire(tcp_sock->grp_lock);
	if (tcp_sock->destroy_req) { //already destroy, so return
		pj_grp_lock_release(tcp_sock->grp_lock);
		PJ_LOG(4,("p2p_tcp_c_s", "p2p_tcp_connect_sock_destroy  %p %d end1", tcp_sock, tcp_sock->id));
		return;
	}
	tcp_sock->destroy_req = PJ_TRUE;

	pj_timer_heap_cancel_if_active(get_p2p_global()->timer_heap, &tcp_sock->timer, P2P_TIMER_NONE);

	activesock = tcp_sock->activesock;
	sock = tcp_sock->sock;
	tcp_sock->sock = PJ_INVALID_SOCKET;
	tcp_sock->activesock = NULL;

	p2p_data = tcp_sock->p2p_send_data_first;
	while(p2p_data)
	{
		next_data = p2p_data->next;
		free_p2p_tcp_data(p2p_data);
		p2p_data = next_data;
	}
	tcp_sock->p2p_send_data_first = NULL;
	tcp_sock->p2p_send_data_last = NULL;

	pj_grp_lock_release(tcp_sock->grp_lock);

	if (activesock != NULL) 
	{
		PJ_LOG(4,("p2p_tcp_c_s", "pj_soc2k_close  %p %d", tcp_sock, tcp_sock->id));
		pj_activesock_close(activesock);
		PJ_LOG(4,("p2p_tcp_c_s", "pj_soc2k_close end %p %d", tcp_sock, tcp_sock->id));
	} else if (sock != PJ_INVALID_SOCKET)
	{
		PJ_LOG(4,("p2p_tcp_c_s", "pj_sock_close22  %p %d", tcp_sock, tcp_sock->id));
		pj_sock_close(sock);
		PJ_LOG(4,("p2p_tcp_c_s", "pj_soc2k_close22 end %p %d", tcp_sock, tcp_sock->id));

	}

	remove_sock_from_connect_proxy(tcp_sock->connect_proxy, tcp_sock);
	pj_grp_lock_dec_ref(tcp_sock->grp_lock); //release reference self

	PJ_LOG(4,("p2p_tcp_c_s", "p2p_tcp_connect_sock_destroy  %p %d end", tcp_sock, tcp_sock->id));
}

/*
 * Timer event.
 */
static void on_timer_event(pj_timer_heap_t *th, pj_timer_entry *e)
{
	p2p_tcp_connect_sock *tcp_sock = (p2p_tcp_connect_sock*)e->user_data;
	PJ_UNUSED_ARG(th);
	if (e->id == P2P_TIMER_DELAY_DESTROY)
	{
		tcp_sock->delay_destroy_times++;
		//if send queue is empty or delay times more than MAX_DELAY_DESTROY_TIMES
		if(tcp_sock->p2p_send_data_first == NULL || tcp_sock->delay_destroy_times >= MAX_DELAY_DESTROY_TIMES)
		{
			p2p_tcp_connect_sock_destroy(tcp_sock);
		}
		else/* again reschedule timer */
		{
			pj_time_val delay = {1, 0};
			pj_timer_heap_schedule_w_grp_lock(get_p2p_global()->timer_heap,
				&tcp_sock->timer,
				&delay, P2P_TIMER_DELAY_DESTROY,
				tcp_sock->grp_lock);
		}
	}
}


//callback to free memory of p2p_tcp_connect_sock
static void p2p_tcp_sock_on_destroy(void *obj)
{
	p2p_tcp_connect_sock *tcp_sock = (p2p_tcp_connect_sock*)obj;
	PJ_LOG(4,("p2p_tcp_c_s", "p2p_tcp_sock_on_destroy %p destroyed", tcp_sock));
	delay_destroy_pool(tcp_sock->pool);
}

//receive tcp data,then send these data to remote
static pj_bool_t on_tcp_proxy_read(pj_activesock_t *asock,
								   void *data,
								   pj_size_t size,
								   pj_status_t status,
								   pj_size_t *remainder)
{
	p2p_tcp_connect_sock *tcp_sock = (p2p_tcp_connect_sock*)pj_activesock_get_user_data(asock);
	pj_bool_t ret = PJ_TRUE;
	PJ_UNUSED_ARG(remainder);
	PJ_UNUSED_ARG(data);
	if(status == PJ_SUCCESS && !tcp_sock->destroy_req)
	{
		p2p_proxy_header* header = (p2p_proxy_header*)tcp_sock->read_buffer;
		//PJ_LOG(4,("p2p_tcp_c_s", "on_tcp_proxy_read %p %d %d %d", tcp_sock, tcp_sock->id, tcp_sock->pause_send_status, size));

		header->data_length = htonl(size);

		if(tcp_sock->connect_proxy->cb.send_tcp_data)
		{
			status = (*tcp_sock->connect_proxy->cb.send_tcp_data)(tcp_sock->connect_proxy, 
				tcp_sock->read_buffer,
				sizeof(p2p_proxy_header) + size);
			if(status == -1)//send to remote blocked,udt socket send buffer is full
			{
				ret = PJ_FALSE;
				tcp_sock->pause_send_status = P2P_TCP_PAUSE_COMPLETED;
				PJ_LOG(4,("p2p_tcp_c_s", "on_tcp_proxy_read P2P_TCP_PAUSE_COMPLETED"));
				return ret;
			}
		}

		if(tcp_sock->pause_send_status == P2P_TCP_PAUSE_READY)
		{
			pj_grp_lock_acquire(tcp_sock->grp_lock);
			if(tcp_sock->pause_send_status == P2P_TCP_PAUSE_READY)
			{
				ret = PJ_FALSE;
				tcp_sock->pause_send_status = P2P_TCP_PAUSE_COMPLETED;
			}
			pj_grp_lock_release(tcp_sock->grp_lock);
		}
	}
	else
	{
		//tcp connection is closed by user application
		PJ_LOG(4,("p2p_tcp_c_s", "on_tcp_proxy_read  close %p %d", tcp_sock, tcp_sock->id));
		send_empty_cmd(tcp_sock, P2P_COMMAND_DESTROY_CONNECTION);
		p2p_tcp_connect_sock_destroy(tcp_sock);

		return PJ_FALSE;
	}
	return ret;
}

static pj_bool_t on_tcp_proxy_send(pj_activesock_t *asock,
								   pj_ioqueue_op_key_t *op_key,
								   pj_ssize_t bytes_sent)
{
	p2p_tcp_connect_sock *tcp_sock = (p2p_tcp_connect_sock*)pj_activesock_get_user_data(asock);
	PJ_UNUSED_ARG(op_key);
	/* Check for error/closure */
	if (bytes_sent <= 0) //tcp connection has exception,so close it
	{
		pj_status_t status;

		PJ_LOG(4,("p2p_tcp_c_s", "TCP send() error, %p %d, sent=%d", tcp_sock, tcp_sock->id, bytes_sent));

		status = (bytes_sent == 0) ? PJ_RETURN_OS_ERROR(OSERR_ENOTCONN) : -bytes_sent;

		send_empty_cmd(tcp_sock, P2P_COMMAND_DESTROY_CONNECTION);
		p2p_tcp_connect_sock_destroy(tcp_sock);

		return PJ_FALSE;
	}
	else //go on send cache data
	{
		p2p_tcp_data* data;
		pj_grp_lock_acquire(tcp_sock->grp_lock);
		while(tcp_sock->p2p_send_data_first)
		{
			data = tcp_sock->p2p_send_data_first;
			tcp_sock->p2p_send_data_first = data->next;
			free_p2p_tcp_data(data);//the data is sent, free it
			if(tcp_sock->p2p_send_data_first)//try send next data
			{
				if(tcp_sock->sock != PJ_INVALID_SOCKET)
				{
					pj_ssize_t size = tcp_sock->p2p_send_data_first->buffer_len;
					pj_status_t status = pj_activesock_send(tcp_sock->activesock, 
						&tcp_sock->send_key, 
						tcp_sock->p2p_send_data_first->buffer,
						&size, 
						0);
					//the activesock is whole data sent,please see pj_activesock_t.whole_data
					if(status != PJ_SUCCESS)
						break;
				}
			}
			else
			{
				tcp_sock->p2p_send_data_last = NULL; //all cache data be sent
			}
		}
		pj_grp_lock_release(tcp_sock->grp_lock);
		return PJ_TRUE;
	}
}

static pj_bool_t on_proxy_connect_complete(pj_activesock_t *asock,
								 pj_status_t status)
{
	p2p_tcp_connect_sock *tcp_sock = (p2p_tcp_connect_sock*)pj_activesock_get_user_data(asock);
	PJ_LOG(4,("p2p_tcp_c_s", "on_proxy_connect_complete %p %d, status=%d", tcp_sock, tcp_sock->id, status));
	if(status == PJ_SUCCESS)
	{
		void *readbuf[1];
		pj_sockaddr_in bound_addr;
		int addr_len = sizeof(bound_addr);
		if(pj_sock_getsockname(tcp_sock->sock, &bound_addr, &addr_len) == PJ_SUCCESS)
		{
			if(tcp_sock->connect_proxy->cb.on_tcp_connected)
			{
				(*tcp_sock->connect_proxy->cb.on_tcp_connected)(
					tcp_sock->connect_proxy,
					pj_sockaddr_get_port(&bound_addr));
			}
		}
		//start read user data,the call back function is on_tcp_proxy_read
		readbuf[0] = tcp_sock->read_buffer + sizeof(p2p_proxy_header);
		status = pj_activesock_start_read2(tcp_sock->activesock, tcp_sock->pool, tcp_sock->connect_proxy->data_package_size,
			readbuf, 0);
		
		PJ_LOG(4,("p2p_tcp_c_s", "pj_activesock_start_read2 %p %d %d, data_package_size=%d", tcp_sock, tcp_sock->id, status, tcp_sock->connect_proxy->data_package_size));

		if (status != PJ_SUCCESS && status != PJ_EPENDING)
		{
			send_empty_cmd(tcp_sock, P2P_COMMAND_DESTROY_CONNECTION);
			p2p_tcp_connect_sock_destroy(tcp_sock);
		}
		else
		{
			send_empty_cmd(tcp_sock, P2P_COMMAND_REMOTE_CONNECTED);
		}
	}
	else
	{
		send_empty_cmd(tcp_sock, P2P_COMMAND_DESTROY_CONNECTION);
		p2p_tcp_connect_sock_destroy(tcp_sock);
	}

	return PJ_SUCCESS;
}

void create_tcp_connection(p2p_tcp_connect_proxy* proxy, pj_int32_t id, pj_uint16_t listen_port)
{
	pj_pool_t *pool;
	p2p_tcp_connect_sock* tcp_sock = NULL;
	pj_status_t status;
	pj_activesock_cfg asock_cfg;
	pj_activesock_cb tcp_callback;
	p2p_proxy_header* header;
	pj_sockaddr rem_addr;
	pj_str_t dest_addr;
	pj_str_t sign = {":", 1};
	char* sign_str = NULL;
	do 
	{
		pool = pj_pool_create(&get_p2p_global()->caching_pool.factory, 
			"p2p_c_s%p", 
			PJNATH_POOL_LEN_ICE_STRANS,
			PJNATH_POOL_INC_ICE_STRANS, 
			NULL);
		tcp_sock = PJ_POOL_ZALLOC_T(pool, p2p_tcp_connect_sock);
		pj_bzero(tcp_sock, sizeof(p2p_tcp_connect_sock));
		tcp_sock->id = id;
		tcp_sock->connect_proxy = proxy;
		tcp_sock->pool = pool;
		tcp_sock->pause_send_status = P2P_TCP_PAUSE_NONE;
		//cross-platform, All the fields are in network byte order
		header = (p2p_proxy_header*)tcp_sock->read_buffer;
		header->listen_port = pj_htons(ID_TO_LISTEN_PORT(id));
		header->sock_id = pj_htons(ID_TO_SOCK_ID(id));
		header->command = pj_htons(P2P_COMMAND_DATA);

		/* Init send_key */
		pj_ioqueue_op_key_init(&tcp_sock->send_key, sizeof(tcp_sock->send_key));

		/* Timer */
		pj_timer_entry_init(&tcp_sock->timer, P2P_TIMER_NONE, tcp_sock, &on_timer_event);

		status = pj_grp_lock_create(tcp_sock->pool, NULL, &tcp_sock->grp_lock);
		if(status != PJ_SUCCESS)
			break;

		add_sock_to_connect_proxy(proxy, tcp_sock);

		//add self reference count
		pj_grp_lock_add_ref(tcp_sock->grp_lock);
		pj_grp_lock_add_handler(tcp_sock->grp_lock, pool, tcp_sock, &p2p_tcp_sock_on_destroy);

		/* Create socket */
		status = pj_sock_socket(pj_AF_INET(), pj_SOCK_STREAM(),	0, &tcp_sock->sock);
		if (status != PJ_SUCCESS)
			break;

		pj_activesock_cfg_default(&asock_cfg);
		asock_cfg.grp_lock = tcp_sock->grp_lock;

		pj_bzero(&tcp_callback, sizeof(tcp_callback));
		tcp_callback.on_data_read = &on_tcp_proxy_read;
		tcp_callback.on_data_sent = &on_tcp_proxy_send;
		tcp_callback.on_connect_complete = &on_proxy_connect_complete;

		status = pj_activesock_create(pool, tcp_sock->sock, pj_SOCK_STREAM(), &asock_cfg,
			get_p2p_global()->ioqueue, &tcp_callback, tcp_sock, &tcp_sock->activesock);
		if (status != PJ_SUCCESS) 
			break;

		/* Start asynchronous connect() operation */
		pj_strdup_with_null(pool, &dest_addr, &proxy->proxy_addr);
		sign_str = pj_strstr(&dest_addr, &sign);
		if(sign_str == NULL)
		{
			pj_sockaddr_init(pj_AF_INET(), &rem_addr, &dest_addr, listen_port);
		}
		else
		{
			listen_port = (pj_uint16_t)atoi(sign_str+1);
			pj_strset(&dest_addr, dest_addr.ptr, sign_str-dest_addr.ptr);
			pj_sockaddr_init(pj_AF_INET(), &rem_addr, &dest_addr, listen_port);

			PJ_LOG(4,("p2p_tcp_c_s", "start tcp connection, %s %d", dest_addr.ptr, listen_port));
		}
		status = pj_activesock_start_connect(tcp_sock->activesock, tcp_sock->pool, &rem_addr,
			pj_sockaddr_get_len(&rem_addr));
		PJ_LOG(4,("p2p_tcp_c_s", "pj_activesock_start_connect %p %d %s %d, status=%d",
			tcp_sock, 
			tcp_sock->id,
			proxy->proxy_addr.ptr,
			listen_port,
			status));
		if (status == PJ_SUCCESS)   //connect successful immediately
		{
			send_empty_cmd(tcp_sock, P2P_COMMAND_REMOTE_CONNECTED);
		}
		else if (status != PJ_EPENDING) 
		{
			break;
		}

		return;
	}while(0);

	if(tcp_sock)
	{
		send_empty_cmd(tcp_sock, P2P_COMMAND_DESTROY_CONNECTION);
		p2p_tcp_connect_sock_destroy(tcp_sock);
	}
}

static void on_create_connection(p2p_tcp_connect_proxy* proxy, p2p_proxy_header* tcp_data)
{
	pj_uint16_t port = pj_ntohs(*(pj_uint16_t*)((char*)tcp_data+sizeof(p2p_proxy_header)));
	pj_int32_t id = COMBINE_ID(tcp_data->listen_port, tcp_data->sock_id);
	create_tcp_connection(proxy, id, port);
}

//remote destroy tcp connection
static void on_recved_destroy_connection(p2p_tcp_connect_proxy* proxy, p2p_proxy_header* tcp_data)
{
	pj_uint32_t hval=0;
	p2p_tcp_connect_sock* sock;
	pj_int32_t id = COMBINE_ID(tcp_data->listen_port, tcp_data->sock_id);

	PJ_LOG(4,("p2p_tcp_c_p", "on_recved_destroy_connection %p %d", proxy, id));

	pj_mutex_lock(proxy->sock_mutex);
	sock = pj_hash_get(proxy->tcp_sock_proxys, &id, sizeof(pj_int32_t), &hval) ;
	if(sock)
		pj_grp_lock_add_ref(sock->grp_lock);
	pj_mutex_unlock(proxy->sock_mutex);

	if(sock)
	{
		if(!sock->p2p_send_data_first)
		{
			p2p_tcp_connect_sock_destroy(sock);
		}
		else //the socket connection's send queue has data, delay close it
		{
			pj_time_val delay = {1, 0};
			pj_timer_heap_schedule_w_grp_lock(get_p2p_global()->timer_heap, &sock->timer,
				&delay, P2P_TIMER_DELAY_DESTROY,
				sock->grp_lock);
		}

		pj_grp_lock_dec_ref(sock->grp_lock);
	}
}

//receive remote tcp data
static void on_recved_p2p_data(p2p_tcp_connect_proxy* proxy, p2p_proxy_header* tcp_data)
{
	pj_uint32_t hval=0;
	p2p_tcp_connect_sock* sock;
	pj_int32_t id = COMBINE_ID(tcp_data->listen_port, tcp_data->sock_id);

	//PJ_LOG(4,("p2p_tcp_c_p", "on_recved_p2p_data %p id=%d, len=%d", proxy, id, tcp_data->data_length));

	pj_mutex_lock(proxy->sock_mutex);
	sock = pj_hash_get(proxy->tcp_sock_proxys, &id, sizeof(pj_int32_t), &hval) ;
	if(sock)
		pj_grp_lock_add_ref(sock->grp_lock);
	pj_mutex_unlock(proxy->sock_mutex);

	if(sock)
	{
		p2p_tcp_data* data = malloc_p2p_tcp_data(((char*)tcp_data+sizeof(p2p_proxy_header)), tcp_data->data_length);
		pj_grp_lock_acquire(sock->grp_lock);
		if(sock->sock != PJ_INVALID_SOCKET)
		{
			if(sock->p2p_send_data_first)//the socket connection's send queue already has data, so cache it
			{
				sock->p2p_send_data_last->next = data;
				sock->p2p_send_data_last = data;
				PJ_LOG(5,("p2p_tcp_c_s", "on_recved_p2p_data cache"));
			}
			else
			{
				pj_ssize_t size = data->buffer_len;
				pj_status_t status = PJ_SUCCESS;
				pj_activesock_t *activesock = sock->activesock;
				pj_ioqueue_op_key_t* send_key = &sock->send_key;
				sock->p2p_send_data_first = sock->p2p_send_data_last = data;

				pj_grp_lock_release(sock->grp_lock); //release it for deadlock
				if(activesock)
					status = pj_activesock_send(activesock, send_key, data->buffer, &size, 0);
				pj_grp_lock_acquire(sock->grp_lock);

				//the activesock is whole data sent,please see pj_activesock_t.whole_data
				if(status == PJ_SUCCESS)
				{
					PJ_LOG(5,("p2p_tcp_c_s", "on_recved_p2p_data sent %d", size));
					if (sock->p2p_send_data_first) //maybe p2p_tcp_connect_sock_destroy called in multi thread
					{
						free_p2p_tcp_data(data);
						sock->p2p_send_data_first = sock->p2p_send_data_last = NULL;
					}
				}
				else
				{
					PJ_LOG(5,("p2p_tcp_c_s", "on_recved_p2p_data cache %d sent %d", status, size));
				}
			}
		}
		pj_grp_lock_dec_ref(sock->grp_lock);
		pj_grp_lock_release(sock->grp_lock);
	}
}

PJ_DECL(void) p2p_tcp_connect_recved_data(p2p_tcp_connect_proxy* proxy, p2p_proxy_header* tcp_data)
{
	switch(tcp_data->command)
	{
	case P2P_COMMAND_DESTROY_CONNECTION:
		on_recved_destroy_connection(proxy, tcp_data);
		break;
	case P2P_COMMAND_DATA:
		on_recved_p2p_data(proxy, tcp_data);
		break;
	case P2P_COMMAND_CREATE_CONNECTION:
		on_create_connection(proxy, tcp_data);
		break;
	default:
		break;
	}
}

PJ_DECL(pj_bool_t) tcp_connect_proxy_find_port(p2p_tcp_connect_proxy* proxy, unsigned short port)
{
	pj_hash_iterator_t itbuf, *it;
	p2p_tcp_connect_sock* sock;
	pj_bool_t finded = PJ_FALSE;
	pj_sockaddr_in bound_addr;
	int addr_len;

	pj_mutex_lock(proxy->sock_mutex);
	it = pj_hash_first(proxy->tcp_sock_proxys, &itbuf);
	while(it) 
	{
		sock = (p2p_tcp_connect_sock*)pj_hash_this(proxy->tcp_sock_proxys, it);
		addr_len = sizeof(bound_addr);
		if(pj_sock_getsockname(sock->sock, &bound_addr, &addr_len) == PJ_SUCCESS)
		{
			if(port == pj_sockaddr_get_port(&bound_addr))
			{
				finded = PJ_TRUE;
				break;
			}
		}
		it = pj_hash_next(proxy->tcp_sock_proxys, it);
	}
	pj_mutex_unlock(proxy->sock_mutex);
	return finded;
}

PJ_DECL(void) tcp_connect_proxy_pause_send(p2p_tcp_connect_proxy* proxy, pj_bool_t pause, int data_package_size)
{
	p2p_tcp_connect_sock** tcp_sock = NULL;
	unsigned proxy_count = 0;
	pj_hash_iterator_t itbuf, *it;
	unsigned i;

	//PJ_LOG(4,("p2p_tcp_c_s", "tcp_connect_proxy_pause_send begin"));

	proxy->data_package_size = data_package_size;
	pj_mutex_lock(proxy->sock_mutex);
	proxy_count = pj_hash_count(proxy->tcp_sock_proxys);
	if(proxy_count)
	{
		p2p_tcp_connect_sock** sock;
		tcp_sock = sock = (p2p_tcp_connect_sock**)p2p_malloc(sizeof(p2p_tcp_connect_sock*)*proxy_count);
		it = pj_hash_first(proxy->tcp_sock_proxys, &itbuf);
		while(it) 
		{
			*sock = (p2p_tcp_connect_sock*)pj_hash_this(proxy->tcp_sock_proxys, it);
			pj_grp_lock_add_ref((*sock)->grp_lock);
			it = pj_hash_next(proxy->tcp_sock_proxys, it);
			sock++;
		}
	}
	pj_mutex_unlock(proxy->sock_mutex);

	if(pause)
	{
		for(i=0; i<proxy_count; i++)
		{
			pj_grp_lock_acquire(tcp_sock[i]->grp_lock);
			tcp_sock[i]->pause_send_status = P2P_TCP_PAUSE_READY;
			pj_grp_lock_dec_ref(tcp_sock[i]->grp_lock);
			pj_grp_lock_release(tcp_sock[i]->grp_lock);

			PJ_LOG(5,("p2p_tcp_c_s", "tcp_connect_proxy_pause_send true,%p %d", tcp_sock[i], tcp_sock[i]->id));
		}
	}
	else
	{
		for(i=0; i<proxy_count; i++)
		{
			pj_grp_lock_acquire(tcp_sock[i]->grp_lock);//must run in pjnath net io thread, else deadlock
			if(tcp_sock[i]->pause_send_status == P2P_TCP_PAUSE_COMPLETED)
			{
				void *readbuf[1];
				//start read user data,the call back function is on_tcp_proxy_read
				readbuf[0] = tcp_sock[i]->read_buffer + sizeof(p2p_proxy_header);
				pj_activesock_post_read(tcp_sock[i]->activesock, data_package_size,	readbuf, 0);
				PJ_LOG(5,("p2p_tcp_c_s", "tcp_connect_proxy_pause_send false P2P_TCP_PAUSE_COMPLETED,%p %d", tcp_sock[i], tcp_sock[i]->id));
			}
			else
				PJ_LOG(5,("p2p_tcp_c_s", "tcp_connect_proxy_pause_send false,%p %d", tcp_sock[i], tcp_sock[i]->id));

			tcp_sock[i]->pause_send_status = P2P_TCP_PAUSE_NONE;
			pj_grp_lock_dec_ref(tcp_sock[i]->grp_lock);
			pj_grp_lock_release(tcp_sock[i]->grp_lock);
		}
	}

	if(tcp_sock)
		p2p_free(tcp_sock);

	//PJ_LOG(4,("p2p_tcp_c_s", "tcp_connect_proxy_pause_send end"));
}