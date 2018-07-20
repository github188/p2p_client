#include <pjnath/p2p_global.h>
#include <pjnath/p2p_tcp_proxy.h>
#include <pjnath/socket_pair.h>
#include <pjnath/p2p_udt.h>
#include <pjnath/p2p_upnp.h>
#include <pjnath/p2p_pool.h>
#include <pjnath/p2p_smooth.h>
#include <pjnath/p2p_port_guess.h>

#define THIS_FILE "p2p_global.c"
#define DELAY_DESTORY_TIME 1

#define MAX_CLIENT_COUNT 4

static p2p_global g_p2p_global;
static pj_bool_t p2p_inited = PJ_FALSE;
p2p_global* get_p2p_global()
{
	return &g_p2p_global;
}

pj_bool_t p2p_is_inited()
{
	return p2p_inited;
}

/*
 * This function checks for events from both timer and ioqueue (for
 * network events). It is invoked by the worker thread.
 */
static pj_status_t handle_events(unsigned max_msec, unsigned *p_count)
{
    enum { MAX_NET_EVENTS = 1 };
    pj_time_val max_timeout = {0, 0};
    pj_time_val timeout = { 0, 0};
    unsigned count = 0, net_event_count = 0;
    int c;

    max_timeout.msec = max_msec;

    /* Poll the timer to run it and also to retrieve the earliest entry. */
    timeout.sec = timeout.msec = 0;
    c = pj_timer_heap_poll(g_p2p_global.timer_heap, &timeout );
    if (c > 0)
	count += c;

	run_socket_pair(g_p2p_global.sock_pair);

    /* timer_heap_poll should never ever returns negative value, or otherwise
     * ioqueue_poll() will block forever!
     */
    pj_assert(timeout.sec >= 0 && timeout.msec >= 0);
    if (timeout.msec >= 1000) timeout.msec = 999;

    /* compare the value with the timeout to wait from timer, and use the 
     * minimum value. 
    */
    if (PJ_TIME_VAL_GT(timeout, max_timeout))
	timeout = max_timeout;

    /* Poll ioqueue. 
     * Repeat polling the ioqueue while we have immediate events, because
     * timer heap may process more than one events, so if we only process
     * one network events at a time (such as when IOCP backend is used),
     * the ioqueue may have trouble keeping up with the request rate.
     *
     * For example, for each send() request, one network event will be
     *   reported by ioqueue for the send() completion. If we don't poll
     *   the ioqueue often enough, the send() completion will not be
     *   reported in timely manner.
     */
    do {
	c = pj_ioqueue_poll( g_p2p_global.ioqueue, &timeout);
	if (c < 0) {
	    pj_status_t err = pj_get_netos_error();
	    pj_thread_sleep(PJ_TIME_VAL_MSEC(timeout));
	    if (p_count)
		*p_count = count;
	    return err;
	} else if (c == 0) {
	    break;
	} else {
	    net_event_count += c;
	    timeout.sec = timeout.msec = 0;
	}
    } while (c > 0 && net_event_count < MAX_NET_EVENTS);

    count += net_event_count;
    if (p_count)
	*p_count = count;

    return PJ_SUCCESS;

}

/*
p2p network io thread
*/
static int p2p_worker_thread(void *unused)
{
	PJ_LOG(4, ("p2p_worker_thread", "p2p_worker_thread begin"));
	PJ_UNUSED_ARG(unused);

	while (!g_p2p_global.thread_quit_flag) {
		handle_events(500, NULL);

	}
	PJ_LOG(4, ("p2p_worker_thread", "p2p_worker_thread end"));
	return 0;
}

int run_global_loop()
{
	if(g_p2p_global.thread_quit_flag)
		return GLOBAL_THREAD_EXIT;

	if(g_p2p_global.thread != pj_thread_this())
		return NO_GLOBAL_THREAD;
	
	handle_events(500, NULL);

	return IS_GLOBAL_THREAD;
}

//callback to free memory of p2p_tcp_listen_proxy
static void p2p_global_on_destroy(void *obj)
{
	PJ_UNUSED_ARG(obj);
}
/*
 * Timer event.
 */
static void on_timer_event(pj_timer_heap_t *th, pj_timer_entry *e)
{
	p2p_delay_destroy_pool* next, *cur;
	pj_time_val delay = {DELAY_DESTORY_TIME, 0};
	static int times = 1;
#define DUMP_TIMES 60
	PJ_UNUSED_ARG(th);

	if(times == DUMP_TIMES)
	{
		//PJ_LOG(4, ("on_timer_event", "on_timer_event begin,memory pool %d", g_p2p_global.caching_pool.used_size));
#ifdef USE_P2P_POOL
		p2p_pool_dump();
#endif
	}

	if(e->id == P2P_TIMER_DELAY_DESTROY)
	{
		pj_time_val now, end;
		pj_gettickcount(&now);

		pj_grp_lock_acquire(g_p2p_global.grp_lock);

		next = g_p2p_global.delay_destroy_pools.next;
		while(next != &g_p2p_global.delay_destroy_pools) 
		{
			if(now.sec - next->release_time.sec < DELAY_DESTORY_TIME)
				break;

			cur = next;
			next = cur->next;
			pj_list_erase(cur);
			pj_pool_release(cur->pool);
			p2p_free(cur);
		}

		pj_grp_lock_release(g_p2p_global.grp_lock);

		pj_gettickcount(&end);

		pj_timer_heap_schedule_w_grp_lock(g_p2p_global.timer_heap, &g_p2p_global.timer,
			&delay, P2P_TIMER_DELAY_DESTROY,
			g_p2p_global.grp_lock);
	}
	if(times == DUMP_TIMES)
	{
		//PJ_LOG(4, ("on_timer_event", "on_timer_event end"));
		times = 1;
	}
	else
		times ++;
}

//#define P2P_ASYNC_LOG 1
//#define PRINT_ANDROID_LOG 1
#define P2P_DEFAULT_LOG_LEVEL 4

#ifdef PRINT_ANDROID_LOG
#include <android/log.h>
#define LOG_TAG "p2p"
#define ANDROID_LOG_PRINT(...) __android_log_print(ANDROID_LOG_INFO,LOG_TAG,__VA_ARGS__)
#endif

static void p2p_log_output(int level, const char *data, int len)
{
	pj_log_write(level, data, len);
#ifdef PRINT_ANDROID_LOG
	ANDROID_LOG_PRINT("%s", data);
#endif
	if (get_p2p_global()->log_func)
	{
		get_p2p_global()->log_func(data, len);
	}
}

#ifdef P2P_ASYNC_LOG

#define MAX_ASYNC_LEN (2048)

typedef struct p2p_async_log_item
{
	int level;
	int len;
	struct p2p_async_log_item* next;
}p2p_async_log_item;

typedef struct p2p_async_log
{
	p2p_async_log_item* first_log;
	p2p_async_log_item* last_log;
	int count;
	pj_mutex_t* mutex;
	pj_thread_t *thread;
}p2p_async_log;

p2p_async_log g_async_log = {NULL, NULL, 0, NULL, NULL};


static int p2p_log_thread(void *unused)
{
	PJ_UNUSED_ARG(unused);

	while (!g_p2p_global.thread_quit_flag) 
	{
		if(g_async_log.count)
		{
			p2p_async_log_item* item = NULL;

			pj_mutex_lock(g_async_log.mutex);
			item = g_async_log.first_log;
			g_async_log.first_log = g_async_log.first_log->next;
			g_async_log.count--;
			pj_mutex_unlock(g_async_log.mutex);

			p2p_log_output(item->level, (const char*)(item+1), item->len);

			free(item);
			
			continue;
		}
		pj_thread_sleep(1);
	}
	return 0;
}

#endif


/* log callback to write to file */
static void p2p_log_func(int level, const char *data, int len)
{
#ifdef P2P_ASYNC_LOG
	if(g_async_log.thread == NULL)
	{
		p2p_log_output(level, data, len);
		return;
	}	

	pj_mutex_lock(g_async_log.mutex);
	if(g_async_log.count==0)
	{
		p2p_async_log_item* item ;
		item = malloc(MAX_ASYNC_LEN);
		item->len = len;
		item->level = level;
		item->next = NULL;
		strcpy((char*)(item+1), data);
		g_async_log.first_log = g_async_log.last_log = item;
		g_async_log.count++;
	}
	else
	{
		if(g_async_log.last_log->len + len < MAX_ASYNC_LEN-sizeof(p2p_async_log_item))
		{
			strcpy((char*)(g_async_log.last_log+1)+g_async_log.last_log->len, data);
			g_async_log.last_log->len +=len;
		}
		else
		{
			p2p_async_log_item* item ;
			item = malloc(MAX_ASYNC_LEN);
			item->len = len;
			item->level = level;
			item->next = NULL;
			strcpy((char*)(item+1), data);
			g_async_log.last_log->next = item;
			g_async_log.last_log = item;
			g_async_log.count++;
		}
	}	
	pj_mutex_unlock(g_async_log.mutex);
#else
	p2p_log_output(level, data, len);
#endif
}

P2P_DECL(void) p2p_log_set_level(int level)
{
	//check_pj_thread();
#ifndef PRINT_ANDROID_LOG
	pj_log_set_level(level);
#endif
}

P2P_DECL(int) p2p_init(LOG_FUNC log_func)
{
	/* Initialize the libraries before anything else */
	pj_status_t status ;
	pj_time_val delay = {DELAY_DESTORY_TIME, 0};
	pj_str_t guid;
	char guid_str[PJ_GUID_MAX_LENGTH+1];
	int thread_pro;
	pj_time_val now;

	if(p2p_inited)
		return PJ_SUCCESS;
	else
		p2p_inited = PJ_TRUE;


	g_p2p_global.log_func = log_func;
	pj_log_set_level(P2P_DEFAULT_LOG_LEVEL);
	pj_log_set_log_func(&p2p_log_func);

	status= pj_init();
	if(status != PJ_SUCCESS)
		return status;

	PJ_LOG(3, ("pj_p2p_init", "pj_p2p_init begin,version:%s", p2p_get_ver()));
    
    pj_dump_config();
    
	PJ_LOG(3, ("pj_p2p_init", "pjlib_util_init"));
	status = pjlib_util_init();
	if(status != PJ_SUCCESS)
		return status;

	PJ_LOG(3, ("pj_p2p_init", "pjnath_init"));
	status = pjnath_init();
	if(status != PJ_SUCCESS)
		return status;

	/* Must create pool factory, where memory allocations come from */
#ifdef USE_P2P_POOL
	init_p2p_pool();
	pj_caching_pool_init(&g_p2p_global.caching_pool, pj_pool_factory_p2p_policy(), 0);
#else
	pj_caching_pool_init(&g_p2p_global.caching_pool, NULL, 0);
#endif

	/* Create memory pool */
	g_p2p_global.pool = pj_pool_create(&g_p2p_global.caching_pool.factory, "p2p", 512, 512, NULL);
	g_p2p_global.pool_timer_heap = pj_pool_create(&g_p2p_global.caching_pool.factory, "p2p_timers", 512, 512, NULL);
	g_p2p_global.pool_udt = pj_pool_create(&g_p2p_global.caching_pool.factory, "udt", 512, 512, NULL);

	g_p2p_global.max_recv_len = P2P_RECV_BUFFER_SIZE;
	g_p2p_global.max_client_count = MAX_CLIENT_COUNT;
	g_p2p_global.enable_relay = 1;
	g_p2p_global.only_relay = 0;
	g_p2p_global.smooth_span = P2P_SMOOTH_DEFAULT_SPAN;
	g_p2p_global.enable_port_guess = 1;
	pj_gettimeofday(&now);
	pj_srand( (unsigned)now.sec );
	g_p2p_global.bind_port = (pj_uint16_t)(pj_rand() % (GUESS_MAX_PORT-GUESS_MIN_PORT) + GUESS_MIN_PORT);

#ifdef P2P_ASYNC_LOG
	g_async_log.first_log = g_async_log.last_log = NULL;
	g_async_log.count = 0;
	pj_mutex_create(g_p2p_global.pool, "log_mutex", PJ_MUTEX_SIMPLE, &g_async_log.mutex);
	pj_thread_create(g_p2p_global.pool, "p2p_log", &p2p_log_thread, NULL, 0, 0, &g_async_log.thread);
#endif

	guid.ptr = guid_str;
	guid.slen = 0;
	pj_generate_unique_string_lower(&guid);
	guid_str[guid.slen] = '\0';
	strcpy(g_p2p_global.client_guid, P2P_CLIENT_PREFIX);
	strcat(g_p2p_global.client_guid, guid_str);

	/* Create timer heap for timer stuff */
	pj_timer_heap_create(g_p2p_global.pool_timer_heap, PJ_IOQUEUE_MAX_HANDLES, &g_p2p_global.timer_heap);
	pj_lock_create_recursive_mutex(g_p2p_global.pool_timer_heap, "p2ptl", &g_p2p_global.timer_heap_lock);
	pj_timer_heap_set_lock(g_p2p_global.timer_heap, g_p2p_global.timer_heap_lock, PJ_TRUE);

	/*create ioqueue for network I/O stuff */
	pj_ioqueue_create(g_p2p_global.pool, PJ_IOQUEUE_MAX_HANDLES, &g_p2p_global.ioqueue);

	status = pj_grp_lock_create(g_p2p_global.pool, NULL, &g_p2p_global.grp_lock);

	pj_grp_lock_add_ref(g_p2p_global.grp_lock);
	pj_grp_lock_add_handler(g_p2p_global.grp_lock, g_p2p_global.pool, 0, &p2p_global_on_destroy);

	/* Timer */
	pj_timer_entry_init(&g_p2p_global.timer, P2P_TIMER_NONE, &g_p2p_global, &on_timer_event);
	pj_list_init(&g_p2p_global.delay_destroy_pools);

	PJ_LOG(3, ("pj_p2p_init", "create_socket_pair"));
	status = create_socket_pair(&g_p2p_global.sock_pair, g_p2p_global.pool);
	if(status != PJ_SUCCESS)
	{
		PJ_LOG(2, ("pj_p2p_init", "failed to create_socket_pair"));
		p2p_uninit();
		return status;
	}

	PJ_LOG(3, ("pj_p2p_init", "create_p2p_upnp"));
	status = create_p2p_upnp();
	if(status != PJ_SUCCESS)
	{
		PJ_LOG(2, ("pj_p2p_init", "failed to create_p2p_upnp"));
		p2p_uninit();
		return status;
	}

	PJ_LOG(3, ("pj_p2p_init", "p2p_udt_init"));
	status = p2p_udt_init();
	if(status != PJ_SUCCESS)
	{
		PJ_LOG(2, ("pj_p2p_init", "failed to create_p2p_upnp"));
		p2p_uninit();
		return status;
	}

	/*rand first connection id*/
	pj_atomic_create(g_p2p_global.pool, pj_rand(), &g_p2p_global.atomic_id);

	/*create io queue thread */	
#if defined(WIN32) || defined(ANDROID_BUILD) || defined(P2P_IOS)
	pj_thread_create(g_p2p_global.pool, "p2p", &p2p_worker_thread, NULL, 0, 0, &g_p2p_global.thread);
	thread_pro = pj_thread_get_prio_max(g_p2p_global.thread);
	if(thread_pro <= 0)
		pj_thread_set_prio(g_p2p_global.thread, thread_pro); 
#else
	status = pj_thread_create(g_p2p_global.pool, "p2p", &p2p_worker_thread, NULL, 0, PJ_THREAD_MAX_PRIO, &g_p2p_global.thread);
	if(status != PJ_SUCCESS)
	{
		status = pj_thread_create(g_p2p_global.pool, "p2p", &p2p_worker_thread, NULL, 0, 0, &g_p2p_global.thread);
	}
#endif
	
	pj_timer_heap_schedule_w_grp_lock(g_p2p_global.timer_heap, &g_p2p_global.timer,
		&delay, P2P_TIMER_DELAY_DESTROY,
		g_p2p_global.grp_lock);

	PJ_LOG(3, ("pj_p2p_init", "pj_p2p_init end, thread_pro %d", thread_pro));
	return PJ_SUCCESS;
}

P2P_DECL(void) p2p_uninit()
{
	p2p_delay_destroy_pool* next, * cur;

	if(!p2p_inited)
		return;

	p2p_inited = PJ_FALSE;
	check_pj_thread();

	g_p2p_global.thread_quit_flag = PJ_TRUE;
	PJ_LOG(4, ("p2p_uninit", "p2p_uninit begin"));
	if (g_p2p_global.thread) {
		pj_thread_join(g_p2p_global.thread);
		pj_thread_destroy(g_p2p_global.thread);
	}
	PJ_LOG(4, ("p2p_uninit", "p2p_udt_uninit begin"));
	p2p_udt_uninit();
	PJ_LOG(4, ("p2p_uninit", "p2p_udt_uninit end"));

	if(g_p2p_global.sock_pair)
		destroy_socket_pair(g_p2p_global.sock_pair);

	pj_timer_heap_cancel_if_active(g_p2p_global.timer_heap, &g_p2p_global.timer, P2P_TIMER_NONE);
	
	pj_grp_lock_acquire(g_p2p_global.grp_lock);
	next = g_p2p_global.delay_destroy_pools.next;
	while(next != &g_p2p_global.delay_destroy_pools) 
	{
		cur = next;
		next = cur->next;
		pj_pool_release(cur->pool);
		p2p_free(cur);
	}
	pj_grp_lock_release(g_p2p_global.grp_lock);

	if (g_p2p_global.ioqueue)
		pj_ioqueue_destroy(g_p2p_global.ioqueue);

	if (g_p2p_global.timer_heap)
		pj_timer_heap_destroy(g_p2p_global.timer_heap);

	pj_atomic_destroy(g_p2p_global.atomic_id);

	PJ_LOG(4, ("p2p_uninit", "destroy_p2p_upnp begin"));
	destroy_p2p_upnp();

	PJ_LOG(4, ("pj_p2p_uninit", "p2p_uninit end"));

#ifdef P2P_ASYNC_LOG
	if (g_async_log.thread) {
		pj_thread_join(g_async_log.thread);
		pj_thread_destroy(g_async_log.thread);
		g_async_log.thread = NULL;
	}
#endif

	pj_pool_release(g_p2p_global.pool);
	pj_pool_release(g_p2p_global.pool_timer_heap);
	pj_pool_release(g_p2p_global.pool_udt);
	pj_caching_pool_destroy(&g_p2p_global.caching_pool);

	pj_shutdown();

#ifdef USE_P2P_POOL
	uninit_p2p_pool();
#endif
}

void delay_destroy_pool(pj_pool_t *pool)
{
	if(!pool)
		return;
	if(p2p_inited)
	{		
		p2p_delay_destroy_pool* delay_pool = p2p_malloc(sizeof(p2p_delay_destroy_pool)); 
		delay_pool->pool = pool;
		pj_gettickcount(&delay_pool->release_time);
		pj_grp_lock_acquire(g_p2p_global.grp_lock);
		pj_list_push_back(&g_p2p_global.delay_destroy_pools, delay_pool);
		pj_grp_lock_release(g_p2p_global.grp_lock);
	}
	else
	{
		pj_pool_release(pool);
	}
}


typedef struct p2p_nat_type_detector
{
	pj_sockaddr_in server;
	pj_stun_config stun_cfg;
	pj_stun_nat_detect_cb* cb;
	void *user_data;
	pj_pool_t *pool;
}p2p_nat_type_detector;

void p2p_nat_detect_cb(void *user_data, const pj_stun_nat_detect_result *res)
{
	p2p_nat_type_detector* detector = user_data;
	pj_thread_sleep(1000);
	if(detector->cb)
	{
		(*detector->cb)(detector->user_data, res);
	}
	delay_destroy_pool(detector->pool);
}

pj_status_t p2p_detect_nat_type(char* turn_server, unsigned short turn_port, void *user_data, pj_stun_nat_detect_cb* cb)
{
	p2p_nat_type_detector* detector;
	pj_pool_t *pool;
	pj_str_t addr = pj_str(turn_server);

	pool = pj_pool_create(&get_p2p_global()->caching_pool.factory, 
		"p2pnat%p", 
		512,
		512, 
		NULL);
	detector = PJ_POOL_ZALLOC_T(pool, p2p_nat_type_detector);
	pj_bzero(detector, sizeof(p2p_nat_type_detector));

	detector->pool = pool;
	detector->cb = cb;
	detector->user_data = user_data;
	pj_sockaddr_init(pj_AF_INET(), (pj_sockaddr*)&detector->server, &addr, turn_port);
	pj_bzero(&detector->stun_cfg, sizeof(detector->stun_cfg));
	detector->stun_cfg.pf = &get_p2p_global()->caching_pool.factory;
	detector->stun_cfg.options = 0;
	detector->stun_cfg.ioqueue = get_p2p_global()->ioqueue;
	detector->stun_cfg.timer_heap = get_p2p_global()->timer_heap;
	detector->stun_cfg.rto_msec = PJ_STUN_RTO_VALUE;
	detector->stun_cfg.res_cache_msec = PJ_STUN_RES_CACHE_DURATION;
	detector->stun_cfg.software_name = pj_str((char*)PJNATH_STUN_SOFTWARE_NAME);

	return pj_stun_detect_nat_type(&detector->server, &detector->stun_cfg, detector, &p2p_nat_detect_cb);

}

static void on_global_timer_event(pj_timer_heap_t *th, pj_timer_entry *e)
{
	p2p_global_timer* timer = (p2p_global_timer*)e->user_data;
	PJ_UNUSED_ARG(th);
	if(timer->cb)
	{
		(*timer->cb)(timer->user_data);
	}

	delay_destroy_pool(timer->pool);
}

pj_status_t p2p_global_set_timer(pj_time_val delay, void *user_data, p2p_global_timer_cb* cb)
{
	p2p_global_timer* timer;
	pj_pool_t *pool;

	pool = pj_pool_create(&get_p2p_global()->caching_pool.factory, 
		"p2p_timer%p", 
		512,
		512, 
		NULL);
	timer = PJ_POOL_ZALLOC_T(pool, p2p_global_timer);
	pj_bzero(timer, sizeof(p2p_global_timer));
	timer->pool = pool;
	timer->user_data = user_data;
	timer->cb = cb;
	pj_timer_entry_init(&timer->timer, P2P_TIMER_NONE, &timer, &on_global_timer_event);
	timer->timer.user_data = timer;

	return pj_timer_heap_schedule_w_grp_lock(g_p2p_global.timer_heap, &timer->timer,
		&delay, P2P_TIMER_GLOBAL, g_p2p_global.grp_lock);
}

P2P_DECL(int) p2p_set_global_opt(p2p_global_opt opt, const void* optval, int optlen)
{
	switch(opt)
	{
	case P2P_MAX_RECV_PACKAGE_LEN:
		{
			int val;
			if(optlen != sizeof(int))
				return PJ_EINVAL;
			val = *(int*)optval;
			if(val < 128*1024) //128k
				return PJ_EINVAL;
			g_p2p_global.max_recv_len = val;
			return PJ_SUCCESS;
		}
	case P2P_MAX_CLIENT_COUNT:
		{
			int val;
			if(optlen != sizeof(int))
				return PJ_EINVAL;
			val = *(int*)optval;
			if(val < MAX_CLIENT_COUNT)
				return PJ_EINVAL;
			g_p2p_global.max_client_count = val;
			return PJ_SUCCESS;
		}
		break;
	case P2P_ENABLE_RELAY:
		{
			int val;
			if(optlen != sizeof(int))
				return PJ_EINVAL;
			val = *(int*)optval;
			g_p2p_global.enable_relay = val;
			return PJ_SUCCESS;
		}
	case P2P_ONLY_RELAY:
		{
			int val;
			if(optlen != sizeof(int))
				return PJ_EINVAL;
			val = *(int*)optval;
			if(val == 1)
				g_p2p_global.enable_relay = 1;
			g_p2p_global.only_relay = val;
			return PJ_SUCCESS;
		}
	case P2P_SMOOTH_SPAN:
		{
			int val;
			if(optlen != sizeof(int))
				return PJ_EINVAL;
			val = *(int*)optval;
			if(val < 0)
				return PJ_EINVAL;
			g_p2p_global.smooth_span = val;
			if(g_p2p_global.smooth_span >0 && g_p2p_global.smooth_span<P2P_SMOOTH_MIN_SPAN)
				g_p2p_global.smooth_span = P2P_SMOOTH_MIN_SPAN;
			return PJ_SUCCESS;
		}
	case P2P_PORT_GUESS:
		{
			int val;
			if(optlen != sizeof(int))
				return PJ_EINVAL;
			val = *(int*)optval;
			g_p2p_global.enable_port_guess = val;
			return PJ_SUCCESS;
		}
	case P2P_BIND_PORT:
		{
			pj_uint16_t val;
			if(optlen != sizeof(pj_uint16_t))
				return PJ_EINVAL;
			val = *(pj_uint16_t*)optval;
			g_p2p_global.bind_port = val;
			return PJ_SUCCESS;
		}
	default:
		return PJ_EINVALIDOP;
	}
}
