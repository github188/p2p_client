#include <pjnath/p2p_global.h>
#include <pjnath/p2p_udp_proxy.h>
#include <pj/compat/socket.h>

#ifdef USE_UDP_PROXY

#define P2P_UDP_CONNECT_SOCK_TIMEOUT 90

typedef struct p2p_udp_connect_sock 
{
	pj_uint16_t sock_id; /*remote sock id*/
	pj_uint32_t hash_value; /*cache hash value for connection id, only calculate once*/

	pj_activesock_t *activesock;
	pj_sock_t		sock;

	pj_pool_t *pool; /*memory manage poll*/

	pj_grp_lock_t  *grp_lock;  /**< Group lock.*/

	pj_bool_t	 destroy_req;//To prevent duplicate destroy

	char read_buffer[sizeof(p2p_proxy_header) + PROXY_SOCK_PACKAGE_SIZE];/*receive data buffer*/

	pj_ioqueue_op_key_t send_key; /*pj io send key*/

	pj_timer_entry idea_timer;	//idea P2P_UDP_CONNECT_SOCK_TIMEOUT second no receive and send data, destroy it 

	p2p_udp_connect_proxy* connect_proxy;

	pj_time_val live_time; //last send or receive time

	pj_str_t proxy_addr; //127.0.0.1 or p2p_transport_cfg.proxy_addr
	pj_uint16_t proxy_port; //p2p_create_udp_proxy third parameter or p2p_transport_cfg.proxy_addr

}p2p_udp_connect_sock;

//callback to free memory of p2p_udp_listen_proxy
static void p2p_udp_listen_proxy_on_destroy(void *obj)
{
	p2p_udp_listen_proxy *proxy = (p2p_udp_listen_proxy*)obj;
	PJ_LOG(4,("p2p_udp_l_p", "p2p_udp_listen_proxy_on_destroy %p destroyed", proxy));
	delay_destroy_pool(proxy->pool);
}

pj_bool_t udp_listen_proxy_on_recvfrom(pj_activesock_t *asock,
							  void *data,
							  pj_size_t size,
							  const pj_sockaddr_t *src_addr,
							  int addr_len,
							  pj_status_t status)
{
	p2p_udp_listen_proxy *proxy = (p2p_udp_listen_proxy*)pj_activesock_get_user_data(asock);
	PJ_UNUSED_ARG(data);
	PJ_UNUSED_ARG(addr_len);

	if(status == PJ_SUCCESS && !proxy->destroy_req && !proxy->pause_send)
	{
		p2p_proxy_header* header = (p2p_proxy_header*)proxy->read_buffer;

		pj_grp_lock_acquire(proxy->grp_lock);

		pj_gettickcount(&proxy->live_time);

		header->data_length = htonl(size);
		header->sock_id = htons(pj_sockaddr_get_port(src_addr));

		if(proxy->cb.send_udp_data)
		{
			status = (*proxy->cb.send_udp_data)(proxy, 
				proxy->read_buffer,
				sizeof(p2p_proxy_header) + size);
			if(status == -1)//send to remote blocked,udt socket send buffer is full
			{
				PJ_LOG(4,("p2p_udp_l_p", "udp_listen_proxy_on_recvfrom %p proxy->pause_send = PJ_TRUE", proxy));
				proxy->pause_send = PJ_TRUE;
			}
		}

		pj_grp_lock_release(proxy->grp_lock);
	}

	return PJ_TRUE;
}

static void async_udp_listen_sock_idea_timeout(void* data)
{
	p2p_udp_listen_proxy *listen_proxy = (p2p_udp_listen_proxy*)data;

	if(!listen_proxy->destroy_req && listen_proxy->cb.on_idea_timeout)
		(*listen_proxy->cb.on_idea_timeout)(listen_proxy);

	pj_grp_lock_dec_ref(listen_proxy->grp_lock);
}
/*
 * Idea P2P_UDP_CONNECT_SOCK_TIMEOUT second no receive and send data, destroy it 
 */
static void on_udp_listen_sock_timer_event(pj_timer_heap_t *th, pj_timer_entry *e)
{
	p2p_udp_listen_proxy *listen_proxy = (p2p_udp_listen_proxy*)e->user_data;
	PJ_UNUSED_ARG(th);
	if (e->id == P2P_DISCONNECT_CONNECTION)
	{
		pj_time_val now;

		pj_gettickcount(&now);
		PJ_TIME_VAL_SUB(now, listen_proxy->live_time);
		if(now.sec >= P2P_UDP_CONNECT_SOCK_TIMEOUT )
		{
			p2p_socket_pair_item item;

			PJ_LOG(3,("p2p_udp_l_p", "on_udp_listen_sock_timer_event %p destroyed", listen_proxy));

			pj_grp_lock_add_ref(listen_proxy->grp_lock); //release in async_udp_listen_sock_idea_timeout
			item.cb = async_udp_listen_sock_idea_timeout;
			item.data = listen_proxy;
			schedule_socket_pair(get_p2p_global()->sock_pair, &item);
		}
		else
		{
			p2p_udp_listen_proxy_idea_timer(listen_proxy);
		}
	}
}

PJ_DECL(void) p2p_udp_listen_proxy_idea_timer(p2p_udp_listen_proxy* listen_proxy)
{
	pj_time_val delay = {P2P_UDP_CONNECT_SOCK_TIMEOUT, 0};
	pj_timer_heap_schedule_w_grp_lock(get_p2p_global()->timer_heap, &listen_proxy->idea_timer,
		&delay, P2P_DISCONNECT_CONNECTION,
		listen_proxy->grp_lock);
}

PJ_DECL(pj_status_t) create_p2p_udp_listen_proxy(pj_uint16_t remote_listen_port, 
												 p2p_udp_listen_proxy_cb* cb,
												 void* user_data,
												 p2p_udp_listen_proxy** proxy)
{
	pj_pool_t *pool;
	pj_status_t status;
	p2p_udp_listen_proxy* listen_proxy;
	pj_sockaddr_in bound_addr;
	pj_activesock_cfg activesock_cfg;
	pj_activesock_cb activesock_cb;
	int addr_len;
	void *readbuf[1];
	p2p_proxy_header* header;
	
	do 
	{
		pool = pj_pool_create(&get_p2p_global()->caching_pool.factory, 
			"p2p_l_p%p", 
			PJNATH_POOL_LEN_ICE_STRANS,
			PJNATH_POOL_INC_ICE_STRANS, 
			NULL);
		listen_proxy = PJ_POOL_ZALLOC_T(pool, p2p_udp_listen_proxy);
		pj_bzero(listen_proxy, sizeof(p2p_udp_listen_proxy));
		listen_proxy->pool = pool;
		listen_proxy->remote_udp_port = remote_listen_port;
		listen_proxy->user_data = user_data;
		listen_proxy->pause_send = PJ_FALSE;
		pj_memcpy(&listen_proxy->cb, cb, sizeof(p2p_udp_listen_proxy_cb));

		status = pj_grp_lock_create(listen_proxy->pool, NULL, &listen_proxy->grp_lock);
		if(status != PJ_SUCCESS)
		{
			pj_pool_release(pool);
			return status;
		}
		//add self reference count
		pj_grp_lock_add_ref(listen_proxy->grp_lock);
		pj_grp_lock_add_handler(listen_proxy->grp_lock, pool, listen_proxy, &p2p_udp_listen_proxy_on_destroy);

		/* Timer */
		pj_timer_entry_init(&listen_proxy->idea_timer, P2P_TIMER_NONE, listen_proxy, &on_udp_listen_sock_timer_event);


		status = pj_sock_socket(pj_AF_INET(), pj_SOCK_DGRAM(), 0, &listen_proxy->udp_sock_fd);
		if (status != PJ_SUCCESS) 
			break;

		status = pj_sockaddr_in_init(&bound_addr, NULL, 0);
		if (status != PJ_SUCCESS) 
			break;
	
		//try use remote listen port, if bind failed, rand bind a port
		pj_sockaddr_set_port((pj_sockaddr*)&bound_addr, remote_listen_port);
		addr_len = sizeof(bound_addr);
		status = pj_sock_bind(listen_proxy->udp_sock_fd, &bound_addr, addr_len);
		if(status != PJ_SUCCESS)
		{
			PJ_LOG(4,("p2p_udp_l_p", "create_p2p_udp_listen_proxy  pj_sock_bind return %d", status));

			//rand bind a port, the port is listen proxy's unique id
			status = pj_sockaddr_in_init(&bound_addr, NULL, 0);
			if (status != PJ_SUCCESS) 
				break;
			addr_len = sizeof(bound_addr);
			status = pj_sock_bind_random(listen_proxy->udp_sock_fd, &bound_addr, 0, MAX_P2P_BIND_RETRY);
			if (status != PJ_SUCCESS) 
				break;
		}

		status = pj_sock_getsockname(listen_proxy->udp_sock_fd, &bound_addr, &addr_len);
		listen_proxy->proxy_port = pj_sockaddr_get_port(&bound_addr);

		header = (p2p_proxy_header*)listen_proxy->read_buffer;
		header->listen_port = pj_htons(listen_proxy->remote_udp_port);
		header->command = pj_htons(P2P_COMMAND_UDP_DATA);

		pj_activesock_cfg_default(&activesock_cfg);
		activesock_cfg.grp_lock = listen_proxy->grp_lock;

		pj_bzero(&activesock_cb, sizeof(activesock_cb));
		activesock_cb.on_data_recvfrom = udp_listen_proxy_on_recvfrom;

		/*create activesock, start asynchronous recvfrom*/
		status = pj_activesock_create(listen_proxy->pool, 
			listen_proxy->udp_sock_fd,
			pj_SOCK_DGRAM(),
			&activesock_cfg, 
			get_p2p_global()->ioqueue, 
			&activesock_cb,
			listen_proxy, 
			&listen_proxy->udp_activesock) ;
		if (status != PJ_SUCCESS) 
			break;

		/* Init send keys */
		pj_ioqueue_op_key_init(&listen_proxy->send_key, sizeof(listen_proxy->send_key));

		readbuf[0] = listen_proxy->read_buffer + sizeof(p2p_proxy_header);
		status = pj_activesock_start_recvfrom2(listen_proxy->udp_activesock,
			listen_proxy->pool, 
			PROXY_SOCK_PACKAGE_SIZE,
			readbuf, 0);
		if (status != PJ_SUCCESS) 
			break;

		PJ_LOG(4,("p2p_udp_l_p", "create_p2p_udp_listen_proxy  %p %d", listen_proxy, listen_proxy->proxy_port));

		*proxy = listen_proxy;
		return PJ_SUCCESS;
	} while (0);

	destroy_p2p_udp_listen_proxy(listen_proxy);
	return status;
}

PJ_DECL(void) destroy_p2p_udp_listen_proxy(p2p_udp_listen_proxy* proxy)
{

	if(proxy == NULL|| proxy->destroy_req)
		return;

	PJ_LOG(4,("p2p_udp_l_p", "destroy_p2p_udp_listen_proxy  %p %d", proxy, proxy->proxy_port));

	pj_grp_lock_acquire(proxy->grp_lock);
	if (proxy->destroy_req) { //already destroy, so return
		pj_grp_lock_release(proxy->grp_lock);
		return;
	}
	proxy->destroy_req = PJ_TRUE;

	pj_timer_heap_cancel_if_active(get_p2p_global()->timer_heap, &proxy->idea_timer, P2P_TIMER_NONE);

	if (proxy->udp_activesock != NULL) 
	{
		proxy->udp_sock_fd = PJ_INVALID_SOCKET;
		pj_activesock_close(proxy->udp_activesock);
	} 
	else if (proxy->udp_sock_fd != PJ_INVALID_SOCKET)
	{
		pj_sock_close(proxy->udp_sock_fd);
		proxy->udp_sock_fd = PJ_INVALID_SOCKET;
	}
	
	pj_grp_lock_release(proxy->grp_lock);


	//release self reference count
	pj_grp_lock_dec_ref(proxy->grp_lock);
}

PJ_DECL(void) p2p_udp_listen_recved_data(p2p_udp_listen_proxy* proxy, p2p_proxy_header* udp_data)
{
	pj_sockaddr_in addr;
	pj_status_t status;
	int addr_len = sizeof(addr);
	pj_ssize_t data_length = udp_data->data_length;
	pj_str_t local_addr = pj_str(LOCAL_HOST_IP);

	pj_gettickcount(&proxy->live_time);

	status = pj_sockaddr_in_init(&addr, &local_addr, udp_data->sock_id);
	if (status != PJ_SUCCESS) 
		return;

	status = pj_activesock_sendto(proxy->udp_activesock, 
		&proxy->send_key, 
		(char*)udp_data+sizeof(p2p_proxy_header),
		&data_length, 
		0, 
		&addr,
		addr_len);

	if (status != PJ_SUCCESS) 
	{
		PJ_LOG(2,("p2p_udp_l_p", "p2p_udp_listen_recved_data send  %p proxy_port %d, status %d", proxy, proxy->proxy_port, status));
	}
}

PJ_DECL(void) udp_listen_proxy_pause_send(p2p_udp_listen_proxy* proxy, pj_bool_t pause)
{
	proxy->pause_send = pause;
}


/**********************************
udp connect proxy
**********************************/

static void add_sock_to_udp_connect_proxy(p2p_udp_connect_proxy* connect_proxy, p2p_udp_connect_sock* udp_sock)
{
	pj_uint32_t hval=0;
	pj_mutex_lock(connect_proxy->sock_mutex);

	if (pj_hash_get(connect_proxy->udp_sock_proxys, &udp_sock->sock_id, sizeof(pj_uint16_t), &hval) == NULL) 
	{		
		pj_hash_set(connect_proxy->pool, connect_proxy->udp_sock_proxys, &udp_sock->sock_id, sizeof(pj_uint16_t), hval, udp_sock);
		udp_sock->hash_value = hval;
	}

	pj_mutex_unlock(connect_proxy->sock_mutex);

	if(connect_proxy->cb.add_ref) //release it in remove_sock_from_udp_connect_proxy
		(*connect_proxy->cb.add_ref)(connect_proxy);
}

static void remove_sock_from_udp_connect_proxy(p2p_udp_connect_proxy* connect_proxy, p2p_udp_connect_sock* udp_sock)
{
	pj_mutex_lock(connect_proxy->sock_mutex);
	//use pj_hash_set NULL, remove from hash table
	if(pj_hash_get(connect_proxy->udp_sock_proxys, &udp_sock->sock_id, sizeof(pj_uint16_t), &udp_sock->hash_value)) 
		pj_hash_set(NULL, connect_proxy->udp_sock_proxys, &udp_sock->sock_id, sizeof(pj_uint16_t), udp_sock->hash_value, NULL);

	pj_mutex_unlock(connect_proxy->sock_mutex);

	if(connect_proxy->cb.release_ref)
		(*connect_proxy->cb.release_ref)(connect_proxy);
}

static void p2p_udp_connect_sock_send(p2p_udp_connect_sock *udp_sock, p2p_proxy_header* udp_data)
{
	pj_sockaddr_in addr;
	pj_status_t status;
	int addr_len = sizeof(addr);
	pj_ssize_t data_length = udp_data->data_length;
	pj_uint16_t port = udp_data->listen_port;
	char* real_data = (char*)udp_data+sizeof(p2p_proxy_header);

	pj_gettickcount(&udp_sock->live_time);

	if(udp_sock->proxy_port)
		port = udp_sock->proxy_port;
	
	status = pj_sockaddr_in_init(&addr, &udp_sock->proxy_addr, port);
	if (status != PJ_SUCCESS) 
		return;

	//PJ_LOG(4,("p2p_udp_c_p", "%d %d %d", real_data[0], real_data[1], pj_ntohs(*(unsigned short*)(real_data+2))));

	status = pj_activesock_sendto(udp_sock->activesock, 
		&udp_sock->send_key, 
		real_data,
		&data_length, 
		0, 
		&addr,
		addr_len);

	if (status != PJ_SUCCESS) 
	{
		PJ_LOG(2,("p2p_udp_c_p", "p2p_dup_connect_sock_send send  %p proxy_port %d, status %d", udp_sock, udp_sock->sock_id, status));
	}
}

pj_bool_t udp_connect_sock_on_recvfrom(pj_activesock_t *asock,
									   void *data,
									   pj_size_t size,
									   const pj_sockaddr_t *src_addr,
									   int addr_len,
									   pj_status_t status)
{
	p2p_udp_connect_sock *udp_sock = (p2p_udp_connect_sock*)pj_activesock_get_user_data(asock);
	PJ_UNUSED_ARG(data);
	PJ_UNUSED_ARG(addr_len);

	if(status == PJ_SUCCESS && !udp_sock->destroy_req)
	{
		p2p_proxy_header* header ;
		pj_uint16_t listen_port;

		pj_grp_lock_acquire(udp_sock->grp_lock);

		pj_gettickcount(&udp_sock->live_time);

		if(!udp_sock->connect_proxy->pause_send)
		{
			header = (p2p_proxy_header*)udp_sock->read_buffer;
			header->data_length = htonl(size);
			listen_port = pj_sockaddr_get_port(src_addr);
			header->listen_port = htons(listen_port);

			if(udp_sock->connect_proxy->cb.send_udp_data)
			{
				status = (*udp_sock->connect_proxy->cb.send_udp_data)(udp_sock->connect_proxy, 
					udp_sock->read_buffer,
					sizeof(p2p_proxy_header) + size);
				if(status == -1)//send to remote blocked,udt socket send buffer is full
				{
					udp_sock->connect_proxy->pause_send = PJ_TRUE;
				}
			}
		}

		pj_grp_lock_release(udp_sock->grp_lock);
	}

	return PJ_TRUE;
}


static void p2p_udp_connect_sock_destroy(p2p_udp_connect_sock *udp_sock)
{
	pj_activesock_t *activesock;
	pj_sock_t sock;
	if(udp_sock == NULL || udp_sock->destroy_req)
		return;

	PJ_LOG(4,("p2p_udp_c_s", "p2p_udp_connect_sock_destroy  %p %d", udp_sock, udp_sock->sock_id));

	pj_grp_lock_acquire(udp_sock->grp_lock);
	if (udp_sock->destroy_req) { //already destroy, so return
		pj_grp_lock_release(udp_sock->grp_lock);
		PJ_LOG(4,("p2p_udp_c_s", "p2p_udp_connect_sock_destroy  %p %d end1", udp_sock, udp_sock->sock_id));
		return;
	}
	udp_sock->destroy_req = PJ_TRUE;

	pj_timer_heap_cancel_if_active(get_p2p_global()->timer_heap, &udp_sock->idea_timer, P2P_TIMER_NONE);

	activesock = udp_sock->activesock;
	sock = udp_sock->sock;
	udp_sock->sock = PJ_INVALID_SOCKET;
	udp_sock->activesock = NULL;


	pj_grp_lock_release(udp_sock->grp_lock);

	if (activesock != NULL) 
	{
		PJ_LOG(4,("p2p_tcp_c_s", "pj_soc2k_close  %p %d", udp_sock, udp_sock->sock_id));
		pj_activesock_close(activesock);
		PJ_LOG(4,("p2p_tcp_c_s", "pj_soc2k_close end %p %d", udp_sock, udp_sock->sock_id));
	} else if (sock != PJ_INVALID_SOCKET)
	{
		PJ_LOG(4,("p2p_tcp_c_s", "pj_sock_close22  %p %d", udp_sock, udp_sock->sock_id));
		pj_sock_close(sock);
		PJ_LOG(4,("p2p_tcp_c_s", "pj_soc2k_close22 end %p %d", udp_sock, udp_sock->sock_id));

	}

	remove_sock_from_udp_connect_proxy(udp_sock->connect_proxy, udp_sock);
	pj_grp_lock_dec_ref(udp_sock->grp_lock); //release reference self

	PJ_LOG(4,("p2p_udp_c_s", "p2p_udp_connect_sock_destroy  %p %d end", udp_sock, udp_sock->sock_id));
}

//callback to free memory of p2p_tcp_connect_sock
static void p2p_udp_connect_sock_on_destroy(void *obj)
{
	p2p_udp_connect_sock *tcp_sock = (p2p_udp_connect_sock*)obj;
	PJ_LOG(4,("p2p_udp_c_s", "p2p_udp_connect_sock_on_destroy %p destroyed", tcp_sock));
	delay_destroy_pool(tcp_sock->pool);
}

/*
 * Idea P2P_UDP_CONNECT_SOCK_TIMEOUT second no receive and send data, destroy it 
 */
static void on_udp_connect_sock_timer_event(pj_timer_heap_t *th, pj_timer_entry *e)
{
	p2p_udp_connect_sock *udp_sock = (p2p_udp_connect_sock*)e->user_data;
	PJ_UNUSED_ARG(th);
	if (e->id == P2P_DISCONNECT_CONNECTION)
	{
		pj_time_val now;

		pj_gettickcount(&now);
		PJ_TIME_VAL_SUB(now, udp_sock->live_time);
		if(now.sec >= P2P_UDP_CONNECT_SOCK_TIMEOUT )
		{
			PJ_LOG(3,("p2p_udp_c_s", "on_udp_connect_sock_timer_event %p destroyed", udp_sock));
			p2p_udp_connect_sock_destroy(udp_sock);
		}
		else
		{
			pj_time_val delay = {P2P_UDP_CONNECT_SOCK_TIMEOUT, 0};
			pj_timer_heap_schedule_w_grp_lock(get_p2p_global()->timer_heap, &udp_sock->idea_timer,
				&delay, P2P_DISCONNECT_CONNECTION,
				udp_sock->grp_lock);
		}
	}
}


static p2p_udp_connect_sock* p2p_udp_connect_sock_create(p2p_udp_connect_proxy* proxy, p2p_proxy_header* udp_data)
{
	pj_pool_t *pool;
	p2p_udp_connect_sock* udp_sock = NULL;
	pj_status_t status;
	pj_activesock_cfg asock_cfg;
	pj_activesock_cb udp_callback;
	p2p_proxy_header* header;
	pj_uint16_t listen_port;
	void *readbuf[1];
	pj_time_val delay = {P2P_UDP_CONNECT_SOCK_TIMEOUT, 0};
	pj_sockaddr_in bound_addr;
	int addr_len;
	pj_str_t sign = {":", 1};
	char* sign_str = NULL;
	do 
	{
		pool = pj_pool_create(&get_p2p_global()->caching_pool.factory, 
			"p2p_c_s%p", 
			PJNATH_POOL_LEN_ICE_STRANS,
			PJNATH_POOL_INC_ICE_STRANS, 
			NULL);
		udp_sock = PJ_POOL_ZALLOC_T(pool, p2p_udp_connect_sock);
		pj_bzero(udp_sock, sizeof(p2p_udp_connect_sock));
		udp_sock->sock_id = udp_data->sock_id;
		udp_sock->connect_proxy = proxy;
		udp_sock->pool = pool;

		listen_port = udp_data->listen_port;

		//cross-platform, All the fields are in network byte order
		header = (p2p_proxy_header*)udp_sock->read_buffer;
		header->listen_port = pj_htons(listen_port);
		header->sock_id = pj_htons(udp_sock->sock_id);
		header->command = pj_htons(P2P_COMMAND_UDP_DATA);

		/* Init send_key */
		pj_ioqueue_op_key_init(&udp_sock->send_key, sizeof(udp_sock->send_key));

		/* Timer */
		pj_timer_entry_init(&udp_sock->idea_timer, P2P_TIMER_NONE, udp_sock, &on_udp_connect_sock_timer_event);

		status = pj_grp_lock_create(udp_sock->pool, NULL, &udp_sock->grp_lock);
		if(status != PJ_SUCCESS)
			break;

		//add self reference count
		pj_grp_lock_add_ref(udp_sock->grp_lock);
		pj_grp_lock_add_handler(udp_sock->grp_lock, pool, udp_sock, &p2p_udp_connect_sock_on_destroy);

		/* Create socket */
		status = pj_sock_socket(pj_AF_INET(), pj_SOCK_DGRAM(),	0, &udp_sock->sock);
		if (status != PJ_SUCCESS)
			break;

		status = pj_sockaddr_in_init(&bound_addr, NULL, 0);
		if (status != PJ_SUCCESS) 
			break;

		//try use remote peer port, if bind failed, rand bind a port
		pj_sockaddr_set_port((pj_sockaddr*)&bound_addr, udp_sock->sock_id);
		addr_len = sizeof(bound_addr);
		status = pj_sock_bind(udp_sock->sock, &bound_addr, addr_len);
		if(status != PJ_SUCCESS)
		{
			PJ_LOG(4,("p2p_udp_c_p", "p2p_udp_connect_sock_create  pj_sock_bind return %d", status));

			//rand bind a port, the port is listen proxy's unique id
			status = pj_sockaddr_in_init(&bound_addr, NULL, 0);
			if (status != PJ_SUCCESS) 
				break;
			addr_len = sizeof(bound_addr);
			status = pj_sock_bind_random(udp_sock->sock, &bound_addr, 0, MAX_P2P_BIND_RETRY);
			if (status != PJ_SUCCESS) 
				break;
		}

		pj_activesock_cfg_default(&asock_cfg);
		asock_cfg.grp_lock = udp_sock->grp_lock;

		pj_bzero(&udp_callback, sizeof(udp_callback));
		udp_callback.on_data_recvfrom = &udp_connect_sock_on_recvfrom;

		status = pj_activesock_create(pool, udp_sock->sock, pj_SOCK_DGRAM(), &asock_cfg,
			get_p2p_global()->ioqueue, &udp_callback, udp_sock, &udp_sock->activesock);
		if (status != PJ_SUCCESS) 
			break;

		readbuf[0] = udp_sock->read_buffer + sizeof(p2p_proxy_header);
		status = pj_activesock_start_recvfrom2(udp_sock->activesock,
			udp_sock->pool, 
			PROXY_SOCK_PACKAGE_SIZE,
			readbuf, 0);
		if (status != PJ_SUCCESS) 
			break;

		pj_timer_heap_schedule_w_grp_lock(get_p2p_global()->timer_heap, &udp_sock->idea_timer,
			&delay, P2P_DISCONNECT_CONNECTION,
			udp_sock->grp_lock);

		pj_strdup_with_null(pool, &udp_sock->proxy_addr, &proxy->proxy_addr);
		sign_str = pj_strstr(&udp_sock->proxy_addr, &sign);
		if(sign_str)
		{
			udp_sock->proxy_port = (pj_uint16_t)atoi(sign_str+1);
			udp_sock->proxy_addr.slen = sign_str-udp_sock->proxy_addr.ptr;
			sign_str[0] = '\0';
		}

		add_sock_to_udp_connect_proxy(proxy, udp_sock);

		PJ_LOG(4,("p2p_udp_c_p", "p2p_udp_connect_sock_create %p %d", udp_sock, udp_sock->sock_id));
		
		return udp_sock;
	}while(0);

	if(udp_sock)
	{
		p2p_udp_connect_sock_destroy(udp_sock);
	}
	return NULL;
}


PJ_DECL(pj_status_t) init_p2p_udp_connect_proxy(p2p_udp_connect_proxy* proxy,
												pj_str_t* proxy_addr,
												pj_pool_t *pool,
												p2p_udp_connect_proxy_cb* cb,
												void* user_data)
{
	PJ_LOG(4,("p2p_udp_c_p", "init_p2p_udp_connect_proxy %p", proxy));
	proxy->pool = pool;
	pj_memcpy(&proxy->cb , cb, sizeof(p2p_udp_connect_proxy_cb));
	proxy->user_data = user_data;
	proxy->pause_send = PJ_FALSE;
	proxy->udp_sock_proxys = pj_hash_create(proxy->pool, SOCK_HASH_TABLE_SIZE);
	pj_strdup_with_null(pool, &proxy->proxy_addr, proxy_addr);
	pj_mutex_create_recursive(proxy->pool, NULL, &proxy->sock_mutex);
	return PJ_SUCCESS;
}

PJ_DECL(void) uninit_p2p_udp_connect_proxy(p2p_udp_connect_proxy* proxy)
{
	p2p_udp_connect_sock **udp_sock = NULL;
	unsigned sock_count = 0;
	pj_hash_iterator_t itbuf, *it;
	unsigned i;

	PJ_LOG(4,("p2p_udp_c_p", "uninit_p2p_udp_connect_proxy %p", proxy));

	pj_mutex_lock(proxy->sock_mutex);
	//prevent deadlock, get items in hash table, then clean hash table
	sock_count = pj_hash_count(proxy->udp_sock_proxys);
	if(sock_count)
	{
		p2p_udp_connect_sock** sock;
		udp_sock = sock = (p2p_udp_connect_sock**)p2p_malloc(sizeof(p2p_udp_connect_sock*)*sock_count);
		it = pj_hash_first(proxy->udp_sock_proxys, &itbuf);
		while (it) 
		{
			*sock = (p2p_udp_connect_sock*) pj_hash_this(proxy->udp_sock_proxys, it);
			//use pj_hash_set NULL, remove from hash table
			pj_hash_set(NULL, proxy->udp_sock_proxys, &(*sock)->sock_id, sizeof(pj_uint16_t), (*sock)->hash_value, NULL);
			it = pj_hash_first(proxy->udp_sock_proxys, &itbuf);
			sock++;
		}
	}
	pj_mutex_unlock(proxy->sock_mutex);

	for(i=0; i<sock_count; i++)
	{
		p2p_udp_connect_sock_destroy(udp_sock[i]);
	}
	if(udp_sock)
		p2p_free(udp_sock);

	pj_mutex_destroy(proxy->sock_mutex);
	PJ_LOG(4,("p2p_udp_c_p", "uninit_p2p_udp_connect_proxy %p end", proxy));
}

PJ_DECL(void) p2p_udp_connect_recved_data(p2p_udp_connect_proxy* proxy, p2p_proxy_header* udp_data)
{
	pj_uint32_t hval=0;
	p2p_udp_connect_sock *udp_sock = NULL;

	pj_mutex_lock(proxy->sock_mutex);
	udp_sock = pj_hash_get(proxy->udp_sock_proxys, &udp_data->sock_id, sizeof(pj_uint16_t), &hval) ;
	if(udp_sock)
		pj_grp_lock_add_ref(udp_sock->grp_lock);
	pj_mutex_unlock(proxy->sock_mutex);

	if(udp_sock)
	{
		p2p_udp_connect_sock_send(udp_sock, udp_data);
		pj_grp_lock_dec_ref(udp_sock->grp_lock);
	}
	else
	{
		PJ_LOG(4,("p2p_udp_c_p", "p2p_udp_connect_recved_data p2p_udp_connect_sock_create %p, sock_id %d", proxy, udp_data->sock_id));

		udp_sock = p2p_udp_connect_sock_create(proxy, udp_data);
		if(udp_sock)
			p2p_udp_connect_sock_send(udp_sock, udp_data);
	}
}

PJ_DECL(void) udp_connect_proxy_pause_send(p2p_udp_connect_proxy* proxy, pj_bool_t pause)
{
	proxy->pause_send = pause;
}

#endif
