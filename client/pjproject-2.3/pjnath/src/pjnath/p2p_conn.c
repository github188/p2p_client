#include <pjnath/p2p_global.h>
#include <pjnath/errno.h>
#include <pjnath/p2p_conn.h>
#define LISTEN_HASH_TABLE_SIZE 8

void p2p_conn_get_sock_addr(pj_sockaddr_t* addr, void* user_data)
{
	pj_ice_strans_p2p_conn* conn = (pj_ice_strans_p2p_conn*)user_data;
	pj_memcpy(addr, &conn->local_addr, pj_sockaddr_get_len(&conn->local_addr));
}

void p2p_conn_get_peer_addr(pj_sockaddr_t* addr, void* user_data)
{
	pj_ice_strans_p2p_conn* conn = (pj_ice_strans_p2p_conn*)user_data;
	pj_memcpy(addr, &conn->remote_addr, pj_sockaddr_get_len(&conn->remote_addr));
}

pj_bool_t p2p_conn_is_valid(pj_ice_strans_p2p_conn* conn)
{
	if(conn == NULL || conn->destroy_req || conn->magic != P2P_DATA_MAGIC)
		return PJ_FALSE;
	return PJ_TRUE;
}

pj_status_t p2p_ice_send_data(void* user_data, const pj_sockaddr_t* addr, const char* buffer, size_t buffer_len)
{
	pj_ice_strans_p2p_conn* conn = (pj_ice_strans_p2p_conn*)user_data;
	pj_status_t status = PJ_EGONE;
	PJ_UNUSED_ARG(addr);

	if(!p2p_conn_is_valid(conn))
		return status;

	pj_mutex_lock(conn->send_mutex);
	if(conn->icest)
	{
		if (pj_ice_strans_has_sess(conn->icest) &&
			pj_ice_strans_sess_is_complete(conn->icest)) 
		{
			PJ_LOG(5,("pj_ice_s_p2p_c", "p2p_ice_send_data %p, %d, %d, %d", conn, conn->conn_id, conn->is_initiative, buffer_len));
			status = pj_ice_strans_sendto(conn->icest, 
				1, 
				buffer,
				buffer_len, 
				&conn->remote_addr, 
				pj_sockaddr_get_len(&conn->remote_addr));
		}
	}
	pj_mutex_unlock(conn->send_mutex);

	if(status == PJ_EBUSY) //net block,so try sleep
		pj_thread_sleep(0);
	return status;
}

//callback to free memory of pj_ice_strans_p2p_conn
static void p2p_conn_on_destroy(void *obj)
{
	pj_ice_strans_p2p_conn *conn = (pj_ice_strans_p2p_conn*)obj;
	PJ_LOG(4,("pj_ice_s_p2p_c", "pj_ice_strans_p2p_conn %p destroyed", obj));
	
	p2p_free(conn->user_data_buf);
	pj_mutex_destroy(conn->send_mutex);
	pj_mutex_destroy(conn->receive_mutex);
	delay_destroy_pool(conn->pool);
	PJ_LOG(4,("pj_ice_s_p2p_c", "pj_ice_strans_p2p_conn %p destroyed end", obj));
}

pj_status_t connect_proxy_send_tcp_data(p2p_tcp_connect_proxy* connect_proxy,
										const char* buffer,
										size_t buffer_len)
{
	pj_ice_strans_p2p_conn* conn = (pj_ice_strans_p2p_conn*)connect_proxy->user_data;
	pj_status_t status = PJ_EGONE;
	if(conn->udt.p2p_udt_accepter)
	{
		status = p2p_udt_accepter_send(conn->udt.p2p_udt_accepter, buffer, buffer_len);
	}

	return status;
}

static void add_p2p_conn_ref(p2p_tcp_connect_proxy* proxy)
{
	pj_ice_strans_p2p_conn* conn = (pj_ice_strans_p2p_conn*)proxy->user_data;
	pj_grp_lock_add_ref(conn->grp_lock);
}

static void release_p2p_conn_ref(p2p_tcp_connect_proxy* proxy)
{
	pj_ice_strans_p2p_conn* conn = (pj_ice_strans_p2p_conn*)proxy->user_data;
	pj_grp_lock_dec_ref(conn->grp_lock);
}

static void on_tcp_proxy_connected(p2p_tcp_connect_proxy* proxy, unsigned short port)
{
	pj_ice_strans_p2p_conn* conn = (pj_ice_strans_p2p_conn*)proxy->user_data;
	
	if(conn && conn->transport && conn->transport->cb->on_tcp_proxy_connected)
	{
		char addr[256];
		pj_ice_strans_cfg * ice_cfg = pj_ice_strans_get_cfg(conn->icest);

		pj_sockaddr_print(&conn->remote_internet_addr, addr, sizeof(addr), 2);
		PJ_LOG(4,("p2p_conn", "on_tcp_proxy_connected %p %d %s", conn, port, addr));

		(*conn->transport->cb->on_tcp_proxy_connected)(conn->transport,
			conn->transport->user_data,
			ice_cfg->turn.alloc_param.user_data,
			port, 
			addr);
	}
}

//#define DEBUG_P2P_CALLBACK 1

#ifdef DEBUG_P2P_CALLBACK

#pragma   pack(1)
typedef struct P2pHead_t
{
	int  			flag;		//消息开始标识
	unsigned int 	size;		//接收发送消息大小(不包括消息头)
	char 			type;		//协议类型1 json 2 json 加密
	char			protoType;	//消息类型1 请求2应答3通知
	int 			msgType;	//IOTYPE消息类型
	char 			reserve[6];	//保留
}P2pHead;
#pragma   pack()

typedef struct _gos_frame_head
{
	unsigned int	nFrameNo;			// 帧号
	unsigned int	nFrameType;			// 帧类型	gos_frame_type_t
	unsigned int	nCodeType;			// 编码类型 gos_codec_type_t
	unsigned int	nFrameRate;			// 视频帧率，音频采样率
	unsigned int	nTimestamp;			// 时间戳
	unsigned short	sWidth;				// 视频宽
	unsigned short	sHeight;			// 视频高
	unsigned int	reserved;			// 预留
	unsigned int	nDataSize;			// data数据长度
	char			data[0];
}gos_frame_head;

#endif


static void p2p_conn_post_pause_data(pj_ice_strans_p2p_conn* conn, pj_ice_strans_cfg* ice_cfg)
{
	pj_mutex_lock(conn->receive_mutex);
	while(conn->pause_recv_first && conn->pause_recv_count && !conn->is_pause_recv)
	{
		p2p_tcp_data* next = conn->pause_recv_first->next;

		if(conn->transport->cb && conn->transport->cb->on_connection_recv)	
		{
#ifdef DEBUG_P2P_CALLBACK
			gos_frame_head* head;
			head = (gos_frame_head*)(conn->pause_recv_first->buffer+sizeof(P2pHead));
			PJ_LOG(4,("p2p_conn", "p2p_conn_post_pause_data on_connection_recv type %d,size %d,nTimestamp %u", head->nFrameType, head->nDataSize, head->nTimestamp));
#endif
			(*conn->transport->cb->on_connection_recv)(conn->transport, 
				conn->conn_id,
				conn->transport->user_data,
				ice_cfg->turn.alloc_param.user_data,
				conn->pause_recv_first->buffer,
				conn->pause_recv_first->buffer_len);
		}

		free_p2p_tcp_data(conn->pause_recv_first);
		conn->pause_recv_first = next;
		conn->pause_recv_count--;
	}

	if(conn->pause_recv_count == 0)
	{
		conn->pause_recv_first = conn->pause_recv_last = NULL;
	}
	pj_mutex_unlock(conn->receive_mutex);
}

static void async_post_pause_data(void* data)
{
	pj_ice_strans_p2p_conn* conn = (pj_ice_strans_p2p_conn*)data;
	pj_ice_strans_cfg* ice_cfg;

	if(conn->destroy_req)
	{
		pj_grp_lock_dec_ref(conn->grp_lock); //add in p2p_conn_set_opt P2P_PAUSE_RECV
		return;
	}

	ice_cfg	= pj_ice_strans_get_cfg(conn->icest);
	p2p_conn_post_pause_data(conn, ice_cfg);

	pj_grp_lock_dec_ref(conn->grp_lock); //add in p2p_conn_set_opt P2P_PAUSE_RECV
}

/*
when playback history file, call p2p_conn_set_opt P2P_PAUSE_RECV
user data do not callback
*/
static void p2p_conn_callback_recv(pj_ice_strans_p2p_conn* conn, const char* buffer, unsigned int len)
{
	pj_ice_strans_cfg * ice_cfg = pj_ice_strans_get_cfg(conn->icest);

#ifdef DEBUG_P2P_CALLBACK
	gos_frame_head* head;

	head = (gos_frame_head*)(buffer+sizeof(P2pHead));
	PJ_LOG(4,("p2p_conn", "p2p_conn_callback_recv type %d,size %d,is_pause_recv %d,pause_recv_count %d", head->nFrameType, head->nDataSize, conn->is_pause_recv, conn->pause_recv_count));

#endif

	if(conn->is_pause_recv)
	{
		if(conn->pause_recv_count == 0)
			conn->pause_recv_first = conn->pause_recv_last = malloc_p2p_tcp_data(buffer, len);
		else
		{
			conn->pause_recv_last->next = malloc_p2p_tcp_data(buffer, len);
			conn->pause_recv_last = conn->pause_recv_last->next;
		}
		conn->pause_recv_count++;
	}
	else
	{
		p2p_conn_post_pause_data(conn, ice_cfg);

		if(conn->pause_recv_count == 0)
		{
#ifdef DEBUG_P2P_CALLBACK
			PJ_LOG(4,("p2p_conn", "p2p_conn_callback_recv on_connection_recv type %d,size %d, nTimestamp %u", head->nFrameType, head->nDataSize, head->nTimestamp));
#endif
			if(conn->transport->cb && conn->transport->cb->on_connection_recv)	
				(*conn->transport->cb->on_connection_recv)(conn->transport, 
				conn->conn_id,
				conn->transport->user_data,
				ice_cfg->turn.alloc_param.user_data,
				(char*)buffer,
				len);
		}
		else
		{
			conn->pause_recv_last->next = malloc_p2p_tcp_data(buffer, len);
			conn->pause_recv_last = conn->pause_recv_last->next;
			conn->pause_recv_count++;
		}
	}	
}

//smooth callback function
static void p2p_smooth_callback(const char* buffer, unsigned int len, void* user_data)
{
	pj_ice_strans_p2p_conn* conn = user_data;
	p2p_conn_callback_recv(conn, buffer, len);
}

pj_ice_strans_p2p_conn* create_p2p_conn(pj_str_t* proxy_addr, pj_bool_t is_initiative)
{
	pj_pool_t *pool;
	pj_ice_strans_p2p_conn* conn;
	p2p_tcp_connect_proxy_cb cb;

	pool = pj_pool_create(&get_p2p_global()->caching_pool.factory, 
		"p2p_conn%p", 
		PJNATH_POOL_LEN_ICE_STRANS,
		PJNATH_POOL_INC_ICE_STRANS, 
		NULL);
	conn = PJ_POOL_ZALLOC_T(pool, pj_ice_strans_p2p_conn);
	pj_bzero(conn, sizeof(pj_ice_strans_p2p_conn));
	conn->magic = P2P_DATA_MAGIC;
	conn->pool = pool;
	conn->recv_buf_size = P2P_RECV_BUFFER_SIZE;
	conn->send_buf_size = P2P_SEND_BUFFER_SIZE;
	conn->tcp_listen_proxys = pj_hash_create(pool, LISTEN_HASH_TABLE_SIZE);

	conn->user_data_buf = p2p_malloc(TCP_SOCK_PACKAGE_SIZE);
	conn->user_data_len = 0;
	conn->user_data_pkg_seq = 0;
	conn->user_data_capacity = TCP_SOCK_PACKAGE_SIZE;

	if(get_p2p_global()->smooth_span > 0 && is_initiative)
		conn->smooth = p2p_create_smooth(p2p_smooth_callback , conn);

	pj_mutex_create_recursive(pool, NULL, &conn->send_mutex);
	pj_mutex_create_recursive(pool, NULL, &conn->receive_mutex);

	cb.send_tcp_data = &connect_proxy_send_tcp_data;
	cb.add_ref = &add_p2p_conn_ref;
	cb.release_ref = &release_p2p_conn_ref;
	cb.on_tcp_connected = &on_tcp_proxy_connected;
	init_p2p_tcp_connect_proxy(&conn->tcp_connect_proxy, proxy_addr, pool, &cb, conn);

	pj_grp_lock_create(conn->pool, NULL, &conn->grp_lock);

	//add self reference count
	pj_grp_lock_add_ref(conn->grp_lock);
	pj_grp_lock_add_handler(conn->grp_lock, pool, conn, &p2p_conn_on_destroy);

		PJ_LOG(4,("p2p_conn", "create_p2p_conn get_p2p_global()->smooth_span %d is_initiative %d", get_p2p_global()->smooth_span, is_initiative));
	return conn;
}


//report session information to p2p server
static void p2p_report_session_destroyed(pj_ice_strans_p2p_conn* conn)
{
	if(conn->is_initiative 
		&& conn->udt.p2p_udt_connector 
		&& p2p_udt_connector_sock_valid(conn->udt.p2p_udt_connector))
	{
		pj_ice_report_session_destroyed(conn->transport->assist_icest, 
			conn->conn_id,
			get_p2p_global()->client_guid + strlen(P2P_CLIENT_PREFIX));	
	}
}
void async_destroy_p2p_conn(void *user_data)
{
	pj_grp_lock_dec_ref((pj_grp_lock_t *)user_data);
}
void destroy_p2p_conn(pj_ice_strans_p2p_conn* conn)
{
	p2p_tcp_listen_proxy** p2p_proxy = NULL;
	unsigned proxy_count = 0;
	pj_hash_iterator_t itbuf, *it;
	unsigned i;
	pj_ice_strans *icest = NULL;
	//pj_time_val delay = {5, 0};

	PJ_LOG(4,("pj_ice_s_p2p_c", "destroy_p2p_conn %p", conn));

	p2p_report_session_destroyed(conn);

	pj_grp_lock_acquire(conn->grp_lock);
	if (conn->destroy_req) { //already destroy, so return
		pj_grp_lock_release(conn->grp_lock);
		return;
	}
	conn->destroy_req = PJ_TRUE;

	//prevent deadlock, get items in hash table, then clean hash table
	proxy_count = pj_hash_count(conn->tcp_listen_proxys);
	if(proxy_count)
	{
		p2p_tcp_listen_proxy** proxy;
		p2p_proxy = proxy = (p2p_tcp_listen_proxy**)p2p_malloc(sizeof(p2p_tcp_listen_proxy*)*proxy_count);
		it = pj_hash_first(conn->tcp_listen_proxys, &itbuf);
		while (it) 
		{
			*proxy = (p2p_tcp_listen_proxy*) pj_hash_this(conn->tcp_listen_proxys, it);
			//use pj_hash_set NULL, remove from hash table
			pj_hash_set(NULL, conn->tcp_listen_proxys, &(*proxy)->proxy_port, sizeof(pj_uint16_t), (*proxy)->hash_value, NULL);
			it = pj_hash_first(conn->tcp_listen_proxys, &itbuf);
			proxy++;
		}
	}

	icest = conn->icest;
	conn->icest = NULL;

	if(conn->smooth)
	{
		p2p_destroy_smooth(conn->smooth);
		conn->smooth = NULL;
	}
#ifdef USE_P2P_PORT_GUESS
	if(conn->port_guess)
	{
		p2p_destroy_port_guess(conn->port_guess);
		conn->port_guess = NULL;
	}
#endif

	while(conn->pause_recv_first && conn->pause_recv_count )
	{
		p2p_tcp_data* next = conn->pause_recv_first->next;
		free_p2p_tcp_data(conn->pause_recv_first);
		conn->pause_recv_first = next;
		conn->pause_recv_count--;
	}

	pj_grp_lock_release(conn->grp_lock);

	if(conn->is_initiative)
	{
		if(conn->udt.p2p_udt_connector)
			destroy_p2p_udt_connector(conn->udt.p2p_udt_connector);
	}
	else
	{
		if(conn->udt.p2p_udt_accepter)
			destroy_p2p_udt_accepter(conn->udt.p2p_udt_accepter);
	}

	if(icest)
		pj_ice_strans_destroy(icest);

	uninit_p2p_tcp_connect_proxy(&conn->tcp_connect_proxy);

	for(i=0; i<proxy_count; i++)
	{
		destroy_p2p_tcp_listen_proxy(p2p_proxy[i]);
		pj_grp_lock_dec_ref(conn->grp_lock);//----*********** when listen proxy created,add conn reference,so release it

	}
	if(p2p_proxy)
		p2p_free(p2p_proxy);

	//async release self reference
	//p2p_global_set_timer(delay, conn->grp_lock, async_destroy_p2p_conn);
	pj_grp_lock_dec_ref(conn->grp_lock);
	PJ_LOG(4,("pj_ice_s_p2p_c", "destroy_p2p_conn %p end", conn));
}

void p2p_conn_wakeup_block_send(pj_ice_strans_p2p_conn* conn)
{
	if(conn->is_initiative)
	{
		if(conn->udt.p2p_udt_connector)
			p2p_udt_connector_wakeup_send(conn->udt.p2p_udt_connector);
	}
	else
	{
		if(conn->udt.p2p_udt_accepter)
			p2p_udt_accepter_wakeup_send(conn->udt.p2p_udt_accepter);
	}
}

void process_p2p_conn_cmd(pj_ice_strans_p2p_conn *conn)
{
	pj_uint32_t hval=0;
	p2p_tcp_proxy_header* header = (p2p_tcp_proxy_header*)conn->recved_buffer;
	if(conn->is_initiative)
	{
		p2p_tcp_listen_proxy* proxy;

		pj_grp_lock_acquire(conn->grp_lock);
		proxy = pj_hash_get(conn->tcp_listen_proxys, &header->listen_port, sizeof(pj_uint16_t), &hval) ;
		if(proxy)
			pj_grp_lock_add_ref(proxy->grp_lock);
		pj_grp_lock_release(conn->grp_lock);

		if(proxy)
		{
			p2p_tcp_listen_recved_data(proxy, header);
			pj_grp_lock_dec_ref(proxy->grp_lock);
		}
	}
	else
	{
		p2p_tcp_connect_recved_data(&conn->tcp_connect_proxy, header);
	}
}

static void p2p_conn_realloc_user_data(pj_ice_strans_p2p_conn* conn, int add_len)
{
	unsigned int capacity;

	capacity = conn->user_data_len + add_len;

	if(capacity > conn->user_data_capacity)
	{
		char* buf;
		unsigned int new_capacity = conn->user_data_capacity;

		new_capacity *= 2;
		while(new_capacity < capacity)
			new_capacity *= 2;

		if(new_capacity > (unsigned int)get_p2p_global()->max_recv_len)
		{
			PJ_LOG(1,("p2pc", "p2p_conn_realloc_user_data %p, user data too long", conn));
			conn->user_data_len = 0;
			return;
		}

		conn->user_data_capacity = new_capacity;
		PJ_LOG(4,("p2pc", "p2p_conn_realloc_user_data %p, capacity %d", conn, new_capacity));

		buf = (char*)p2p_malloc(new_capacity);
		memcpy(buf, conn->user_data_buf, conn->user_data_len);
		p2p_free(conn->user_data_buf);
		conn->user_data_buf = buf;
	}
}

static void p2p_conn_smooth_recv(pj_ice_strans_p2p_conn* conn, pj_int16_t command, char* data, int len)
{
	pj_bool_t is_smooth = PJ_FALSE;

	if(conn->is_initiative 
		&& conn->smooth
		&& (command == P2P_COMMAND_USER_AV_DATA || command == P2P_COMMAND_USER_AV_NORESEND))
		is_smooth = PJ_TRUE;

	/*PJ_LOG(4,("p2p_conn", "p2p_conn_smooth_recv command %d, smooth %d, len %d,cache_span %d", 
		command, is_smooth, len, is_smooth?conn->smooth->cache_span:0));*/

	if(is_smooth)
	{
		p2p_smooth_push(conn->smooth, data, len);
	}
	else
	{	
		p2p_conn_callback_recv(conn, data, len);
	}
}

//P2P_COMMAND_USER_DATA command cache
//if receive data is not full command, do not callback user 
//if receive a full command, callback to user
static void on_p2p_conn_recved_user_data(pj_ice_strans_p2p_conn* conn)
{
	p2p_tcp_proxy_header* header = (p2p_tcp_proxy_header*)conn->recved_buffer;
	char* data_buf = (char*)(header+1);

	//PJ_LOG(4,("p2p_conn", "on_p2p_conn_recved_user_data sock_id %d, listen_port %d, data_length %d, command %d", header->sock_id, header->listen_port, header->data_length, header->command));

	//if command is P2P_COMMAND_USER_DATA, listen_port is package sequence number
	//sometime peer multithread send,may be pkg_seq changed
	if(conn->user_data_pkg_seq && conn->user_data_pkg_seq != header->listen_port)
	{
		if(header->sock_id == P2P_LAST_DATA_SEQ)
		{
			p2p_conn_smooth_recv(conn, header->command, data_buf, header->data_length);
		}
		else
		{
			if(header->sock_id == 0)
			{
				conn->user_data_len = 0;
				p2p_conn_realloc_user_data(conn, header->data_length);

				memcpy(conn->user_data_buf, data_buf, header->data_length);
				conn->user_data_len = header->data_length;
				conn->user_data_pkg_seq = header->listen_port;
			}			
		}
		return;
	}
	conn->user_data_pkg_seq = header->listen_port;

	//if command is P2P_COMMAND_USER_DATA, sock_id is sub sequence number
	if(header->sock_id == P2P_LAST_DATA_SEQ)
	{
		if(conn->user_data_len == 0) //alone user data command
		{
			p2p_conn_smooth_recv(conn, header->command, data_buf, header->data_length);
		}
		else
		{
			p2p_conn_realloc_user_data(conn, header->data_length);

			//copy and merge user data
			memcpy(conn->user_data_buf+conn->user_data_len, data_buf, header->data_length);
			conn->user_data_len += header->data_length;

			p2p_conn_smooth_recv(conn, header->command, conn->user_data_buf, conn->user_data_len);
			conn->user_data_len = 0;
		}
	}
	else
	{
		p2p_conn_realloc_user_data(conn, header->data_length);

		//copy and merge user data
		memcpy(conn->user_data_buf+conn->user_data_len, data_buf, header->data_length);
		conn->user_data_len += header->data_length;
	}
}

void on_p2p_conn_recved_noresend_data(void* user_data, const char* receive_buffer, size_t buffer_len)
{
	pj_ice_strans_p2p_conn* conn = (pj_ice_strans_p2p_conn*)user_data;
	p2p_tcp_proxy_header* header = (p2p_tcp_proxy_header*)receive_buffer;

	header->listen_port = pj_ntohs(header->listen_port);
	header->sock_id = pj_ntohs(header->sock_id);
	header->command = pj_ntohs(header->command);
	header->data_length = pj_ntohl(header->data_length);

	if(header->data_length+sizeof(p2p_tcp_proxy_header) != buffer_len)
	{
		PJ_LOG(4,("p2p_conn", "##### invalid data_length 3 %d", header->data_length));
		return;
	}

	//PJ_LOG(4,("p2p_conn", "on_p2p_conn_recved_noresend_data %d", buffer_len));

	//no resend data
	if(header->command == P2P_COMMAND_USER_AV_NORESEND)
	{
		pj_mutex_lock(conn->receive_mutex);
		p2p_conn_smooth_recv(conn, header->command, (char*)(header+1), header->data_length);
		pj_mutex_unlock(conn->receive_mutex);
	}
}

void on_p2p_conn_recved_data(void* user_data, const char* receive_buffer, size_t buffer_len)
{
	pj_ice_strans_p2p_conn* conn = (pj_ice_strans_p2p_conn*)user_data;
	size_t pos = 0;
	size_t remain = buffer_len;
	size_t copyed = 0;
	p2p_tcp_proxy_header* header;

	if(!p2p_conn_is_valid(conn))
		return;
	if(conn->is_udt_close)
		return;

	//PJ_LOG(4,("p2p_conn", "on_p2p_conn_recved_data %d, recved_buffer_len %d", buffer_len, conn->recved_buffer_len ));

	pj_mutex_lock(conn->receive_mutex);
	while (remain)
	{
		if(conn->recved_buffer_len < sizeof(p2p_tcp_proxy_header))
		{
			if((conn->recved_buffer_len+remain) < sizeof(p2p_tcp_proxy_header))//not enough head length
			{
				memcpy(conn->recved_buffer+conn->recved_buffer_len, receive_buffer+pos, remain);
				conn->recved_buffer_len += remain;
				break;
			}
			else
			{
				copyed = sizeof(p2p_tcp_proxy_header)-conn->recved_buffer_len;
				memcpy(conn->recved_buffer+conn->recved_buffer_len, receive_buffer+pos, copyed); //get command head
				pos += copyed;
				remain -= copyed;

				//cross-platform, All the fields are in network byte order, must convert to host byte order
				header = (p2p_tcp_proxy_header*)conn->recved_buffer;
				header->listen_port = pj_ntohs(header->listen_port);
				header->sock_id = pj_ntohs(header->sock_id);
				header->command = pj_ntohs(header->command);
				header->data_length = pj_ntohl(header->data_length);
                
				if(header->data_length == 0) //the command no append data,only head
				{
					process_p2p_conn_cmd(conn);
					conn->recved_buffer_len =0 ;
				}
				else
				{
					conn->recved_buffer_len = sizeof(p2p_tcp_proxy_header);
                    if(header->data_length > TCP_SOCK_PACKAGE_SIZE)
                    {
                        PJ_LOG(2,("p2p_conn", "##### invalid data_length %d", header->data_length));
						pj_mutex_unlock(conn->receive_mutex);

						p2p_conn_udt_on_close(conn);
					    return;
                    }
				}
			}
		}
		else
		{
			header = (p2p_tcp_proxy_header*)conn->recved_buffer;
            if(header->data_length > TCP_SOCK_PACKAGE_SIZE)
            {
                PJ_LOG(2,("p2p_conn", "##### invalid data_length 2 %d", header->data_length));
				pj_mutex_unlock(conn->receive_mutex);
				p2p_conn_udt_on_close(conn);
                return;
            }
			if((conn->recved_buffer_len+remain) < (sizeof(p2p_tcp_proxy_header)+header->data_length))//not enough data length
			{
				memcpy(conn->recved_buffer+conn->recved_buffer_len, receive_buffer+pos, remain);
				conn->recved_buffer_len += remain;
				break;
			}
			else //get a full command
			{
				copyed = sizeof(p2p_tcp_proxy_header)+header->data_length-conn->recved_buffer_len;
				memcpy(conn->recved_buffer+conn->recved_buffer_len, receive_buffer+pos, copyed);
				
				if(header->command == P2P_COMMAND_USER_DATA 
					|| header->command == P2P_COMMAND_USER_AV_DATA
					|| header->command == P2P_COMMAND_USER_AV_NORESEND)//the user data
					on_p2p_conn_recved_user_data(conn);
				else
					process_p2p_conn_cmd(conn);
				conn->recved_buffer_len =0 ;
				pos += copyed;
				remain -= copyed;
			}
		}
	}
	pj_mutex_unlock(conn->receive_mutex);

}

void p2p_conn_pause_send(void* user_data, pj_bool_t pause)
{
	pj_ice_strans_p2p_conn* conn = (pj_ice_strans_p2p_conn*)user_data;
	if(!p2p_conn_is_valid(conn))
		return;

	if(conn->is_initiative)
	{
		p2p_tcp_listen_proxy** p2p_proxy = NULL;
		unsigned proxy_count = 0;
		pj_hash_iterator_t itbuf, *it;
		unsigned i;

		pj_grp_lock_acquire(conn->grp_lock);
		proxy_count = pj_hash_count(conn->tcp_listen_proxys);
		if(proxy_count)
		{
			p2p_tcp_listen_proxy** proxy;
			p2p_proxy = proxy = (p2p_tcp_listen_proxy**)p2p_malloc(sizeof(p2p_tcp_listen_proxy*)*proxy_count);

			it = pj_hash_first(conn->tcp_listen_proxys, &itbuf);
			while(it) 
			{
				*proxy = (p2p_tcp_listen_proxy*)pj_hash_this(conn->tcp_listen_proxys, it);
				pj_grp_lock_add_ref((*proxy)->grp_lock);
				it = pj_hash_next(conn->tcp_listen_proxys, it);
				proxy++;
			}
		}
		pj_grp_lock_release(conn->grp_lock);

		for(i=0; i<proxy_count; i++)
		{
			tcp_listen_proxy_pause_send(p2p_proxy[i], pause);
			pj_grp_lock_dec_ref(p2p_proxy[i]->grp_lock);
			
		}
		if(p2p_proxy)
			p2p_free(p2p_proxy);
	}
	else
	{
		tcp_connect_proxy_pause_send(&conn->tcp_connect_proxy, pause, TCP_SOCK_PACKAGE_SIZE);
	}
}

static void p2p_conn_clear_recv_buf(void* data)
{
	pj_ice_strans_p2p_conn* conn = (pj_ice_strans_p2p_conn*)data;
	if(conn->destroy_req)
	{
		pj_grp_lock_dec_ref(conn->grp_lock); //add in p2p_conn_set_opt P2P_RESET_BUF
		return;
	}

	pj_grp_lock_acquire(conn->grp_lock);

	if(conn->smooth) 
		p2p_smooth_reset(conn->smooth);

	pj_grp_lock_release(conn->grp_lock);

	pj_mutex_lock(conn->receive_mutex);
	//user command buffer
	conn->user_data_len = 0;
	conn->user_data_pkg_seq = 0;

	//pause receive buffer
	while(conn->pause_recv_first && conn->pause_recv_count )
	{
		p2p_tcp_data* next = conn->pause_recv_first->next;
		free_p2p_tcp_data(conn->pause_recv_first);
		conn->pause_recv_first = next;
		conn->pause_recv_count--;
	}
	conn->pause_recv_last = NULL;

	pj_mutex_unlock(conn->receive_mutex);

	pj_grp_lock_dec_ref(conn->grp_lock); //add in p2p_conn_set_opt P2P_RESET_BUF
}

pj_status_t p2p_conn_set_opt(pj_ice_strans_p2p_conn* conn, p2p_opt opt, const void* optval, int optlen)
{
	int status = PJ_SUCCESS;
	if(!p2p_conn_is_valid(conn))
		return PJ_EGONE;
	switch(opt)
	{
	case P2P_SNDBUF:
	case P2P_RCVBUF:
	case P2P_PAUSE_RECV:
		if(conn->is_initiative)
		{
			if(conn->udt.p2p_udt_connector)
				status = p2p_udt_connector_set_opt(conn->udt.p2p_udt_connector, opt, optval, optlen);
		}
		else
		{
			if(conn->udt.p2p_udt_accepter)
				status = p2p_udt_accepter_set_opt(conn->udt.p2p_udt_accepter, opt, optval, optlen);
		}
		break;
	case P2P_RESET_BUF:
		if(conn->is_initiative)
		{
			//clear all receive buffer
			p2p_socket_pair_item item;

			pj_grp_lock_add_ref(conn->grp_lock); //release in p2p_conn_clear_recv_buf
			item.cb = p2p_conn_clear_recv_buf;
			item.data = conn;
			schedule_socket_pair(get_p2p_global()->sock_pair, &item);		
			return PJ_SUCCESS;
		}
		else
		{
			//clear all send buffer
			p2p_udt_accepter_clear_send_buf(conn->udt.p2p_udt_accepter);	
			return PJ_SUCCESS;
		}
	default:
		return PJ_EINVALIDOP;
	}

	switch(opt)
	{
	case P2P_SNDBUF:
		{
			if(status == PJ_SUCCESS)
				conn->send_buf_size = *(int*)optval;
			break;
		}
	case P2P_RCVBUF:
		{
			if(status == PJ_SUCCESS)
				conn->recv_buf_size = *(int*)optval;
			break;
		}
	case P2P_PAUSE_RECV:
		{
			if(status == PJ_SUCCESS)
			{
				conn->is_pause_recv = *(pj_uint8_t*)optval;

				//cancel pause receive, 
				//post asynchronous request to callback pause data to user
				if(conn->is_pause_recv== 0 && conn->pause_recv_count)
				{
					p2p_socket_pair_item item;

					pj_grp_lock_add_ref(conn->grp_lock); //release in p2p_conn_post_pause_data
					item.cb = async_post_pause_data;
					item.data = conn;
					schedule_socket_pair(get_p2p_global()->sock_pair, &item);
				}
			}
		}
		break;
	}
	return status;
}

pj_status_t p2p_conn_proxy_get_remote_addr(pj_ice_strans_p2p_conn* conn, unsigned short port, char* addr, int* add_len)
{
	if(tcp_connect_proxy_find_port(&conn->tcp_connect_proxy, port))
	{
		pj_sockaddr_print(&conn->remote_internet_addr, addr, *add_len, 2);
		*add_len = strlen(addr);
		return PJ_SUCCESS;
	}
	else
	{
		return PJ_ENOTFOUND;
	}
}
