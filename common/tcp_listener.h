#ifndef __TCP_LISTENER_H__
#define __TCP_LISTENER_H__


#ifdef __cplusplus
extern "C" {
#endif

#include <event2/event.h>

typedef void (*on_accept_func)(void* listener, evutil_socket_t fd);

typedef struct tcp_listener{

	on_accept_func on_accept;

	char* listen_ip;

	int listen_port;

	evutil_socket_t listen_fd;

	struct event* accept_event;

	void* user_data;

	struct event_base * eb;
}tcp_listener;



void* create_tcp_listener(const char* ip, int port, on_accept_func on_accept, struct event_base * eb, void* user_data); 
void destroy_tcp_listener(void* listener);
int tcp_listener_listen(void* listener);

inline void* tcp_listener_user_data(void* listener)
{
	tcp_listener* l = (tcp_listener*)listener;
	if(l == NULL)
		return NULL;
	return l->user_data;
}

#ifdef __cplusplus
}
#endif


#endif
