#ifndef __TCP_CLIENT_H__
#define __TCP_CLIENT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <event2/event.h>
#include "gss_protocol.h"
#ifdef USE_SHARED_CMD
#include "shared_cmd.h"
#endif


typedef struct SEND_DATA{
	unsigned int sended_len;
	unsigned int total_len;
	struct SEND_DATA* next;
#ifdef USE_SHARED_CMD
	shared_cmd* sc;
#endif
}SEND_DATA;

typedef int (*tcp_client_on_recv_func)(void* client, const char* buf, int buf_len);
typedef void (*tcp_client_on_close_func)(void* client);
typedef void (*tcp_client_on_connect_func)(void* client, int result);

typedef struct tcp_client{
	tcp_client_on_recv_func on_recv;
	tcp_client_on_close_func on_close;

	struct event_base* eb;

	evutil_socket_t sock_fd;	

	struct event* send_event;	
	struct SEND_DATA* first_send_data;
	struct SEND_DATA* last_send_data;

	struct event* recv_event;	
	char* recv_buffer;	
	unsigned int recved_len;	
	unsigned int recv_capacity;
	unsigned char recvbuf_ratio;

	struct event *connect_event;
	tcp_client_on_connect_func on_connect;

	void* user_data;
}tcp_client;

void* create_tcp_client(evutil_socket_t sock, struct event_base * eb, 
						tcp_client_on_recv_func on_recv, tcp_client_on_close_func on_close, void* user_data, unsigned char async_read);
void destroy_tcp_client(void* client);
int tcp_client_send(void* client, const char* buf, int buf_len);
int tcp_client_connect(void* client, const char* addr, unsigned short port, tcp_client_on_connect_func on_connect);


#define MAX_IP_ADDR_LEN (64)
void print_tcp_client_peer_addr(void* client, char* str);

void* tcp_client_user_data(void* client);

evutil_socket_t tcp_client_get_handle(void* client);

void tcp_client_set_cb(void* client, tcp_client_on_recv_func on_recv, tcp_client_on_close_func on_close, void* user_data);

void tcp_client_set_recvbuf_ratio(void* client, unsigned char recvbuf_ratio);

#ifdef WIN32
struct iovec
{
	int iov_len;
	char* iov_base;
};
#endif

int tcp_client_writev(void* client, const struct iovec *iov, int cnt);

#ifdef USE_SHARED_CMD
int tcp_client_shared_writev(void* client, const struct iovec *iov, shared_cmd** sc, int cnt);
#endif

#ifdef __cplusplus
}
#endif


#endif
