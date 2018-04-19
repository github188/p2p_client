#include <pjnath/p2p_global.h>
#include <pjnath/p2p_udt.h>
#include <pjnath/socket_pair.h>
#include <pjnath/p2p_tcp_proxy.h>
#include <pjnath/mem_allocator.h>
#include <pjnath/p2p_pool.h>
#include <pjnath/p2p_tcp.h>
#include <pjnath/p2p_port_guess.h>

#ifdef WIN32
#include <WinSock2.h>
#pragma warning(disable:4251)
#endif

#ifndef USE_P2P_TCP
#include <udt.h>
#endif

#define DECL_UDT_BASE \
	pj_uint32_t magic; /*magic data, must is P2P_DATA_MAGIC*/\
	pj_pool_t *pool; /*memory manage poll*/ \
	pj_grp_lock_t  *grp_lock;  /**< Group lock.*/ \
	pj_bool_t destroy_req;/*To prevent duplicate destroy*/ \
	void* user_data; /*user data, it is pj_ice_strans_p2p_conn*/ \
	p2p_udt_cb udt_cb; /*udt socket call back*/ \
	p2p_tcp_data* first_send_data; /*fist cache data for pending send*/ \
	p2p_tcp_data* last_send_data; /*last cache data for pending send*/ \
	pj_bool_t pause_send_data; /*if cache data too many, pause receive user data*/ \
	pj_event_t *send_event;\
	pj_uint16_t pkg_seq; \
	pj_uint8_t pause_recv;\

typedef struct p2p_udt_connector
{
	DECL_UDT_BASE

#ifdef USE_P2P_TCP
	p2p_tcp_sock* p2p_tcp;
#else
	UDT_P2P::UDTSOCKET udt_sock; /*udt socket*/ 
#endif

	pj_thread_t* connect_thread;
}p2p_udt_connector;

typedef struct p2p_udt_accepter
{
	DECL_UDT_BASE

#ifdef USE_P2P_TCP
		p2p_tcp_sock* p2p_tcp;
#else
		UDT_P2P::UDTSOCKET udt_sock; /*udt socket*/ 
#endif
}p2p_udt_accepter;

typedef struct p2p_udt_listener
{
	pj_uint32_t magic; /*magic data, must is P2P_DATA_MAGIC*/

	pj_pool_t *pool; /*memory manage poll*/

	pj_grp_lock_t  *grp_lock;  /**< Group lock.*/

	pj_bool_t	 destroy_req;//To prevent duplicate destroy

	void* user_data;//user data, it is p2p_transport

#ifndef USE_P2P_TCP
 	UDT_P2P::UDTSOCKET udt_sock;//udt socket
#endif

	p2p_udt_cb udt_cb;//udt socket call back
}p2p_udt_listener;

//#define UDT_GRP_LOCK_LOG 1

#ifdef UDT_GRP_LOCK_LOG
static pj_status_t udt_grp_lock_acquire_log(const char* file, int line, pj_grp_lock_t *grp_lock)
{
	PJ_LOG(3, ("grp_lock", "udt_grp_lock_acquire %s %d %p", file, line, grp_lock));
	return pj_grp_lock_acquire(grp_lock);
}
static pj_status_t udt_grp_lock_release_log(const char* file, int line, pj_grp_lock_t *grp_lock)
{
	PJ_LOG(3, ("grp_lock", "udt_grp_lock_release %s %d %p", file, line, grp_lock));
	return pj_grp_lock_release(grp_lock);
}

#define udt_grp_lock_acquire(grp_lock) udt_grp_lock_acquire_log(__FILE__, __LINE__, grp_lock)
#define udt_grp_lock_release(grp_lock) udt_grp_lock_release_log(__FILE__, __LINE__, grp_lock)
#else

#define udt_grp_lock_acquire pj_grp_lock_acquire
#define udt_grp_lock_release pj_grp_lock_release

#endif



#ifdef USE_P2P_POOL
#define UDT_RECV_DATA_LEN (sizeof(p2p_tcp_proxy_header) + TCP_SOCK_PACKAGE_SIZE)
#else
#define UDT_RECV_DATA_LEN ((sizeof(p2p_tcp_proxy_header) + TCP_SOCK_PACKAGE_SIZE)*4)
#endif

#ifndef USE_P2P_POOL

struct udt_recved_base
{
public:
	virtual void call_back(char* buffer, int buffer_len) = 0;
	void* t;
};

template<typename T>
class udt_recved_impl :public udt_recved_base
{
public:
	virtual void call_back(char* buffer, int buffer_len)
	{
		T* udt_obj = (T*)t;
		if(udt_obj == NULL || udt_obj->magic != P2P_DATA_MAGIC)
			return;
		if(udt_obj->destroy_req)
		{
			pj_grp_lock_dec_ref(udt_obj->grp_lock); //add in udt_obj_on_recv
			return;
		}
		udt_obj->udt_cb.udt_on_recved(udt_obj->user_data, buffer, buffer_len);
		pj_grp_lock_dec_ref(udt_obj->grp_lock); //add in udt_obj_on_recv
	}
};

//#if _WIN32
//	#define UDT_USE_MEM_ALLOCATOR 1
//#endif

typedef struct p2p_udt_recved_datas
{
	pj_thread_t* thread; //udt received data thread
	int run; 
	udt_recved_data* first_data; /*fist cache data for pending recv*/ 
	udt_recved_data* last_data; /*last cache data for pending recv*/
	pj_mutex_t *data_mutex;
	pj_event_t *data_event;

#ifdef UDT_USE_MEM_ALLOCATOR
	fixed_size_allocator* mem_allocator;
#endif

}p2p_udt_recved_datas;

p2p_udt_recved_datas udt_recved_datas = {
	0,
	0, 
	NULL, 
	NULL,
	NULL, 
	NULL, 
#ifdef UDT_USE_MEM_ALLOCATOR
	NULL
#endif
};
static int udt_async_recved_thread(void *unused);

static void free_udt_recved_data(udt_recved_data* data)
{
	if(data->base)
		delete data->base;
#ifdef UDT_USE_MEM_ALLOCATOR
	free_fixed_size_block(udt_recved_datas.mem_allocator, (unsigned char*)data);
#else
	p2p_free(data);
#endif
}

static udt_recved_data* malloc_udt_recved_data()
{
#ifdef UDT_USE_MEM_ALLOCATOR
	udt_recved_data* data = (udt_recved_data*)alloc_fixed_size_block(udt_recved_datas.mem_allocator);
#else
	udt_recved_data* data = (udt_recved_data*)p2p_malloc(UDT_RECV_DATA_LEN+sizeof(udt_recved_data));
#endif
	data->buffer = (char*)data + sizeof(udt_recved_data);
	data->buffer_len = UDT_RECV_DATA_LEN;
	data->base = NULL;
	data->next = NULL;
	return data;
}
#endif // #ifndef USE_P2P_POOL 

pj_status_t p2p_udt_init()
{
#ifndef USE_P2P_TCP
	UDT_P2P::UDT::startup(get_p2p_global()->pool_udt);
#endif

#ifndef USE_P2P_POOL
	udt_recved_datas.thread = 0;
	udt_recved_datas.run = 1;
	udt_recved_datas.first_data = udt_recved_datas.last_data = 0;
	pj_event_create(get_p2p_global()->pool, "udt_recved", PJ_FALSE, PJ_FALSE, &udt_recved_datas.data_event);
	pj_mutex_create_recursive(get_p2p_global()->pool, NULL, &udt_recved_datas.data_mutex);
	pj_thread_create(get_p2p_global()->pool, "udt_recved", &udt_async_recved_thread, NULL, 0, 0, &udt_recved_datas.thread);

#ifdef UDT_USE_MEM_ALLOCATOR
	udt_recved_datas.mem_allocator = create_fixed_size_allocator(UDT_RECV_DATA_LEN+sizeof(udt_recved_data), 16);
#endif

#endif //#ifndef USE_P2P_POOL

	return PJ_SUCCESS;
}

void p2p_udt_uninit()
{
#ifndef USE_P2P_POOL
	udt_recved_data* data;
#endif

#ifndef USE_P2P_TCP
	PJ_LOG(4, ("p2p_udt_uninit", "begin p2p_udt_uninit"));
	UDT_P2P::UDT::cleanup();
	PJ_LOG(4, ("p2p_udt_uninit", "cleanup end"));
#endif

#ifndef USE_P2P_POOL
	udt_recved_datas.run = 0;
	if(udt_recved_datas.data_event)
		pj_event_set(udt_recved_datas.data_event);
	if (udt_recved_datas.thread) {
		pj_thread_join(udt_recved_datas.thread);
		pj_thread_destroy(udt_recved_datas.thread);
		udt_recved_datas.thread = 0;
	}
	
	PJ_LOG(4, ("p2p_udt_uninit", "free_udt_recved_data"));

	if(udt_recved_datas.data_mutex)
		pj_mutex_lock(udt_recved_datas.data_mutex);
	data = udt_recved_datas.first_data;
	while(data)
	{
		udt_recved_data* cur = data;
		data = data->next;
		free_udt_recved_data(cur);
	}
	udt_recved_datas.first_data = udt_recved_datas.last_data = 0;
	if(udt_recved_datas.data_mutex)
		pj_mutex_unlock(udt_recved_datas.data_mutex);

	PJ_LOG(4, ("p2p_udt_uninit", "pj_event_destroy"));
	if(udt_recved_datas.data_event)
		pj_event_destroy(udt_recved_datas.data_event);
	PJ_LOG(4, ("p2p_udt_uninit", "pj_mutex_destroy"));
	if(udt_recved_datas.data_mutex)
		pj_mutex_destroy(udt_recved_datas.data_mutex);
#ifdef UDT_USE_MEM_ALLOCATOR
	if(udt_recved_datas.mem_allocator)
		destroy_fixed_size_allocator(udt_recved_datas.mem_allocator);
#endif 

#endif //#ifndef USE_P2P_POOL

	PJ_LOG(4, ("p2p_udt_uninit", "end p2p_udt_uninit"));
}

template<typename T>
PJ_INLINE(pj_bool_t) udt_obj_is_valid(T* t)
{
	if(t==NULL || t->destroy_req || t->magic != P2P_DATA_MAGIC)
		return PJ_FALSE;
	
	//udt_grp_lock_acquire(t->grp_lock);
	//if(t->destroy_req)
	//{
	//	udt_grp_lock_release(t->grp_lock);
	//	return PJ_FALSE;
	//}
	//udt_grp_lock_release(t->grp_lock);
	return PJ_TRUE;
}

//use ice of pj_ice_strans_p2p_conn, udt socket send data to remote ice 
template<typename T>
static int udt_obj_send_cb(const sockaddr* addr, const char* buf, int buf_len, void* user_data)
{
	T* t = (T*)user_data;
	if(!udt_obj_is_valid(t))
		return -1;
	if( t->udt_cb.udt_send(t->user_data, addr, buf, buf_len) == PJ_SUCCESS)
	{
		return buf_len;
	}
	else
	{
		return -1;
	}
}
template<typename T>
void udt_obj_on_noresned_recved(void *user_data, const char* buffer, int buffer_len)
{
	T* t = (T*)user_data;
	t->udt_cb.udt_on_noresend_recved(t->user_data, buffer, buffer_len);
}


#ifndef USE_P2P_POOL
static int udt_async_recved_thread(void *unused)
{
	PJ_UNUSED_ARG(unused);
	while (udt_recved_datas.run)
	{
		udt_recved_data* data = 0;

		pj_mutex_lock(udt_recved_datas.data_mutex);

		if(udt_recved_datas.first_data)
		{
			data = udt_recved_datas.first_data;
			udt_recved_datas.first_data = udt_recved_datas.first_data->next;
			if(udt_recved_datas.first_data == 0)
				udt_recved_datas.last_data = 0;
		}

		pj_mutex_unlock(udt_recved_datas.data_mutex);

		if(data == 0)
			pj_event_wait(udt_recved_datas.data_event);
		else
		{
			data->base->call_back(data->buffer, data->buffer_len);
			free_udt_recved_data(data);
		}
	}
	return 0;
}

//receive data from udt socket
//udt socket is asynchronous recv, if udt socket receive buffer data is empty, immediately return -1
template<typename T>
static void udt_obj_on_recv(void* data)
{
	T* t = (T*)data;
	if(!udt_obj_is_valid(t))
		return;
	
	while(true && !t->pause_recv)//t->pause_recv user pause receive data
	{		
		udt_recved_data* recved_data = malloc_udt_recved_data();
#ifdef USE_P2P_TCP
		int recv_len = recved_data->buffer_len = p2p_tcp_recv(t->p2p_tcp, recved_data->buffer, recved_data->buffer_len);
#else
		int recv_len = recved_data->buffer_len = UDT_P2P::UDT::recv(t->udt_sock, recved_data->buffer, recved_data->buffer_len, 0);
#endif
		if(recved_data->buffer_len<=0)
		{
			free_udt_recved_data(recved_data);
			break;
		}
		recved_data->base = new udt_recved_impl<T>();
		recved_data->base->t = t;
		pj_grp_lock_add_ref(t->grp_lock); //add ref for multi-thread,release in udt_recved_impl

		//add to async list
		pj_mutex_lock(udt_recved_datas.data_mutex);

		if(udt_recved_datas.last_data)
		{
			udt_recved_datas.last_data->next = recved_data;
			udt_recved_datas.last_data = recved_data;
		}
		else
			udt_recved_datas.first_data = udt_recved_datas.last_data = recved_data;

		pj_mutex_unlock(udt_recved_datas.data_mutex);

		pj_event_set(udt_recved_datas.data_event);

		if(recv_len < UDT_RECV_DATA_LEN)
			break;
	}
}
#else

//receive data from udt socket
//udt socket is asynchronous recv, if udt socket receive buffer data is empty, immediately return -1
template<typename T>
static void on_io_thread_udt_obj_recv(void* data)
{
	T* t = (T*)data;
	if(!udt_obj_is_valid(t))
		return;
	
	while(true && !t->pause_recv) //t->pause_recv user pause receive data
	{	
		char read_buffer[UDT_RECV_DATA_LEN];
#ifdef USE_P2P_TCP
		int result = p2p_tcp_recv(t->p2p_tcp, read_buffer, UDT_RECV_DATA_LEN);
#else
		int result = UDT_P2P::UDT::recv(t->udt_sock, read_buffer, UDT_RECV_DATA_LEN, 0);
#endif
		if(result<=0)
			break;

		t->udt_cb.udt_on_recved(t->user_data, read_buffer, result);
	}
}

//schedule socket_pair for recv udt data in network io thread 
template<typename T>
static void udt_obj_on_recv(void* user_data)
{
	T* t = (T*)user_data;
	p2p_socket_pair_item item;
	if(t->pause_recv)
		return;
	item.cb = on_io_thread_udt_obj_recv<T>;
	item.data = user_data;
	schedule_socket_pair(get_p2p_global()->sock_pair, &item);
}

#endif //#ifndef USE_P2P_POOL

template<typename T>
static void udt_obj_get_sock_addr(sockaddr* addr, void* user_data)
{
	T* t = (T*)user_data;
	if(!udt_obj_is_valid<T>(t))
		return;
	if(t && t->udt_cb.get_sock_addr)
		t->udt_cb.get_sock_addr(addr, t->user_data);
}

template<typename T>
static void udt_obj_get_peer_addr(sockaddr* addr, void* user_data)
{
	T* t = (T*)user_data;
	if(!udt_obj_is_valid<T>(t))
		return;
	if(t && t->udt_cb.get_peer_addr)
		t->udt_cb.get_peer_addr(addr, t->user_data);
}

template<typename T>
static void udt_obj_on_close( void* user_data)
{
	T* t = (T*)user_data;

	PJ_LOG(4,("p2p_udt", "udt_obj_on_close %p", t));

	if(!udt_obj_is_valid<T>(t))
		return;
	if(t && t->udt_cb.udt_on_close)
		t->udt_cb.udt_on_close(t->user_data);

	PJ_LOG(4,("p2p_udt", "udt_obj_on_close %p end", t));
}


template<typename T>
static void on_udt_obj_pause_send(void* data)
{
	T* t = (T*)data;
	pj_bool_t pause_send = PJ_FALSE;

	if (t == NULL || t->destroy_req || t->magic != P2P_DATA_MAGIC) 
		return;
	udt_grp_lock_acquire(t->grp_lock);
	if (t->destroy_req) 
	{ 
		//already destroy, so return
		udt_grp_lock_release(t->grp_lock);
		return;
	}
	pause_send = t->pause_send_data;
	udt_grp_lock_release(t->grp_lock);

	if(pause_send && t->udt_cb.udt_pause_send)
		t->udt_cb.udt_pause_send(t->user_data, PJ_TRUE);
}

template<typename T>
static void on_udt_obj_continue_send(void* data)
{
	T* t = (T*)data;
	pj_bool_t pause_send = PJ_FALSE;

	if (t==NULL || t->destroy_req || t->magic != P2P_DATA_MAGIC) 
		return;
	udt_grp_lock_acquire(t->grp_lock);
	if (t->destroy_req) 
	{ 
		//already destroy, so return
		udt_grp_lock_release(t->grp_lock);
		return;
	}
	pause_send = t->pause_send_data;
	udt_grp_lock_release(t->grp_lock);

	if(!pause_send && t->udt_cb.udt_pause_send)
		t->udt_cb.udt_pause_send(t->user_data, PJ_FALSE);
}

template<typename T>
static void udt_obj_on_send(void* user_data)
{
	T* t = (T*)user_data;
	int result;
	p2p_tcp_data* send_data;
	pj_bool_t continue_send = PJ_TRUE;

	if (t== NULL || t->destroy_req || t->magic != P2P_DATA_MAGIC) 
		return;

	udt_grp_lock_acquire(t->grp_lock);
	if (t->destroy_req) 
	{ 
		//already destroy, so return
		udt_grp_lock_release(t->grp_lock);
		return;
	}
	while(t->first_send_data)
	{
		send_data = t->first_send_data;
		pj_grp_lock_add_ref(t->grp_lock);//for multi-thread, add ref
		udt_grp_lock_release(t->grp_lock); 

#ifdef USE_P2P_TCP
		result = p2p_tcp_send(t->p2p_tcp, send_data->buffer+send_data->pos, send_data->buffer_len-send_data->pos);
#else
		result = UDT_P2P::UDT::send(t->udt_sock, send_data->buffer+send_data->pos, send_data->buffer_len-send_data->pos, 0);
#endif
		udt_grp_lock_acquire(t->grp_lock);
		pj_grp_lock_dec_ref(t->grp_lock);//for multi-thread, add ref
		if(result > 0)
		{
			if((size_t)result < send_data->buffer_len-send_data->pos)//only send part of data
			{
				continue_send = PJ_FALSE;
				send_data->pos += result;
				break;
			}
			else
			{
				t->first_send_data = send_data->next;//the data is send, then send next data
				free_p2p_tcp_data(send_data);
				continue_send = PJ_TRUE;
			}			
		}
		else//udt socket send buffer is full
		{
			continue_send = PJ_FALSE;
			break;
		}
	}

	if(t->first_send_data == NULL)//all cache data is sent
		t->last_send_data = NULL;

	if(continue_send)//notify p2p connection continue receive user's tcp data
	{
		if(t->pause_send_data)
			t->pause_send_data = PJ_FALSE;
		else
			continue_send = PJ_FALSE;
	}
	udt_grp_lock_release(t->grp_lock);
	if(continue_send)
	{
		pj_event_set(t->send_event);
		p2p_socket_pair_item item;
		item.cb = on_udt_obj_continue_send<T>;
		item.data = t;
		schedule_socket_pair(get_p2p_global()->sock_pair, &item);
	}
}

template<typename T>
static pj_status_t p2p_udt_obj_send(T* t, const char* buffer, size_t buffer_len)
{
#ifdef USE_P2P_TCP
	int result = -1;
#else
	int result = UDT_P2P::UDT::ERROR;
#endif
	pj_bool_t pause_send = PJ_FALSE;

#ifdef USE_P2P_TCP
	if(t->p2p_tcp)
#else
	if(t->udt_sock != UDT_P2P::UDT::INVALID_SOCK)
#endif
	{
		udt_grp_lock_acquire(t->grp_lock);
		if (t->destroy_req) 
		{ 
			//already destroy, so return
			udt_grp_lock_release(t->grp_lock);
			return PJ_EEOF;
		}

		if(t->first_send_data)//if cached data, add data to cache data list
		{
			p2p_tcp_data* data = malloc_p2p_tcp_data(buffer, buffer_len);
			t->last_send_data->next = data;
			t->last_send_data = data;
			pause_send = PJ_TRUE;
		}
		else
		{
#ifdef USE_P2P_TCP
			result = p2p_tcp_send(t->p2p_tcp, buffer, buffer_len);
#else
			result = UDT_P2P::UDT::send(t->udt_sock, buffer, buffer_len, 0);
#endif
			if(result > 0)
			{
				if((size_t)result < buffer_len)//only send part of data
				{
					t->first_send_data = t->last_send_data = malloc_p2p_tcp_data(buffer+result, buffer_len-result);
					pause_send = PJ_TRUE;
				}
			}
			else//udt socket send buffer is full, add data to cache data list
			{
				t->first_send_data = t->last_send_data = malloc_p2p_tcp_data(buffer, buffer_len);
				pause_send = PJ_TRUE;
			}			
		}

		if(pause_send)
		{
			if(t->pause_send_data == PJ_FALSE)
				t->pause_send_data = PJ_TRUE;
		}
		udt_grp_lock_release(t->grp_lock);
		if(pause_send)
		{
			/*p2p_socket_pair_item item;
			item.cb = on_udt_obj_pause_send<T>;
			item.data = t;
			schedule_socket_pair(get_p2p_global()->sock_pair, &item);*/
			//udt send buffer is full, return -1,notify caller udt socket blocked
			PJ_LOG(5,("p2p_udt_obj_send", "udt buffer is full, pause send tcp data %p", t));
			return -1;
		}
		return PJ_SUCCESS;
	}
	else
	{
		return PJ_EEOF;
	}
}

//callback to free memory of p2p_udt_connector
static void p2p_udt_connector_on_destroy(void *obj)
{
	p2p_udt_connector *connector = (p2p_udt_connector*)obj;
	PJ_LOG(4,("p2p_udtc", "p2p_udt_connector_on_destroy %p", connector));
	pj_event_destroy(connector->send_event);
	delay_destroy_pool(connector->pool);
}

#ifndef USE_P2P_TCP

static void on_io_thead_udt_connected(void* data)
{
	p2p_udt_connector* connector = (p2p_udt_connector*)data;
	PJ_LOG(4,("on_io_thead_udt_connected", "pj_thread_join %p %d", connector));
	pj_thread_join(connector->connect_thread);
	pj_thread_destroy(connector->connect_thread);
	PJ_LOG(4,("on_io_thead_udt_connected", "pj_thread_join %p %d end", connector));
	
	if(connector->udt_cb.udt_on_connect)
		(connector->udt_cb.udt_on_connect)(connector->user_data, connector->udt_sock != UDT_P2P::UDT::INVALID_SOCK);
	
	pj_grp_lock_dec_ref(connector->grp_lock);//in async_udt_connect function add reference, so release it
}

//local udt connect to remote udt, UDT_P2P::UDT::connect is blocked, so create a thread to connect
static int p2p_udt_connect_thread(void *arg)
{
	p2p_udt_connector* connector = (p2p_udt_connector*)arg;
	int result;
	pj_sockaddr remote_addr;
	p2p_socket_pair_item item;
	UDT_P2P::UDTSOCKET udt_sock = UDT_P2P::UDT::INVALID_SOCK;
	char addr_info[PJ_INET6_ADDRSTRLEN+10];
	PJ_LOG(4, ("p2p_udt_connect_thread", "p2p_udt_connect_thread begin"));

	if(!udt_obj_is_valid(connector))
	{
		pj_grp_lock_dec_ref(connector->grp_lock);//in async_udt_connect function add reference, so release it
		return 0;
	}
	
	udt_obj_get_peer_addr<p2p_udt_connector>((sockaddr*)&remote_addr, connector);
	pj_sockaddr_print(&remote_addr, addr_info, sizeof(addr_info), 3);
	PJ_LOG(4,("p2p_udt_connect_thread", "remote addr %s", addr_info));

	result = UDT_P2P::UDT::connect(connector->udt_sock,
		(struct sockaddr*)&remote_addr,
		pj_sockaddr_get_len(&remote_addr));

	if(result == UDT_P2P::UDT::ERROR)
	{
		udt_grp_lock_acquire(connector->grp_lock);
		if(connector->udt_sock != UDT_P2P::UDT::INVALID_SOCK)//maybe had destroyed,so check it
		{
			udt_sock = connector->udt_sock;
			connector->udt_sock = UDT_P2P::UDT::INVALID_SOCK;
		}
		udt_grp_lock_release(connector->grp_lock);

		if(udt_sock != UDT_P2P::UDT::INVALID_SOCK)
			UDT_P2P::UDT::close(udt_sock);
	}
	else
	{
		//must set asynchronous, otherwise block network io thread
		int async = 0;
		UDT_P2P::UDT::setsockopt(connector->udt_sock, 0, UDT_P2P::UDT_RCVSYN, &async, sizeof(int));
		UDT_P2P::UDT::setsockopt(connector->udt_sock, 0, UDT_P2P::UDT_SNDSYN, &async, sizeof(int));
	}
	//schedule socket_pair to net io thread, notify udt connect result
	item.cb = on_io_thead_udt_connected;
	item.data = connector;
	schedule_socket_pair(get_p2p_global()->sock_pair, &item);

	PJ_LOG(4, ("p2p_udt_connect_thread", "p2p_udt_connect_thread end"));
	return 0;
}

pj_status_t async_udt_connect(p2p_udt_connector* connector)
{
	/*create connect thread */
	pj_grp_lock_add_ref(connector->grp_lock);//release in on_io_thead_udt_connected or p2p_udt_connect_thread
	return pj_thread_create(connector->pool, 
		"udt_connect", 
		&p2p_udt_connect_thread,
		connector, 
		0, 
		0, 
		&connector->connect_thread);
}


template<typename T>
static pj_status_t p2p_udt_set_opt(T* t, p2p_opt opt, const void* optval, int optlen)
{
	if(!udt_obj_is_valid(t))
		return PJ_EGONE;
	int val;
	switch(opt)
	{
	case P2P_SNDBUF:
		if(optlen < sizeof(int))
			return PJ_EINVAL;
		val = *(int*)optval;
		if(val < P2P_MIN_BUFFER_SIZE)
			val = P2P_MIN_BUFFER_SIZE;
		if(val > P2P_SEND_BUFFER_SIZE)
			val = P2P_SEND_BUFFER_SIZE;
		UDT_P2P::UDT::setsockopt(t->udt_sock, 0, UDT_P2P::UDT_SNDBUF, &val, sizeof(int));
		break;
	case P2P_RCVBUF:
		if(optlen < sizeof(int))
			return PJ_EINVAL;

		val = *(int*)optval;
		if(val < P2P_MIN_BUFFER_SIZE)
			val = P2P_MIN_BUFFER_SIZE;
		if(val > P2P_RECV_BUFFER_SIZE)
			val = P2P_RECV_BUFFER_SIZE;
		UDT_P2P::UDT::setsockopt(t->udt_sock, 0, UDT_P2P::UDT_RCVBUF, &val, sizeof(int));
		break;
	default:
		return PJ_EINVALIDOP;
	}
	return PJ_SUCCESS;
}
#else

template<typename T>
static pj_status_t p2p_udt_set_opt(T* t, p2p_opt opt, const void* optval, int optlen)
{
	if(!udt_obj_is_valid(t))
		return PJ_EGONE;
	switch(opt)
	{
	case P2P_PAUSE_RECV:
		if(optval == NULL || optlen != 1)
			return PJ_EINVAL;
		t->pause_recv = *(pj_uint8_t*)optval;
		if(t->pause_recv == 0)
			udt_obj_on_recv<T>(t);
		break;
	default:
		return PJ_EINVALIDOP;
	}
	return PJ_SUCCESS;
}
#endif

pj_status_t create_p2p_udt_connector(p2p_udt_cb* cb, void* user_data, int send_buf_size, int recv_buf_size, pj_sock_t sock, p2p_udt_connector** connector)
{
	pj_status_t status;
	pj_pool_t *pool;
	p2p_udt_connector* udt_connector; 
#ifdef USE_P2P_TCP
	p2p_tcp_cb tcp_cb;
	pj_sockaddr remote_addr;
	char addr_info[PJ_INET6_ADDRSTRLEN+10];
	PJ_UNUSED_ARG(send_buf_size);
	PJ_UNUSED_ARG(recv_buf_size);
#endif

	pool = pj_pool_create(&get_p2p_global()->caching_pool.factory, 
		"p2p_udtc%p", 
		PJNATH_POOL_LEN_ICE_STRANS,
		PJNATH_POOL_INC_ICE_STRANS, 
		NULL);

	udt_connector = PJ_POOL_ZALLOC_T(pool, p2p_udt_connector);
	pj_bzero(udt_connector, sizeof(udt_connector));
	udt_connector->magic = P2P_DATA_MAGIC;
	udt_connector->pool = pool;
	udt_connector->user_data = user_data;
	status = pj_grp_lock_create(pool, NULL, &udt_connector->grp_lock);
	if (status != PJ_SUCCESS)
	{
		pj_pool_release(pool);
		return status;
	}

	status = pj_event_create(pool, "p2p_c_s_event", PJ_FALSE, PJ_FALSE, &udt_connector->send_event);
	if (status != PJ_SUCCESS)
	{
		pj_pool_release(pool);
		return status;
	}

	//add self reference count
	pj_grp_lock_add_ref(udt_connector->grp_lock);
	pj_grp_lock_add_handler(udt_connector->grp_lock, pool, udt_connector, &p2p_udt_connector_on_destroy);

	pj_memcpy(&udt_connector->udt_cb, cb, sizeof(p2p_udt_cb));
	*connector = udt_connector;
	//create udt socket
#ifdef USE_P2P_TCP
	tcp_cb.user_data = udt_connector;
	tcp_cb.on_close = &udt_obj_on_close<p2p_udt_connector>;
	tcp_cb.on_recved  = &udt_obj_on_recv<p2p_udt_connector>;
	tcp_cb.send = &udt_obj_send_cb<p2p_udt_connector>;
	tcp_cb.on_send = &udt_obj_on_send<p2p_udt_connector>;
	tcp_cb.on_noresned_recved = &udt_obj_on_noresned_recved<p2p_udt_connector>;

	udt_obj_get_peer_addr<p2p_udt_connector>((sockaddr*)&remote_addr, udt_connector);
	pj_sockaddr_print(&remote_addr, addr_info, sizeof(addr_info), 3);
	PJ_LOG(4,("create_p2p_udt_connector", "remote addr %s", addr_info));

	udt_connector->p2p_tcp = p2p_tcp_create(&tcp_cb, sock, &remote_addr, udt_connector->grp_lock);

	if(udt_connector->udt_cb.udt_on_connect)
		(udt_connector->udt_cb.udt_on_connect)(udt_connector->user_data, PJ_TRUE);
#else
	udt_connector->udt_sock = UDT_P2P::UDT::socket(pj_AF_INET(), pj_SOCK_STREAM(), 0);
	if(udt_connector->udt_sock == UDT_P2P::UDT::INVALID_SOCK)
	{
		destroy_p2p_udt_connector(udt_connector);
		return UDT_P2P::UDT::getlasterror_code();
	}

	p2p_udt_set_opt(udt_connector, P2P_SNDBUF, &send_buf_size, sizeof(int));
	p2p_udt_set_opt(udt_connector, P2P_RCVBUF, &recv_buf_size, sizeof(int));

	UDT_P2P::p2p_socket_cb udt_cb;
	pj_memset(&udt_cb, 0, sizeof(UDT_P2P::p2p_socket_cb));
	udt_cb.send_cb = &udt_obj_send_cb<p2p_udt_connector>;
	udt_cb.get_peer_addr = &udt_obj_get_peer_addr<p2p_udt_connector>;
	udt_cb.get_sock_addr = &udt_obj_get_sock_addr<p2p_udt_connector>;
	udt_cb.on_recv = &udt_obj_on_recv<p2p_udt_connector>;
	udt_cb.on_send = &udt_obj_on_send<p2p_udt_connector>;
	udt_cb.on_close= &udt_obj_on_close<p2p_udt_connector>;
	udt_cb.user_data = udt_connector;
	udt_cb.ice_socket = sock;
	UDT_P2P::UDT::set_p2p_call_back(udt_connector->udt_sock, &udt_cb);

	//UDT_P2P::UDT::connect is blocked, so create a thread to connect
	status = async_udt_connect(udt_connector);
	if(status != PJ_SUCCESS)
	{
		destroy_p2p_udt_connector(udt_connector);
		return status;
	}
#endif	

	return PJ_SUCCESS;
}

pj_status_t p2p_udt_connector_send(p2p_udt_connector* connector, const char* buffer, size_t buffer_len)
{
	return p2p_udt_obj_send(connector, buffer, buffer_len);
}


//received ice user data, then put the data to receive buffer of udt socket
pj_status_t p2p_udt_connector_on_recved(p2p_udt_connector* connector, 
										const char* buffer, 
										size_t buffer_len,
										const pj_sockaddr_t *src_addr,
										unsigned src_addr_len)
{
#ifdef USE_P2P_TCP	
	PJ_UNUSED_ARG(src_addr);
	PJ_UNUSED_ARG(src_addr_len);
	if(connector->p2p_tcp)
	{		
		p2p_tcp_data_recved(connector->p2p_tcp, buffer, buffer_len);
	}
#else	
	if(connector->udt_sock != UDT_P2P::UDT::INVALID_SOCK)
	{
		//PJ_LOG(4,("p2p_udt_connector", "p2p_udt_connector_on_recved %d", buffer_len));
		UDT_P2P::UDT::on_p2p_data_recved(connector->udt_sock, 
			buffer, buffer_len,
			(struct sockaddr*)src_addr,
			src_addr_len);
	}
#endif

	return PJ_SUCCESS;
}

void async_destroy_udt_obj(void *user_data)
{
	pj_grp_lock_dec_ref((pj_grp_lock_t *)user_data);
}

void destroy_p2p_udt_connector(p2p_udt_connector* connector)
{
#ifdef USE_P2P_TCP	
	p2p_tcp_sock* p2p_tcp = NULL;
#else
	UDT_P2P::UDTSOCKET udt_sock = UDT_P2P::UDT::INVALID_SOCK;
#endif
	
	p2p_tcp_data* send_data;

	PJ_LOG(4,("p2p_udtc", "destroy_p2p_udt_connector %p", connector));

	udt_grp_lock_acquire(connector->grp_lock);
	if (connector->destroy_req) { //already destroy, so return
		udt_grp_lock_release(connector->grp_lock);
		return;
	}
	connector->destroy_req = PJ_TRUE;


#ifdef USE_P2P_TCP
	if(connector->p2p_tcp)
	{
		p2p_tcp = connector->p2p_tcp;
		connector->p2p_tcp = NULL;

	}
#else
	if(connector->udt_sock != UDT_P2P::UDT::INVALID_SOCK)
	{
		udt_sock = connector->udt_sock;
		connector->udt_sock = UDT_P2P::UDT::INVALID_SOCK;
	}
#endif

	while(connector->first_send_data)
	{
		send_data = connector->first_send_data;
		connector->first_send_data = send_data->next;
		free_p2p_tcp_data(send_data);
	}
	connector->last_send_data = NULL;

	udt_grp_lock_release(connector->grp_lock);

	pj_event_set(connector->send_event);

#ifdef USE_P2P_TCP
	if(p2p_tcp)
		p2p_tcp_destory(p2p_tcp);
#else
	if(udt_sock != UDT_P2P::UDT::INVALID_SOCK)
	{
		UDT_P2P::p2p_socket_cb udt_cb;
		pj_bzero(&udt_cb, sizeof(udt_cb));//reset all call back
		UDT_P2P::UDT::set_p2p_call_back(udt_sock, &udt_cb);
		UDT_P2P::UDT::close(udt_sock);
	}
#endif
	
	//release self reference
	pj_grp_lock_dec_ref(connector->grp_lock);
	PJ_LOG(4,("p2p_udtc", "destroy_p2p_udt_connector %p end", connector));
}

//callback to free memory of p2p_udt_accepter
static void p2p_udt_accepter_on_destroy(void *obj)
{
	p2p_udt_accepter *accepter = (p2p_udt_accepter*)obj;
	PJ_LOG(4,("p2p_udtc", "p2p_udt_accepter_on_destroy %p", accepter));
	pj_event_destroy(accepter->send_event);
	delay_destroy_pool(accepter->pool);
}

pj_status_t create_p2p_udt_accepter(p2p_udt_cb* cb, void* user_data, int send_buf_size, int recv_buf_size, void* udt_sock, pj_sock_t sock, p2p_udt_accepter** accepter)
{
	pj_status_t status;
	pj_pool_t *pool;
	p2p_udt_accepter* udt_accepter; 
#ifdef USE_P2P_TCP
	p2p_tcp_cb tcp_cb;
	pj_sockaddr remote_addr;
	char addr_info[PJ_INET6_ADDRSTRLEN+10];
	PJ_UNUSED_ARG(udt_sock);
#endif

	PJ_UNUSED_ARG(send_buf_size);
	PJ_UNUSED_ARG(recv_buf_size);

	pool = pj_pool_create(&get_p2p_global()->caching_pool.factory, 
		"p2p_udta%p", 
		PJNATH_POOL_LEN_ICE_STRANS,
		PJNATH_POOL_INC_ICE_STRANS, 
		NULL);

	udt_accepter = PJ_POOL_ZALLOC_T(pool, p2p_udt_accepter);

	PJ_LOG(4,("p2p_udta", "create_p2p_udt_accepter %p", udt_accepter));

	pj_bzero(udt_accepter, sizeof(udt_accepter));
	udt_accepter->magic = P2P_DATA_MAGIC;
	udt_accepter->pool = pool;
	udt_accepter->user_data = user_data;

	status = pj_grp_lock_create(pool, NULL, &udt_accepter->grp_lock);
	if (status != PJ_SUCCESS)
	{
		pj_pool_release(pool);
		return status;
	}
	status = pj_event_create(pool, "p2p_a_s_event", PJ_FALSE, PJ_FALSE, &udt_accepter->send_event);
	if (status != PJ_SUCCESS)
	{
		pj_pool_release(pool);
		return status;
	}

	//add self reference count
	pj_grp_lock_add_ref(udt_accepter->grp_lock);
	pj_grp_lock_add_handler(udt_accepter->grp_lock, pool, udt_accepter, &p2p_udt_accepter_on_destroy);

	pj_memcpy(&udt_accepter->udt_cb, cb, sizeof(p2p_udt_cb));

#ifdef USE_P2P_TCP
	tcp_cb.user_data = udt_accepter;
	tcp_cb.on_close = &udt_obj_on_close<p2p_udt_accepter>;
	tcp_cb.on_recved  = &udt_obj_on_recv<p2p_udt_accepter>;
	tcp_cb.send = &udt_obj_send_cb<p2p_udt_accepter>;
	tcp_cb.on_send = &udt_obj_on_send<p2p_udt_accepter>;
	tcp_cb.on_noresned_recved = &udt_obj_on_noresned_recved<p2p_udt_accepter>;

	udt_obj_get_peer_addr<p2p_udt_accepter>((sockaddr*)&remote_addr, udt_accepter);
	pj_sockaddr_print(&remote_addr, addr_info, sizeof(addr_info), 3);
	PJ_LOG(4,("create_p2p_udt_accepter", "remote addr %s", addr_info));

	udt_accepter->p2p_tcp = p2p_tcp_create(&tcp_cb, sock, &remote_addr, udt_accepter->grp_lock);
#else
	//udt_accepter->udt_sock = (UDT_P2P::UDTSOCKET)udt_sock;
	udt_accepter->udt_sock = (long)udt_sock; //long is for x64 build
	//must set asynchronous, otherwise block io thread
	int async = 0;
	UDT_P2P::UDT::setsockopt(udt_accepter->udt_sock, 0, UDT_P2P::UDT_RCVSYN, &async, sizeof(int));
	UDT_P2P::UDT::setsockopt(udt_accepter->udt_sock, 0, UDT_P2P::UDT_SNDSYN, &async, sizeof(int));

	UDT_P2P::p2p_socket_cb udt_cb;
	pj_bzero(&udt_cb, sizeof(udt_cb));
	udt_cb.get_peer_addr = &udt_obj_get_peer_addr<p2p_udt_accepter>;
	udt_cb.get_sock_addr = &udt_obj_get_sock_addr<p2p_udt_accepter>;
	udt_cb.on_recv = &udt_obj_on_recv<p2p_udt_accepter>;
	udt_cb.on_send = &udt_obj_on_send<p2p_udt_accepter>;
	udt_cb.on_close= &udt_obj_on_close<p2p_udt_accepter>;
	udt_cb.user_data = udt_accepter;
	udt_cb.ice_socket = sock;
	UDT_P2P::UDT::set_p2p_call_back(udt_accepter->udt_sock, &udt_cb);
#endif

	*accepter = udt_accepter;
	PJ_LOG(4,("p2p_udta", "create_p2p_udt_accepter %p end", udt_accepter));
	return PJ_SUCCESS;
}

void destroy_p2p_udt_accepter(p2p_udt_accepter* accepter)
{
#ifdef USE_P2P_TCP
	p2p_tcp_sock* p2p_tcp = NULL;
#else
	UDT_P2P::UDTSOCKET udt_sock = UDT_P2P::UDT::INVALID_SOCK ;
#endif

	p2p_tcp_data* send_data;

	PJ_LOG(4,("p2p_udtc", "destroy_p2p_udt_accepter %p", accepter));

	udt_grp_lock_acquire(accepter->grp_lock);
	if (accepter->destroy_req) { //already destroy, so return
		udt_grp_lock_release(accepter->grp_lock);
		return;
	}
	accepter->destroy_req = PJ_TRUE;

#ifdef USE_P2P_TCP
	if(accepter->p2p_tcp)
	{
		p2p_tcp = accepter->p2p_tcp;
		accepter->p2p_tcp = NULL;
	}
#else
	if(accepter->udt_sock != UDT_P2P::UDT::INVALID_SOCK)
	{
		udt_sock = accepter->udt_sock;
		accepter->udt_sock = UDT_P2P::UDT::INVALID_SOCK;
	}
#endif

	while(accepter->first_send_data)
	{
		send_data = accepter->first_send_data;
		accepter->first_send_data = send_data->next;
		free_p2p_tcp_data(send_data);
	}
	accepter->last_send_data = NULL;

	udt_grp_lock_release(accepter->grp_lock);

	pj_event_set(accepter->send_event);

#ifdef USE_P2P_TCP
	if(p2p_tcp)
		p2p_tcp_destory(p2p_tcp);
#else
	if(udt_sock != UDT_P2P::UDT::INVALID_SOCK)
	{
		UDT_P2P::p2p_socket_cb udt_cb;
		pj_bzero(&udt_cb, sizeof(udt_cb));//reset all call back
		UDT_P2P::UDT::set_p2p_call_back(udt_sock, &udt_cb);

		UDT_P2P::UDT::close(udt_sock);
	}
#endif

	pj_grp_lock_dec_ref(accepter->grp_lock);

	PJ_LOG(4,("p2p_udtc", "destroy_p2p_udt_accepter %p end", accepter));

}

//callback to free memory of p2p_udt_listener
static void p2p_udt_listener_on_destroy(void *obj)
{
	p2p_udt_listener *listener = (p2p_udt_listener*)obj;
	PJ_LOG(4,("p2p_udtc", "p2p_udt_accepter_on_destroy %p", listener));
	delay_destroy_pool(listener->pool);
}

#ifndef USE_P2P_TCP
//accept a udt socket in network io thread 
static void on_io_thread_udt_accept(void* data)
{
	p2p_udt_listener* listener = (p2p_udt_listener*)data;
	pj_sockaddr addr;
	int addr_len;
	UDT_P2P::UDTSOCKET sock ;
	pj_status_t result;
	if(!udt_obj_is_valid(listener))
		return;
	pj_sockaddr_init(pj_AF_INET(), &addr, NULL, 0);
	addr_len = pj_sockaddr_get_len(&addr);
	sock = UDT_P2P::UDT::accept(listener->udt_sock, (struct sockaddr*)&addr, &addr_len);
	result = listener->udt_cb.udt_on_accept(listener->user_data, (void*)sock, &addr);
	if(result != PJ_SUCCESS)//accept failed,then close udt socket
	{
		UDT_P2P::UDT::close(sock);
		PJ_LOG(4,("p2p_udt", "on_io_thread_udt_accept result %d", result));
	}
}

//schedule socket_pair for accept a udt socket in network io thread 
void udt_listener_on_accept(void* user_data)
{
	p2p_socket_pair_item item;
	item.cb = on_io_thread_udt_accept;
	item.data = user_data;
	schedule_socket_pair(get_p2p_global()->sock_pair, &item);
}
#endif

pj_status_t create_p2p_udt_listener(p2p_udt_cb* cb, void* user_data, const pj_sockaddr_t *bind_addr, p2p_udt_listener** listener)
{
	pj_status_t status;
	pj_pool_t *pool;
	p2p_udt_listener* udt_listener; 
#ifndef USE_P2P_TCP
	int result;
	int reuse = 0;
#else
	PJ_UNUSED_ARG(bind_addr);
#endif
	pool = pj_pool_create(&get_p2p_global()->caching_pool.factory, 
		"p2p_udtl%p", 
		PJNATH_POOL_LEN_ICE_STRANS,
		PJNATH_POOL_INC_ICE_STRANS, 
		NULL);

	udt_listener = PJ_POOL_ZALLOC_T(pool, p2p_udt_listener);
	pj_bzero(udt_listener, sizeof(udt_listener));
	udt_listener->magic = P2P_DATA_MAGIC;
	udt_listener->pool = pool;
	udt_listener->user_data = user_data;
	status = pj_grp_lock_create(pool, NULL, &udt_listener->grp_lock);
	if (status != PJ_SUCCESS)
	{
		pj_pool_release(pool);
		return status;
	}

	//add self reference count
	pj_grp_lock_add_ref(udt_listener->grp_lock);
	pj_grp_lock_add_handler(udt_listener->grp_lock, pool, udt_listener, &p2p_udt_listener_on_destroy);

	pj_memcpy(&udt_listener->udt_cb, cb, sizeof(p2p_udt_cb));

#ifndef USE_P2P_TCP
	//create udt socket,and listen it
	udt_listener->udt_sock = UDT_P2P::UDT::socket(pj_AF_INET(), pj_SOCK_STREAM(), 0);
	if(udt_listener->udt_sock == UDT_P2P::UDT::INVALID_SOCK)
	{
		destroy_p2p_udt_listener(udt_listener);
		return UDT_P2P::UDT::getlasterror_code();
	}

	UDT_P2P::p2p_socket_cb udt_cb;
	pj_memset(&udt_cb, 0, sizeof(UDT_P2P::p2p_socket_cb));
	udt_cb.send_cb = &udt_obj_send_cb<p2p_udt_listener>;
	udt_cb.get_peer_addr = &udt_obj_get_peer_addr<p2p_udt_listener>;
	udt_cb.get_sock_addr = &udt_obj_get_sock_addr<p2p_udt_listener>;
	udt_cb.on_accept = &udt_listener_on_accept;
	udt_cb.user_data = udt_listener;
	UDT_P2P::UDT::set_p2p_call_back(udt_listener->udt_sock, &udt_cb);

	//close udt listen socket so slow , so do not reuse address
	UDT_P2P::UDT::setsockopt(udt_listener->udt_sock, 0, UDT_P2P::UDT_REUSEADDR, &reuse, sizeof(reuse));
	result = UDT_P2P::UDT::bind(udt_listener->udt_sock, 
		(struct sockaddr*)bind_addr,
		pj_sockaddr_get_len(bind_addr));
	if(result == UDT_P2P::UDT::ERROR)
	{
		destroy_p2p_udt_listener(udt_listener);
		return UDT_P2P::UDT::getlasterror_code();
	}

	result = UDT_P2P::UDT::listen(udt_listener->udt_sock, 5);
	if(result == UDT_P2P::UDT::ERROR)
	{
		destroy_p2p_udt_listener(udt_listener);
		return UDT_P2P::UDT::getlasterror_code();
	}
#endif

	*listener = udt_listener;
	return PJ_SUCCESS;
}

pj_status_t p2p_udt_accepter_send(p2p_udt_accepter* accepter, const char* buffer, size_t buffer_len)
{
	return p2p_udt_obj_send(accepter, buffer, buffer_len);
}

//received user data or udt socket connect request, then put the data to receive buffer of udt listen socket
pj_status_t p2p_udt_accepter_on_recved(p2p_udt_accepter* accepter, 
									   const char* buffer, 
									   size_t buffer_len,
									   const pj_sockaddr_t *src_addr,
									   unsigned src_addr_len)
{
#ifdef USE_P2P_TCP
	PJ_UNUSED_ARG(src_addr);
	PJ_UNUSED_ARG(src_addr_len);
	if(accepter->p2p_tcp)
	{
		p2p_tcp_data_recved(accepter->p2p_tcp, buffer, buffer_len);
	}
	return PJ_SUCCESS;
#else
	PJ_UNUSED_ARG(accepter);
	PJ_UNUSED_ARG(buffer);
	PJ_UNUSED_ARG(buffer_len);
	PJ_UNUSED_ARG(src_addr);
	PJ_UNUSED_ARG(src_addr_len);
	return PJ_ENOTSUP;
#endif
}

//received user data or udt socket connect request, then put the data to receive buffer of udt listen socket
pj_status_t p2p_udt_listener_on_recved(p2p_udt_listener* listener, 
									   const char* buffer, 
									   size_t buffer_len,
									   const pj_sockaddr_t *src_addr,
									   unsigned src_addr_len)
{
#ifdef USE_P2P_TCP
	PJ_UNUSED_ARG(listener);
	PJ_UNUSED_ARG(buffer);
	PJ_UNUSED_ARG(buffer_len);
	PJ_UNUSED_ARG(src_addr);
	PJ_UNUSED_ARG(src_addr_len);
	return PJ_ENOTSUP;
#else
	if(listener->udt_sock != UDT_P2P::UDT::INVALID_SOCK)
	{
		//PJ_LOG(4,("p2p_udt", "p2p_udt_listener_on_recved %d", buffer_len));
		UDT_P2P::UDT::on_p2p_data_recved(listener->udt_sock, 
			buffer, buffer_len,
			(struct sockaddr*)src_addr,
			src_addr_len);
	}
	return PJ_SUCCESS;
#endif
}

void destroy_p2p_udt_listener(p2p_udt_listener* listener)
{
#ifndef USE_P2P_TCP
	UDT_P2P::UDTSOCKET udt_sock = UDT_P2P::UDT::INVALID_SOCK;
#endif

	udt_grp_lock_acquire(listener->grp_lock);
	if (listener->destroy_req) { //already destroy, so return
		udt_grp_lock_release(listener->grp_lock);
		return;
	}
	listener->destroy_req = PJ_TRUE;

#ifndef USE_P2P_TCP
	if(listener->udt_sock != UDT_P2P::UDT::INVALID_SOCK)
	{
		udt_sock = listener->udt_sock;
		listener->udt_sock = UDT_P2P::UDT::INVALID_SOCK;
	}
#endif

	udt_grp_lock_release(listener->grp_lock);

#ifndef USE_P2P_TCP
	if(udt_sock != UDT_P2P::UDT::INVALID_SOCK)
	{
		UDT_P2P::p2p_socket_cb udt_cb;
		pj_bzero(&udt_cb, sizeof(udt_cb));//reset all call back
		UDT_P2P::UDT::set_p2p_call_back(udt_sock, &udt_cb);
		UDT_P2P::UDT::close(udt_sock);
	}
#endif
	//release self reference
	pj_grp_lock_dec_ref(listener->grp_lock);
}

template<typename T>
static pj_status_t p2p_udt_obj_model_send(T* t, const char* buffer, size_t buffer_len, p2p_send_model model, int type)
{
	pj_status_t status;
	size_t sended = 0;
	pj_uint16_t data_seq = 0;
	pj_uint16_t data_count;
	int max_package_len = TCP_SOCK_PACKAGE_SIZE;

	char send_buffer[sizeof(p2p_tcp_proxy_header) + TCP_SOCK_PACKAGE_SIZE];
	p2p_tcp_proxy_header* header = (p2p_tcp_proxy_header* )send_buffer;

#ifdef USE_P2P_TCP
	if(!t || !t->p2p_tcp || t->destroy_req)
		return PJ_EGONE;
#else
	if(!t || t->udt_sock == UDT_P2P::UDT::INVALID_SOCK || t->destroy_req)
		return PJ_EGONE;
#endif

	//cache full, nonblock send
	if(model == P2P_SEND_NONBLOCK && t->first_send_data)
	{
		return PJ_CACHE_FULL;
	}

	//PJ_LOG(4,("p2p_udt", "p2p_udt_obj_model_send begin %p buffer_len %d", t, buffer_len));
	
	udt_grp_lock_acquire(t->grp_lock);
	t->pkg_seq++;
	if(t->pkg_seq == 0)
		t->pkg_seq++;
	//if command is P2P_COMMAND_USER_DATA, listen_port is package sequence number
	header->listen_port = pj_htons(t->pkg_seq); 
	
	switch(type)
	{
	case P2P_DATA_AV:
		header->command = pj_htons(P2P_COMMAND_USER_AV_DATA);
		break;
#ifdef USE_P2P_TCP
	case P2P_DATA_AV_NO_RESEND:
		header->command = pj_htons(P2P_COMMAND_USER_AV_NORESEND);
		max_package_len = P2P_TCP_MAX_DATA_LEN;
		break;
#endif
	default:
		header->command = pj_htons(P2P_COMMAND_USER_DATA);
		break;
	}

	data_count = (pj_uint16_t)((buffer_len+max_package_len-1) / max_package_len);

	while(sended < buffer_len)
	{
		int data_length;
#ifdef USE_P2P_TCP
		if(!t->p2p_tcp || t->destroy_req)
		{
			udt_grp_lock_release(t->grp_lock);
			return PJ_EGONE;
		}
#else
		if(t->udt_sock == UDT_P2P::UDT::INVALID_SOCK || t->destroy_req)
		{
			udt_grp_lock_release(t->grp_lock);
			return PJ_EGONE;
		}
#endif
		//block send
		if(model == P2P_SEND_BLOCK && t->pause_send_data)
		{
			udt_grp_lock_release(t->grp_lock);
			int ret = run_global_loop();
			if(ret == NO_GLOBAL_THREAD)
				pj_event_wait(t->send_event);
			if(ret == GLOBAL_THREAD_EXIT)
			{
				udt_grp_lock_release(t->grp_lock);
				return PJ_EGONE;
			}
			udt_grp_lock_acquire(t->grp_lock);
			continue;
		}

		data_length = buffer_len-sended;
		if(data_length > max_package_len)
			data_length = max_package_len;

		if(data_seq == data_count-1)//last sub sequence number
			data_seq = P2P_LAST_DATA_SEQ;

		header->data_length = pj_htonl(data_length);
		//if command is P2P_COMMAND_USER_DATA, sock_id is sub sequence number
		header->sock_id = pj_htons(data_seq);
		
		memcpy(send_buffer+sizeof(p2p_tcp_proxy_header), buffer+sended, data_length);
#ifdef USE_P2P_TCP
		if(type == P2P_DATA_AV_NO_RESEND)
			status = p2p_tcp_no_resend(t->p2p_tcp, send_buffer, sizeof(p2p_tcp_proxy_header)+data_length);
		else
#endif
			status = p2p_udt_obj_send(t, send_buffer, sizeof(p2p_tcp_proxy_header)+data_length);
		if(status != PJ_SUCCESS && status != -1)
		{
			udt_grp_lock_release(t->grp_lock);
			return status;
		}
		data_seq++;
		sended += data_length;
	}
	udt_grp_lock_release(t->grp_lock);

	//PJ_LOG(4,("p2p_udt", "p2p_udt_obj_model_send end %p buffer_len %d", t, buffer_len));
	return PJ_SUCCESS;
}

pj_status_t p2p_udt_connector_model_send(p2p_udt_connector* connector, const char* buffer, size_t buffer_len, p2p_send_model model, int type)
{
	return p2p_udt_obj_model_send(connector, buffer, buffer_len, model, type); 
}

pj_status_t p2p_udt_accepter_model_send(p2p_udt_accepter* accepter, const char* buffer, size_t buffer_len, p2p_send_model model, int type)
{
	return p2p_udt_obj_model_send(accepter, buffer, buffer_len, model, type); 
}

void p2p_udt_connector_wakeup_send(p2p_udt_connector* connector)
{
	if(connector)
		pj_event_set(connector->send_event);
}
void p2p_udt_accepter_wakeup_send(p2p_udt_accepter* accepter)
{
	if(accepter)
		pj_event_set(accepter->send_event);
}


pj_status_t p2p_udt_connector_set_opt(p2p_udt_connector* connector, p2p_opt opt, const void* optval, int optlen)
{
	return p2p_udt_set_opt(connector, opt, optval, optlen);
}

pj_status_t p2p_udt_accepter_set_opt(p2p_udt_accepter* accepter, p2p_opt opt, const void* optval, int optlen)
{
	return p2p_udt_set_opt(accepter, opt, optval, optlen);
}

pj_bool_t p2p_udt_connector_sock_valid(p2p_udt_connector* connector)
{
	if(connector == NULL)
		return PJ_FALSE;
#ifdef USE_P2P_TCP
	return connector->p2p_tcp != NULL;
#else
	return connector->udt_sock != UDT_P2P::UDT::INVALID_SOCK;
#endif
}

void p2p_udt_connector_guess_port(p2p_udt_connector* connector,
								  pj_sock_t sock,
								  const pj_sockaddr_t *src_addr,
								  unsigned src_addr_len)
{
#ifdef USE_P2P_TCP
	p2p_tcp_sock_guess_port(connector->p2p_tcp, sock, src_addr, src_addr_len);
#endif
}
void p2p_udt_accepter_guess_port(p2p_udt_accepter* accepter,
								 pj_sock_t sock,
								 const pj_sockaddr_t *src_addr,
								 unsigned src_addr_len)
{
#ifdef USE_P2P_TCP
	p2p_tcp_sock_guess_port(accepter->p2p_tcp, sock, src_addr, src_addr_len);
#endif
}


//clear all send buffer
void p2p_udt_accepter_clear_send_buf(p2p_udt_accepter* accepter)
{
	p2p_tcp_data* send_data;

	PJ_LOG(4,("p2p_udt", "p2p_udt_accepter_clear_send_buf accepter %p", accepter));

	udt_grp_lock_acquire(accepter->grp_lock);

	while(accepter->first_send_data)
	{
		send_data = accepter->first_send_data;
		accepter->first_send_data = send_data->next;
		free_p2p_tcp_data(send_data);
	}
	accepter->last_send_data = NULL;

	p2p_tcp_clear_send_buf(accepter->p2p_tcp);

	udt_grp_lock_release(accepter->grp_lock);
}