
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#ifdef WIN32
#include <Windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif
#include "signaling_conn.h"
#include "gss_transport.h" 
#include "av_conn.h"
#include "gss_common.h"

static void* main_conn = NULL;
static void* signaling_conn = NULL;

extern char* g_uid;
extern char* g_server;
extern unsigned short g_port;

extern int g_deamon;

static char* signaling_text = "signaling connection send text, signaling connection send text, signaling connection send text, signaling connection send text";

void on_main_connect_result(void *transport, void* user_data, int status)
{
	if(status == 0)
	{
		if(!g_deamon)
			printf("device main connect success!\r\n");
	}
	else
		printf("failed to device main connect server, error %d\r\n", status); 
}

void on_main_disconnect(void *transport, void* user_data, int status)
{
	printf("device main disconnect, error %d\r\n", status); 
}

void on_main_accept_client(void *transport, void* user_data, unsigned short client_conn)
{
	printf("device main accept client, client index %d\r\n", client_conn); 
}

void on_main_disconnect_client(void *transport, void* user_data, unsigned short client_conn)
{
	printf("device main disconnect client, client index %d\r\n", client_conn); 
}

void on_main_recv(void *transport, void *user_data, unsigned short client_conn, char* data, int len)
{
	if(!g_deamon)
		printf("device main receive date, client index %d, length %d\r\n", client_conn, len); 

	if(memcmp(data, signaling_text, len))
	{
		printf("on_main_recv memcmp error!!!!!");
		return;
	}
	//response data to signaling client 
	gss_dev_main_send(transport, client_conn, data, len, P2P_SEND_BLOCK);
}

void on_dev_recv_av_request(void *transport, void* user_data, unsigned int client_conn)
{
	dev_av_connect_server(g_server, g_port, g_uid, client_conn);
}

void device_main_connect_server(char* server, unsigned short port, char* uid)
{
	int result;
	gss_dev_main_cfg cfg;
	gss_dev_main_cb cb;

	if(main_conn)
	{
		printf("main connection is not null\r\n");
		return;
	}

	cfg.server = server;
	cfg.port = port;
	cfg.uid = uid;
	cfg.user_data = NULL;
	cfg.cb = &cb;

	cb.on_recv = on_main_recv;
	cb.on_disconnect_signaling_client = on_main_disconnect_client;
	cb.on_accept_signaling_client= on_main_accept_client;
	cb.on_disconnect = on_main_disconnect;
	cb.on_connect_result= on_main_connect_result;
	cb.on_recv_av_request = on_dev_recv_av_request;

	result = gss_dev_main_connect(&cfg, &main_conn); 
	if(result != 0)
		printf("gss_dev_main_connect return %d", result);
}

#ifdef _WIN32
static DWORD WINAPI signaling_send_thread(void* arg)
#else
static void* signaling_send_thread(void* arg)
#endif
{	
	while(1)
	{
		if(!signaling_conn)
			break;
		signaling_connection_send();
		usleep(100*1000);
	}
#ifndef _WIN32
	pthread_detach(pthread_self());
#endif
	return 0;
}

static void on_signaling_connect_result(void *transport, void* user_data, int status)
{
	if(status == 0)
	{
#ifdef _WIN32
		HANDLE thread;
#else
		pthread_t thread;
#endif
		printf("signaling connect success!\r\n");
		if(g_deamon)
		{
#ifdef _WIN32
			thread = CreateThread(NULL, 0, signaling_send_thread, NULL, 0, NULL);
			CloseHandle(thread);
#else
			pthread_create(&thread, NULL, signaling_send_thread, NULL);
#endif
		}
	}
	else
	{
		printf("failed to signaling connect server, error %d\r\n", status); 
		gss_client_signaling_destroy(signaling_conn);
		signaling_conn = NULL;
	}
}

static void on_signaling_disconnect(void *transport, void* user_data, int status)
{
	printf("signaling disconnect, error %d\r\n", status); 
	gss_client_signaling_destroy(signaling_conn);
	signaling_conn = NULL;
}

static void on_signaling_recv(void *transport, void *user_data, char* data, int len)
{
	if(!g_deamon)
		printf("signaling receive date, length %d\r\n", len); 
	
	if(memcmp(data, signaling_text, len))
	{
		printf("on_signaling_recv memcmp error!!!!!");
		return;
	}
}

static void on_signaling_device_disconnect(void *transport, void *user_data)
{
	printf("signaling device disconnect\r\n"); 
	gss_client_signaling_destroy(signaling_conn);
	signaling_conn = NULL;
}

void signaling_connect_server(char* server, unsigned short port, char* uid)
{
	int result;
	gss_client_conn_cfg cfg;
	gss_client_conn_cb cb;

	if(signaling_conn)
	{
		printf("signaling connection is not null\r\n");
		return;
	}

	cfg.server = server;
	cfg.port = port;
	cfg.uid = uid;
	cfg.user_data = NULL;
	cfg.cb = &cb;

	cb.on_recv = on_signaling_recv;
	cb.on_disconnect = on_signaling_disconnect;
	cb.on_connect_result= on_signaling_connect_result;
	cb.on_device_disconnect= on_signaling_device_disconnect;

	result = gss_client_signaling_connect(&cfg, &signaling_conn); 
	if(result != 0)
		printf("signaling_connect_server return %d", result);
}

void device_main_disconnect_server()
{
	if(!main_conn)
	{
		printf("main connection is null\r\n");
		return;
	}
	gss_dev_main_destroy(main_conn);
	main_conn = NULL;
}

void signaling_disconnect_server()
{
	if(!signaling_conn)
	{
		printf("signaling connection is null\r\n");
		return;
	}
	gss_client_signaling_destroy(signaling_conn);
	signaling_conn = NULL;
}

void signaling_connection_send()
{	
	if(!signaling_conn)
	{
		printf("signaling connection is null\r\n");
		return;
	}
	
	gss_client_signaling_send(signaling_conn, signaling_text, strlen(signaling_text), P2P_SEND_BLOCK) ;
}

void device_main_send(int client_index)
{
	if(!main_conn)
	{
		printf("main connection is null\r\n");
		return;
	}
	gss_dev_main_send(main_conn, client_index, signaling_text, strlen(signaling_text), P2P_SEND_BLOCK); 
}