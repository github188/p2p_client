#include <pjnath/errno.h>
#include <pjnath/p2p_global.h>
#include <pjnath/p2p_smooth.h>
#include <pjnath.h>

//pop to callback user in P2P_SMOOTH_PLAY_SPAN ms timer
#define P2P_SMOOTH_PLAY_SPAN (40)

//net good span, ms
#define P2P_SMOOTH_NET_GOOD_SPAN (10000)

//smooth buffer size, 1 M
#define P2P_SMOOTH_BUFFER_SIZE (1024*1024)

#define USE_P2P_SMOOTH_MALLOC 1

//malloc from smooth buffer

#define P2P_SMOOTH_BOUNDARY 4
#define P2P_SMOOTH_ALIGN(size, boundary) (((size) + ((boundary) - 1)) & ~((boundary) - 1))

static void* p2p_smooth_malloc(p2p_smooth* smooth, unsigned int len)
{
#ifdef USE_P2P_SMOOTH_MALLOC
	void* ret = NULL;

	//align to P2P_SMOOTH_BOUNDARY
	len = P2P_SMOOTH_ALIGN(len, P2P_SMOOTH_BOUNDARY);

	pj_mutex_lock(smooth->list_mutex);

	if(smooth->smooth_buffer.begin < smooth->smooth_buffer.end)
	{
		/*
		|------| ----------  |------|
		| used | begin ~ end | used |
		|------| ----------  |------|
		*/
		unsigned int remain = smooth->smooth_buffer.end - smooth->smooth_buffer.begin;
		if(remain >= len)
		{
			ret = smooth->smooth_buffer.begin;
			smooth->smooth_buffer.begin += len;
			smooth->smooth_buffer.valid -= len;
		}
		else
		{
			ret = p2p_malloc(len);
		}
	}
	else
	{

		/*
		|------| ----------  |------|
		| end  |    used	 | begin|
		|------| ----------  |------|
		*/
		if(smooth->smooth_buffer.valid < len)
			ret = p2p_malloc(len);
		else
		{
			unsigned int remain = smooth->smooth_buffer.size - (smooth->smooth_buffer.begin-smooth->smooth_buffer.buffer);
			if(remain >= len)
			{
				ret = smooth->smooth_buffer.begin;
				smooth->smooth_buffer.begin += len;
				smooth->smooth_buffer.valid -= len;
			}
			else
			{
				smooth->smooth_buffer.valid -= remain;
				smooth->smooth_buffer.begin = smooth->smooth_buffer.buffer;
				remain = smooth->smooth_buffer.end - smooth->smooth_buffer.begin;
				if(remain >= len)
				{
					ret = smooth->smooth_buffer.begin;
					smooth->smooth_buffer.begin += len;
					smooth->smooth_buffer.valid -= len;
				}
				else
				{
					ret = p2p_malloc(len);
				}
			}
		}
	}

	pj_mutex_unlock(smooth->list_mutex);

	return ret;
#else
	return p2p_malloc(len);
#endif
	
}

static void p2p_smooth_free(p2p_smooth* smooth, void* buffer, unsigned int len)
{
#ifdef USE_P2P_SMOOTH_MALLOC
	//align to P2P_SMOOTH_BOUNDARY
	len = P2P_SMOOTH_ALIGN(len, P2P_SMOOTH_BOUNDARY);

	//malloc by p2p_malloc
	if((char*)buffer < smooth->smooth_buffer.buffer 
		|| (char*)buffer >= smooth->smooth_buffer.buffer+smooth->smooth_buffer.size)
		p2p_free(buffer);
	else
	{
		//first in first out
		pj_mutex_lock(smooth->list_mutex);
		if ((char*)buffer < smooth->smooth_buffer.end )
		{
			/*add remain bytes
			|--------- | ----------  |------- |
			| new end  |			 | old end|
			|--------  | ----------  |------- |
			*/
			smooth->smooth_buffer.valid += (smooth->smooth_buffer.size - (smooth->smooth_buffer.end - smooth->smooth_buffer.buffer));
		}
		smooth->smooth_buffer.end = (char*)buffer+len;
		smooth->smooth_buffer.valid += len;
		pj_mutex_unlock(smooth->list_mutex);
	}
#else
	p2p_free(buffer);
#endif
}

//pop from cache list and callback to user
static void p2p_smooth_callback_play(p2p_smooth* smooth, unsigned int play_count)
{
	unsigned int i;
	if(play_count > smooth->item_count)
		play_count = smooth->item_count;
	for(i=0; i<play_count; i++)
	{
		p2p_smooth_item* item = NULL;

		pj_mutex_lock(smooth->list_mutex);
		if(smooth->item_count)
		{
			item = smooth->first_item;
			smooth->first_item = smooth->first_item->next;
			if(smooth->first_item == NULL)
				smooth->last_item = NULL;
			smooth->item_count--;
		}
		pj_mutex_unlock(smooth->list_mutex);

		if(item)
		{
			if(smooth->cb)
				(*smooth->cb)((const char*)(item+1), item->len, smooth->user_data);

			p2p_smooth_free(smooth, item, item->len+sizeof(p2p_smooth_item));
		}
		else
			break;
	}
}

static double p2p_smooth_pop_cache_span(p2p_smooth* smooth)
{
	double cache_span = smooth->cache_span;

	//if cache down status, more and fast callback
	if(smooth->status == P2P_SMOOTH_CACHE_DOWN) 
	{
		double delta; //time span from cache_down_begin_tm
		pj_time_val t ;
		double delta10 = smooth->cache_span*10;//ten times cache_span

		pj_gettickcount(&t);
		PJ_TIME_VAL_SUB(t, smooth->cache_down_begin_tm);
		delta = PJ_TIME_VAL_MSEC(t);

		if(delta < delta10)
		{
			//More and more big 
			cache_span = smooth->cache_span*(1.0 - delta/delta10);
			if(cache_span < P2P_SMOOTH_PLAY_SPAN)
				cache_span = P2P_SMOOTH_PLAY_SPAN;
		}
		else
			cache_span = P2P_SMOOTH_PLAY_SPAN;
	}
	return cache_span;
}

static void p2p_smooth_pop(p2p_smooth* smooth)
{
	pj_time_val t;
	double double_play_count;
	int play_count;
	double cache_span;

	cache_span = p2p_smooth_pop_cache_span(smooth);
	
	double_play_count = smooth->remain + (1.0 * smooth->item_count * P2P_SMOOTH_PLAY_SPAN / cache_span);
	play_count = (int)double_play_count;

	//PJ_LOG(4,("p2p_smooth", "p2p_smooth_pop %p, status %d, item_count %d, double_play_count %.2f", smooth, smooth->status, smooth->item_count, double_play_count));

	p2p_smooth_callback_play(smooth, play_count);

	smooth->remain = double_play_count - play_count;
	t.sec = 0;
	t.msec = P2P_SMOOTH_PLAY_SPAN;
	pj_timer_heap_schedule_w_grp_lock(get_p2p_global()->timer_heap, 
		&smooth->timer,
		&t, 
		0, 
		NULL);
}

static void p2p_smooth_timer(pj_timer_heap_t *th, pj_timer_entry *e)
{
	p2p_smooth* smooth = (p2p_smooth*) e->user_data;
	PJ_UNUSED_ARG(th);

	switch(smooth->status)
	{
	case P2P_SMOOTH_CACHEING:
		{
			//enter play status
			smooth->remain = 0;
			smooth->status = P2P_SMOOTH_PLAYING;
			
			//PJ_LOG(4,("p2p_smooth", "p2p_smooth_push %p enter playing status", smooth));

			if((int)smooth->cache_span == get_p2p_global()->smooth_span)
				pj_gettickcount(&smooth->net_good_begin_tm);
			p2p_smooth_pop(smooth);
		}
		return;
	case P2P_SMOOTH_PLAYING:
		{
			//cache count too little,enter cache status
			if(smooth->item_count <= (smooth->cache_span/200+1))
			{
				pj_time_val t;

				p2p_smooth_callback_play(smooth, smooth->item_count);

				//PJ_LOG(4,("p2p_smooth", "p2p_smooth_push %p status playing to cache status", smooth));

				smooth->status = P2P_SMOOTH_CACHEING;
				smooth->cache_span += 100;
				if((int)smooth->cache_span > get_p2p_global()->smooth_span)
					smooth->cache_span = get_p2p_global()->smooth_span;

				t.sec = smooth->cache_span / 1000;
				t.msec = smooth->cache_span % 1000;
				pj_timer_heap_schedule_w_grp_lock(get_p2p_global()->timer_heap, 
					&smooth->timer,
					&t, 
					0, 
					NULL);
				return;
			}
			else
			{
				p2p_smooth_pop(smooth);
			}
		}
		return;
	case P2P_SMOOTH_CACHE_DOWN:
		{
			p2p_smooth_pop(smooth);
			if(smooth->item_count == 0)
				p2p_smooth_reset(smooth);
		}
	default:
		return;
	}
}

struct p2p_smooth* p2p_create_smooth(P2P_SMOOTH_CB cb, void* user_data)
{
	pj_pool_t *pool = NULL;
	p2p_smooth* smooth = NULL;

	PJ_LOG(4,("p2p_smooth", "p2p_create_smooth begin"));

	pool = pj_pool_create(&get_p2p_global()->caching_pool.factory, 
		"p2p_tcp%p", 
		1024,
		256, 
		NULL);
	smooth = PJ_POOL_ZALLOC_T(pool, p2p_smooth);

	smooth->pool = pool;
	smooth->cb = cb;
	smooth->user_data = user_data;
	smooth->first_item = smooth->last_item = NULL;
	smooth->item_count = 0;
	smooth->status = P2P_SMOOTH_NONE;
	smooth->remain = 0;
	smooth->cache_span = P2P_SMOOTH_MIN_SPAN;
	smooth->last_push_tm.sec  = smooth->last_push_tm.msec = 0;

#ifdef USE_P2P_SMOOTH_MALLOC	
	smooth->smooth_buffer.valid = smooth->smooth_buffer.size = P2P_SMOOTH_BUFFER_SIZE;
	smooth->smooth_buffer.buffer = p2p_malloc(P2P_SMOOTH_BUFFER_SIZE);
	smooth->smooth_buffer.begin = smooth->smooth_buffer.buffer;
	smooth->smooth_buffer.end = smooth->smooth_buffer.buffer+P2P_SMOOTH_BUFFER_SIZE;
#endif

	pj_mutex_create_recursive(pool, NULL, &smooth->list_mutex);

	pj_timer_entry_init(&smooth->timer, 0, smooth, &p2p_smooth_timer);

	PJ_LOG(4,("p2p_smooth", "p2p_create_smooth %p end", smooth));
	return smooth;
}

static void p2p_smooth_free_items(struct p2p_smooth* smooth)
{
	p2p_smooth_item* item = NULL;
	p2p_smooth_item* next = NULL;
	if(smooth==NULL)
		return;

	pj_mutex_lock(smooth->list_mutex);

	item = smooth->first_item;
	while(item)
	{
		next = item->next;
		p2p_smooth_free(smooth, item, item->len+sizeof(p2p_smooth_item));
		item = next;
	}
	smooth->first_item = smooth->last_item = NULL;
	smooth->item_count = 0;
	pj_mutex_unlock(smooth->list_mutex);
}

//clean all smooth cache
void p2p_smooth_reset(struct p2p_smooth* smooth)
{
	if(smooth==NULL)
		return;	

	PJ_LOG(4,("p2p_smooth", "p2p_smooth_reset begin %p", smooth));

	pj_timer_heap_cancel_if_active(get_p2p_global()->timer_heap, &smooth->timer, 0);

	p2p_smooth_free_items(smooth);

	smooth->status = P2P_SMOOTH_NONE;
	smooth->remain = 0;
	smooth->cache_span = P2P_SMOOTH_MIN_SPAN;
	smooth->last_push_tm.sec  = smooth->last_push_tm.msec = 0;

	PJ_LOG(4,("p2p_smooth", "p2p_smooth_reset end %p", smooth));
}

void p2p_destroy_smooth(struct p2p_smooth* smooth)
{
	if(smooth==NULL)
		return;

	PJ_LOG(4,("p2p_smooth", "p2p_destroy_smooth %p begin", smooth));

	pj_timer_heap_cancel_if_active(get_p2p_global()->timer_heap, &smooth->timer, 0);

	p2p_smooth_free_items(smooth);
#ifdef USE_P2P_SMOOTH_MALLOC	
	p2p_free(smooth->smooth_buffer.buffer);
#endif
	pj_mutex_destroy(smooth->list_mutex);

	delay_destroy_pool(smooth->pool);

	PJ_LOG(4,("p2p_smooth", "p2p_destroy_smooth %p end", smooth));
}

static pj_bool_t p2p_smooth_none(struct p2p_smooth* smooth,
							const char* buffer, 
							unsigned int len, 
							pj_time_val* now)
{
	if(smooth->last_push_tm.sec == 0
		&& smooth->last_push_tm.msec == 0) //first data
	{
		smooth->last_push_tm = *now;
		if(smooth->cb)
			(*smooth->cb)(buffer, len, smooth->user_data);
		return PJ_TRUE;
	}
	else
	{
		pj_uint32_t delta;
		pj_time_val now1 = *now;
		PJ_TIME_VAL_SUB(now1, smooth->last_push_tm);
		delta = PJ_TIME_VAL_MSEC(now1);
		if(delta >= P2P_SMOOTH_MIN_SPAN)
		{
			//delta too long, enter cache status
			pj_time_val t;

			PJ_LOG(4,("p2p_smooth", "p2p_smooth_push %p enter cache status", smooth));

			smooth->status = P2P_SMOOTH_CACHEING;
			t.sec = smooth->cache_span / 1000;
			t.msec = smooth->cache_span % 1000;
			pj_timer_heap_schedule_w_grp_lock(get_p2p_global()->timer_heap, 
				&smooth->timer,
				&t, 
				0, 
				NULL);
			return PJ_FALSE;
		}
		else
		{
			//frame interval too short, net is good, do not cache
			smooth->last_push_tm = *now;
			if(smooth->cb)
				(*smooth->cb)(buffer, len, smooth->user_data);
			return PJ_TRUE;
		}
	}
}

static void p2p_smooth_check_net(struct p2p_smooth* smooth, pj_time_val* now)
{
	pj_uint32_t delta;
	pj_time_val t = *now;

	if((int)smooth->cache_span < get_p2p_global()->smooth_span)
		return;

	PJ_TIME_VAL_SUB(t, smooth->net_good_begin_tm);
	delta = PJ_TIME_VAL_MSEC(t);

	//PJ_LOG(4,("p2p_smooth", "p2p_smooth_check_net %p delta1 %d", smooth, delta));

	if(delta >= P2P_SMOOTH_NET_GOOD_SPAN)  
	{
		//net status continued good in P2P_SMOOTH_NET_GOOD_SPAN
		PJ_LOG(4,("p2p_smooth", "p2p_smooth_push %p enter cache down status", smooth));

		smooth->status = P2P_SMOOTH_CACHE_DOWN;
		smooth->cache_down_begin_tm = *now;
		//PJ_LOG(4,("p2p_smooth", "p2p_smooth_check_net %p enter P2P_SMOOTH_CACHE_DOWN", smooth));
	}
	else
	{
		t = *now;
		PJ_TIME_VAL_SUB(t, smooth->last_push_tm);
		delta = PJ_TIME_VAL_MSEC(t);
		//PJ_LOG(4,("p2p_smooth", "p2p_smooth_check_net %p delta2 %d", smooth, delta));
		if(delta > P2P_SMOOTH_MIN_SPAN) //net status bad
			smooth->net_good_begin_tm = *now;
	}
}

void p2p_smooth_push(struct p2p_smooth* smooth, const char* buffer, unsigned int len)
{
	p2p_smooth_item* item;
	pj_time_val now;

	if(smooth==NULL || buffer == NULL || len == 0)
		return;
	
	pj_gettickcount(&now);

	if(smooth->status == P2P_SMOOTH_NONE)
	{
		if(p2p_smooth_none(smooth, buffer, len, &now))
			return;
	}
	else if(smooth->status == P2P_SMOOTH_PLAYING)
	{
		p2p_smooth_check_net(smooth, &now);
	}
	
	smooth->last_push_tm = now;

	//copy and put to cache list
	item = (p2p_smooth_item*)p2p_smooth_malloc(smooth, sizeof(p2p_smooth_item)+len);
	pj_memcpy(item+1, buffer, len);
	item->len = len;
	item->next = NULL;

	pj_mutex_lock(smooth->list_mutex);
	if(smooth->item_count == 0)
	{
		smooth->first_item = smooth->last_item = item;
	}
	else
	{
		smooth->last_item->next = item;
		smooth->last_item = item;
	}
	smooth->item_count++;
	pj_mutex_unlock(smooth->list_mutex);
}