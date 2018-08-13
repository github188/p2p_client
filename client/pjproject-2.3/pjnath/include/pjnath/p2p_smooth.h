#ifndef __PJNATH_P2P_SMOOTH_H__
#define __PJNATH_P2P_SMOOTH_H__

#include <pjlib.h>

/*
mobile phone or windows client
p2p or tcp receive data frame interval is not regular, sometime fast, sometime slow
push data frame to list 
pop to callback user in P2P_SMOOTH_PLAY_SPAN ms timer
*/

//smooth cache data time, ms
#define P2P_SMOOTH_DEFAULT_SPAN (600)
#define P2P_SMOOTH_MIN_SPAN (200)

PJ_BEGIN_DECL

typedef struct p2p_smooth_buffer
{
	unsigned int size; //buffer size
	char* begin; //malloc begin
	char* end; //malloc end
	char* buffer; //buffer begin
	unsigned int valid; //remain valid size
}p2p_smooth_buffer;

typedef void (*P2P_SMOOTH_CB)(const char* buffer, unsigned int len, void* user_data);

typedef struct p2p_smooth_item
{
	unsigned int len;
	struct p2p_smooth_item* next;
}p2p_smooth_item;

typedef enum p2p_smooth_status
{
	P2P_SMOOTH_NONE, //no cache,direct callback
	P2P_SMOOTH_CACHEING, //cache,do not callback
	P2P_SMOOTH_PLAYING, //cache, callback in timer
	P2P_SMOOTH_CACHE_DOWN, //cache reduce, fast callback in timer
}p2p_smooth_status;

typedef struct p2p_smooth
{
	pj_pool_t *pool; /* pj memory pool*/
	P2P_SMOOTH_CB cb; /*call back function*/
	void* user_data;  /*user data*/
	pj_mutex_t* list_mutex; /*data list lock*/
	p2p_smooth_item* first_item, *last_item; /*data list*/
	unsigned int item_count; /*data list count*/
	pj_timer_entry timer; /*smooth timer*/
	p2p_smooth_status status; /*smooth status*/
	double remain; /*prev pop remain double value*/
	unsigned int cache_span; /*smooth cache span*/
	pj_time_val last_push_tm; /*last push time*/
	pj_time_val net_good_begin_tm;/*in playing, check net status*/
	pj_time_val cache_down_begin_tm; /*last push time*/

	p2p_smooth_buffer smooth_buffer;/*user data buffer, malloc by p2p_smooth_malloc*/
}p2p_smooth;

struct p2p_smooth* p2p_create_smooth(P2P_SMOOTH_CB cb, void* user_data);
void p2p_destroy_smooth(struct p2p_smooth* smooth);

//clean all smooth cache
void p2p_smooth_reset(struct p2p_smooth* smooth);

//push data to smooth cache
void p2p_smooth_push(struct p2p_smooth* smooth, const char* buffer, unsigned int len);

PJ_END_DECL

#endif