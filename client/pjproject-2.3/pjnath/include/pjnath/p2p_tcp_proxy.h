#ifndef __PJNATH_P2P_TCP_PROXY_H__
#define __PJNATH_P2P_TCP_PROXY_H__

PJ_BEGIN_DECL

#include "p2p_tcp.h"

#define COMBINE_ID(listen_port, sock_id) ((listen_port<<16) + sock_id)
#define ID_TO_LISTEN_PORT(id) ((pj_uint16_t)(id>>16))
#define ID_TO_SOCK_ID(id) ((pj_uint16_t)(id))

typedef struct p2p_tcp_listen_proxy p2p_tcp_listen_proxy;
typedef struct p2p_tcp_connect_proxy p2p_tcp_connect_proxy;

typedef struct p2p_tcp_data{	char* buffer;	size_t buffer_len;	int pos;	struct p2p_tcp_data* next;}p2p_tcp_data;
p2p_tcp_data* malloc_p2p_tcp_data(const char* buffer, size_t len);
void free_p2p_tcp_data(p2p_tcp_data* data);

enum timer_id_t
{
	P2P_TIMER_NONE,
	P2P_TIMER_DELAY_DESTROY,
	P2P_TIMER_REMOTE_CONNECTED,
	P2P_DISCONNECT_CONNECTION,
	P2P_TIMER_GLOBAL,
};

enum P2P_TCP_PAUSE_STATUS
{
	P2P_TCP_PAUSE_NONE,
	P2P_TCP_PAUSE_READY,
	P2P_TCP_PAUSE_COMPLETED,
};

typedef struct p2p_tcp_listen_proxy_cb
{
	pj_status_t (*send_tcp_data)(p2p_tcp_listen_proxy* listen_proxy,
		const char* buffer,
		size_t buffer_len);
}p2p_tcp_listen_proxy_cb;

struct p2p_tcp_listen_proxy
{
	pj_uint16_t proxy_port; /*listen port*/
	pj_uint16_t remote_listen_port;
	pj_uint32_t hash_value; /*cache hash value for proxy_port, only calculate once*/

	pj_grp_lock_t  *grp_lock;  /**< Group lock.*/
	pj_pool_t		*pool;	/**< Pool used by this object.	*/

	pj_activesock_t* listen_activesock;
	pj_sock_t		 listen_sock_fd;	/* Socket descriptor*/

	pj_bool_t		 destroy_req;//To prevent duplicate destroy

	pj_hash_table_t* tcp_sock_proxys;

	void* user_data;
	p2p_tcp_listen_proxy_cb cb;

	pj_uint16_t sock_id;
};

typedef struct p2p_tcp_connect_proxy_cb
{
	pj_status_t (*send_tcp_data)(p2p_tcp_connect_proxy* proxy,
		const char* buffer,
		size_t buffer_len);

	void (*add_ref)(p2p_tcp_connect_proxy* proxy);

	void (*release_ref)(p2p_tcp_connect_proxy* proxy);

	void (*on_tcp_connected)(p2p_tcp_connect_proxy* proxy,
		unsigned short port);
}p2p_tcp_connect_proxy_cb;

struct p2p_tcp_connect_proxy
{
	pj_str_t proxy_addr;
	pj_mutex_t *sock_mutex;  /**< sock mutex.*/
	pj_pool_t *pool;	/**< Pool used by this object.	*/
	void* user_data;
	pj_hash_table_t* tcp_sock_proxys;
	p2p_tcp_connect_proxy_cb cb;
	int data_package_size;
};

#define P2P_COMMAND_CREATE_CONNECTION 1 //request peer tcp proxy start tcp connection
#define P2P_COMMAND_DESTROY_CONNECTION 2 //notice peer tcp proxy tcp connection disconnect 
#define P2P_COMMAND_DATA 3				//tcp proxy data
#define P2P_COMMAND_REMOTE_CONNECTED 4  //peer tcp proxy tcp connected
#define P2P_COMMAND_USER_DATA 5		    //user call p2p_transport_send data
#define P2P_COMMAND_USER_AV_DATA 6      //user call p2p_transport_av_send data, flag is 1 resend
#define P2P_COMMAND_USER_AV_NORESEND 7 //user call p2p_transport_av_send data, flag is 0 no resend
#define P2P_COMMAND_UDP_DATA 8			//udp proxy data

#define PROXY_SOCK_PACKAGE_SIZE  (P2P_TCP_MAX_DATA_LEN*7) //the value affect p2p speed

#define SOCK_HASH_TABLE_SIZE 31
#define MAX_DELAY_DESTROY_TIMES 5

#pragma pack(1)
/**
 * This structure data header. All the fields are in network byte
 * order when it's on the wire.
 */
#define P2P_LAST_DATA_SEQ (0xFFFF)
typedef struct p2p_proxy_header
{
	pj_uint16_t listen_port;
	//if command is P2P_COMMAND_USER_DATA, sock_id is sequence number
	//if command is P2P_COMMAND_DATA, sock_id is p2p_tcp_sock_proxy.sock_id
	//if command is P2P_COMMAND_UDP_DATA, sock_id is udp peer port 
    pj_uint16_t sock_id; 
	pj_int16_t command;
	pj_int32_t data_length;
} p2p_proxy_header;

#pragma pack()


PJ_DECL(pj_status_t) create_p2p_tcp_listen_proxy(pj_uint16_t remote_listen_port, 
												 p2p_tcp_listen_proxy_cb* cb,
												 void* user_data,
												 p2p_tcp_listen_proxy** proxy);
PJ_DECL(void) destroy_p2p_tcp_listen_proxy(p2p_tcp_listen_proxy* proxy);
PJ_DECL(void) p2p_tcp_listen_recved_data(p2p_tcp_listen_proxy* proxy, p2p_proxy_header* tcp_data);
PJ_DECL(void) tcp_listen_proxy_pause_send(p2p_tcp_listen_proxy* proxy, pj_bool_t pause);

PJ_DECL(pj_status_t) init_p2p_tcp_connect_proxy(p2p_tcp_connect_proxy* proxy,
												pj_str_t* proxy_addr,
												pj_pool_t *pool,
												p2p_tcp_connect_proxy_cb* cb,
												void* user_data);
PJ_DECL(void) uninit_p2p_tcp_connect_proxy(p2p_tcp_connect_proxy* proxy);
PJ_DECL(void) p2p_tcp_connect_recved_data(p2p_tcp_connect_proxy* proxy, p2p_proxy_header* tcp_data);
PJ_DECL(void) tcp_connect_proxy_pause_send(p2p_tcp_connect_proxy* proxy, pj_bool_t pause, int data_package_size);

PJ_DECL(pj_bool_t) tcp_connect_proxy_find_port(p2p_tcp_connect_proxy* proxy, unsigned short port);
PJ_END_DECL

#endif	/* __PJNATH_P2P_TCP_PROXY_H__ */