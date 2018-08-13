#include <pjnath/p2p_global.h>
#include <pjnath/p2p_tcp_proxy.h>
#include <pj/compat/socket.h>

#define THIS_FILE "p2p_tcp_proxy.c"

typedef struct p2p_tcp_sock_proxy 
{
	pj_uint16_t sock_id; /*Unique id, use p2p_tcp_listen_proxy.sock_id auto increment*/
	pj_uint32_t hash_value; /*cache hash value for connection id, only calculate once*/

	p2p_tcp_listen_proxy* listen_proxy;
	pj_activesock_t *activesock;
	pj_sock_t		sock;

	pj_pool_t *pool; /*memory manage poll*/

	pj_grp_lock_t  *grp_lock;  /**< Group lock.*/

	pj_bool_t	 destroy_req;//To prevent duplicate destroy

	char read_buffer[sizeof(p2p_proxy_header) + PROXY_SOCK_PACKAGE_SIZE];/*receive data buffer*/

	pj_ioqueue_op_key_t send_key; /*pj io send key*/
	p2p_tcp_data* p2p_send_data_first; /*fist cache data for pending send*/
	p2p_tcp_data* p2p_send_data_last; /*last cache data for pending send*/

	pj_timer_entry timer;	
	pj_uint8_t delay_destroy_times;/*if has cache send data, delay destroy*/

	pj_bool_t remote_connected; /*remote socket is connected */
	p2p_tcp_data* first_before_remote_connected;/*fist cache data before remote socket is connected */
	p2p_tcp_data* last_before_remote_connected;/*last cache data before remote socket is connected*/

	int pause_send_status;
}p2p_tcp_sock_proxy;

static void p2p_tcp_sock_destroy(p2p_tcp_sock_proxy* tcp_sock_proxy);
static void on_timer_event(pj_timer_heap_t *th, pj_timer_entry *e);

//#define DEBUG_TCP_DATA 1 //for watch memory

#ifdef DEBUG_TCP_DATA
pj_grp_lock_t *g_tcp_data_grp_lock =0;
unsigned int tcp_data_used= 0;
#endif

void free_p2p_tcp_data(p2p_tcp_data* data)
{
#ifdef DEBUG_TCP_DATA
	pj_grp_lock_acquire(g_tcp_data_grp_lock);
	tcp_data_used -= (sizeof(p2p_tcp_data)+data->buffer_len);
	pj_grp_lock_release(g_tcp_data_grp_lock);
	PJ_LOG(4,("free_p2p_tcp_data", "free_p2p_tcp_data %d\n", tcp_data_used));
#endif
	p2p_free(data);
}

p2p_tcp_data* malloc_p2p_tcp_data(const char* buffer, size_t len)
{
	p2p_tcp_data* data = 0;
#ifdef DEBUG_TCP_DATA
	if(g_tcp_data_grp_lock == 0)
	{
		pj_grp_lock_create(get_p2p_global()->pool, NULL, &g_tcp_data_grp_lock);
		pj_grp_lock_add_ref(g_tcp_data_grp_lock);
	}

	pj_grp_lock_acquire(g_tcp_data_grp_lock);
	tcp_data_used += (sizeof(p2p_tcp_data)+len);
	pj_grp_lock_release(g_tcp_data_grp_lock);
	PJ_LOG(4,("malloc_p2p_tcp_data", "malloc_p2p_tcp_data %d\n", tcp_data_used));
#endif
	data = (p2p_tcp_data*)p2p_malloc(sizeof(p2p_tcp_data)+len);
	data->buffer = (char*)data + sizeof(p2p_tcp_data);
	if(buffer)
		pj_memcpy(data->buffer, buffer, len);
	data->buffer_len = len;
	data->next = NULL;
	data->pos = 0;
	return data;
}

static pj_uint16_t get_next_sock_id(p2p_tcp_listen_proxy* listen_proxy)
{
	pj_uint16_t id = 0;
	pj_grp_lock_acquire(listen_proxy->grp_lock);
	id = listen_proxy->sock_id++;
	pj_grp_lock_release(listen_proxy->grp_lock);
	return id;
}

//callback to free memory of p2p_tcp_sock_proxy
static void p2p_tcp_sock_proxy_on_destroy(void *obj)
{
	p2p_tcp_sock_proxy *tcp_sock = (p2p_tcp_sock_proxy*)obj;
	PJ_LOG(4,("p2p_tcp_s_p", "p2p_tcp_sock_proxy_on_destroy %p destroyed", tcp_sock));
	delay_destroy_pool(tcp_sock->pool);
}

static void send_empty_tcp_cmd(p2p_tcp_sock_proxy *tcp_sock, pj_int16_t cmd)
{
	//cross-platform, All the fields are in network byte order
	p2p_proxy_header header;
	PJ_LOG(4,("p2p_tcp_s_p", "send_empty_tcp_cmd %p %d %d", tcp_sock, tcp_sock->sock_id, cmd));
	header.listen_port = pj_htons(tcp_sock->listen_proxy->proxy_port);
	header.sock_id = pj_htons(tcp_sock->sock_id);
	header.command = pj_htons(cmd);
	header.data_length = 0;
	if(tcp_sock->listen_proxy->cb.send_tcp_data)
		(*tcp_sock->listen_proxy->cb.send_tcp_data)(tcp_sock->listen_proxy, 
		(const char*)&header,
		sizeof(p2p_proxy_header));
}

//request create tcp connection
static void send_tcp_connect_cmd(p2p_tcp_sock_proxy *tcp_sock)
{
	//cross-platform, All the fields are in network byte order
#define CONNECT_COMMAND_LEN (sizeof(p2p_proxy_header) + sizeof(pj_uint16_t))
	const size_t len = CONNECT_COMMAND_LEN;
	char buffer[CONNECT_COMMAND_LEN];
	p2p_proxy_header* header = (p2p_proxy_header*)buffer;

	PJ_LOG(4,("p2p_tcp_s_p", "send_tcp_connect_cmd %p %d", tcp_sock, tcp_sock->sock_id));

	header->listen_port = pj_htons(tcp_sock->listen_proxy->proxy_port);
	header->sock_id = pj_htons(tcp_sock->sock_id);
	header->command = pj_htons(P2P_COMMAND_CREATE_CONNECTION);
	header->data_length = pj_htonl(sizeof(pj_uint16_t));
	*(pj_uint16_t*)(buffer+sizeof(p2p_proxy_header)) = pj_htons(tcp_sock->listen_proxy->remote_listen_port);

	if(tcp_sock->listen_proxy->cb.send_tcp_data)
		(*tcp_sock->listen_proxy->cb.send_tcp_data)(tcp_sock->listen_proxy, 
		buffer,
		len);
}

//receive tcp data,then send these data to remote
static pj_bool_t on_tcp_proxy_read(pj_activesock_t *asock,
							  void *data,
							  pj_size_t size,
							  pj_status_t status,
							  pj_size_t *remainder)
{
	p2p_tcp_sock_proxy *tcp_sock = (p2p_tcp_sock_proxy*)pj_activesock_get_user_data(asock);
	pj_bool_t ret = PJ_TRUE;
	PJ_UNUSED_ARG(remainder);
	if(status == PJ_SUCCESS && !tcp_sock->destroy_req)
	{
		p2p_proxy_header* header = (p2p_proxy_header*)tcp_sock->read_buffer;

		//PJ_LOG(4,("p2p_tcp_s_p", "on_tcp_proxy_read %p %d %d", tcp_sock, tcp_sock->sock_id, size));
		
		pj_grp_lock_acquire(tcp_sock->grp_lock);
		
		header->data_length = htonl(size);
		if(!tcp_sock->remote_connected)//if remote connection had not connected, cache the data
		{
			data = malloc_p2p_tcp_data(tcp_sock->read_buffer, size+sizeof(p2p_proxy_header));
			if(tcp_sock->first_before_remote_connected)
			{
				tcp_sock->last_before_remote_connected->next = data;
				tcp_sock->last_before_remote_connected = data;
			}
			else
			{
				tcp_sock->last_before_remote_connected = tcp_sock->first_before_remote_connected = data;
			}
		}
		else
		{
			if(tcp_sock->listen_proxy->cb.send_tcp_data)
			{
				status = (*tcp_sock->listen_proxy->cb.send_tcp_data)(tcp_sock->listen_proxy, 
					tcp_sock->read_buffer,
					sizeof(p2p_proxy_header) + size);
				if(status == -1)//send to remote blocked,udt socket send buffer is full
				{
					ret = PJ_FALSE;
					tcp_sock->pause_send_status = P2P_TCP_PAUSE_COMPLETED;
				}
			}
		}

		if(tcp_sock->pause_send_status == P2P_TCP_PAUSE_READY)
		{
			ret = PJ_FALSE;
			tcp_sock->pause_send_status = P2P_TCP_PAUSE_COMPLETED;
		}

		pj_grp_lock_release(tcp_sock->grp_lock);

	}
	else
	{
		//tcp connection is closed by user application
		PJ_LOG(4,("p2p_tcp_s_p", "on_tcp_proxy_read  close %p %d %d", tcp_sock, tcp_sock->sock_id, status));
		send_empty_tcp_cmd(tcp_sock, P2P_COMMAND_DESTROY_CONNECTION);
		p2p_tcp_sock_destroy(tcp_sock);

		return PJ_FALSE;
	}
	return ret;
}

static pj_bool_t on_tcp_proxy_send(pj_activesock_t *asock,
							  pj_ioqueue_op_key_t *op_key,
							  pj_ssize_t bytes_sent)
{
	p2p_tcp_sock_proxy *tcp_sock = (p2p_tcp_sock_proxy*)pj_activesock_get_user_data(asock);
	PJ_UNUSED_ARG(op_key);
	/* Check for error/closure */
	if (bytes_sent <= 0) //tcp connection has exception,so close it
	{
		pj_status_t status;

		PJ_LOG(4,("p2p_tcp_s_p", "TCP send() error, %p %d, sent=%d", tcp_sock, tcp_sock->sock_id, bytes_sent));

		status = (bytes_sent == 0) ? PJ_RETURN_OS_ERROR(OSERR_ENOTCONN) : -bytes_sent;

		send_empty_tcp_cmd(tcp_sock, P2P_COMMAND_DESTROY_CONNECTION);
		p2p_tcp_sock_destroy(tcp_sock);

		return PJ_FALSE;
	}
	else //go on send catch pending data
	{
		p2p_tcp_data* data;
		pj_grp_lock_acquire(tcp_sock->grp_lock);
		while(tcp_sock->p2p_send_data_first)
		{
			//PJ_LOG(4,("p2p_tcp_s_p", "on_tcp_proxy_send %p", tcp_sock));

			data = tcp_sock->p2p_send_data_first;
			tcp_sock->p2p_send_data_first = data->next;
			free_p2p_tcp_data(data);//the data be send, free it
			if(tcp_sock->p2p_send_data_first) 
			{
				if(tcp_sock->sock != PJ_INVALID_SOCKET)//send next data
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

static void add_sock_to_listen_proxy(p2p_tcp_listen_proxy* listen_proxy, p2p_tcp_sock_proxy* tcp_sock_proxy)
{
	pj_uint32_t hval=0;
	pj_grp_lock_acquire(listen_proxy->grp_lock);

	if (pj_hash_get(listen_proxy->tcp_sock_proxys, &tcp_sock_proxy->sock_id, sizeof(pj_uint16_t), &hval) == NULL) 
	{		
		pj_hash_set(listen_proxy->pool, listen_proxy->tcp_sock_proxys, &tcp_sock_proxy->sock_id, sizeof(pj_uint16_t), hval, tcp_sock_proxy);
		tcp_sock_proxy->hash_value = hval;
	}

	pj_grp_lock_release(listen_proxy->grp_lock);
}

static void remove_sock_from_listen_proxy(p2p_tcp_listen_proxy* listen_proxy, p2p_tcp_sock_proxy*tcp_sock_proxy)
{
	pj_grp_lock_acquire(listen_proxy->grp_lock);
	//use pj_hash_set NULL, remove from hash table
	if(pj_hash_get(listen_proxy->tcp_sock_proxys, &tcp_sock_proxy->sock_id, sizeof(pj_uint16_t), &tcp_sock_proxy->hash_value)) 
		pj_hash_set(NULL, listen_proxy->tcp_sock_proxys, &tcp_sock_proxy->sock_id, sizeof(pj_uint16_t), tcp_sock_proxy->hash_value, NULL);

	pj_grp_lock_release(listen_proxy->grp_lock);
}

static void p2p_tcp_sock_destroy(p2p_tcp_sock_proxy* tcp_sock_proxy)
{
	p2p_tcp_data* p2p_data, *next_data;
	pj_activesock_t *activesock;
	pj_sock_t sock;
	if(tcp_sock_proxy == NULL|| tcp_sock_proxy->destroy_req)
		return;

	PJ_LOG(4,("p2p_tcp_s_p", "p2p_tcp_sock_destroy  %p %d", tcp_sock_proxy, tcp_sock_proxy->sock_id));

	pj_grp_lock_acquire(tcp_sock_proxy->grp_lock);

	PJ_LOG(4,("p2p_tcp_s_p", "pj_grp_lock_acquire enter %p %d", tcp_sock_proxy, tcp_sock_proxy->sock_id));

	if (tcp_sock_proxy->destroy_req) { //already destroy, so return
		pj_grp_lock_release(tcp_sock_proxy->grp_lock);
		PJ_LOG(4,("p2p_tcp_s_p", "pj_grp_lock_acquire out22  %p %d", tcp_sock_proxy, tcp_sock_proxy->sock_id));
		return;
	}
	tcp_sock_proxy->destroy_req = PJ_TRUE;
	
	pj_timer_heap_cancel_if_active(get_p2p_global()->timer_heap, &tcp_sock_proxy->timer, P2P_TIMER_NONE);

	activesock = tcp_sock_proxy->activesock;
	sock = tcp_sock_proxy->sock;
	tcp_sock_proxy->sock = PJ_INVALID_SOCKET;
	tcp_sock_proxy->activesock = NULL;

	//free cache pending data
	p2p_data = tcp_sock_proxy->p2p_send_data_first;
	while(p2p_data)
	{
		next_data = p2p_data->next;
		free_p2p_tcp_data(p2p_data);
		p2p_data = next_data;
	}
	tcp_sock_proxy->p2p_send_data_first = NULL;
	tcp_sock_proxy->p2p_send_data_last = NULL;

	//free cache data before remote socket is connected
	p2p_data = tcp_sock_proxy->first_before_remote_connected;
	while(p2p_data)
	{
		next_data = p2p_data->next;
		free_p2p_tcp_data(p2p_data);
		p2p_data = next_data;
	}
	tcp_sock_proxy->first_before_remote_connected = NULL;
	tcp_sock_proxy->last_before_remote_connected = NULL;
	
	pj_grp_lock_release(tcp_sock_proxy->grp_lock);

	PJ_LOG(4,("p2p_tcp_s_p", "pj_grp_lock_acquire out  %p %d", tcp_sock_proxy, tcp_sock_proxy->sock_id));

	if (activesock != NULL) 
	{
		PJ_LOG(4,("p2p_tcp_s_p", "pj_soc2k_close  %p %d", tcp_sock_proxy, tcp_sock_proxy->sock_id));
		pj_activesock_close(activesock);
		PJ_LOG(4,("p2p_tcp_s_p", "pj_soc2k_close end %p %d", tcp_sock_proxy, tcp_sock_proxy->sock_id));
	} else if (sock != PJ_INVALID_SOCKET)
	{
		PJ_LOG(4,("p2p_tcp_s_p", "pj_sock_close22  %p %d", tcp_sock_proxy, tcp_sock_proxy->sock_id));
		pj_sock_close(sock);
		PJ_LOG(4,("p2p_tcp_s_p", "pj_soc2k_close22 end %p %d", tcp_sock_proxy, tcp_sock_proxy->sock_id));

	}

	remove_sock_from_listen_proxy(tcp_sock_proxy->listen_proxy, tcp_sock_proxy);
	//release listen proxy reference count, add reference in create_p2p_tcp_sock_proxy
	pj_grp_lock_dec_ref(tcp_sock_proxy->listen_proxy->grp_lock);

	//release self reference count
	pj_grp_lock_dec_ref(tcp_sock_proxy->grp_lock);
}

static pj_status_t create_p2p_tcp_sock_proxy(p2p_tcp_listen_proxy *listen_proxy, pj_sock_t newsock)
{
	pj_pool_t *pool;
	p2p_tcp_sock_proxy* tcp_sock_proxy;
	pj_status_t status;
	pj_activesock_cfg asock_cfg;
	pj_activesock_cb tcp_callback;
	void *readbuf[1];
	p2p_proxy_header* header;
	do 
	{
		pool = pj_pool_create(&get_p2p_global()->caching_pool.factory, 
			"tcp_s_p%p", 
			PJNATH_POOL_LEN_ICE_STRANS,
			PJNATH_POOL_INC_ICE_STRANS, 
			NULL);

		tcp_sock_proxy = PJ_POOL_ZALLOC_T(pool, p2p_tcp_sock_proxy);
		pj_bzero(tcp_sock_proxy, sizeof(p2p_tcp_sock_proxy));
		tcp_sock_proxy->pool = pool;
		tcp_sock_proxy->sock = newsock;
		tcp_sock_proxy->listen_proxy = listen_proxy;
		tcp_sock_proxy->pause_send_status = P2P_TCP_PAUSE_NONE;
		tcp_sock_proxy->sock_id = get_next_sock_id(listen_proxy);

		/* Init send_key */
		pj_ioqueue_op_key_init(&tcp_sock_proxy->send_key, sizeof(tcp_sock_proxy->send_key));

		/* Timer */
		pj_timer_entry_init(&tcp_sock_proxy->timer, P2P_TIMER_NONE, tcp_sock_proxy, &on_timer_event);

		status = pj_grp_lock_create(tcp_sock_proxy->pool, NULL, &tcp_sock_proxy->grp_lock);
		if(status != PJ_SUCCESS)
		{
			pj_pool_release(pool);
			return status;
		}
		//add listen proxy reference count. release reference in p2p_tcp_sock_destroy
		pj_grp_lock_add_ref(listen_proxy->grp_lock);

		//add self reference count
		pj_grp_lock_add_ref(tcp_sock_proxy->grp_lock);
		pj_grp_lock_add_handler(tcp_sock_proxy->grp_lock, pool, tcp_sock_proxy, &p2p_tcp_sock_proxy_on_destroy);

		add_sock_to_listen_proxy(listen_proxy, tcp_sock_proxy);

		pj_activesock_cfg_default(&asock_cfg);
		asock_cfg.grp_lock = tcp_sock_proxy->grp_lock;

		pj_bzero(&tcp_callback, sizeof(tcp_callback));
		tcp_callback.on_data_read = &on_tcp_proxy_read;
		tcp_callback.on_data_sent = &on_tcp_proxy_send;
		
		status = pj_activesock_create(pool, newsock, pj_SOCK_STREAM(), &asock_cfg,
			get_p2p_global()->ioqueue, &tcp_callback, tcp_sock_proxy, &tcp_sock_proxy->activesock);
		if (status != PJ_SUCCESS) 
			break;

		header = (p2p_proxy_header*)tcp_sock_proxy->read_buffer;
		header->listen_port = pj_htons(listen_proxy->proxy_port);
		header->sock_id = pj_htons(tcp_sock_proxy->sock_id);
		header->command = pj_htons(P2P_COMMAND_DATA);

		//start read user data,the call back function is on_tcp_proxy_read
		readbuf[0] = tcp_sock_proxy->read_buffer + sizeof(p2p_proxy_header);
		status = pj_activesock_start_read2(tcp_sock_proxy->activesock, 
			tcp_sock_proxy->pool, 
			PROXY_SOCK_PACKAGE_SIZE,
			readbuf, 0);
		if (status != PJ_SUCCESS && status != PJ_EPENDING)
			break;

		PJ_LOG(4,("p2p_tcp_s_p", "create_p2p_tcp_sock_proxy  %p %d", tcp_sock_proxy, tcp_sock_proxy->sock_id));

		//notify remote to create tcp connection
		send_tcp_connect_cmd(tcp_sock_proxy);
		return PJ_SUCCESS;
	} while (0);

	PJ_LOG(4,("p2p_tcp_s_p", "create_p2p_tcp_sock_proxy failed"));
	if(tcp_sock_proxy)
		p2p_tcp_sock_destroy(tcp_sock_proxy);
	return status;
}

//accept a user tcp connection
static pj_bool_t p2p_proxy_on_accept_complete(pj_activesock_t *asock,
								pj_sock_t newsock,
								const pj_sockaddr_t *src_addr,
								int src_addr_len)
{
	
	p2p_tcp_listen_proxy *listen_proxy = (p2p_tcp_listen_proxy*) pj_activesock_get_user_data(asock);
	PJ_UNUSED_ARG(src_addr);
	PJ_UNUSED_ARG(src_addr_len);
	if(listen_proxy)
		create_p2p_tcp_sock_proxy(listen_proxy, newsock);

	return PJ_TRUE;
}

//callback to free memory of p2p_tcp_listen_proxy
static void p2p_tcp_listen_proxy_on_destroy(void *obj)
{
	p2p_tcp_listen_proxy *proxy = (p2p_tcp_listen_proxy*)obj;
	PJ_LOG(4,("p2p_tcp_l_p", "p2p_tcp_listen_proxy_on_destroy %p destroyed", proxy));
	delay_destroy_pool(proxy->pool);
}

PJ_DECL(pj_status_t) create_p2p_tcp_listen_proxy(pj_uint16_t remote_listen_port, 
												  p2p_tcp_listen_proxy_cb* cb,
												  void* user_data,
												  p2p_tcp_listen_proxy** proxy)
{
	pj_pool_t *pool;
	pj_status_t status;
	p2p_tcp_listen_proxy* listen_proxy;
	pj_sockaddr_in bound_addr;
	pj_activesock_cfg activesock_cfg;
	pj_activesock_cb activesock_cb;
	int addr_len;
#ifndef WIN32
	int reuse_addr = 1;
#endif
	do 
	{
		pool = pj_pool_create(&get_p2p_global()->caching_pool.factory, 
			"p2p_l_p%p", 
			PJNATH_POOL_LEN_ICE_STRANS,
			PJNATH_POOL_INC_ICE_STRANS, 
			NULL);
		listen_proxy = PJ_POOL_ZALLOC_T(pool, p2p_tcp_listen_proxy);
		pj_bzero(listen_proxy, sizeof(p2p_tcp_listen_proxy));
		listen_proxy->pool = pool;
		listen_proxy->remote_listen_port = remote_listen_port;
		listen_proxy->user_data = user_data;
		listen_proxy->sock_id = 0;
		pj_memcpy(&listen_proxy->cb, cb, sizeof(p2p_tcp_listen_proxy_cb));

		status = pj_grp_lock_create(listen_proxy->pool, NULL, &listen_proxy->grp_lock);
		if(status != PJ_SUCCESS)
		{
			pj_pool_release(pool);
			return status;
		}
		//add self reference count
		pj_grp_lock_add_ref(listen_proxy->grp_lock);
		pj_grp_lock_add_handler(listen_proxy->grp_lock, pool, listen_proxy, &p2p_tcp_listen_proxy_on_destroy);

		listen_proxy->tcp_sock_proxys = pj_hash_create(listen_proxy->pool, SOCK_HASH_TABLE_SIZE);

		status = pj_sock_socket(pj_AF_INET(), pj_SOCK_STREAM(), 0, &listen_proxy->listen_sock_fd);
		if (status != PJ_SUCCESS) 
			break;

		status = pj_sockaddr_in_init(&bound_addr, NULL, 0);
		if (status != PJ_SUCCESS) 
			break;
#ifndef WIN32
		status = pj_sock_setsockopt(listen_proxy->listen_sock_fd, SOL_SOCKET, SO_REUSEADDR,	&reuse_addr, sizeof(reuse_addr));
		if (status != PJ_SUCCESS)
			break;
#endif		
		//try use remote listen port, if bind failed, rand bind a port
		pj_sockaddr_set_port((pj_sockaddr*)&bound_addr, remote_listen_port);
		addr_len = sizeof(bound_addr);
		status = pj_sock_bind(listen_proxy->listen_sock_fd, &bound_addr, addr_len);
		if(status != PJ_SUCCESS)
		{
			PJ_LOG(4,("p2p_tcp_l_p", "create_p2p_tcp_listen_proxy  pj_sock_bind return %d", status));

			//rand bind a port, the port is listen proxy's unique id
			status = pj_sockaddr_in_init(&bound_addr, NULL, 0);
			if (status != PJ_SUCCESS) 
				break;
			addr_len = sizeof(bound_addr);
			status = pj_sock_bind_random(listen_proxy->listen_sock_fd, &bound_addr, 0, MAX_P2P_BIND_RETRY);
			if (status != PJ_SUCCESS) 
				break;
		}
		
		status = pj_sock_getsockname(listen_proxy->listen_sock_fd, &bound_addr,	&addr_len);
		listen_proxy->proxy_port = pj_sockaddr_get_port(&bound_addr);

		status = pj_sock_listen(listen_proxy->listen_sock_fd, 5);
		if (status != PJ_SUCCESS)
			break;

		pj_activesock_cfg_default(&activesock_cfg);
		activesock_cfg.grp_lock = listen_proxy->grp_lock;

		pj_bzero(&activesock_cb, sizeof(activesock_cb));
		activesock_cb.on_accept_complete = p2p_proxy_on_accept_complete;

		/*create activesock, start asynchronous accept operation, the call back function is p2p_proxy_on_accept_complete*/
		status = pj_activesock_create(listen_proxy->pool, 
			listen_proxy->listen_sock_fd,
			pj_SOCK_STREAM(),
			&activesock_cfg, 
			get_p2p_global()->ioqueue, 
			&activesock_cb,
			listen_proxy, 
			&listen_proxy->listen_activesock) ;
		if (status != PJ_SUCCESS) 
			break;

		status = pj_activesock_start_accept(listen_proxy->listen_activesock, listen_proxy->pool);
		if (status != PJ_SUCCESS) 
			break;

		PJ_LOG(4,("p2p_tcp_l_p", "create_p2p_tcp_listen_proxy  %p %d", listen_proxy, listen_proxy->proxy_port));

		*proxy = listen_proxy;
		return PJ_SUCCESS;
	} while (0);
	
	destroy_p2p_tcp_listen_proxy(listen_proxy);
	return status;
}

PJ_DECL(void) destroy_p2p_tcp_listen_proxy(p2p_tcp_listen_proxy* proxy)
{
	p2p_tcp_sock_proxy **p2p_tcp = NULL;
	unsigned sock_count = 0;
	pj_hash_iterator_t itbuf, *it;
	unsigned i;

	if(proxy == NULL|| proxy->destroy_req)
		return;

	PJ_LOG(4,("p2p_tcp_l_p", "destroy_p2p_tcp_listen_proxy  %p %d", proxy, proxy->proxy_port));

	pj_grp_lock_acquire(proxy->grp_lock);
	if (proxy->destroy_req) { //already destroy, so return
		pj_grp_lock_release(proxy->grp_lock);
		return;
	}
	proxy->destroy_req = PJ_TRUE;

	if (proxy->listen_activesock != NULL) 
	{
		proxy->listen_sock_fd = PJ_INVALID_SOCKET;
		pj_activesock_close(proxy->listen_activesock);
	} else if (proxy->listen_sock_fd != PJ_INVALID_SOCKET)
	{
		pj_sock_close(proxy->listen_sock_fd);
		proxy->listen_sock_fd = PJ_INVALID_SOCKET;
	}

	//prevent deadlock, get items in hash table, then clean hash table
	sock_count = pj_hash_count(proxy->tcp_sock_proxys);
	if(sock_count)
	{
		p2p_tcp_sock_proxy** sock;
		p2p_tcp = sock = (p2p_tcp_sock_proxy**)p2p_malloc(sizeof(p2p_tcp_sock_proxy*)*sock_count);
		it = pj_hash_first(proxy->tcp_sock_proxys, &itbuf);
		while (it) 
		{
			*sock = (p2p_tcp_sock_proxy*) pj_hash_this(proxy->tcp_sock_proxys, it);
			//use pj_hash_set NULL, remove from hash table
			pj_hash_set(NULL, proxy->tcp_sock_proxys, &(*sock)->sock_id, sizeof(pj_uint16_t), (*sock)->hash_value, NULL);
			it = pj_hash_first(proxy->tcp_sock_proxys, &itbuf);
			sock++;
		}
	}
	pj_grp_lock_release(proxy->grp_lock);

	for(i=0; i<sock_count; i++)
	{
		p2p_tcp_sock_destroy(p2p_tcp[i]);
	}
	if(p2p_tcp)
		p2p_free(p2p_tcp);

	//release self reference count
	pj_grp_lock_dec_ref(proxy->grp_lock);
}

/*
 * Timer event.
 */
static void on_timer_event(pj_timer_heap_t *th, pj_timer_entry *e)
{
	p2p_tcp_sock_proxy *tcp_sock = (p2p_tcp_sock_proxy*)e->user_data;
	PJ_UNUSED_ARG(th);
	if (e->id == P2P_TIMER_DELAY_DESTROY)
	{
		tcp_sock->delay_destroy_times++;
		//if has pending data, delay destroy
		if(tcp_sock->p2p_send_data_first == NULL || tcp_sock->delay_destroy_times >= MAX_DELAY_DESTROY_TIMES)
		{
			p2p_tcp_sock_destroy(tcp_sock);
		}
		else/*again schedule timer */
		{
			pj_time_val delay = {1, 0};
			pj_timer_heap_schedule_w_grp_lock(get_p2p_global()->timer_heap,
				&tcp_sock->timer,
				&delay, P2P_TIMER_DELAY_DESTROY,
				tcp_sock->grp_lock);
		}
	} else if(e->id == P2P_TIMER_REMOTE_CONNECTED)
	{
		p2p_tcp_data* p2p_data, *next_data;
		if(tcp_sock->remote_connected)
			return;
		pj_grp_lock_acquire(tcp_sock->grp_lock);
		//if remote connection had connected, send cache data to remote
		p2p_data = tcp_sock->first_before_remote_connected;
		while(p2p_data)
		{
			next_data = p2p_data->next;
			if(tcp_sock->listen_proxy->cb.send_tcp_data)
			{
				(*tcp_sock->listen_proxy->cb.send_tcp_data)(tcp_sock->listen_proxy, 
					p2p_data->buffer,
					p2p_data->buffer_len);
			}
			free_p2p_tcp_data(p2p_data);
			p2p_data = next_data;
		}
		tcp_sock->first_before_remote_connected = NULL;
		tcp_sock->last_before_remote_connected = NULL;
		tcp_sock->remote_connected = PJ_TRUE;
		pj_grp_lock_release(tcp_sock->grp_lock);
	}
}

//remote destroy tcp connection
static void on_recved_destroy_connection(p2p_tcp_listen_proxy* proxy, p2p_proxy_header* tcp_data)
{
	pj_uint32_t hval=0;
	p2p_tcp_sock_proxy* sock;

	PJ_LOG(4,("p2p_tcp_l_p", "p2p_tcp_listen_proxy on_recved_destroy_connection %p %d", proxy, tcp_data->sock_id));

	pj_grp_lock_acquire(proxy->grp_lock);
	sock = pj_hash_get(proxy->tcp_sock_proxys, &tcp_data->sock_id, sizeof(pj_uint16_t), &hval) ;
	if(sock) 
		pj_grp_lock_add_ref(sock->grp_lock);
	pj_grp_lock_release(proxy->grp_lock);

	if(sock)
	{
		if(!sock->p2p_send_data_first)
		{
			p2p_tcp_sock_destroy(sock);
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
static void on_recved_p2p_data(p2p_tcp_listen_proxy* proxy, p2p_proxy_header* tcp_data)
{
	pj_uint32_t hval=0;
	p2p_tcp_sock_proxy* sock;

	//PJ_LOG(4,("p2p_tcp_l_p", "on_recved_p2p_data %p %d %d", proxy, tcp_data->sock_id, tcp_data->data_length));

	pj_grp_lock_acquire(proxy->grp_lock);
	sock = pj_hash_get(proxy->tcp_sock_proxys, &tcp_data->sock_id, sizeof(pj_uint16_t), &hval) ;
	if(sock)
		pj_grp_lock_add_ref(sock->grp_lock);
	pj_grp_lock_release(proxy->grp_lock);

	if(sock)
	{
		p2p_tcp_data* data = malloc_p2p_tcp_data(((char*)tcp_data+sizeof(p2p_proxy_header)), tcp_data->data_length);
		pj_grp_lock_acquire(sock->grp_lock);
		if(sock->sock != PJ_INVALID_SOCKET)
		{
			if(sock->p2p_send_data_first)//the socket connection's send queue already has data, so catch it
			{
				sock->p2p_send_data_last->next = data;
				sock->p2p_send_data_last = data;
			}
			else
			{
				pj_ssize_t size = data->buffer_len;
				pj_status_t status = PJ_SUCCESS;
				pj_activesock_t *activesock = sock->activesock;
				pj_ioqueue_op_key_t* send_key = &sock->send_key;
				sock->p2p_send_data_first = sock->p2p_send_data_last = data;				

				pj_grp_lock_release(sock->grp_lock);//release it for deadlock
				if(activesock)
					status = pj_activesock_send(activesock, send_key, data->buffer, &size, 0);
				pj_grp_lock_acquire(sock->grp_lock);

				//the activesock is whole data sent,please see pj_activesock_t.whole_data
				if(status == PJ_SUCCESS)
				{
					PJ_LOG(5,("p2p_tcp_l_p", "on_recved_p2p_data sent %d", size));
					if(sock->p2p_send_data_first) //maybe destroy_p2p_tcp_listen_proxy called in multi thread
					{
						free_p2p_tcp_data(data);
						sock->p2p_send_data_first = sock->p2p_send_data_last = NULL;
					}
				}
				else
				{
					PJ_LOG(4,("p2p_tcp_l_p", "on_recved_p2p_data cache %d sent %d", status, size));
				}
			}
		}
		pj_grp_lock_dec_ref(sock->grp_lock);
		pj_grp_lock_release(sock->grp_lock);
	}
}

//remote had connected to user listen socket
static void on_remote_connected(p2p_tcp_listen_proxy* proxy, p2p_proxy_header* tcp_data)
{
	pj_uint32_t hval=0;
	p2p_tcp_sock_proxy* sock;

	PJ_LOG(4,("p2p_tcp_l_p", "p2p_tcp_listen_proxy on_remote_connected %p %d", proxy, tcp_data->sock_id));

	pj_grp_lock_acquire(proxy->grp_lock);
	sock = pj_hash_get(proxy->tcp_sock_proxys, &tcp_data->sock_id, sizeof(pj_uint16_t), &hval) ;
	if(sock)
		pj_grp_lock_add_ref(sock->grp_lock);
	pj_grp_lock_release(proxy->grp_lock);

	if(sock)
	{
		pj_time_val delay = {0, 0};

		pj_grp_lock_acquire(sock->grp_lock); //schedule network io thread, send cache data
		if(sock->remote_connected == PJ_FALSE)
		{
			pj_timer_heap_schedule_w_grp_lock(get_p2p_global()->timer_heap, &sock->timer,
				&delay, P2P_TIMER_REMOTE_CONNECTED,
				sock->grp_lock);
		}
		pj_grp_lock_dec_ref(sock->grp_lock);
		pj_grp_lock_release(sock->grp_lock);
	}
}

PJ_DECL(void) p2p_tcp_listen_recved_data(p2p_tcp_listen_proxy* proxy, p2p_proxy_header* tcp_data)
{
	switch(tcp_data->command)
	{
	case P2P_COMMAND_DESTROY_CONNECTION:
		on_recved_destroy_connection(proxy, tcp_data);
		break;
	case P2P_COMMAND_DATA:
		on_recved_p2p_data(proxy, tcp_data);
		break;
	case P2P_COMMAND_REMOTE_CONNECTED:
		on_remote_connected(proxy, tcp_data);
		break;
	default:
		break;
	}
}

PJ_DECL(void) tcp_listen_proxy_pause_send(p2p_tcp_listen_proxy* proxy, pj_bool_t pause)
{
	p2p_tcp_sock_proxy** tcp_sock = NULL;
	unsigned proxy_count = 0;
	pj_hash_iterator_t itbuf, *it;
	unsigned i;

	pj_grp_lock_acquire(proxy->grp_lock);
	proxy_count = pj_hash_count(proxy->tcp_sock_proxys);
	if(proxy_count)
	{
		p2p_tcp_sock_proxy** sock;
		tcp_sock = sock = (p2p_tcp_sock_proxy**)p2p_malloc(sizeof(p2p_tcp_sock_proxy*)*proxy_count);
		it = pj_hash_first(proxy->tcp_sock_proxys, &itbuf);
		while(it) 
		{
			*sock = (p2p_tcp_sock_proxy*)pj_hash_this(proxy->tcp_sock_proxys, it);
			pj_grp_lock_add_ref((*sock)->grp_lock);
			it = pj_hash_next(proxy->tcp_sock_proxys, it);
			sock++;
		}
	}
	pj_grp_lock_release(proxy->grp_lock);

	if(pause)
	{
		for(i=0; i<proxy_count; i++)
		{
			pj_grp_lock_acquire(tcp_sock[i]->grp_lock);
			tcp_sock[i]->pause_send_status = P2P_TCP_PAUSE_READY;
			pj_grp_lock_dec_ref(tcp_sock[i]->grp_lock);
			pj_grp_lock_release(tcp_sock[i]->grp_lock);

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
				pj_activesock_post_read(tcp_sock[i]->activesock, PROXY_SOCK_PACKAGE_SIZE,	readbuf, 0);
			}

			tcp_sock[i]->pause_send_status = P2P_TCP_PAUSE_NONE;
			pj_grp_lock_dec_ref(tcp_sock[i]->grp_lock);
			pj_grp_lock_release(tcp_sock[i]->grp_lock);
		}
	}

	if(tcp_sock)
		p2p_free(tcp_sock);
}