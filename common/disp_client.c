#include "disp_client.h"
#include <stdlib.h>
#include <string.h>
#include <event2/event.h>
#include <time.h>
#include "tcp_client.h"
#include "queue.h"
#include "gss_common.h"

typedef enum DISP_CLIENT_STATUS
{
	DISP_CLIENT_DISCONNECT,
	DISP_CLIENT_CONNECTTING,
	DISP_CLIENT_CONNECTED
}DISP_CLIENT_STATUS;

typedef struct disp_client
{
	disp_svr_info svr_info;
	DISP_CLIENT_STATUS status;
	evutil_socket_t socket_fd;
	void* tcp_client;

	TAILQ_ENTRY(disp_client) tailq;

	time_t last_recv_time;
}disp_client;

typedef struct disp_clients
{
	send_disp_info_func send_func;
	reload_disp_svr_info_func reload_func;

	struct event * timer_event;
	struct event_base* evb;

	TAILQ_HEAD(disp_client_list, disp_client) client_list;
}disp_clients;

disp_clients g_disp_clients = {NULL, NULL, NULL, NULL, TAILQ_HEAD_INITIALIZER(g_disp_clients.client_list)};

static int disp_svr_info_eq(disp_svr_info* svr1, disp_svr_info* svr2)
{
	if(svr1->port == svr2->port)
	{
		if(strcmp(svr1->addr, svr2->addr) == 0)
			return 1;
	}
	return 0;
}

static void disp_client_on_connect(void* tcp_client, int result)
{
	disp_client* client = (disp_client*)tcp_client_user_data(tcp_client);
	if(result == 0)	
	{
		client->status = DISP_CLIENT_CONNECTED;
		time(&client->last_recv_time);
		if(g_disp_clients.send_func)
			(*g_disp_clients.send_func)(client->tcp_client);
	}
	else
		client->status = DISP_CLIENT_DISCONNECT;
}

static int disp_client_on_recv(void* tcp_client, const char* buffer, int len)
{
	disp_client* client = (disp_client*)tcp_client_user_data(tcp_client);
	GSS_UNUSED_ARG(buffer);
	GSS_UNUSED_ARG(len);
	time(&client->last_recv_time);
	return 0;
}

static void disp_client_on_close(void* tcp_client)
{
	disp_client* client;
	void* user_data = tcp_client_user_data(tcp_client);

	TAILQ_FOREACH(client, &g_disp_clients.client_list, tailq)
	{
		if(client == user_data)
		{
			TAILQ_REMOVE(&g_disp_clients.client_list, client, tailq);
			destroy_tcp_client(client->tcp_client);
			free(client);
			return;
		}
	}
}

static void connect_disp_client(disp_client* client)
{
	int result ;

	if(client->status != DISP_CLIENT_DISCONNECT)
		return;
	if(client->socket_fd == -1)
	{
		client->socket_fd = socket(AF_INET, SOCK_STREAM, 0);
		client->tcp_client = create_tcp_client(client->socket_fd, g_disp_clients.evb, 
			disp_client_on_recv, disp_client_on_close,	client, 0);
	}

	result = tcp_client_connect(client->tcp_client, client->svr_info.addr, client->svr_info.port, disp_client_on_connect);
	if(result == 0)
	{
		disp_client_on_connect(client->tcp_client, 0);
		client->status = DISP_CLIENT_CONNECTED;
	}
	else if(result == -1)
	{
		client->status = DISP_CLIENT_CONNECTTING;
	}
	else
	{
		client->status = DISP_CLIENT_DISCONNECT;
	}
}


static void disp_client_timer(evutil_socket_t fd, short events, void *arg)
{
	disp_svr_info svr_info[MAX_DISP_SERVER_COUNT];
	int svr_count = 0;
	int i;
	struct disp_client* client;

	GSS_UNUSED_ARG(fd);
	GSS_UNUSED_ARG(events);
	GSS_UNUSED_ARG(arg);

	if(g_disp_clients.reload_func)
		g_disp_clients.reload_func(svr_info, &svr_count);

	for(i=0; i<svr_count; i++)
	{
		char find = 0;

		client = TAILQ_FIRST(&g_disp_clients.client_list);
		while(client != TAILQ_END(&g_disp_clients.client_list))
		{
			if(disp_svr_info_eq(&client->svr_info, &svr_info[i]))
			{
				find = 1;
				switch(client->status)
				{
				case DISP_CLIENT_DISCONNECT: //try to reconnect
					connect_disp_client(client);
					break;
				case DISP_CLIENT_CONNECTED:
					{
						time_t now;
						time(&now);
						if(now - client->last_recv_time > 2*DISP_HEART_SPAN)
						{
							struct disp_client* next = TAILQ_NEXT(client, tailq);
							disp_client_on_close(client->tcp_client);
							client = next;
							continue;
						}
						else 
						{
							if(g_disp_clients.send_func)
								(*g_disp_clients.send_func)(client->tcp_client);
						}
					}
					
					break;
				default:
					break;
				}
			}
			client = TAILQ_NEXT(client, tailq);
		}

		if(!find)//create new client, and try to connect dispatch server
		{
			disp_client* client = (disp_client*)malloc(sizeof(disp_client));

			strcpy(client->svr_info.addr, svr_info[i].addr);
			client->svr_info.port = svr_info[i].port;
			client->socket_fd = -1;
			client->status = DISP_CLIENT_DISCONNECT;

			//insert tail
			TAILQ_INSERT_TAIL(&g_disp_clients.client_list, client, tailq);

			connect_disp_client(client);
		}
	}

	//remove dispatch client if no exist 
	client = TAILQ_FIRST(&g_disp_clients.client_list);
	while(client != TAILQ_END(&g_disp_clients.client_list))
	{
		int i=0;
		for(i=0; i<svr_count; i++)
		{
			if(disp_svr_info_eq(&client->svr_info, &svr_info[i]))
				break;
		}
		if(i == svr_count)
		{
			struct disp_client* next = TAILQ_NEXT(client, tailq);

			TAILQ_REMOVE(&g_disp_clients.client_list, client, tailq);
			destroy_tcp_client(client->tcp_client);
			free(client);

			client = next;
		}
		else
		{
			client = TAILQ_NEXT(client, tailq);
		}
	}
}

int start_disp_client(struct event_base* evb, send_disp_info_func send_func,  reload_disp_svr_info_func reload_func)
{
	struct timeval tv = {DISP_HEART_SPAN, 0};

	g_disp_clients.reload_func = reload_func;
	g_disp_clients.send_func = send_func;
	g_disp_clients.evb = evb;

	TAILQ_INIT(&g_disp_clients.client_list);

	g_disp_clients.timer_event = event_new(evb, -1,	EV_TIMEOUT|EV_PERSIST, disp_client_timer, NULL);

	disp_client_timer(-1, 0, 0);

	evtimer_add(g_disp_clients.timer_event, &tv);

	return 0;
}

void stop_disp_client(void)
{
	if(g_disp_clients.timer_event)
	{
		event_free(g_disp_clients.timer_event);
		g_disp_clients.timer_event = NULL;
	}

}