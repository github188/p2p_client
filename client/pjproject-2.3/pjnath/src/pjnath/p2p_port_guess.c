#include <pjnath/errno.h>
#include <pjnath/p2p_global.h>
#include <pjnath/p2p_port_guess.h>
#include <pjnath.h>
#include <pjnath/p2p_conn.h>

#ifdef USE_P2P_PORT_GUESS

#ifdef WIN32
#include <WinSock2.h>
#include <ws2tcpip.h>
#else
#include <sys/time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#endif

#ifdef USE_P2P_TCP

#define TRY_TIMES_PER_TIMER (36)
#define RAND_COUNT_PER_TIMER (10)

#define PORT_COUNT_PER_TIMER (12)

#define ORDER_COUNT_PER_TIMER ((PORT_COUNT_PER_TIMER-RAND_COUNT_PER_TIMER)/2)

#define GUESS_TIMER_SPAN (50)
#define GUESS_TIMES_LIMITS (200)

#define GUESS_MAX_RESPONSE_TIME (8)

#define GUESS_MIN_TTL (4)
#define GUESS_MAX_TTL (64)

//set port used, GUESS_MIN_PORT ~ GUESS_MAX_PORT, a port per bit
PJ_INLINE(void) p2p_guess_set_port_bit(p2p_port_guess* guess, pj_uint16_t port)
{
	int byte_index = ((port-GUESS_MIN_PORT) >> 3);
	int bit_index = port - (byte_index<<3) - GUESS_MIN_PORT;
	guess->port_bits[byte_index] |= (1<<bit_index);
}
//get port is used,GUESS_MIN_PORT ~ GUESS_MAX_PORT, a port per bit
PJ_INLINE(pj_bool_t) p2p_guess_get_port_bit(p2p_port_guess* guess, pj_uint16_t port)
{
	int byte_index = ((port-GUESS_MIN_PORT) >> 3);
	int bit_index = port - (byte_index<<3) - GUESS_MIN_PORT;
	return guess->port_bits[byte_index] & (1<<bit_index);
}

//send port guess request
static void p2p_guess_sendto_request(p2p_port_guess* guess, pj_uint16_t* ports, pj_uint16_t ports_count)
{
	pj_uint16_t i;
	pj_sockaddr remote_addr;

	pj_sockaddr_cp(&remote_addr, &guess->remote_addr);

	for(i=0; i<ports_count; i++)
	{
		int sended;
		pj_sockaddr_set_port(&remote_addr, ports[i]);

		p2p_guess_set_port_bit(guess,  ports[i]);

		guess->guess_data.port = pj_htons(ports[i]);

		//PJ_LOG(4,("p2p_guess", "p2p_guess_ports_send port %d", ports[i]));

		sended = sendto(guess->sock,
			(const char*)&guess->guess_data,
			sizeof(p2p_port_guess_data), 
			0,
			(const struct sockaddr*)&remote_addr,
			pj_sockaddr_get_len(&remote_addr));
		if(sended != sizeof(p2p_port_guess_data))
		{
			int err = pj_get_native_netos_error();
			PJ_LOG(2,("p2p_guess", "p2p_guess_ports_send sendto return %d error%d", sended, err));
		}
	}
}
//generate rand ports
static pj_uint16_t p2p_guess_rand_ports(p2p_port_guess* guess, pj_uint16_t* ports, pj_uint16_t ports_count )
{
	pj_uint16_t n=0;
	int i;
	for(i=0; i<TRY_TIMES_PER_TIMER; i++)
	{
		pj_uint16_t port = (pj_uint16_t)(pj_rand() % (GUESS_MAX_PORT-GUESS_MIN_PORT) + GUESS_MIN_PORT);

		if(p2p_guess_get_port_bit(guess, port))
			continue;

		ports[n++] = port;
		if(n == ports_count)
			break;
	}

	return n;
}

//generate  order ports
static pj_uint16_t p2p_guess_order_ports(p2p_port_guess* guess, pj_uint16_t* ports, pj_uint16_t ports_count )
{
	pj_uint16_t low=0;
	pj_uint16_t high=0;
	int i;

	for(i=0; i<TRY_TIMES_PER_TIMER; i++)
	{
		guess->port_low--;
		if(guess->port_low < GUESS_MIN_PORT)
			guess->port_low = GUESS_MAX_PORT;
		if(p2p_guess_get_port_bit(guess, guess->port_low))
			continue;

		ports[low++] = guess->port_low;
		if(low == ports_count)
			break;
	}

	for(i=0; i<TRY_TIMES_PER_TIMER; i++)
	{
		guess->port_high++;
		if(guess->port_high < GUESS_MIN_PORT)
			guess->port_high += GUESS_MIN_PORT;
		if(p2p_guess_get_port_bit(guess, guess->port_high))
			continue;

		ports[low+high] = guess->port_high;
		high++;
		if(high == ports_count)
			break;
	}

	return low+high;
}

/*
check port guess data(type, conn_id, length) is valid
*/
static pj_bool_t p2p_port_gess_data_valid(struct p2p_port_guess* guess,
										  unsigned char* buf, 
										  pj_size_t len)
{
	p2p_port_guess_data* data = (p2p_port_guess_data*)buf;
	pj_int32_t conn_id;

	if(len != sizeof(p2p_port_guess_data))
		return PJ_FALSE;

	if(buf[0] != P2P_GUESS_REQUEST
		&& buf[0] != P2P_GUESS_RESPONSE)
		return PJ_FALSE;

	if(guess->p2p_conn->is_initiative)
		conn_id = guess->p2p_conn->conn_id;
	else
		conn_id = guess->p2p_conn->remote_conn_id;

	data->conn_id = pj_ntohl(data->conn_id);
	if(data->conn_id != conn_id)
		return PJ_FALSE;

	PJ_LOG(3,("p2p_guess", "guess port %d", pj_ntohs(data->port)));

	return PJ_TRUE;
}

static void on_io_thead_recvfrom2(void* data)
{
	p2p_port_guess* guess = data;

	if(guess->valid_holes != -1 && guess->holes_activesock[guess->valid_holes])
	{
		void *readbuf[1];

		PJ_LOG(3,("p2p_guess", "on_io_thead_recvfrom2 call pj_activesock_post_read"));

		guess->valid_hole_recv_buf = pj_pool_zalloc(guess->pool, P2P_TCP_MSS);
		readbuf[0] = guess->valid_hole_recv_buf;
		pj_activesock_post_read(guess->holes_activesock[guess->valid_holes], 
			P2P_TCP_MSS,
			readbuf, 
			0);
	}


	pj_grp_lock_dec_ref(guess->grp_lock); //decrease reference, add in on_holes_recv_request
}


static void on_holes_recv_request(p2p_port_guess* guess, 
								  pj_activesock_t *asock,
								  void *data,
								  pj_size_t size,
								  const pj_sockaddr_t *src_addr,
								  int addr_len)
{
	int i;

	if(!p2p_port_gess_data_valid(guess, data, size))
		return ;

	//keep valid hole, close others udp socket holes
	for(i=0; i<guess->total_holes; i++)
	{
		if(guess->holes_activesock[i] == asock)
		{
			p2p_socket_pair_item item;

			//restore the ttl
			int ttl = GUESS_MAX_TTL;
			setsockopt(guess->holes[i], IPPROTO_IP, IP_TTL, (const char*)&ttl, sizeof(ttl));

			guess->valid_holes = i;

			//old hole receive buffer size is sizeof(p2p_port_guess_data)
			//so must reset buffer and size
			item.cb = on_io_thead_recvfrom2;
			item.data = guess;
			pj_grp_lock_add_ref(guess->grp_lock); //add reference, release in on_io_thead_recvfrom2
			schedule_socket_pair(get_p2p_global()->sock_pair, &item);

			break;
		}
		else
		{
			pj_activesock_close(guess->holes_activesock[i]);
			guess->holes_activesock[i] = NULL;
			guess->holes[i] = 0;
		}
	}

	if(guess->valid_holes == -1)
		return ;

	p2p_port_guess_on_request(guess, data, size, src_addr, addr_len);
}

/*
hole socket receive callback 
*/
static pj_bool_t on_holes_data_read(pj_activesock_t *asock,
									void *data,
									pj_size_t size,
									const pj_sockaddr_t *src_addr,
									int addr_len,
									pj_status_t status)
{
	p2p_port_guess* guess  = (p2p_port_guess*) pj_activesock_get_user_data(asock);

	unsigned char* buf = (unsigned char*)data;

	if (guess == NULL || status != PJ_SUCCESS) 
		return PJ_TRUE;

	if(buf[0] == P2P_GUESS_RESPONSE)
	{
		PJ_LOG(3,("p2p_guess", "on_holes_data_read status %d, type %d", status, buf[0]));

		if(!p2p_port_gess_data_valid(guess, data, size))
			return PJ_TRUE;
		p2p_port_guess_on_response(guess, data, size, src_addr, addr_len);
	}
	else if(buf[0] == P2P_GUESS_REQUEST)
	{
		PJ_LOG(3,("p2p_guess", "on_holes_data_read status %d, type %d, response_times %d", status, buf[0], guess->response_times));
		
		if(guess->response_times >= 1)
			return PJ_TRUE;

		on_holes_recv_request(guess, asock, data, size, src_addr, addr_len);

		//do not post read, in on_io_thead_recvfrom2 will post
		return PJ_FALSE; 
	}
	else //p2p tcp data
	{
		if(guess->p2p_conn->is_initiative)
		{
			if(guess->p2p_conn->udt.p2p_udt_connector)
				p2p_udt_connector_on_recved(guess->p2p_conn->udt.p2p_udt_connector, data, size, src_addr, addr_len);
		}
		else
		{
			if(guess->p2p_conn->udt.p2p_udt_accepter)
				p2p_udt_accepter_on_recved(guess->p2p_conn->udt.p2p_udt_accepter, data, size, src_addr, addr_len);
		}
	}
	return PJ_TRUE;
}

static void p2p_guess_create_holes(p2p_port_guess* guess, int count)
{
	int i;
	char dummy = '\0';
	int sended = 0;
	pj_status_t status;

	for(i=guess->total_holes; i<PORT_GUESS_HOLE_TOTAL_COUNT && count; i++)
	{
		pj_activesock_cfg asock_cfg;
		pj_activesock_cb activesock_cb;
		void *readbuf[1];

		int ttl = GUESS_MIN_TTL;

		count--;

		// let OS choose available ports
		status = pj_sock_socket(pj_AF_INET(), pj_SOCK_DGRAM(),	0, &guess->holes[i]);
		if (status != PJ_SUCCESS)
		{
			PJ_LOG(2,("p2p_guess", "p2p_guess_create_holes failed to pj_sock_socket ,status %d", status));
			break;
		}

		//make sure this packet can not reach the peer but through the NAT 
		setsockopt(guess->holes[i], IPPROTO_IP, IP_TTL, (const char*)&ttl, sizeof(ttl));

		pj_activesock_cfg_default(&asock_cfg);
		asock_cfg.grp_lock = guess->grp_lock;

		pj_bzero(&activesock_cb, sizeof(activesock_cb));
		activesock_cb.on_data_recvfrom = &on_holes_data_read;

		status = pj_activesock_create(guess->pool, guess->holes[i], pj_SOCK_DGRAM(), &asock_cfg,	get_p2p_global()->ioqueue, &activesock_cb, guess, &guess->holes_activesock[i]);
		if (status != PJ_SUCCESS) 
		{
			pj_sock_close(guess->holes[i]);
			guess->holes[i] = 0;
			PJ_LOG(2,("p2p_guess", "p2p_guess_create_holes failed to pj_activesock_create,status %d", status));
			break;
		}


		sended = sendto(guess->holes[i], &dummy, 1, 0,
			(const struct sockaddr*)&guess->remote_addr,
			pj_sockaddr_get_len(&guess->remote_addr));
		if(sended != 1)
		{
			int err = pj_get_native_netos_error();

			pj_activesock_close(guess->holes_activesock[i]);
			guess->holes_activesock[i] = NULL;
			guess->holes[i] = 0;
			
			PJ_LOG(2,("p2p_guess", "p2p_guess_create_holes failed to sendto,error %d", err));
			break;
		}

		readbuf[0] = &(guess->holes_recv_data[i]);
		status = pj_activesock_start_recvfrom2(guess->holes_activesock[i], 
			guess->pool, 
			sizeof(p2p_port_guess_data),
			readbuf, 0);
		if (status != PJ_SUCCESS && status != PJ_EPENDING)
		{
			PJ_LOG(2,("p2p_guess", "p2p_guess_create_holes failed to pj_activesock_start_recvfrom2,status %d", status));

			pj_activesock_close(guess->holes_activesock[i]);
			guess->holes_activesock[i] = NULL;
			guess->holes[i] = 0;
			break;
		}

		guess->total_holes++;
	}
}

static void p2p_guess_send_probe(p2p_port_guess* guess)
{
	pj_uint16_t ports[PORT_COUNT_PER_TIMER];
	pj_uint16_t ports_count;

	if(guess->p2p_conn->is_initiative)
	{		
		if(guess->guess_times%100 == 0)
		{
			int i;
			char dummy = '\0';
			int sended = 0;

			//only send holes dummy, remote udp package can be received
			for(i=0; i<guess->total_holes; i++)
			{
				if(guess->holes[i] && guess->holes_activesock[i])
				{
					sended = sendto(guess->holes[i], &dummy, 1, 0,
						(const struct sockaddr*)&guess->remote_addr,
						pj_sockaddr_get_len(&guess->remote_addr));
					if(sended != 1)
						break;
				}
			}

			ports[0] = guess->port_peer;
			p2p_guess_sendto_request(guess, ports, 1);
		}
	}
	else
	{
		/*rand port*/
		ports_count = p2p_guess_rand_ports(guess, ports, RAND_COUNT_PER_TIMER);
		p2p_guess_sendto_request(guess, ports, ports_count);

		/*order port*/
		ports_count = p2p_guess_order_ports(guess, ports, ORDER_COUNT_PER_TIMER);
		p2p_guess_sendto_request(guess, ports, ports_count);
	}	

	guess->guess_times++;	
}

/*
send port guess response and request to peer
if has a valid hole,use hole socket send data,else use stun socket send data
*/
static void p2p_port_guess_response(struct p2p_port_guess* guess)
{
	int sended;
	p2p_port_guess_data response;
	char addr_info[PJ_INET6_ADDRSTRLEN+10];
	pj_sock_t sock = guess->sock;

	if(guess->valid_holes != -1)
		sock = guess->holes[guess->valid_holes];

	pj_sockaddr_print(&guess->response_addr, addr_info, sizeof(addr_info), 3);
	PJ_LOG(3, ("p2p_guess", "p2p_port_guess_response %d,address %s", guess->response_times, addr_info));

	if(guess->p2p_conn->is_initiative)
		response.conn_id = pj_htonl(guess->p2p_conn->conn_id);
	else
		response.conn_id = pj_htonl(guess->p2p_conn->remote_conn_id);

	response.port = pj_htons(pj_sockaddr_get_port(&guess->response_addr));

	response.padding = 0;

	response.type = P2P_GUESS_RESPONSE;
	sended = sendto(sock,
		(const char*)&response,
		sizeof(p2p_port_guess_data), 
		0,
		(const struct sockaddr*)&guess->response_addr,
		pj_sockaddr_get_len(&guess->response_addr));
	if(sended != sizeof(p2p_port_guess_data))
	{
		int err = pj_get_native_netos_error();
		PJ_LOG(2,("p2p_guess", "p2p_port_guess_response sendto return %d error%d", sended, err));
	}

	response.type = P2P_GUESS_REQUEST;
	sended = sendto(sock,
		(const char*)&response,
		sizeof(p2p_port_guess_data), 
		0,
		(const struct sockaddr*)&guess->response_addr,
		pj_sockaddr_get_len(&guess->response_addr));
	if(sended != sizeof(p2p_port_guess_data))
	{
		int err = pj_get_native_netos_error();
		PJ_LOG(2,("p2p_guess", "p2p_port_guess_response sendto return %d error%d", sended, err));
	}
	guess->response_times++;
}

static void p2p_guess_close_all_holes(p2p_port_guess* guess)
{
	if(guess->p2p_conn->is_initiative)
	{
		int i;
		for(i=0; i<guess->total_holes; i++)
		{
			if(guess->holes_activesock[i])
			{
				pj_activesock_close(guess->holes_activesock[i]);
				guess->holes_activesock[i] = NULL;
				guess->holes[i] = 0;
			}
		}
	}	
}

static void p2p_guess_timer(pj_timer_heap_t *th, pj_timer_entry *e)
{
	p2p_port_guess* guess = (p2p_port_guess*) e->user_data;

	PJ_UNUSED_ARG(th);

	//received peer request,do not send probe, send response
	if(guess->response_times >= 1) 
	{
		p2p_port_guess_response(guess);
		if(guess->response_times < GUESS_MAX_RESPONSE_TIME)
		{
			pj_time_val t;
			t.sec = 0;
			t.msec = GUESS_TIMER_SPAN*2;
			pj_timer_heap_schedule_w_grp_lock(get_p2p_global()->timer_heap, 
				&guess->timer,
				&t, 
				0, 
				NULL);
		}
	}
	else
	{	
		if(guess->guess_times < GUESS_TIMES_LIMITS)
		{
			pj_time_val t;
			t.sec = 0;
			t.msec = GUESS_TIMER_SPAN;
			pj_timer_heap_schedule_w_grp_lock(get_p2p_global()->timer_heap, 
				&guess->timer,
				&t, 
				0, 
				NULL);
			p2p_guess_send_probe(guess);

			//add PORT_GUESS_HOLE_ADD_COUNT holes per once timer
			if(guess->p2p_conn->is_initiative)
				p2p_guess_create_holes(guess, PORT_GUESS_HOLE_ADD_COUNT);
		}
		else //failed to probe,close all holes socket
		{
			PJ_LOG(3, ("p2p_guess", "p2p_guess_timer failed to probe,close all holes socket"));

			p2p_report_session_info(guess->p2p_conn, P2P_SESSION_OK, 0);

			p2p_guess_close_all_holes(guess);
		}
	}
}

/*
create udp socket holes, send dummy for receive peer guess request
*/
static void p2p_port_guess_holes(struct p2p_port_guess* guess)
{
	PJ_LOG(4,("p2p_guess", "p2p_port_guess_holes begin"));

	guess->valid_holes = -1;
	guess->total_holes = 0;

	if(!guess->p2p_conn->is_initiative)
	{
		return;
	}

	p2p_guess_create_holes(guess, PORT_GUESS_HOLE_INIT_COUNT);

	PJ_LOG(4,("p2p_guess", "p2p_port_guess_holes end"));
}

static void p2p_port_guess_on_destroy(void *comp)
{
	p2p_port_guess* guess = (p2p_port_guess*) comp;
	PJ_LOG(4,("p2p_guess", "p2p_port_guess_on_destroy %p", guess));
	/* Destroy pool */
	if (guess->pool) {
		pj_pool_t *pool = guess->pool;
		guess->pool = NULL;
		delay_destroy_pool(pool);
	}
}

struct p2p_port_guess* p2p_create_port_guess(pj_sock_t sock, 
	pj_sockaddr* remote_addr,
	struct pj_ice_strans_p2p_conn* p2p_conn)
{
	pj_pool_t *pool = NULL;
	p2p_port_guess* guess = NULL;
	char addr_info[PJ_INET6_ADDRSTRLEN+10];
	pj_time_val t;
	pj_sockaddr local_addr;
	int addr_len;
	char local_addr_info[PJ_INET6_ADDRSTRLEN+10];
	pj_status_t status;

	PJ_LOG(4,("p2p_guess", "p2p_create_port_guess begin"));

	pool = pj_pool_create(&get_p2p_global()->caching_pool.factory, 
		"p2p_tcp%p", 
		sizeof(p2p_port_guess),
		P2P_TCP_MSS, 
		NULL);
	guess = PJ_POOL_ZALLOC_T(pool, p2p_port_guess);
	guess->pool = pool;
	guess->sock = sock;
	pj_sockaddr_cp(&guess->remote_addr, remote_addr);

	status = pj_grp_lock_create(guess->pool, NULL, &guess->grp_lock);
	if (status != PJ_SUCCESS) 
	{
		delay_destroy_pool(guess->pool);
		return NULL;	
	}

	guess->guess_data.type = P2P_GUESS_REQUEST;
	if(p2p_conn->is_initiative)
		guess->guess_data.conn_id = pj_htonl(p2p_conn->conn_id);
	else
		guess->guess_data.conn_id = pj_htonl(p2p_conn->remote_conn_id);

	guess->guess_data.padding = 0;

	guess->p2p_conn = p2p_conn;
	guess->guess_times = 0;
	guess->response_times = 0;
	guess->guess_success = 0;

	pj_timer_entry_init(&guess->timer, 0, guess, &p2p_guess_timer);

	guess->port_peer = pj_sockaddr_get_port(&guess->remote_addr);
	p2p_guess_set_port_bit(guess, guess->port_peer);

	guess->port_low = guess->port_high = guess->port_peer;

	pj_sockaddr_print(remote_addr, addr_info, sizeof(addr_info), 3);

	t.sec = 0;
	t.msec = GUESS_TIMER_SPAN;
	pj_timer_heap_schedule_w_grp_lock(get_p2p_global()->timer_heap, 
		&guess->timer,
		&t, 
		0, 
		NULL);

	p2p_guess_send_probe(guess);

	addr_len = sizeof(local_addr);
	pj_sock_getsockname(sock, &local_addr, &addr_len);
	pj_sockaddr_print(&local_addr, local_addr_info, sizeof(local_addr_info), 3);

	p2p_port_guess_holes(guess);

	pj_grp_lock_add_ref(guess->grp_lock);
	pj_grp_lock_add_handler(guess->grp_lock, pool, guess, &p2p_port_guess_on_destroy);

	PJ_LOG(4,("p2p_guess", "p2p_create_port_guess %p end, remote %s, local %s", 
		guess, addr_info, local_addr_info));
	return guess;
}

void p2p_destroy_port_guess(struct p2p_port_guess* guess)
{
	if(guess==NULL)
		return;

	PJ_LOG(4,("p2p_guess", "p2p_destroy_port_guess %p begin", guess));

	pj_timer_heap_cancel_if_active(get_p2p_global()->timer_heap, &guess->timer, 0);

	p2p_guess_close_all_holes(guess);

	//release self reference count
	pj_grp_lock_dec_ref(guess->grp_lock);

	PJ_LOG(4,("p2p_guess", "p2p_destroy_port_guess %p end", guess));
}


/*
hole socket received peer request
use hole socket send request and response to peer response address
*/
void p2p_port_guess_on_request(struct p2p_port_guess* guess,
							   unsigned char* buf, 
							   pj_size_t len,
							   const pj_sockaddr_t *src_addr,
							   unsigned src_addr_len)
{
	PJ_UNUSED_ARG(src_addr_len);
	if(!guess)
		return;

	if(guess->valid_holes == -1 && !p2p_port_gess_data_valid(guess, buf, len))
		return;

	if(guess->response_times >= 1)
		return;

	pj_sockaddr_cp(&guess->response_addr, src_addr);
	
	p2p_port_guess_response(guess);
}


void p2p_port_guess_on_response(struct p2p_port_guess* guess,
							   unsigned char* buf, 
							   pj_size_t len,
							   const pj_sockaddr_t *src_addr,
							   unsigned src_addr_len)
{
	pj_sock_t sock;
	if(!guess || guess->guess_success)
		return;

	sock = guess->sock;
	if(guess->valid_holes != -1)
		sock = guess->holes[guess->valid_holes];

	if(guess->valid_holes == -1 && !p2p_port_gess_data_valid(guess, buf, len))
		return;
	
	pj_timer_heap_cancel_if_active(get_p2p_global()->timer_heap, &guess->timer, 0);

	PJ_LOG(4,("p2p_guess", "p2p_port_guess_on_response %p OK, guess_success %d", guess, guess->guess_success));

	guess->guess_success = 1;

	//guess success, change p2p_tcp socket handle and remote address
	if(guess->p2p_conn->is_initiative)
	{
		p2p_report_session_info(guess->p2p_conn, P2P_SESSION_OK, 1);

		if(guess->p2p_conn->udt.p2p_udt_connector)
			p2p_udt_connector_guess_port(guess->p2p_conn->udt.p2p_udt_connector, 
				sock, src_addr, src_addr_len);
	}
	else
	{
		if(guess->p2p_conn->udt.p2p_udt_accepter)
			p2p_udt_accepter_guess_port(guess->p2p_conn->udt.p2p_udt_accepter, 
				sock, src_addr, src_addr_len);
	}
}
#endif //end of USE_P2P_TCP

#endif //end of USE_P2P_PORT_GUESS
