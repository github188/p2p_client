#include <pjnath/p2p_global.h>
#include <p2p_dispatch.h>
#include <common/gss_protocol.h>

#ifdef WIN32
#include <WinSock2.h>
#else
#include <sys/socket.h>
#include <sys/types.h>
#endif

#define GSS_DISP_TIMEOUT (30) //in second

extern void check_pj_thread();

struct p2p_dispatch_requester;

typedef struct addr_info_arg
{
	struct p2p_dispatch_requester* requester;
	int index;
	pj_status_t status;
	pj_addrinfo server_addr_info;
	pj_thread_t* thread;
}addr_info_arg;

//p2p dispatch requester
typedef struct p2p_dispatch_requester
{
	pj_pool_t* pool;	
	pj_grp_lock_t  *grp_lock;
	pj_bool_t destroy_req;/*To prevent duplicate destroy*/ 

	pj_str_t user;
	pj_str_t password;
	pj_str_t dest_user;
	void* user_data;
	DISPATCH_CALLBACK call_back;

	pj_timer_entry timer;
	char send_buffer[GSS_MAX_DISPATCH_LEN];

	char** recv_buffer;

	pj_activesock_t **activesock;
	pj_sock_t*		sock;
	pj_ioqueue_op_key_t* send_key; /*pj io send key*/

	pj_sockaddr** server_addr;
	unsigned int server_addr_count;
	pj_str_t* server_addr_str;
	int* server_port; 
	addr_info_arg* server_addr_info_arg; 

	char result_server[GSS_MAX_ADDR_LEN+16];

	unsigned int failed_count;

	unsigned char disp_cmd;
}p2p_dispatch_requester;

static void send_request_to_ds(p2p_dispatch_requester* requester, unsigned int index)
{
	GSS_DATA_HEADER* header = (GSS_DATA_HEADER*)requester->send_buffer;
	pj_ssize_t size = pj_ntohs(header->len) + sizeof(GSS_DATA_HEADER);

	if(requester->server_addr[index] && requester->activesock[index])
	{
		pj_activesock_send(requester->activesock[index], 
			&requester->send_key[index], 
			requester->send_buffer, 
			&size, 
			0);
	}
}

static void on_request_failed(p2p_dispatch_requester *requester, unsigned int index, int status)
{
	PJ_UNUSED_ARG(index);

	pj_grp_lock_acquire(requester->grp_lock);
	if(requester->destroy_req)
	{
		PJ_LOG(4,("on_request_failed", "destroyed"));
		pj_grp_lock_release(requester->grp_lock);
		return ;
	}
	requester->failed_count++;

	if(requester->failed_count == requester->server_addr_count && requester->call_back)
	{
		(*requester->call_back)(requester, status , requester->user_data, 0, 0, 0);
		requester->call_back = NULL;
	}

	pj_grp_lock_release(requester->grp_lock);
}

static void on_request_timer_event(pj_timer_heap_t *th, pj_timer_entry *e)
{
	p2p_dispatch_requester *requester = (p2p_dispatch_requester *) e->user_data;
	PJ_UNUSED_ARG(th);

	pj_grp_lock_acquire(requester->grp_lock);
	if(requester->destroy_req)
	{
		PJ_LOG(4,("on_request_timer_event", "destroyed"));
		pj_grp_lock_release(requester->grp_lock);
		return ;
	}
	
	PJ_LOG(4,("p2p_ds", "on_request_timer_event timeout %p", requester));
	if(requester->call_back)
	{
		(*requester->call_back)(requester, PJ_ETIMEDOUT , requester->user_data, 0, 0, 0);
		requester->call_back = NULL;
	}

	pj_grp_lock_release(requester->grp_lock);
}

/* Callback from active socket when incoming packet is received */
static pj_bool_t on_request_data_recv(pj_activesock_t *asock,
									  void *data,
									  pj_size_t size,
									  pj_status_t status,
									  pj_size_t *remainder)
{
	p2p_dispatch_requester *requester = (p2p_dispatch_requester*)pj_activesock_get_user_data(asock);

	pj_grp_lock_acquire(requester->grp_lock);
	if(requester->destroy_req)
	{
		PJ_LOG(4,("on_request_data_recv", "destroyed"));
		pj_grp_lock_release(requester->grp_lock);
		return PJ_TRUE;
	}

	if(status == PJ_SUCCESS)
	{
		GSS_DATA_HEADER* header = (GSS_DATA_HEADER*)data;
		unsigned short cmd_len = pj_ntohs(header->len)+sizeof(GSS_DATA_HEADER);

		if(cmd_len <= size) //got a full command
		{
			header->len = cmd_len - sizeof(GSS_DATA_HEADER);
			//
			if(header->cmd == GSS_DISPATCH_RESPONSE)
			{
				GSS_DISP_RESPONSE* response = (GSS_DISP_RESPONSE*)(header+1);
				if(requester->call_back)
				{
					(*requester->call_back)(requester, pj_ntohl(response->result), requester->user_data, 
						response->server, pj_ntohs(response->port), pj_ntohl(response->server_id));
					requester->call_back = NULL;
				}
			}

			*remainder = size - cmd_len;
			if(*remainder)
				memmove(data, (char*)data+cmd_len, *remainder);
		}
		else
		{
			*remainder = size;
		}
		status = PJ_TRUE;
	}
	else
	{
		unsigned int i;
		//tcp connection is closed 
		PJ_LOG(4,("p2p_ds", "on_request_data_recv closed, %p %p status %d", requester, asock, status));

		for(i=0; i<requester->server_addr_count; i++)
		{
			if(asock == requester->activesock[i])
			{
				on_request_failed(requester, i, status);
				break;
			}
		}
		status = PJ_FALSE;
	}

	pj_grp_lock_release(requester->grp_lock);

	return status;
}

static void dispatch_requester_on_destroy(void *comp)
{
	p2p_dispatch_requester *requester = (p2p_dispatch_requester*) comp;
	PJ_LOG(4,("p2p_ds", "dispatch_requester_on_destroy %p", requester));
	/* Destroy pool */
	if (requester->pool) {
		pj_pool_t *pool = requester->pool;
		requester->pool = NULL;
		delay_destroy_pool(pool);
	}
}

static int parse_multi_addr(p2p_dispatch_requester* requester, char* ds_addr)
{
	int i=0;
	char* addr = 0;
	char seps[] = ",;";
	char *token = 0;
	
	//get address count
	while(ds_addr[i] != '\0')
	{
		if(ds_addr[i]==',' || ds_addr[i]==';')
			requester->server_addr_count++;
		i++;
	}
	if(ds_addr[i-1] != ',' && ds_addr[i-1] !=';')
		requester->server_addr_count++;
	if(requester->server_addr_count == 0)
		return PJ_EINVAL;

	requester->activesock = pj_pool_zalloc(requester->pool, sizeof(pj_activesock_t*)*requester->server_addr_count);
	requester->sock = pj_pool_zalloc(requester->pool, sizeof(pj_sock_t)*requester->server_addr_count);
	requester->send_key = pj_pool_zalloc(requester->pool, sizeof(pj_ioqueue_op_key_t)*requester->server_addr_count);

	requester->recv_buffer = pj_pool_zalloc(requester->pool, sizeof(char*)*requester->server_addr_count);

	requester->server_addr = pj_pool_zalloc(requester->pool, sizeof(pj_sockaddr*)*requester->server_addr_count);
	requester->server_addr_str = pj_pool_zalloc(requester->pool, sizeof(pj_str_t)*requester->server_addr_count);
	requester->server_port = pj_pool_zalloc(requester->pool, sizeof(int)*requester->server_addr_count);
	requester->server_addr_info_arg = pj_pool_zalloc(requester->pool, sizeof(addr_info_arg)*requester->server_addr_count);
	i=0;
	addr = strdup(ds_addr);
	token = strtok(addr, seps);
	while(token)
	{
		int port = -1; 
		//reverse find ':'
		int len = strlen(token);
		int n=len-2;
		for(; n>0; n--)
		{
			if(token[n]==':')
			{
				port = atoi(token+n+1);
				token[n] = '\0';
				break;
			}
		}

		if(port <=0 || port >=65535)
		{
			free(addr);
			return PJ_EINVAL;
		}

		requester->server_addr[i] = NULL;
		pj_strdup2_with_null(requester->pool, &requester->server_addr_str[i], token);
		requester->server_port[i] = port;
		token = strtok(0, seps);
		i++;
	}
	free(addr);
	return PJ_SUCCESS;
}

static pj_bool_t on_request_connect_complete(pj_activesock_t *asock,  pj_status_t status)
{
	p2p_dispatch_requester *requester = (p2p_dispatch_requester*)pj_activesock_get_user_data(asock);
	
	PJ_LOG(4,("p2p_ds", "on_request_connect_complete %p, %p, status=%d", requester, asock, status));
	if(status == PJ_SUCCESS)
	{
		unsigned int i;
		for(i=0; i<requester->server_addr_count; i++)
		{
			if(asock == requester->activesock[i])
			{
				void *readbuf[1];

				send_request_to_ds(requester, i);

				/*start read socket data, callback in on_request_data_recv*/
				requester->recv_buffer[i] = pj_pool_zalloc(requester->pool, GSS_MAX_DISPATCH_LEN);
				readbuf[0] = requester->recv_buffer[i];
				status = pj_activesock_start_read2(requester->activesock[i], requester->pool, GSS_MAX_DISPATCH_LEN, readbuf, 0);
				break;
			}
		}
	}
	else
	{
		unsigned int i;
		for(i=0; i<requester->server_addr_count; i++)
		{
			if(asock == requester->activesock[i])
			{
				on_request_failed(requester, i, status);
				break;
			}
		}		
	}
	return status == PJ_SUCCESS;
}

static int p2p_dispatch_create_socket(p2p_dispatch_requester* requester, int index)
{
	int status;
	pj_activesock_cfg activesock_cfg;
	pj_activesock_cb activesock_cb;
#ifndef __LITEOS__ 
	struct linger so_linger;
#endif	
	status = pj_sock_socket(requester->server_addr[index]->addr.sa_family, pj_SOCK_STREAM(), 0, &requester->sock[index]);
	if(status != PJ_SUCCESS)
	{
		PJ_LOG(1,("p2p_ds", "create_p2p_dispatch_requester pj_sock_socket %d end", status));
		return status;
	}

	/*load test disable socket TIME_WAIT*/
#ifndef __LITEOS__ 
	so_linger.l_onoff = 0;
	so_linger.l_linger = 0;
	status = setsockopt(requester->sock[index], SOL_SOCKET, SO_LINGER, (const char*)&so_linger, sizeof(so_linger));
#endif

	/* Init send_key */
	pj_ioqueue_op_key_init(&requester->send_key[index], sizeof(requester->send_key[index]));

	pj_activesock_cfg_default(&activesock_cfg);
	activesock_cfg.grp_lock = requester->grp_lock;

	pj_bzero(&activesock_cb, sizeof(activesock_cb));
	activesock_cb.on_data_read = &on_request_data_recv;
	activesock_cb.on_connect_complete = &on_request_connect_complete;

	status = pj_activesock_create(requester->pool, requester->sock[index], 
		pj_SOCK_STREAM(), 
		&activesock_cfg, get_p2p_global()->ioqueue,
		&activesock_cb, requester,
		&requester->activesock[index]);
	if(status != PJ_SUCCESS)
	{
		PJ_LOG(1,("p2p_ds", "create_p2p_dispatch_requester pj_activesock_create %d end", status));
		pj_sock_close(requester->sock[index]);
		requester->sock[index] = PJ_INVALID_SOCKET;
		return status;
	}

	//connect dispatch server
	status = pj_activesock_start_connect(requester->activesock[index], requester->pool, requester->server_addr[index], pj_sockaddr_get_len(requester->server_addr[index]));

	PJ_LOG(4,("p2p_ds", "create_p2p_dispatch_requester pj_activesock_start_connect index %d, status %d, asock %p", index, status, requester->activesock[index]));

	if (status == PJ_SUCCESS)   //connect successful immediately
	{
		on_request_connect_complete(requester->activesock[index], PJ_SUCCESS);
		return PJ_SUCCESS;
	}
	else if (status != PJ_EPENDING) 
	{
		PJ_LOG(1,("p2p_ds", "create_p2p_dispatch_requester pj_activesock_start_connect %d", status));

		pj_activesock_close(requester->activesock[index]);
		requester->sock[index] = PJ_INVALID_SOCKET;
		requester->activesock[index] = NULL;
		return status;
	}


	return PJ_SUCCESS;
}

static int create_p2p_dispatch_requester(char* user,
										 char* password, 
										 char* dest_user, 
										 char* ds_addr,
										 unsigned char disp_cmd,
										 void* user_data, 
										 DISPATCH_CALLBACK cb,
										 p2p_dispatch_requester** r)
{
	pj_pool_t *pool;
	p2p_dispatch_requester* requester;
	int status;


	PJ_LOG(4,("p2p_ds", "create_p2p_dispatch_requester begin"));

	pool = pj_pool_create(&get_p2p_global()->caching_pool.factory, 
		"p2p_ds%p", 
		PJNATH_POOL_LEN_ICE_STRANS,
		PJNATH_POOL_INC_ICE_STRANS, 
		NULL);
	requester = PJ_POOL_ZALLOC_T(pool, p2p_dispatch_requester);
	pj_bzero(requester, sizeof(p2p_dispatch_requester));

	status = pj_grp_lock_create(pool, NULL, &requester->grp_lock);
	if(status != PJ_SUCCESS)
	{
		PJ_LOG(4,("p2p_ds", "create_p2p_dispatch_requester pj_grp_lock_create end"));
		pj_pool_release(pool);
		return status;
	}
	pj_grp_lock_add_ref(requester->grp_lock);
	pj_grp_lock_add_handler(requester->grp_lock, pool, requester, &dispatch_requester_on_destroy);

	requester->pool = pool;
	if(user)
		pj_strdup2_with_null(pool, &requester->user, user);
	if(password)
		pj_strdup2_with_null(pool, &requester->password, password);
	if(dest_user)
		pj_strdup2_with_null(pool, &requester->dest_user, dest_user);
	requester->disp_cmd = disp_cmd;
	requester->user_data = user_data;
	requester->call_back = cb;
	status = parse_multi_addr(requester, ds_addr);
	if(status != PJ_SUCCESS)
	{
		PJ_LOG(4,("p2p_ds", "create_p2p_dispatch_requester parse_multi_addr %s end", ds_addr));
		pj_pool_release(pool);
		return status;
	}

	pj_timer_entry_init(&requester->timer, 0, requester, &on_request_timer_event);

	*r = requester;
	PJ_LOG(4,("p2p_ds", "create_p2p_dispatch_requester end"));
	return PJ_SUCCESS;
}

void fill_request_send_buffer(p2p_dispatch_requester* requester)
{
	GSS_DATA_HEADER* header = (GSS_DATA_HEADER*)requester->send_buffer;
	GSS_DISP_REQUEST* disp_request = (GSS_DISP_REQUEST*)(header+1);

	header->cmd = requester->disp_cmd;
	header->data_seq = LAST_DATA_SEQ;
	header->len = pj_htons(sizeof(GSS_DISP_REQUEST));

	strcpy(disp_request->user, requester->user.ptr);
	strcpy(disp_request->password, requester->password.ptr);
}

void fill_query_send_buffer(p2p_dispatch_requester* requester)
{
	GSS_DATA_HEADER* header = (GSS_DATA_HEADER*)requester->send_buffer;
	GSS_DISP_QUERY* disp_query = (GSS_DISP_QUERY*)(header+1);

#define QUERY_PADDING_LEN (64) //too small size,the data will be  intercepted

	header->cmd = requester->disp_cmd;
	header->data_seq = LAST_DATA_SEQ;
	header->len = pj_htons(sizeof(GSS_DISP_QUERY)+QUERY_PADDING_LEN);

	strcpy(disp_query->dest_user, requester->dest_user.ptr);
}

//getaddrinfo or gethostbyname completed, send request to server
static void on_get_addr_info(void* data)
{
	addr_info_arg* aia = (addr_info_arg*)data;
	p2p_dispatch_requester* requester = aia->requester;
	int index = aia->index;
	char addr_str[PJ_INET6_ADDRSTRLEN];
	
	if(aia == NULL || aia->thread == NULL)
		return;
	
	PJ_LOG(4,("on_get_addr_info", "pj_thread_join %p %d", requester, index));
	pj_thread_join(aia->thread);
	pj_thread_destroy(aia->thread);
	PJ_LOG(4,("on_get_addr_info", "pj_thread_join %p %d end", requester, index));
	if(aia->status != PJ_SUCCESS)
	{
		PJ_LOG(4,("on_get_addr_info", "on_get_addr_info failed %d", index));
		on_request_failed(requester, index, aia->status);
		pj_grp_lock_dec_ref(requester->grp_lock);//add in p2p_dispatch_server
		return;
	}
	if(requester->destroy_req)
	{
		PJ_LOG(4,("on_get_addr_info", "destroyed1"));
		pj_grp_lock_dec_ref(requester->grp_lock);//add in p2p_dispatch_server
		return;
	}

	pj_grp_lock_acquire(requester->grp_lock);
	if(requester->destroy_req)
	{
		PJ_LOG(4,("on_get_addr_info", "destroyed2"));
		pj_grp_lock_release(requester->grp_lock);
		pj_grp_lock_dec_ref(requester->grp_lock);//add in p2p_dispatch_server
		return;
	}

	pj_sockaddr_print(&aia->server_addr_info.ai_addr, addr_str, sizeof(addr_str), 3);
	PJ_LOG(4,("on_get_addr_info", "on_get_addr_info ok %d %s", index, addr_str));

	requester->server_addr[index] = &aia->server_addr_info.ai_addr;
	//create tcp socket
	p2p_dispatch_create_socket(requester, index);
	
	pj_grp_lock_release(requester->grp_lock);
	pj_grp_lock_dec_ref(requester->grp_lock);//add in p2p_dispatch_server
}

static int get_addr_info_thread(void *arg)
{
	unsigned cnt = 1;
	addr_info_arg* aia = (addr_info_arg*)arg;
	int index = aia->index;
	p2p_dispatch_requester* requester = aia->requester;
	p2p_socket_pair_item item;

	PJ_LOG(4,("get_addr_info_thread", "pj_getaddrinfo begin %p %d %.*s", requester, index, requester->server_addr_str[index].slen, requester->server_addr_str[index].ptr));
	//maybe long time
	aia->status = pj_getaddrinfo(
		pj_AF_UNSPEC(),  //for support ipv6 and ipv4
		&requester->server_addr_str[index], 
		&cnt,
		&aia->server_addr_info);

	PJ_LOG(4,("get_addr_info_thread", "pj_getaddrinfo end %p %d %d", requester, index, aia->status));
	if(aia->status == PJ_SUCCESS)
		pj_sockaddr_set_port(&aia->server_addr_info.ai_addr, (pj_uint16_t)requester->server_port[index]);

	//schedule io thread call on_get_addr_info
	item.cb = on_get_addr_info;
	item.data = aia;
	schedule_socket_pair(get_p2p_global()->sock_pair, &item);
	return 0;
}

typedef void (*fill_send_buffer_func)(p2p_dispatch_requester* requester);

P2P_DECL(int) p2p_dispatch_server(char* user,
								  char* password, 
								  char* dest_user, 
								  char* ds_addr,
								  unsigned char disp_cmd,	
								  void* user_data,
								  DISPATCH_CALLBACK cb,
								  fill_send_buffer_func fill_buffer_func,
								  void** dispatcher)
{
	int result;
	unsigned int i;
	p2p_dispatch_requester* requester = 0;
	pj_time_val delay = { GSS_DISP_TIMEOUT, 0 };

	result = create_p2p_dispatch_requester(user, password, dest_user, ds_addr, disp_cmd, user_data, cb, &requester) ; 
	if(result != PJ_SUCCESS)
		return result;

	fill_buffer_func(requester);

	pj_timer_heap_schedule_w_grp_lock(get_p2p_global()->timer_heap, &requester->timer,
		&delay, 1,	requester->grp_lock);

	//for dns, getaddrinfo or gethostbyname maybe long time,so use thread 
	for(i=0; i<requester->server_addr_count; i++)
	{
		pj_status_t status;

		requester->server_addr_info_arg[i].requester = requester;
		requester->server_addr_info_arg[i].index = i;

		//check address is ipv4
		requester->server_addr_info_arg[i].server_addr_info.ai_addr.addr.sa_family = pj_AF_INET();
		status = pj_inet_pton(pj_AF_INET(), 
			&requester->server_addr_str[i], 
			&requester->server_addr_info_arg[i].server_addr_info.ai_addr.ipv4.sin_addr);
		
		//check address is ipv6
		if (status != PJ_SUCCESS)
		{
			requester->server_addr_info_arg[i].server_addr_info.ai_addr.addr.sa_family = pj_AF_INET6();
			status = pj_inet_pton(pj_AF_INET6(), 
				&requester->server_addr_str[i], 
				&requester->server_addr_info_arg[i].server_addr_info.ai_addr.ipv6.sin6_addr);
		}
		if (status != PJ_SUCCESS)
		{
			pj_grp_lock_add_ref(requester->grp_lock);//release in on_get_addr_info
			pj_thread_create(requester->pool, 
				"get_addr_info", 
				&get_addr_info_thread, 
				&requester->server_addr_info_arg[i], 
				0, 
				0, 
				&requester->server_addr_info_arg[i].thread);
		}
		else
		{
			pj_sockaddr_set_port(&requester->server_addr_info_arg[i].server_addr_info.ai_addr, (pj_uint16_t)requester->server_port[i]);
			requester->server_addr[i] = &requester->server_addr_info_arg[i].server_addr_info.ai_addr;
			p2p_dispatch_create_socket(requester, i);
		}
	}
	*dispatcher = requester;

	PJ_LOG(4,("p2p_ds", "p2p_request_dispatch_server user %s,password %s,dest_user %s,ds_addr %s,disp_cmd %d,requester %p",
		user, password, dest_user, ds_addr, disp_cmd, requester));
	return result;
}

P2P_DECL(int) p2p_request_dispatch_server(char* user, 
										  char* password, 
										  char* ds_addr, 
										  void* user_data, 
										  DISPATCH_CALLBACK cb,
										  void** dispatcher)
{
	if(dispatcher == 0
		||user == 0 
		|| password == 0
		|| ds_addr == 0
		|| cb == 0
		|| strlen(user) >= MAX_UID_LEN
		|| strlen(password) >= MAX_UID_LEN)
		return PJ_EINVAL;
	check_pj_thread();
	return p2p_dispatch_server(user, password, 0, ds_addr, GSS_P2P_DISPATCH_REQUEST, user_data, cb, fill_request_send_buffer, dispatcher); 
}

P2P_DECL(int) gss_request_dispatch_server(char* user, 
										  char* password, 
										  char* ds_addr, 
										  void* user_data, 
										  DISPATCH_CALLBACK cb,
										  void** dispatcher)
{
	if(dispatcher == 0
		||user == 0 
		|| password == 0
		|| ds_addr == 0
		|| cb == 0
		|| strlen(user) >= MAX_UID_LEN
		|| strlen(password) >= MAX_UID_LEN)
		return PJ_EINVAL;
	check_pj_thread();
	return p2p_dispatch_server(user, password, 0, ds_addr, GSS_DISPATCH_REQUEST, user_data, cb, fill_request_send_buffer, dispatcher);
}

P2P_DECL(int) p2p_query_dispatch_server(char* dest_user, 
										char* ds_addr, 
										void* user_data, 
										DISPATCH_CALLBACK cb,
										void** dispatcher)
{
	if(dispatcher == 0
		||ds_addr == 0
		|| dest_user == 0
		|| cb == 0
		|| strlen(dest_user) >= MAX_UID_LEN)
		return PJ_EINVAL;
	check_pj_thread();
	return p2p_dispatch_server(0, 0, dest_user, ds_addr, GSS_P2P_DISPATCH_QUERY, user_data, cb, fill_query_send_buffer, dispatcher); 
}

P2P_DECL(int) gss_query_dispatch_server(char* dest_user, 
										char* ds_addr, 
										void* user_data, 
										DISPATCH_CALLBACK cb,
										void** dispatcher)
{
	if(dispatcher == 0
		||ds_addr == 0
		|| dest_user == 0
		|| cb == 0
		|| strlen(dest_user) >= MAX_UID_LEN)
		return PJ_EINVAL;
	check_pj_thread();
	return p2p_dispatch_server(0, 0, dest_user, ds_addr, GSS_DISPATCH_QUERY, user_data, cb, fill_query_send_buffer, dispatcher);
}

static void destroy_dispatch_requester(void* dispatcher)
{
	p2p_dispatch_requester* requester = (p2p_dispatch_requester*)dispatcher;
	pj_activesock_t **activesock = NULL;
	pj_sock_t* sock = NULL;
	unsigned int server_count = 0;
	unsigned int i;

	check_pj_thread();

	PJ_LOG(4,("p2p_ds", "destroy_dispatch_requester %p", requester));
	
	if (requester == NULL || requester->destroy_req) { //already destroy, so return
		return;
	}

	pj_grp_lock_acquire(requester->grp_lock);
	if (requester->destroy_req) { //already destroy, so return
		pj_grp_lock_release(requester->grp_lock);
		return;
	}
	requester->destroy_req = PJ_TRUE;

	pj_timer_heap_cancel_if_active(get_p2p_global()->timer_heap, &requester->timer, 0);

	server_count = requester->server_addr_count;
	if(server_count > 0)
	{
		sock = (pj_sock_t*)p2p_malloc(sizeof(pj_sock_t)*server_count);
		memset(sock, 0, sizeof(pj_sock_t)*server_count);

		activesock = (pj_activesock_t **)p2p_malloc(sizeof(pj_activesock_t *)*server_count);
		memset(activesock, 0, sizeof(pj_activesock_t *)*server_count);

		for(i=0; i<server_count; i++)
		{
			if (requester->activesock[i] != NULL) 
			{
				activesock[i] = requester->activesock[i];
				requester->sock[i] = PJ_INVALID_SOCKET;		
				requester->activesock[i] = NULL;
			} 
			else if (requester->sock[i] != PJ_INVALID_SOCKET)
			{
				sock[i] = requester->sock[i];		
				requester->sock[i] = PJ_INVALID_SOCKET;
			}
		}
	}

	pj_grp_lock_release(requester->grp_lock);

	for(i=0; i<server_count; i++)
	{
		if(activesock[i])
			pj_activesock_close(activesock[i]);
		else if(sock[i] != PJ_INVALID_SOCKET && sock[i]) 
			pj_sock_close(sock[i]);
	}	

	if(activesock)
		p2p_free(activesock);

	if(sock)
		p2p_free(sock);

	//release self reference count
	pj_grp_lock_dec_ref(requester->grp_lock);

}

P2P_DECL(void) destroy_p2p_dispatch_requester(void* dispatcher)
{
	destroy_dispatch_requester(dispatcher);
}

P2P_DECL(void) destroy_gss_dispatch_requester(void* dispatcher)
{
	destroy_dispatch_requester(dispatcher);
}