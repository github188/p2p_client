#ifndef __PJNATH_P2P_UDP_PROXY_H__
#define __PJNATH_P2P_UDP_PROXY_H__

//#define USE_UDP_PROXY 1

#ifdef USE_UDP_PROXY

#include "p2p_tcp_proxy.h"

PJ_BEGIN_DECL

typedef struct p2p_udp_listen_proxy p2p_udp_listen_proxy;
typedef struct p2p_udp_connect_proxy p2p_udp_connect_proxy;

typedef struct p2p_udp_listen_proxy_cb
{
	pj_status_t (*send_udp_data)(p2p_udp_listen_proxy* listen_proxy,
		const char* buffer,
		size_t buffer_len);


	void (*on_idea_timeout)(p2p_udp_listen_proxy* listen_proxy);
}p2p_udp_listen_proxy_cb;

struct p2p_udp_listen_proxy
{
	pj_uint16_t proxy_port; /*local udp port*/
	pj_uint16_t remote_udp_port; /*remote udp port*/
	pj_uint32_t hash_value; /*cache hash value for proxy_port, only calculate once*/

	pj_grp_lock_t  *grp_lock;  /**< Group lock.*/
	pj_pool_t		*pool;	/**< Pool used by this object.	*/

	pj_activesock_t* udp_activesock;
	pj_sock_t		 udp_sock_fd;	/* Socket descriptor*/

	pj_bool_t		 destroy_req;//To prevent duplicate destroy

	void* user_data;
	p2p_udp_listen_proxy_cb cb;

	char read_buffer[sizeof(p2p_proxy_header) + PROXY_SOCK_PACKAGE_SIZE];/*receive data buffer*/

	pj_bool_t pause_send;
	pj_ioqueue_op_key_t send_key;

	pj_timer_entry idea_timer;	//idea P2P_UDP_CONNECT_SOCK_TIMEOUT second no receive and send data, destroy it 
	pj_time_val live_time; //last send or receive time
};

typedef struct p2p_udp_connect_proxy_cb
{
	pj_status_t (*send_udp_data)(p2p_udp_connect_proxy* proxy,
		const char* buffer,
		size_t buffer_len);

	void (*add_ref)(p2p_udp_connect_proxy* proxy);

	void (*release_ref)(p2p_udp_connect_proxy* proxy);
}p2p_udp_connect_proxy_cb;

struct p2p_udp_connect_proxy
{
	pj_str_t proxy_addr;
	pj_mutex_t *sock_mutex;  /**< sock mutex.*/
	pj_pool_t *pool;	/**< Pool used by this object.	*/
	void* user_data;
	pj_hash_table_t* udp_sock_proxys;
	p2p_udp_connect_proxy_cb cb;
	pj_bool_t pause_send;
};

PJ_DECL(pj_status_t) create_p2p_udp_listen_proxy(pj_uint16_t remote_listen_port, 
												 p2p_udp_listen_proxy_cb* cb,
												 void* user_data,
												 p2p_udp_listen_proxy** proxy);
PJ_DECL(void) destroy_p2p_udp_listen_proxy(p2p_udp_listen_proxy* proxy);
PJ_DECL(void) p2p_udp_listen_recved_data(p2p_udp_listen_proxy* proxy, p2p_proxy_header* udp_data);
PJ_DECL(void) udp_listen_proxy_pause_send(p2p_udp_listen_proxy* proxy, pj_bool_t pause);
PJ_DECL(void) p2p_udp_listen_proxy_idea_timer(p2p_udp_listen_proxy* proxy);


PJ_DECL(pj_status_t) init_p2p_udp_connect_proxy(p2p_udp_connect_proxy* proxy,
												pj_str_t* proxy_addr,
												pj_pool_t *pool,
												p2p_udp_connect_proxy_cb* cb,
												void* user_data);
PJ_DECL(void) uninit_p2p_udp_connect_proxy(p2p_udp_connect_proxy* proxy);
PJ_DECL(void) p2p_udp_connect_recved_data(p2p_udp_connect_proxy* proxy, p2p_proxy_header* udp_data);
PJ_DECL(void) udp_connect_proxy_pause_send(p2p_udp_connect_proxy* proxy, pj_bool_t pause);

PJ_END_DECL

#endif

#endif	/* __PJNATH_P2P_UDP_PROXY_H__ */