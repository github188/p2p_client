#ifndef __GSS_CONN_H__
#define __GSS_CONN_H__

#ifdef __cplusplus
	extern "C" {
#endif

#include <pjnath/p2p_global.h>
#include <common/gss_protocol.h>

struct p2p_tcp_data;

enum gss_timer_id_t
{
	GSS_TIMER_NONE,
	GSS_CONN_HEART_TIMER_ID,
    GSS_RECONNECT_TIMER_ID,
};

typedef enum gss_conn_status
{
	GSS_CONN_DISCONNECT,
	GSS_CONN_CONNECTTING,
	GSS_CONN_CONNECTED
}gss_conn_status;


typedef struct gss_conn_cb
{
	void (*on_connect_result)(void *conn, void* user_data, int status);

	void (*on_disconnect)(void *conn, void* user_data, int status);

	//receive device data
	void (*on_recv)(void *conn, void *user_data, char* data, int len);

	void (*on_destroy)(void *conn, void *user_data);
}gss_conn_cb;


enum GSS_CONN_PAUSE_STATUS
{
	GSS_CONN_PAUSE_NONE,
	GSS_CONN_PAUSE_READY,
	GSS_CONN_PAUSE_COMPLETED,
};

//gss connection 
typedef struct gss_conn
{
	char		    *obj_name;	/**< Log ID.			*/

	pj_pool_t		*pool;	/**< Pool used by this object.	*/

	pj_grp_lock_t	*grp_lock;  /**< Group lock*/	

	pj_bool_t		 destroy_req;//To prevent duplicate destroy

	pj_str_t server; /*server address*/
	unsigned short port; /*server port*/

	pj_str_t uid;  /*device uid*/

	void *user_data; /*user data*/

	pj_event_t* destroy_event; /*async destroy event*/
	pj_bool_t destroy_in_net_thread; //call gss_conn_destroy in main net loop thread

	gss_conn_cb cb; /*callback functions*/

	pj_sock_t sock; /*socket handle*/
	pj_activesock_t *activesock; /*pjlib active socket*/

	pj_ioqueue_op_key_t send_key; /*pj io send key*/
	struct p2p_tcp_data* send_data_first;/*fist cache data for pending send*/
	struct p2p_tcp_data* send_data_last;/*last cache data for pending send*/
	int send_cache_count;

	char read_buffer[GSS_MAX_CMD_LEN*2];/*receive data buffer*/

	pj_timer_entry heart_timer; /*send heart command timer*/ 

	pj_time_val last_recv_time;

	pj_event_t *send_event;

	gss_conn_status conn_status;

	pj_mutex_t *send_mutex;  /**< send mutex.*/

	int pause_recv_status;
}gss_conn;

int gss_conn_create(char* uid, char* server, unsigned short port, void* user_data, gss_conn_cb* cb, gss_conn** gssc) ;

void gss_conn_destroy(gss_conn* conn);

int gss_conn_connect_server(gss_conn* conn);

void gss_conn_disconnect_server(gss_conn *conn);

int gss_conn_send(gss_conn* conn, char* buf, int buffer_len, char* prefix, int prefix_len, unsigned char cmd, p2p_send_model model);

void gss_conn_pause_recv(gss_conn* conn, int is_pause);

//clean all send buffer data
void gss_conn_clean_send_buf(gss_conn* conn);

#ifdef __cplusplus
	}
#endif

#endif