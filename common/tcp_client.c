#include "tcp_client.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifndef WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

#ifdef TCP_CLIENT_USE_LOG
#include "log.h"
#endif

#include "gss_common.h"
#include "gss_mem.h"

/*initialize receive buffer length*/
#define INIT_RECV_BUFFER_LEN (1024)

/*max receive buffer length,64k*/
#define MAX_RECV_BUFFER_LEN (1024*64) 

/*close socket and gss_free all event*/
static void tcp_client_close_internal(tcp_client* client){
	if(client->recv_event){
		event_free(client->recv_event);
		client->recv_event = NULL;
	}

	if(client->send_event){
		event_free(client->send_event);
		client->send_event = NULL;
	}

	if(client->connect_event)
	{
		event_free(client->connect_event);
		client->connect_event = NULL;
	}

	if(client->sock_fd != -1){
		evutil_closesocket(client->sock_fd);
		client->sock_fd = -1;
	}
}


static void close_tcp_client(void* client, unsigned char callback){	
	tcp_client* c = (tcp_client*)client;
	if(c){
		tcp_client_close_internal(c);

		/*if destroy_tcp_client not called*/
		if(callback && c->on_close)
			c->on_close(c);
	}
}


/*socket receive event callback function*/
static void tcp_client_recv_cb(evutil_socket_t sockfd, short event_type, void *arg){
	unsigned char closed = 0;
	tcp_client* client = (tcp_client*)arg;
	int recved;

	GSS_UNUSED_ARG(event_type);

	recved = recv(sockfd,
		client->recv_buffer+client->recved_len, 
		client->recv_capacity-client->recved_len, 
		0);
	if(recved <= 0){
#ifdef TCP_CLIENT_USE_LOG
		LOG(LOG_LEVEL_INFO, "tcp_client_recv_cb recv error,socket %d, client %p,recved %d,errno %d",sockfd, client, recved, errno);
#endif
		/*socket is closed*/
		closed = 1 ;
	}
	else{
		unsigned int pos = 0;
		GSS_DATA_HEADER* h;
		unsigned int data_len;
		unsigned int package_len;

		client->recved_len += recved;

		while(1){
			if(client->recved_len < sizeof(GSS_DATA_HEADER)){
				if(client->recved_len > 0 && pos != 0)
					memmove(client->recv_buffer, client->recv_buffer+pos, client->recved_len);
				return;
			}
			h = (GSS_DATA_HEADER*)(client->recv_buffer+pos);
			data_len = ntohs(h->len);
			
			/*check data is valid*/
			if(data_len > MAX_RECV_BUFFER_LEN-sizeof(GSS_DATA_HEADER) ){
#ifdef TCP_CLIENT_USE_LOG
				LOG(LOG_LEVEL_ERROR, "recv invalid data data_len %d,socket %d, client %p", data_len, sockfd, client);
#endif
				/*invalid data,close socket*/
				closed = 1 ; 
				break;
			}
			package_len = data_len+sizeof(GSS_DATA_HEADER);
			/*receive a full data package, call on_recv */
			if(client->recved_len >= package_len && client->on_recv){
				int ret;
				ret = client->on_recv(client, client->recv_buffer+pos, package_len);
				if(ret != 0){
#ifdef TCP_CLIENT_USE_LOG
					LOG(LOG_LEVEL_WARN, "on_recv return ret %d,socket %d, client %p", ret, sockfd, client);
#endif
					closed = 1 ;
					break;
				}
				pos += package_len;
				client->recved_len -= package_len;
			}
			else{
				/*only receive part data*/
				if(pos != 0)
					memmove(client->recv_buffer, client->recv_buffer+pos, client->recved_len);

				//realloc receive buffer, package_len*recv_buf_ratio
				if(package_len*client->recvbuf_ratio > client->recv_capacity){
					client->recv_capacity *= 2;
					while(client->recv_capacity < package_len*client->recvbuf_ratio)
						client->recv_capacity *= 2;
					client->recv_buffer = (char*)gss_realloc(client->recv_buffer, client->recv_capacity);
#ifdef TCP_CLIENT_USE_LOG
					LOG(LOG_LEVEL_DEBUG, "tcp_client_recv_cb realloc %p %d", client, client->recv_capacity);
#endif
				}
				return;
			}
		}
	}
	
	if(closed)
		close_tcp_client(client, 1);
}


/*socket send event callback function*/
static void tcp_client_send_cb(int sockfd, short event_type, void *arg)
{
	tcp_client* client = (tcp_client*)arg;
	
	GSS_UNUSED_ARG(event_type);
	GSS_UNUSED_ARG(sockfd);

	while(client->first_send_data){
		SEND_DATA* send_data = client->first_send_data;
		SEND_DATA* next = send_data->next;
		char* buffer;
		int sended;

#ifdef USE_SHARED_CMD
		if(send_data->sc)
			buffer = send_data->sc->cmd;
		else
#endif
			buffer = (char*)(send_data+1);

		sended = send(client->sock_fd, buffer+send_data->sended_len, send_data->total_len-send_data->sended_len, 0);
		if(sended < 0){
			/*if socket system buffer is full,pending*/
#ifdef WIN32
			if(EVUTIL_SOCKET_ERROR() != WSAEWOULDBLOCK)
#else
			if(EVUTIL_SOCKET_ERROR() != EAGAIN)
#endif		
			{
				/*socket maybe closed or invalid*/
#ifdef TCP_CLIENT_USE_LOG
				LOG(LOG_LEVEL_DEBUG, "tcp_client_send_cb socket return %d,%d", sended, errno);
#endif
				return;
			}
			sended = 0;
		}
		send_data->sended_len += sended;
		/*only send part of data, add send event*/
		if(send_data->sended_len < send_data->total_len){
#ifdef TCP_CLIENT_USE_LOG
			LOG(LOG_LEVEL_DEBUG, "tcp_client_send_cb event_add, socket %d, client %p", client->sock_fd, client);
#endif
			event_add(client->send_event, NULL);
			return;
		}
		/*remove first data and send next data*/
#ifdef USE_SHARED_CMD
		if(send_data->sc)
			shared_cmd_release(send_data->sc);
#endif

		gss_free(send_data);
		client->first_send_data = next;		
	}
}



//add socket receive event for receive peer data
static int tcp_client_async_read(tcp_client* client)
{
	if(client){
		int ev_result = -1;
		if(client->recv_event)
			ev_result = event_add(client->recv_event, NULL); 
#ifdef TCP_CLIENT_USE_LOG
		LOG(LOG_LEVEL_DEBUG, "tcp_client_async_read %p fd %d error %d", client, client->sock_fd, ev_result);
#endif
		return ev_result;
	}
	return -1;
}

void* create_tcp_client(evutil_socket_t sock, struct event_base * eb,
						tcp_client_on_recv_func on_recv, tcp_client_on_close_func on_close, void* user_data, unsigned char async_read){
	tcp_client* client = (tcp_client*)gss_malloc(sizeof(tcp_client));

	client->sock_fd = sock;
	evutil_make_socket_nonblocking(sock);

	client->eb = eb;
	client->user_data = user_data;
	
	client->on_recv = on_recv;
	client->on_close = on_close;

	client->send_event = event_new(client->eb, client->sock_fd, EV_WRITE, tcp_client_send_cb, (void*)client);
	client->first_send_data = NULL;
	client->last_send_data = NULL;

	client->connect_event = NULL;
	client->on_connect = NULL;
	
	client->recvbuf_ratio = 1;
	client->recved_len = 0;		
	client->recv_buffer = (char*)gss_malloc(INIT_RECV_BUFFER_LEN);
	client->recv_capacity = INIT_RECV_BUFFER_LEN;
	client->recv_event = event_new(client->eb, client->sock_fd, EV_READ|EV_PERSIST, tcp_client_recv_cb, (void*)client);
	
	if(async_read)
		tcp_client_async_read(client);
	return client;
}

void destroy_tcp_client(void* client){
	tcp_client* c = (tcp_client*)client;
	SEND_DATA* send_data;
	if(c == NULL)
		return;
	
	tcp_client_close_internal(c);

	send_data = c->first_send_data;
	while(send_data){
		SEND_DATA* next = send_data->next;
#ifdef USE_SHARED_CMD
		if(send_data->sc)
			shared_cmd_release(send_data->sc);
#endif
		gss_free(send_data);
		send_data = next;
	}
	gss_free(c->recv_buffer);

	gss_free(c);
}

#ifdef WIN32
int tcp_client_writev(void* client, const struct iovec *iov, int cnt){
	int sended = 0;
	int i;
	for(i=0; i<cnt; i++)
	{
		int ret = tcp_client_send(client, iov[i].iov_base, iov[i].iov_len);
		if(ret <= 0)
			return ret;

		sended += ret;
	}
	return sended;
}


#ifdef USE_SHARED_CMD
int tcp_client_shared_writev(void* client, const struct iovec *iov, shared_cmd** sc, int cnt)
{
	int sended = 0;
	int i;
	for(i=0; i<cnt; i++)
	{
		int ret = tcp_client_send(client, (const char*)iov[i].iov_base, iov[i].iov_len);
		if(ret <= 0)
		{
			int m;
			for(m=i; m<cnt; m++)
				shared_cmd_release(sc[m]);
			return ret;
		}
		shared_cmd_release(sc[i]);
		sended += ret;
	}
	return sended;
}
#endif

#else

#ifdef USE_SHARED_CMD
int tcp_client_shared_writev(void* client, const struct iovec *iov, shared_cmd** sc, int cnt)
{
	tcp_client* c = (tcp_client*)client;
	int total_len = 0;
	int i;

	for(i=0; i<cnt; i++)
		total_len += iov[i].iov_len;

	/*if send data list is not empty, add data to tail*/
	if(c->first_send_data){
		for(i=0; i<cnt; i++){
			SEND_DATA* send_data = (SEND_DATA*)gss_malloc(sizeof(SEND_DATA));
			send_data->total_len = iov[i].iov_len;
			send_data->sended_len = 0;
			send_data->next = NULL;
			send_data->sc = sc[i];
			c->last_send_data->next = send_data;
			c->last_send_data = send_data;
		}
		return total_len;
	}
	else{
		int sended = writev(c->sock_fd, iov, cnt);

		if(sended < 0){
			//socket system buffer is full,pending
#ifdef WIN32
			if(EVUTIL_SOCKET_ERROR() != WSAEWOULDBLOCK)
#else
			if(EVUTIL_SOCKET_ERROR() != EAGAIN)
#endif		
			{
#ifdef TCP_CLIENT_USE_LOG
				LOG(LOG_LEVEL_WARN, "tcp_client_writev return %d, socket %d, error %d", sended, c->sock_fd, errno);
#endif
				return sended;
			}
			sended = 0;
		}
		/*only send part of data*/
		if(sended < total_len){
			
			SEND_DATA* send_data;

			for (i=0; sended >= iov[i].iov_len; i++) {
				shared_cmd_release(sc[i]);
				sended -= iov[i].iov_len;
			}
			
			send_data = (SEND_DATA*)gss_malloc(sizeof(SEND_DATA));
			
			send_data->sc = sc[i];
			send_data->total_len = iov[i].iov_len;
			send_data->sended_len = sended;
			send_data->next = NULL;
			c->first_send_data = c->last_send_data = send_data;

			for(i++; i<cnt; i++){
				send_data = (SEND_DATA*)gss_malloc(sizeof(SEND_DATA));
				send_data->total_len = iov[i].iov_len;
				send_data->sended_len = 0;
				send_data->next = NULL;
				send_data->sc = sc[i];
				c->last_send_data->next = send_data;
				c->last_send_data = send_data;
			}
#ifdef TCP_CLIENT_USE_LOG
			LOG(LOG_LEVEL_DEBUG, "tcp_client_shared_writev event_add, socket %d, client %p", c->sock_fd, c);
#endif

			/*for call tcp_client_send_cb*/
			event_add(c->send_event, NULL);
		}
		else
		{
			for(i=0; i<cnt; i++)
				shared_cmd_release(sc[i]);
		}
		return total_len;
	}
}
#endif

int tcp_client_writev(void* client, const struct iovec *iov, int cnt){
	tcp_client* c = (tcp_client*)client;
	int total_len = 0;
	int i;

	for(i=0; i<cnt; i++)
		total_len += iov[i].iov_len;
	
	/*if send data list is not empty, copy data and add data to tail*/
	if(c->first_send_data){
		SEND_DATA* send_data = (SEND_DATA*)gss_malloc(sizeof(SEND_DATA)+total_len);
		char* p = (char*)(send_data+1);
		send_data->total_len = total_len;
		send_data->sended_len = 0;
		send_data->next = NULL;
#ifdef USE_SHARED_CMD
		send_data->sc = NULL;
#endif
		for(i=0; i<cnt; i++){
			memcpy(p, iov[i].iov_base, iov[i].iov_len);
			p += iov[i].iov_len;
		}
		c->last_send_data->next = send_data;
		c->last_send_data = send_data;
		return total_len;
	}
	else{
		int sended = writev(c->sock_fd, iov, cnt);
		
		if(sended < 0){
			//socket system buffer is full,pending
#ifdef WIN32
			if(EVUTIL_SOCKET_ERROR() != WSAEWOULDBLOCK)
#else
			if(EVUTIL_SOCKET_ERROR() != EAGAIN)
#endif		
			{
#ifdef TCP_CLIENT_USE_LOG
				LOG(LOG_LEVEL_WARN, "tcp_client_writev return %d, socket %d, error %d", sended, c->sock_fd, errno);
#endif
				return sended;
			}
			sended = 0;
		}
		/*only send part of data*/
		if(sended < total_len){
			int remain = total_len-sended;
			SEND_DATA* send_data = (SEND_DATA*)gss_malloc(sizeof(SEND_DATA)+remain);
			char* p = (char*)(send_data+1);

			send_data->total_len = remain;
			send_data->sended_len = 0;
			send_data->next = NULL;
#ifdef USE_SHARED_CMD
			send_data->sc = NULL;
#endif

			for (i=0; sended >= (int)iov[i].iov_len; i++) {
                sended -= iov[i].iov_len;
            }
			/*the first part of iov[i].iov_base*/			
			memcpy(p, (char*)iov[i].iov_base+sended, iov[i].iov_len-sended);
			p += (iov[i].iov_len-sended);
			
			for(i++; i<cnt; i++){
				memcpy(p, iov[i].iov_base, iov[i].iov_len);
				p += iov[i].iov_len;
			}
			
			c->first_send_data = c->last_send_data = send_data;

			/*for call tcp_client_send_cb*/
#ifdef TCP_CLIENT_USE_LOG
			LOG(LOG_LEVEL_DEBUG, "tcp_client_writev event_add, socket %d, client %p", c->sock_fd, c);
#endif
			event_add(c->send_event, NULL);
		}
		return total_len;
	}
}
#endif


int tcp_client_send(void* client, const char* buf, int buf_len){
	tcp_client* c = (tcp_client*)client;

	/*if send data list is not empty, copy data and add data to tail*/
	if(c->first_send_data){
		SEND_DATA* send_data = (SEND_DATA*)gss_malloc(sizeof(SEND_DATA)+buf_len);
		send_data->total_len = buf_len;
		send_data->sended_len = 0;
		send_data->next = NULL;
#ifdef USE_SHARED_CMD
		send_data->sc = NULL;
#endif
		memcpy(send_data+1, buf, buf_len);
		c->last_send_data->next = send_data;
		c->last_send_data = send_data;
		return buf_len;
	}
	else{
		int sended = send(c->sock_fd, buf, buf_len, 0);
		if(sended < 0){
			//socket system buffer is full,pending
#ifdef WIN32
			if(EVUTIL_SOCKET_ERROR() != WSAEWOULDBLOCK)
#else
			if(EVUTIL_SOCKET_ERROR() != EAGAIN)
#endif		
			{
#ifdef TCP_CLIENT_USE_LOG
				LOG(LOG_LEVEL_WARN, "tcp_client_send return %d, socket %d, error %d", sended, c->sock_fd, errno);
#endif
				return sended;
			}
			sended = 0;
		}

		/*only send part of data*/
		if(sended < buf_len){
			int remain = buf_len-sended;
			struct SEND_DATA* send_data = (SEND_DATA*)gss_malloc(sizeof(struct SEND_DATA)+remain);
#ifdef TCP_CLIENT_USE_LOG
			LOG(LOG_LEVEL_DEBUG, "net_socket_send event_add, socket %d, client %p", c->sock_fd, c); 
#endif

#ifdef USE_SHARED_CMD
			send_data->sc = NULL;
#endif
			send_data->total_len = remain;
			send_data->sended_len = 0;
			send_data->next = NULL;
			memcpy(send_data+1, buf+sended, remain);
			c->first_send_data = c->last_send_data = send_data;

			/*for call tcp_client_send_cb*/
			event_add(c->send_event, NULL);
		}
		return buf_len;
	}
}

void print_tcp_client_peer_addr(void* client, char* str)
{
	struct sockaddr_in addr; 
#ifdef _WIN32
	int nSize = sizeof(addr);  
#else
	socklen_t nSize = sizeof(addr);  
#endif
	
	tcp_client* c = (tcp_client*)client;

	if(c == NULL)
		return;

	memset((void *)&addr, 0, sizeof(addr));
	getpeername(c->sock_fd, (struct sockaddr *)&addr, &nSize);

	sprintf(str, "%s:%d", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port) );
}

static void tcp_client_connect_cb(evutil_socket_t sockfd, short event_type, void *arg)
{
	tcp_client* c = (tcp_client*)arg;
	int err;  
#ifdef WIN32
	int len = sizeof(err);  
#else
	socklen_t len = sizeof(err);
#endif

	GSS_UNUSED_ARG(event_type);
	GSS_UNUSED_ARG(sockfd);

	getsockopt(c->sock_fd, SOL_SOCKET, SO_ERROR, (char*)&err, &len);  
	if(c->connect_event)
	{
		event_free(c->connect_event);
		c->connect_event = NULL;
	}
	if(c->on_connect)
		(*c->on_connect)(c, err);

	if(err == 0)
		tcp_client_async_read(c);
}

int tcp_client_connect(void* client, const char* addr, unsigned short port, tcp_client_on_connect_func on_connect)
{
	tcp_client* c = (tcp_client*)client;
	struct sockaddr_in address;
	int result;

	c->on_connect = on_connect;

	memset((void *)&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = inet_addr(addr);
	if(address.sin_addr.s_addr == INADDR_NONE)
	{
		struct hostent *host = gethostbyname(addr);
		if(host == NULL)
		{
#ifdef TCP_CLIENT_USE_LOG
			LOG(LOG_LEVEL_ERROR, "tcp_client_connect gethostbyname failed");
#endif
			return -2;
		}
		address.sin_addr.s_addr = *(unsigned long*)host->h_addr_list[0];
	}
	address.sin_port = htons(port);

	result = connect(c->sock_fd, (struct sockaddr *)&address, sizeof(address));
	if(result == 0)
	{
		return 0;
	}
	else
	{
		int ev_result;

#ifdef WIN32
		if(EVUTIL_SOCKET_ERROR() != WSAEWOULDBLOCK)
#else
		if(EVUTIL_SOCKET_ERROR() != EAGAIN && EVUTIL_SOCKET_ERROR() != EINPROGRESS)
#endif
		{			
			return EVUTIL_SOCKET_ERROR();
		}
		if(c->connect_event == NULL)
			c->connect_event = event_new(c->eb, 
			c->sock_fd, 
			EV_WRITE, 
			tcp_client_connect_cb,
			(void*)c);
		ev_result = event_add(c->connect_event, NULL);
		if(ev_result != 0)
			return -3;
		return result;
	}
}

void tcp_client_set_cb(void* client, tcp_client_on_recv_func on_recv, tcp_client_on_close_func on_close, void* user_data) 
{
	tcp_client* c = (tcp_client*)client;
	if(c == NULL)
		return ;

	c->user_data = user_data;
	c->on_recv = on_recv;
	c->on_close = on_close;
}

void tcp_client_set_recvbuf_ratio(void* client, unsigned char recvbuf_ratio)
{
	tcp_client* c = (tcp_client*)client;
	if(c == NULL)
		return ;

	c->recvbuf_ratio = recvbuf_ratio;
}

evutil_socket_t tcp_client_get_handle(void* client)
{
	tcp_client* c = (tcp_client*)client;
	if(c == NULL)
		return 0;
	return c->sock_fd;
}

void* tcp_client_user_data(void* client)
{
	tcp_client* c = (tcp_client*)client;
	if(c == NULL)
		return NULL;
	return c->user_data;
}
