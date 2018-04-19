#include "tcp_listener.h"

#include <errno.h>
#ifndef WIN32
#include <arpa/inet.h>
#endif
#include <string.h>
#include "log.h"
#include "gss_mem.h"


//linux,limit by /proc/sys/net/core/somaxconn
//512 in srs,so copy it
#define LISTEN_BACKLOG (512)


/*libevent socket accepet callback function*/
static void tcp_listener_accept_cb(evutil_socket_t sockfd, short event_type, void *arg){
	tcp_listener* listener = (tcp_listener*)arg;
	evutil_socket_t new_fd;
	new_fd = accept(listener->listen_fd, NULL, NULL);
	if(new_fd == -1){
		LOG(LOG_LEVEL_ERROR, "accept error socket %d error %d", listener->listen_fd, errno); 
		return;
	}
	if(listener->on_accept){
		LOG(LOG_LEVEL_DEBUG, "socket %d accept %d", listener->listen_fd, new_fd); 
		
		listener->on_accept(listener, new_fd);
	}
}



void* create_tcp_listener(const char* ip, int port, on_accept_func on_accept, struct event_base * eb, void* user_data){

	tcp_listener* listener = (tcp_listener*)gss_malloc(sizeof(tcp_listener));

	listener->listen_ip = gss_strdup(ip);
	listener->listen_port = port;
	listener->on_accept = on_accept;
	listener->listen_fd = -1;
	listener->user_data = user_data;
	listener->eb = eb;
	listener->accept_event = NULL;
	
	return listener;
}

void destroy_tcp_listener(void* listener){
	tcp_listener* l = (tcp_listener*)listener;
	if(l == NULL)
		return;
	
	if(l->accept_event)
		event_free(l->accept_event);

	if(l->listen_fd != -1)
		evutil_closesocket(l->listen_fd);
	gss_free(l->listen_ip);
	gss_free(l);

}

int tcp_listener_listen(void* listener){ 
	evutil_socket_t socket_fd;	
	struct sockaddr_in address;
	int result;
	tcp_listener* l = (tcp_listener*)listener;

	socket_fd = socket(AF_INET, SOCK_STREAM, 0);	
	if(socket_fd == -1){		
		LOG(LOG_LEVEL_ERROR, "call socket return -1,%d", errno);
		return -1;	
	}	

	evutil_make_listen_socket_reuseable(socket_fd);	
	
	memset((void *)&address, 0, sizeof(address));	
	address.sin_family = AF_INET;	
	if(l->listen_ip[0])
		address.sin_addr.s_addr = inet_addr(l->listen_ip);
	else
		address.sin_addr.s_addr = INADDR_ANY;	
	address.sin_port =  htons(l->listen_port);	
	result =  bind(socket_fd, (struct sockaddr *)&address, sizeof(address));	
	if(result == -1){		
		evutil_closesocket(socket_fd);		
		LOG(LOG_LEVEL_ERROR, "bind socket return -1,%d", errno);	
		return -1;	
	}	
	
	//no block socket
	evutil_make_socket_nonblocking(socket_fd);
	
	result = listen(socket_fd, LISTEN_BACKLOG);
	if(result == -1){
		evutil_closesocket(socket_fd);
		LOG(LOG_LEVEL_ERROR, "listen socket return -1,%d", errno);		
		return -1;	
	}

	l->listen_fd = socket_fd;

	l->accept_event = event_new(l->eb, l->listen_fd, EV_READ|EV_PERSIST, tcp_listener_accept_cb, (void*)l);
	event_add(l->accept_event, NULL); 
	
	LOG(LOG_LEVEL_PROMPT, "listen ok, socket %d, ip:%s,port:%d", socket_fd, l->listen_ip[0]?l->listen_ip:"all host ip", l->listen_port);	
	
	return 0;
}

