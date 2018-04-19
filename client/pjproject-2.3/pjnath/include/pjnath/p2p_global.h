#ifndef __PJNATH_P2P_GLOBAL_H__
#define __PJNATH_P2P_GLOBAL_H__

#include <pjlib.h>
#include <pjlib-util.h>
#include <pjnath.h>
#include <pjnath/socket_pair.h>
#include "p2p_transport.h"
PJ_BEGIN_DECL

typedef struct p2p_delay_destroy_pool
{
	PJ_DECL_LIST_MEMBER(struct p2p_delay_destroy_pool);
	pj_pool_t		*pool;
	pj_time_val		release_time;
}p2p_delay_destroy_pool;

#define MAX_THREAD_DESC_COUNT (256)

#define P2P_DATA_MAGIC (0xABACADEF)

#define P2P_CLIENT_PASSWORD "p2ppassword"
#define P2P_CLIENT_PREFIX "p2pc____"

typedef struct p2p_global
{
	pj_caching_pool	 caching_pool;
	pj_pool_t		*pool; /*memory pool used by global*/
	pj_thread_t		*thread;/*network io thread*/
	pj_bool_t		 thread_quit_flag;
	pj_timer_heap_t *timer_heap; /*all timer heap for timer stuff*/
	pj_ioqueue_t    *ioqueue; /*network io event demultiplexer*/

	pj_timer_entry timer;
	pj_grp_lock_t  *grp_lock;  /**< Group lock.*/
	p2p_delay_destroy_pool delay_destroy_pools;

	pj_atomic_t *atomic_id; /*connection id atomic number*/

	p2p_socket_pair* sock_pair;

	pj_pool_t		*pool_timer_heap; /*memory pool used by timer heap*/
	pj_pool_t		*pool_udt; /*memory pool used by timer heap*/

	pj_lock_t *timer_heap_lock;

	LOG_FUNC log_func;

	char client_guid[128];

	int max_recv_len ; //default 1 M
	int max_client_count ; //default 4

	int enable_relay;//enable p2p relay

	int only_relay; //disable HOST SRFLX PRFLX, only relay

	int smooth_span ;//0 disable p2p smooth, min P2P_SMOOTH_MIN_SPAN ms,default P2P_SMOOTH_DEFAULT_SPAN ms

	int enable_port_guess;
}p2p_global;

enum { MAX_P2P_BIND_RETRY = 32 };

p2p_global* get_p2p_global();
pj_bool_t p2p_is_inited();
void delay_destroy_pool(pj_pool_t *pool);

pj_status_t p2p_detect_nat_type(char* turn_server, unsigned short turn_port, void *user_data, pj_stun_nat_detect_cb* cb);

typedef void p2p_global_timer_cb(void *user_data);
typedef struct p2p_global_timer
{
	pj_timer_entry timer;
	pj_pool_t		*pool;
	void *user_data;
	p2p_global_timer_cb* cb;
}p2p_global_timer;
pj_status_t p2p_global_set_timer(pj_time_val delay, void *user_data, p2p_global_timer_cb* cb);

enum {IS_GLOBAL_THREAD, NO_GLOBAL_THREAD, GLOBAL_THREAD_EXIT };
int run_global_loop();

void check_pj_thread();

PJ_END_DECL

#endif	/* __PJNATH_P2P_GLOBAL_H__ */