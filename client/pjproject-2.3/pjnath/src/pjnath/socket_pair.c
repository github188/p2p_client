#include <pjnath/p2p_global.h>
#include <pjnath/socket_pair.h>

enum{CONNECT_INDEX, ACCEPT_INDEX};

typedef struct p2p_socket_pair_list_item
{
	p2p_socket_pair_item item;
	struct p2p_socket_pair_list_item* next;
}p2p_socket_pair_list_item;

struct p2p_socket_pair
{
	pj_sock_t fd[2]; /*0 is connect socket, 1 is accept socket*/
	pj_activesock_t* activesock; /*accept activesock*/
	char read_buffer; /*async read buffer,1 byte*/

	pj_mutex_t* list_mutex;
	/*first_item is the first call back
	  last_item is the last call back
	  free_item is free list for reuse memory
	*/
	p2p_socket_pair_list_item* first_item, *last_item, *free_item;
};

static pj_bool_t on_socket_pair_read(pj_activesock_t *asock,
									 void *data,
									 pj_size_t size,
									 pj_status_t status,
									 pj_size_t *remainder);

#ifdef ANDROID_BUILD
#define HAVE_SOCKET_PAIR 1
#include <sys/socket.h>
#endif


static int	socket_pair(pj_sock_t fd[2])
{
#ifdef HAVE_SOCKET_PAIR
	int sock[2];
	int r = socketpair(AF_UNIX, SOCK_STREAM, 0, sock);
	if(r == 0)
	{
		fd[0] = sock[0];
		fd[1] = sock[1];
	}
	return r;
#else
	pj_sock_t listener = PJ_INVALID_SOCKET;
	pj_sock_t connector = PJ_INVALID_SOCKET;
	pj_sock_t acceptor = PJ_INVALID_SOCKET;
	int addr_len;
	pj_status_t status;
	pj_sockaddr_in listen_addr;
	pj_str_t local_addr = pj_str(LOCAL_HOST_IP);
	do 
	{
		/*create socket and listen*/
		status = pj_sock_socket(pj_AF_INET(), pj_SOCK_STREAM(),	0, &listener);
		if (status != PJ_SUCCESS)
			return -1;

		status = pj_sockaddr_in_init(&listen_addr, &local_addr, 0);
		if (status != PJ_SUCCESS) 
			break;

		status = pj_sock_bind_random(listener, &listen_addr, 0, MAX_P2P_BIND_RETRY);
		if (status != PJ_SUCCESS) 
			break;

		addr_len = sizeof(listen_addr);
		status = pj_sock_getsockname(listener, &listen_addr, &addr_len);
		if (status != PJ_SUCCESS) 
			break;

		status = pj_sock_listen(listener, 1);
		if (status != PJ_SUCCESS)
			break;
	
		//create socket and connect local host listen socket
		status = pj_sock_socket(pj_AF_INET(), pj_SOCK_STREAM(),	0, &connector);
		if (status != PJ_SUCCESS)
			break;
	
		status = pj_sock_connect(connector, &listen_addr, addr_len);
		if (status != PJ_SUCCESS)
			break;
		
		//accept a socket
		status = pj_sock_accept(listener, &acceptor, NULL, NULL);
		if (status != PJ_SUCCESS)
			break;
		pj_sock_close(listener); //the listener out of use, close it
		listener = PJ_INVALID_SOCKET;

		fd[CONNECT_INDEX] = connector;
		fd[ACCEPT_INDEX] = acceptor;

		return 0;
	} while (0);

	if (listener != PJ_INVALID_SOCKET)
		pj_sock_close(listener);
	if (connector != PJ_INVALID_SOCKET)
		pj_sock_close(connector);
	if (acceptor != PJ_INVALID_SOCKET)
		pj_sock_close(acceptor);
	return -1;
#endif
}

/*if free list has item, get it. else call system malloc*/
PJ_INLINE(p2p_socket_pair_list_item*) malloc_list_item(p2p_socket_pair* sock_pair, p2p_socket_pair_item* item)
{
	p2p_socket_pair_list_item* list_item;
	if(sock_pair->free_item)
	{
		list_item = sock_pair->free_item;
		sock_pair->free_item = sock_pair->free_item->next;
	}
	else
	{
		list_item = p2p_malloc(sizeof(p2p_socket_pair_list_item));
	}
	list_item->item.cb = item->cb;
	list_item->item.data = item->data;
	list_item->next = NULL;
	return list_item;
}

//add item to free list
PJ_INLINE(void) free_list_item(p2p_socket_pair* sock_pair, p2p_socket_pair_list_item* list_item)
{
	list_item->next = sock_pair->free_item;
	sock_pair->free_item = list_item;
}

PJ_INLINE(void) free_list_memory(p2p_socket_pair_list_item* list_item)
{
	p2p_socket_pair_list_item *cur_list_item;
	while(list_item)
	{
		cur_list_item = list_item;
		list_item = list_item->next;
		p2p_free(cur_list_item);
	}
}

void run_socket_pair(p2p_socket_pair *sock_pair)
{
	p2p_socket_pair_list_item* list_item, *cur_list_item;

	if(!sock_pair->first_item)
		return;

	pj_mutex_lock(sock_pair->list_mutex);
	cur_list_item = list_item = sock_pair->first_item;
	sock_pair->first_item = sock_pair->last_item = NULL;//reset call back list
	pj_mutex_unlock(sock_pair->list_mutex);

	while(cur_list_item)
	{
		if(cur_list_item->item.cb)
			(*cur_list_item->item.cb)(cur_list_item->item.data);

		PJ_LOG(5, ("on_socket_pair_read", "on_socket_pair_read call back %p", cur_list_item));
		cur_list_item = cur_list_item->next;
	}

	pj_mutex_lock(sock_pair->list_mutex);
	while(list_item)
	{
		cur_list_item = list_item;
		list_item = list_item->next;
		free_list_item(sock_pair, cur_list_item);
		PJ_LOG(5, ("on_socket_pair_read", "free_list_item"));
	}
	pj_mutex_unlock(sock_pair->list_mutex);
}

void socket_pair_close_activesock(p2p_socket_pair* sock_pair)
{
	if(sock_pair->activesock)
	{
		pj_activesock_close(sock_pair->activesock);
	}
	else
	{
		pj_sock_close(sock_pair->fd[ACCEPT_INDEX]);
	}
	pj_sock_close(sock_pair->fd[CONNECT_INDEX]);
	sock_pair->fd[CONNECT_INDEX] = sock_pair->fd[ACCEPT_INDEX] = PJ_INVALID_SOCKET;
}

pj_status_t socket_pair_create_activesock(p2p_socket_pair* pair, pj_pool_t *pool)
{
	pj_status_t status;
	pj_activesock_cfg asock_cfg;
	pj_activesock_cb tcp_callback;
	void *readbuf[1];

	//init accept socket to activesock
	pj_activesock_cfg_default(&asock_cfg);
	pj_bzero(&tcp_callback, sizeof(tcp_callback));
	tcp_callback.on_data_read = &on_socket_pair_read;

	status = pj_activesock_create(pool, 
		pair->fd[ACCEPT_INDEX],
		pj_SOCK_STREAM(),
		&asock_cfg, 
		get_p2p_global()->ioqueue, 
		&tcp_callback,
		pair, 
		&pair->activesock) ;
	if (status != PJ_SUCCESS) 
	{
		pj_sock_close(pair->fd[ACCEPT_INDEX]);
		pj_sock_close(pair->fd[CONNECT_INDEX]);
		return status;
	}

	//start receive connect sock data,the call back function is on_socket_pair_read
	readbuf[0] = &pair->read_buffer;
	status = pj_activesock_start_read2(pair->activesock, pool, 1, readbuf, 0);
	if (status != PJ_SUCCESS && status != PJ_EPENDING)
	{
		pj_sock_close(pair->fd[ACCEPT_INDEX]);
		pj_sock_close(pair->fd[CONNECT_INDEX]);
		return status;
	}
	return PJ_SUCCESS;
}


//get items from call back list, call all. then clean call back list
static pj_bool_t on_socket_pair_read(pj_activesock_t *asock,
								   void *data,
								   pj_size_t size,
								   pj_status_t status,
								   pj_size_t *remainder)
{
	p2p_socket_pair *sock_pair = (p2p_socket_pair*)pj_activesock_get_user_data(asock);

	PJ_UNUSED_ARG(remainder);
	PJ_UNUSED_ARG(data);
	PJ_UNUSED_ARG(size);
	if(status == PJ_SUCCESS)
	{
		run_socket_pair(sock_pair);
	}
	else //in ios,app goto background 180 second,socket is closed by system,so recreate
	{
		int result;
		PJ_LOG(2, ("on_socket_pair_read", "status=%d", status));

		socket_pair_close_activesock(sock_pair);

		result = socket_pair(sock_pair->fd);
		if(result == -1)
			return PJ_FALSE;

		result = socket_pair_create_activesock(sock_pair, get_p2p_global()->pool);
		if(result != PJ_SUCCESS)
			return PJ_FALSE;
	}
	return PJ_TRUE;
}


pj_status_t create_socket_pair(p2p_socket_pair** sock_pair, pj_pool_t *pool)
{
	p2p_socket_pair* pair;
	int result;

	pair = PJ_POOL_ZALLOC_T(pool, p2p_socket_pair);
	pj_bzero(pair, sizeof(p2p_socket_pair));

	result = socket_pair(pair->fd);
	if(result == -1)
		return PJ_SOCK_PAIR_ERROR;

	result = socket_pair_create_activesock(pair, pool);
	if(result != PJ_SUCCESS)
		return result;
	
	pair->first_item = pair->last_item = pair->free_item = NULL;
	pj_mutex_create_recursive(pool, NULL, &pair->list_mutex);
	
	*sock_pair = pair;
	return PJ_SUCCESS;
}


void destroy_socket_pair(p2p_socket_pair* sock_pair)
{
	pj_mutex_lock(sock_pair->list_mutex);
	
	socket_pair_close_activesock(sock_pair);

	free_list_memory(sock_pair->first_item);
	free_list_memory(sock_pair->free_item);
	sock_pair->first_item = sock_pair->last_item = sock_pair->free_item = NULL;
	pj_mutex_unlock(sock_pair->list_mutex);

	pj_mutex_destroy(sock_pair->list_mutex);
}

//schedule a call back to network io thread
void schedule_socket_pair(p2p_socket_pair* sock_pair, p2p_socket_pair_item* item)
{
	static char buf[1]={0};
	static pj_ssize_t size = 1;
	p2p_socket_pair_list_item* list_item;

	pj_mutex_lock(sock_pair->list_mutex);
	list_item = malloc_list_item(sock_pair, item);
	if(sock_pair->first_item)
	{
		sock_pair->last_item->next = list_item;
		sock_pair->last_item = list_item;
	}
	else
	{
		sock_pair->last_item = sock_pair->first_item = list_item;
	}
	pj_mutex_unlock(sock_pair->list_mutex);

	pj_sock_send(sock_pair->fd[CONNECT_INDEX], buf, &size, 0);
	PJ_LOG(5, ("schedule_socket_pair", "schedule_socket_pair called %p", list_item));
}