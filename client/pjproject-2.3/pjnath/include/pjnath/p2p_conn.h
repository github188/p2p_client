#ifndef __PJNATH_P2P_CONN_H__
#define __PJNATH_P2P_CONN_H__

#include <pjnath/p2p_udt.h>
#include <pjnath/p2p_tcp_proxy.h>
#include <pjnath/p2p_smooth.h>
#include <pjnath/p2p_global.h>
#include <pjnath/p2p_port_guess.h>
#include <pjnath/p2p_udp_proxy.h>

PJ_BEGIN_DECL

typedef struct p2p_disconnection_id
{
	PJ_DECL_LIST_MEMBER(struct p2p_disconnection_id);
	pj_int32_t conn_id;
}p2p_disconnection_id;

struct p2p_transport
{
	char		    *obj_name;	/**< Log ID.			*/
	pj_pool_t		*pool;	/**< Pool used by this object.	*/
	void		    *user_data;	/**< Application data.		*/
	pj_grp_lock_t	*grp_lock;  /**< Group lock.		*/
	pj_ice_strans	*assist_icest; /*assist ice transport for send or receive request of connect remote user*/
	p2p_transport_cb *cb; /*call back for application*/

	pj_bool_t		 destroy_req;//To prevent duplicate destroy

	pj_bool_t		 connected;//connect server is successful,if reconnect,do not call back to user

	pj_bool_t first_assist_complete;//first call back cb_on_ice_assist_complete

	pj_hash_table_t* conn_hash_table;  /*p2p connection hash table, hash key is connection id*/

	pj_hash_table_t* udt_conn_hash_table; /*p2p connection hash table, hash key is remote addr*/

	pj_timer_entry timer; /*asynchronous disconnect p2p connection timer*/
	p2p_disconnection_id disconnect_conns; /*asynchronous disconnect p2p connection id list */

	p2p_udt_listener *udt_listener; /*udt listener in device*/

	pj_str_t proxy_addr; /*tcp proxy address*/

	pj_event_t* destroy_event; /*asynchronous p2p_transport destroy event*/

	pj_bool_t destroy_in_net_thread; /*p2p_transport destroy in io thread*/

	pj_bool_t delay_destroy; /*if conn_hash_table is not empty, delay destroy p2p_transport*/
};

#define UDT_STATUS_NONE (0)
#define UDT_STATUS_CONNECTED (1)
#define UDT_STATUS_DISCONNECT (2)

//p2p connection
typedef struct pj_ice_strans_p2p_conn
{
	int magic;/*magic data, must is P2P_DATA_MAGIC*/
	p2p_transport*	transport;
	pj_int32_t      conn_id; /*connection unique id */
	pj_ice_strans  *icest; /*transfer data ice transport*/
	pj_uint32_t     hash_value; /*cache hash value for connection id, only calculate once*/
	pj_bool_t       is_initiative;

	pj_grp_lock_t  *grp_lock;  /**< Group lock.*/
	pj_pool_t		*pool;	/**< Pool used by this object.	*/

	pj_hash_table_t* tcp_listen_proxys;  /*p2p connection hash table*/

	pj_bool_t		 destroy_req;//To prevent duplicate destroy

	char recved_buffer[sizeof(p2p_proxy_header) + PROXY_SOCK_PACKAGE_SIZE];/*remote tcp data buffer*/
	size_t recved_buffer_len;

	pj_sockaddr remote_addr; //p2p remote address
	int remote_addr_type;
	pj_sockaddr local_addr; //p2p local address
	int local_addr_type;

	//tcp proxy in device
	p2p_tcp_connect_proxy tcp_connect_proxy;

	pj_mutex_t *send_mutex;  /**< send mutex.*/
	pj_mutex_t *receive_mutex;  /**< receive mutex.*/

	union
	{
		p2p_udt_connector* p2p_udt_connector; /*udt object in mobile phone*/
		p2p_udt_accepter* p2p_udt_accepter;/*udt object in device*/
	}udt;

	pj_bool_t recved_first_data; /*receive first data flag*/

	pj_str_t user; /*local user, device id or rand guid*/

	pj_str_t remote_user; /*remote user*/
	pj_int32_t remote_conn_id;/*remote pj_ice_strans_p2p_conn.conn_id*/

	int recv_buf_size;
	int send_buf_size;

	pj_time_val connect_begin_time; //time for call p2p_transport_connect
	pj_sockaddr remote_internet_addr; //remote internet address
	pj_sockaddr local_internet_addr; //local internet address

	//P2P_COMMAND_USER_DATA command cache
	//if receive data is not full command, do not callback user 
	//if receive a full command, callback to user
	char* user_data_buf;
	unsigned int user_data_len; //command cache length
	unsigned int user_data_capacity; //command cache capacity
	pj_uint16_t user_data_pkg_seq; //current command sequence number

	pj_int32_t conn_flag; //to see conn_flag in on_connection_disconnect

	/*smooth receive data*/
	p2p_smooth* smooth;

#ifdef USE_P2P_PORT_GUESS
	p2p_port_guess* port_guess;
#endif

	/*when playback history file, call p2p_conn_set_opt P2P_PAUSE_RECV
	user data do not callback,to see function p2p_conn_callback_recv
	*/
	pj_uint8_t is_pause_recv;
	p2p_tcp_data* pause_recv_first;
	p2p_tcp_data* pause_recv_last;
	pj_uint32_t pause_recv_count;

	//default PJ_FALSE, p2p_transport_disconnect set disconnect_req = PJ_TRUE
	pj_bool_t disconnect_req;

	//
	pj_uint8_t udt_status;

#ifdef USE_UDP_PROXY
	pj_hash_table_t* udp_listen_proxys;  /*udp connection hash table*/
	//udp proxy in device
	p2p_udp_connect_proxy udp_connect_proxy;
#endif

	//device receive PJ_STUN_P2P_CONNNECT_REQUEST, but no receive PJ_STUN_P2P_EXCHANGE_INFO_REQUEST
	//the connection can not be destroy,so add timer to destroy the connection
	pj_timer_entry destroy_timer;

}pj_ice_strans_p2p_conn;

pj_ice_strans_p2p_conn* create_p2p_conn(pj_str_t* proxy_add, pj_bool_t is_initiativer);
void destroy_p2p_conn(pj_ice_strans_p2p_conn* conn);
pj_status_t p2p_ice_send_data(void* user_data, const pj_sockaddr_t* addr, const char* buffer, size_t buffer_len, pj_uint8_t force_relay);
void on_p2p_conn_recved_data(void* user_data, const char* receive_buffer, size_t buffer_len);
void p2p_conn_get_peer_addr(pj_sockaddr_t* addr, void* user_data);
void p2p_conn_get_sock_addr(pj_sockaddr_t* addr, void* user_data);
pj_bool_t p2p_conn_is_valid(pj_ice_strans_p2p_conn* conn);
void p2p_conn_pause_send(void* user_data, pj_bool_t pause);

void on_p2p_conn_recved_noresend_data(void* user_data, const char* receive_buffer, size_t buffer_len);

pj_status_t p2p_conn_set_opt(pj_ice_strans_p2p_conn* conn, p2p_opt opt, const void* optval, int optlen);

pj_status_t p2p_conn_proxy_get_remote_addr(pj_ice_strans_p2p_conn* conn, unsigned short port, char* addr, int* add_len);

void p2p_conn_wakeup_block_send(pj_ice_strans_p2p_conn* conn);

//udt notify p2p connection closed
void p2p_conn_udt_on_close(void* user_data);

//report session information to p2p server
#define P2P_SESSION_OK 1
#define P2P_FAILED_EXCHANGE_INFO -2 //failed to exchange ip information
#define P2P_DATA_CONNECT_SERVER -3 //data connection failed to connect server
#define P2P_CREATE_ICE_ERROR -4 //failed to create ice object
#define P2P_CREATE_UDT_ERROR -5 //failed to create udt object
#define P2P_FAILED_NEGO -6 //failed to p2p negotiate
#define P2P_CONNECT_USER -7 //failed to connect remote user
void p2p_report_session_info(pj_ice_strans_p2p_conn* conn, int result, unsigned char port_guess_ok);

PJ_END_DECL
#endif