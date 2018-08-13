#include <pjnath/p2p_upnp.h>
#include <pjnath/p2p_global.h>
#include <miniupnpc.h>
#include <upnpcommands.h>
#include <upnperrors.h>
#include <pj/sock.h>

#define UPNP_BASE_PORT 10000
#define MAX_UPNP_ERROR_TIMES 3

#define USE_P2P_UPNP 1

typedef struct p2p_upnp_item
{
	pj_uint16_t port;
	struct p2p_upnp_item* next;
}p2p_upnp_item;

typedef struct p2p_upnp 
{
	pj_pool_t	   *pool; /*memory pool used by p2p_upnp*/
	pj_thread_t	   *thread;/*upnp thread*/
	pj_bool_t		thread_quit_flag;
	pj_grp_lock_t  *grp_lock;  /**< Group lock.*/
	pj_event_t      *thread_event;

	p2p_upnp_item*  first_upnp_item;
	pj_uint32_t upnp_item_count;

	pj_bool_t upnp_valid;
	pj_uint16_t upnp_port;
}p2p_upnp;

static p2p_upnp g_p2p_upnp;

//one minute
#define SLEEP_TIMES 120
#define SLEEP_MSEC 500

#define RAND_UPNP_PORT 1
#define UPNP_RAND_BASE_PORT 1024

PJ_INLINE(void) sleep_wait()
{
	int i;
	for(i=0; i<SLEEP_TIMES; i++)
	{
		if(g_p2p_upnp.thread_quit_flag)
			break;
		pj_thread_sleep(SLEEP_MSEC);
	}
}
/*
p2p upnp thread
*/
static int p2p_upnp_thread(void *unused)
{
	struct UPNPDev * devlist = 0;
	const char * multicastif = 0;
	const char * minissdpdpath = 0;
	int ipv6 = 0;
	int error = 0;
	struct UPNPUrls urls;
	struct IGDdatas data;
	char lanaddr[64];	/* my ip address on the LAN */
	int result, i, add_count = 0;
	static const char proto_udp[4] = { 'U', 'D', 'P', 0};
	char port_str[64];
	int err_times = 0;

#ifndef USE_P2P_UPNP
	return 0;
#endif

	PJ_UNUSED_ARG(unused);
	PJ_LOG(4, ("p2p_upnp_thread", "p2p_upnp_thread begin"));
	
	while(!g_p2p_upnp.thread_quit_flag)
	{
		//Discover upnp device
		devlist = upnpDiscover(2000, multicastif, minissdpdpath,0, ipv6, &error);
		if(!devlist)
		{
			sleep_wait();
			continue;
		}
		//get upnp information
		urls.controlURL = 0;
		result = UPNP_GetValidIGD(devlist, &urls, &data, lanaddr, sizeof(lanaddr));
		if(result != 1)
		{
			if(urls.controlURL)
				FreeUPNPUrls(&urls);
			freeUPNPDevlist(devlist); 
			sleep_wait();
			continue;
		}

		g_p2p_upnp.upnp_valid = PJ_TRUE;
		err_times = 0;
		while (!g_p2p_upnp.thread_quit_flag) 
		{		
			pj_grp_lock_acquire(g_p2p_upnp.grp_lock);
			add_count = UPNP_ITEM_RESERVE_COUNT - g_p2p_upnp.upnp_item_count;
			pj_grp_lock_release(g_p2p_upnp.grp_lock);

			for(i=0; i<add_count; i++)
			{
				sprintf(port_str, "%d", g_p2p_upnp.upnp_port);
				result = UPNP_AddPortMapping(urls.controlURL, data.first.servicetype,
					port_str, port_str, lanaddr, 0, proto_udp, 0, "0");

				if(result == UPNPCOMMAND_SUCCESS)
				{
					free_p2p_upnp_port(g_p2p_upnp.upnp_port);
					g_p2p_upnp.upnp_port++;
					err_times = 0;
				}
				else
				{
					//if error times more then MAX_UPNP_ERROR_TIMES, try again discover upnp device
					err_times++;
				}
				if(g_p2p_upnp.thread_quit_flag || err_times >= MAX_UPNP_ERROR_TIMES)
					break;
			}

			if(g_p2p_upnp.thread_quit_flag || err_times >= MAX_UPNP_ERROR_TIMES)
				break;

			if(g_p2p_upnp.upnp_item_count >= UPNP_ITEM_RESERVE_COUNT)
				pj_event_wait(g_p2p_upnp.thread_event);
		}

		FreeUPNPUrls(&urls);
		freeUPNPDevlist(devlist); 

		g_p2p_upnp.upnp_valid = PJ_FALSE;
	}
	
	PJ_LOG(4, ("p2p_upnp_thread", "p2p_upnp_thread end"));
	return 0;
}

//callback to free memory of p2p_tcp_listen_proxy
static void p2p_upnp_on_destroy(void *obj)
{
	PJ_UNUSED_ARG(obj);
	pj_pool_release(g_p2p_upnp.pool);
}

pj_status_t create_p2p_upnp()
{
	/* Create memory pool */
	pj_status_t status = PJ_SUCCESS;
	pj_sockaddr local_addr;
#ifdef RAND_UPNP_PORT
	pj_time_val now;
#endif
	pj_bzero(&g_p2p_upnp, sizeof(p2p_upnp));

	/* Get the default address,use local host ip as seed, calculate first upnp map port.
	Guarantee the same first port on every time running
	*/
	status = pj_gethostip(pj_AF_INET(), &local_addr);
	if(status != PJ_SUCCESS)
		return status;

#ifdef RAND_UPNP_PORT
	pj_gettimeofday(&now);
	pj_srand( (unsigned)now.sec );
	g_p2p_upnp.upnp_port = pj_rand() % (65535-UPNP_RAND_BASE_PORT) + UPNP_RAND_BASE_PORT;
#else
	if (local_addr.addr.sa_family == PJ_AF_INET6)
		g_p2p_upnp.upnp_port = UPNP_BASE_PORT + local_addr.ipv6.sin6_addr.u6_addr32[3] % UPNP_BASE_PORT;
	else
		g_p2p_upnp.upnp_port = UPNP_BASE_PORT + local_addr.ipv4.sin_addr.s_addr % UPNP_BASE_PORT;
#endif	

	g_p2p_upnp.pool = pj_pool_create(&get_p2p_global()->caching_pool.factory, "p2p_upnp", 64, 64, NULL);

	status = pj_grp_lock_create(g_p2p_upnp.pool, NULL, &g_p2p_upnp.grp_lock);
	if(status != PJ_SUCCESS)
	{
		pj_pool_release(g_p2p_upnp.pool);
		return status;
	}

	pj_grp_lock_add_ref(g_p2p_upnp.grp_lock);
	pj_grp_lock_add_handler(g_p2p_upnp.grp_lock, g_p2p_upnp.pool, 0, &p2p_upnp_on_destroy);

	status = pj_event_create(g_p2p_upnp.pool, "p2p_upnp_event", PJ_FALSE, PJ_FALSE, &g_p2p_upnp.thread_event);
	if(status != PJ_SUCCESS)
	{
		pj_grp_lock_dec_ref(g_p2p_upnp.grp_lock);
		return status;
	}

	/*create upnp thread */
	status = pj_thread_create(g_p2p_upnp.pool, "p2p_upnp", &p2p_upnp_thread, NULL, 0, 0, &g_p2p_upnp.thread);
	if(status != PJ_SUCCESS)
	{
		pj_event_destroy(g_p2p_upnp.thread_event);
		pj_grp_lock_dec_ref(g_p2p_upnp.grp_lock);
		return status;
	}

	return PJ_SUCCESS;
}

pj_bool_t malloc_p2p_upnp_port(pj_uint16_t* port)
{
	p2p_upnp_item* item;

	//create_p2p_upnp may be failed, so check it.
	if(g_p2p_upnp.upnp_valid == PJ_FALSE)
		return PJ_FALSE;

	pj_grp_lock_acquire(g_p2p_upnp.grp_lock);
	if(g_p2p_upnp.upnp_valid == PJ_FALSE || g_p2p_upnp.upnp_item_count == 0 || g_p2p_upnp.first_upnp_item == NULL)
	{
		pj_grp_lock_release(g_p2p_upnp.grp_lock);
		return PJ_FALSE;
	}
	item = g_p2p_upnp.first_upnp_item;
	g_p2p_upnp.first_upnp_item = item->next;
	g_p2p_upnp.upnp_item_count--;
	pj_grp_lock_release(g_p2p_upnp.grp_lock);

	pj_event_set(g_p2p_upnp.thread_event);//active upnp thread, map more upnp port
	*port = item->port;
	p2p_free(item);
	return PJ_TRUE;
}

void free_p2p_upnp_port(pj_uint16_t port)
{
	p2p_upnp_item* item = (p2p_upnp_item*)p2p_malloc(sizeof(p2p_upnp_item));
	item->port = port;

	pj_grp_lock_acquire(g_p2p_upnp.grp_lock);
	item->next = g_p2p_upnp.first_upnp_item;
	g_p2p_upnp.first_upnp_item = item;
	g_p2p_upnp.upnp_item_count++;
	pj_grp_lock_release(g_p2p_upnp.grp_lock);
}

void destroy_p2p_upnp()
{
	p2p_upnp_item* item;
	PJ_LOG(4, ("destroy_p2p_upnp", "destroy_p2p_upnp begin"));

	if (!g_p2p_upnp.thread)
		return;

	g_p2p_upnp.thread_quit_flag = PJ_TRUE;
	pj_event_set(g_p2p_upnp.thread_event);
	
	pj_thread_join(g_p2p_upnp.thread);
	pj_thread_destroy(g_p2p_upnp.thread);

	pj_grp_lock_acquire(g_p2p_upnp.grp_lock);
	while(g_p2p_upnp.first_upnp_item)
	{
		item = g_p2p_upnp.first_upnp_item;
		g_p2p_upnp.first_upnp_item = g_p2p_upnp.first_upnp_item->next;
		p2p_free(item);
	}
	pj_event_destroy(g_p2p_upnp.thread_event);
	g_p2p_upnp.upnp_valid = PJ_FALSE;
	g_p2p_upnp.upnp_item_count = 0;

	pj_grp_lock_release(g_p2p_upnp.grp_lock);

	pj_grp_lock_dec_ref(g_p2p_upnp.grp_lock);
	PJ_LOG(4, ("destroy_p2p_upnp", "destroy_p2p_upnp end"));
}