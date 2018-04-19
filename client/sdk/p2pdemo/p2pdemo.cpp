// p2pdemo.cpp : Defines the entry point for the console application.
//
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#ifdef WIN32
#include "getopt.h"
#include <Windows.h>
#else
#include<unistd.h>
#include <pthread.h>
#endif

#include "p2p_transport.h" 
#include "p2p_dispatch.h"
#include "tm.h"

static p2p_transport_cfg cfg;
static p2p_transport *transport = 0;
static int conn_id = -1;
static unsigned short listen_port = 0;
static char* ds_server = NULL;
static int deamon = 0;
static char* remote_user = NULL;

#define P2P_UNUSED_ARG(arg)  (void)arg
#define MAX_ERROR_STRING_LEN 256
#define P2P_SERVER_PORT 34780

#define TEST_P2P_SEND_FILE 1
#ifdef TEST_P2P_SEND_FILE
#ifdef WIN32
#define P2P_SEND_FILE "c:\\p2psend"
#define P2P_RECV_FILE "c:\\p2precv"
#else
#define P2P_SEND_FILE "/p2psend"
#define P2P_RECV_FILE "/p2precv"
#endif
#endif

static int sendfile_to_remote_user();

static void on_connection_disconnect(p2p_transport *transport,
							  int connection_id,
							  void *transport_user_data,
							  void *connect_user_data)
{
	P2P_UNUSED_ARG(transport);
	P2P_UNUSED_ARG(transport_user_data);
	P2P_UNUSED_ARG(connect_user_data);
	printf("p2p connection is disconnected %d\r\n", connection_id);
}

static void on_create_complete(p2p_transport *transport,
						   int status,
						   void *user_data)
{
	P2P_UNUSED_ARG(transport);
	P2P_UNUSED_ARG(user_data);
	if (status == P2P_SUCCESS) 
	{
		printf("p2p connect server successful, net state %d\r\n", p2p_transport_server_net_state(transport));
		if(deamon && remote_user)
		{
			int status = p2p_transport_connect(transport, remote_user, 0, 0, &conn_id);
			if (status != P2P_SUCCESS) 
			{
				char errmsg[MAX_ERROR_STRING_LEN];
				p2p_strerror(status, errmsg, sizeof(errmsg));
				printf("p2p connect remote user failed: %s\r\n", errmsg);
			}
		}
	} 
	else 
	{
		char errmsg[MAX_ERROR_STRING_LEN];
		p2p_strerror(status, errmsg, sizeof(errmsg));
		printf("p2p connect server failed: %s\r\n", errmsg);
	}
}

static void on_disconnect_server(p2p_transport *transport,
								 int status,
								 void *user_data)
{
	P2P_UNUSED_ARG(transport);
	P2P_UNUSED_ARG(user_data);

	char errmsg[MAX_ERROR_STRING_LEN];
	p2p_strerror(status, errmsg, sizeof(errmsg));
	printf("p2p disconnect server, %s\r\n", errmsg);
}

#ifdef _WIN32
static DWORD WINAPI send_file_thread(void* arg)
#else
static void* send_file_thread(void* arg)
#endif
{
	while(1)
	{	
		if(sendfile_to_remote_user())
			break;
	}	
	
#ifndef _WIN32
	pthread_detach(pthread_self());
#endif
	return 0;
}

static void on_connect_complete(p2p_transport *transport,
							int connection_id,
							int status,
							void *transport_user_data,
							void *connect_user_data)
{
	P2P_UNUSED_ARG(transport);
	P2P_UNUSED_ARG(transport_user_data);
	P2P_UNUSED_ARG(connect_user_data);
	if (status == P2P_SUCCESS) 
	{
		char addr[256];
		int len = sizeof(addr);
		p2p_addr_type addr_type;
		p2p_get_conn_remote_addr(transport, connection_id, addr, &len, &addr_type);
		printf("p2p connect remote user successful, connection id %d, address %s, type %d\r\n", connection_id, addr, addr_type);

		if(deamon)
		{
#ifdef _WIN32
			HANDLE thread;
#else
			pthread_t thread;
#endif
#ifdef _WIN32
			thread = CreateThread(NULL, 0, send_file_thread, NULL, 0, NULL);
			CloseHandle(thread);
#else
			pthread_create(&thread, NULL, send_file_thread, NULL);
#endif
		}
	} 
	else 
	{
		char errmsg[MAX_ERROR_STRING_LEN];
		p2p_strerror(status, errmsg, sizeof(errmsg));
		printf("p2p connect remote user failed: %s, connection id %d\r\n", errmsg, connection_id);
	}
}

static void on_accept_remote_connection(p2p_transport *transport,
										int connection_id,
										int conn_flag,
										void *transport_user_data)
{
	P2P_UNUSED_ARG(transport);
	P2P_UNUSED_ARG(transport_user_data);
	printf("accept remote connection %d,%d\r\n", conn_flag, connection_id);
}

void on_connection_recv(p2p_transport *transport,
						int connection_id,
						void *transport_user_data,
						void *connect_user_data,
						char* data,
						int len)
{
	P2P_UNUSED_ARG(transport_user_data);
	P2P_UNUSED_ARG(connect_user_data);
	P2P_UNUSED_ARG(data);

#ifdef TEST_P2P_SEND_FILE
	static FILE *f = NULL;
#endif
	P2P_UNUSED_ARG(transport_user_data);
	P2P_UNUSED_ARG(connect_user_data);

#ifdef TEST_P2P_SEND_FILE
	if(deamon)
		return;
	f = fopen(P2P_RECV_FILE, "ab");
	if(f)
	{
		fwrite(data, sizeof(char), len, f);
		fclose(f);
	}
#else
	P2P_UNUSED_ARG(data);
#endif

	//printf("on_connection_recv %p %d %d\r\n", transport, connection_id, len);
}

static void connect_server()
{
	if(transport)
	{
		printf("p2p transport already created, destroy it first\r\n");
	}
	else
	{
		int status = p2p_transport_create(&cfg, &transport);
		if (status != P2P_SUCCESS) 
		{
			char errmsg[MAX_ERROR_STRING_LEN];
			p2p_strerror(status, errmsg, sizeof(errmsg));
			printf("create p2p transport failed: %s\r\n", errmsg);
		}
	}
}

static void disconnect_server()
{
	if(transport)
	{
		p2p_transport_destroy(transport);
		transport = 0;
	}
}

static void connect_remote_user()
{
	if(transport)
	{
		const char *SEP = " \t\r\n";
		char *name = strtok(NULL, SEP);
		if(name)
		{
			int status = p2p_transport_connect(transport, name, 0, 0, &conn_id);
			if (status != P2P_SUCCESS) 
			{
				char errmsg[MAX_ERROR_STRING_LEN];
				p2p_strerror(status, errmsg, sizeof(errmsg));
				printf("p2p connect remote user failed: %s\r\n", errmsg);
			}
		}
		else
		{
			printf("remote user is empty\r\n");
		}
	}
	else
	{
		printf("p2p transport is not created, crate it first\r\n");
	}
}

static void disconnect_remote_user()
{
	if(transport && conn_id != -1)
	{
		p2p_transport_disconnect(transport, conn_id);
	}
	else
	{
		printf("p2p transport is not created, crate it first. or connection is invalid\r\n");
	}
}

static int sendfile_to_remote_user()
{
	FILE *f;
	char buffer[8192];
	int readed, error;
	const int fps = 25;
	double total_sended=0;
	int64_t begin = now_ms_time();
	unsigned int time_stamp = 0;
	int64_t span;
	p2p_send_model model = P2P_SEND_NONBLOCK; // P2P_SEND_BLOCK, P2P_SEND_NONBLOCK

	f = fopen(P2P_SEND_FILE, "rb");
	if(!f)
	{
		printf("p2p send, failed to open source file!\r\n");
		return -1;
	}
	while( !feof(f) )
	{
		int sended;
		readed = fread(buffer, sizeof(char), sizeof(buffer), f);
		if(model == P2P_SEND_BLOCK)
		{
			sended = p2p_transport_send(transport, conn_id, buffer, readed, model, &error);
			if(sended <= 0)
			{
				printf("failed to p2p send file %d %d\r\n", sended, error);
				fclose(f);
				return error;
			}
		}
		else
		{
			while(1)
			{
				sended = p2p_transport_send(transport, conn_id, buffer, readed, model, &error);
				if(sended <= 0)
				{
					if(error != 70027) //nonblock, send buffer full
					{
						printf("failed to p2p send file %d %d\r\n", sended, error);
						fclose(f);
						return error;
					}
					else
					{
#ifdef WIN32
						Sleep(1);
#else
						usleep(1000);
#endif
					}

				}
				else
					break;
			}				
		}
		total_sended += sended;

		time_stamp += (1000/fps);
		span = now_ms_time()-begin;
		if(time_stamp > span)
#ifdef WIN32
			Sleep(time_stamp-(unsigned int)span);
#else
			usleep(1000*(time_stamp-(unsigned int)span));
#endif
		else
			time_stamp = (unsigned int)span; //may be p2p_transport_send too long time

	}
	span=now_ms_time()-begin;
	if(span == 0) //disable zero exception
		span = 1;
	total_sended /= 1024;
	printf("total time:%lldms, total bytes:%.2fk, avg:%.2fk\n", span, total_sended, total_sended*1000/span);
	fclose(f);
	return 0;
}

static void send_to_remote_user()
{
	if(transport && conn_id != -1)
	{
#ifdef TEST_P2P_SEND_FILE
		sendfile_to_remote_user();
#else
		int error;
		char* text = "test";
		int sended = p2p_transport_send(transport, conn_id, text, strlen(text), P2P_SEND_BLOCK, &error); 
		if(sended > 0)
		{
			printf("p2p send successful!\r\n");
		}
		else
		{
			printf("p2p send failed %d %d\r\n", sended, error);
		}
#endif
	}
	else
	{
		printf("p2p transport is not created, crate it first. or connection is invalid\r\n");
	}
}

static void create_tcp_proxy()
{
	if(transport && conn_id != -1)
	{
		const char *SEP = " \t\r\n";
		char *port = strtok(NULL, SEP);
		if(port) 
		{
			unsigned short p=(unsigned short)atoi(port);
			if(p)
			{
				int status = p2p_create_tcp_proxy(transport, conn_id, p, &listen_port);
				if(status == P2P_SUCCESS)
				{
					printf("p2p listen successful, port is %d\r\n", listen_port);
				}
				else
				{
					char errmsg[MAX_ERROR_STRING_LEN];
					p2p_strerror(status, errmsg, sizeof(errmsg));
					printf("p2p listen failed: %s\r\n", errmsg);
				}
			}
			else
			{
				printf("remote port is invalid\r\n");
			}
		}
		else
		{
			printf("remote port is empty\r\n");
		}
	}
	else
	{
		printf("p2p transport is not created, crate it first\r\n");
	}
}

static void destroy_tcp_proxy()
{
	if(transport && conn_id != -1 && listen_port != 0)
	{
		p2p_destroy_tcp_proxy(transport, conn_id, listen_port);
		listen_port = 0;
	}
	else
	{
		printf("p2p transport is not created, crate it first\r\n");
	}
}

void ds_callback(void* dispatcher, int status, void* user_data, char* server, unsigned short port, unsigned int server_id)
{
	if(status == P2P_SUCCESS)
		printf("ds_callback successed, %s, %d, %d\r\n", server, port, server_id);
	else
		printf("ds_callback failed, %d\r\n", status);
	destroy_p2p_dispatch_requester(dispatcher);
}

void request_dispatch()
{
	void* dispatcher = NULL;
	if(ds_server)
		p2p_request_dispatch_server(cfg.user, cfg.password, ds_server, 0, ds_callback, &dispatcher);
}

void query_dispatch()
{
	void* dispatcher = NULL;
	if(ds_server)
		p2p_query_dispatch_server(cfg.user, ds_server, 0, ds_callback, &dispatcher);
}

void tcp_proxy_get_addr()
{
	if(transport)
	{
		const char *SEP = " \t\r\n";
		char *port = strtok(NULL, SEP);
		if(port) 
		{
			unsigned short p=(unsigned short)atoi(port);
			if(p)
			{
				char addr[256];
				int addr_len = sizeof(addr);
				int status = p2p_proxy_get_remote_addr(transport, p, addr, &addr_len);
				if(status == P2P_SUCCESS)
				{
					printf("p2p_proxy_get_remote_addr successful, address is %s\r\n", addr);
				}
				else
				{
					char errmsg[MAX_ERROR_STRING_LEN];
					p2p_strerror(status, errmsg, sizeof(errmsg));
					printf("p2p_proxy_get_remote_addr failed: %s\r\n", errmsg);
				}
			}
			else
			{
				printf("tcp port is invalid\r\n");
			}
		}
		else
		{
			printf("tcp port is empty\r\n");
		}
	}
	else
	{
		printf("p2p transport is not created, crate it first\r\n");
	}
}

/*
 * Main console loop.
 */
static void p2p_console(void)
{
	int app_quit = 0;
	while (!app_quit)
	{
		if(deamon){
#ifdef WIN32
		Sleep(1000);
#else
		usleep(1000*1000);
#endif

			continue;
		}
		char input[80], *cmd;
		const char *SEP = " \t\r\n";
		int len;

		printf("r : request from dispatch server\r\n"
			"qu : query from dispatch server\r\n"
			"i : initialize\r\n"
			"u : uninitialize\r\n"
			"c : connect remote user\r\n"
			"d : disconnect remote user\r\n"
			"s : send data to remote user\r\n"
			"l : create listen proxy\r\n"
			"dl : destroy listen proxy\r\n"
			"q : quit\r\n"
			"Please input command: ");
		if (stdout) fflush(stdout);

		memset(input, 0, sizeof(input));
		if (fgets(input, sizeof(input), stdin) == NULL)
			break;

		len = strlen(input);
		while (len && (input[len-1]=='\r' || input[len-1]=='\n'))
			input[--len] = '\0';

		cmd = strtok(input, SEP);
		if (!cmd)
			continue;
		
		if (strcmp(cmd, "request")==0 || strcmp(cmd, "r")==0)
		{
			request_dispatch();
		}
		else if (strcmp(cmd, "query")==0 || strcmp(cmd, "qu")==0)
		{
			query_dispatch();
		}
		else if (strcmp(cmd, "initialize")==0 || strcmp(cmd, "i")==0)
		{
			connect_server();
		}
		else if (strcmp(cmd, "uninitialize")==0 || strcmp(cmd, "u")==0)
		{
			disconnect_server();
		} 
		else if (strcmp(cmd, "connect")==0 || strcmp(cmd, "c")==0)
		{
			connect_remote_user();
		}
		else if (strcmp(cmd, "disconnect")==0 || strcmp(cmd, "d")==0)
		{
			disconnect_remote_user();
		}
		else if (strcmp(cmd, "send")==0 || strcmp(cmd, "s")==0)
		{
			send_to_remote_user();
		}
		else if (strcmp(cmd, "listen")==0 || strcmp(cmd, "l")==0) 
		{
			create_tcp_proxy();
		}
		else if (strcmp(cmd, "delisten")==0 || strcmp(cmd, "dl")==0) 
		{
			destroy_tcp_proxy();
		}
		else if (strcmp(cmd, "addr")==0 || strcmp(cmd, "a")==0)
		{
			tcp_proxy_get_addr();
		}
		else if (strcmp(cmd, "quit")==0 || strcmp(cmd, "q")==0)
		{
			disconnect_server();
			app_quit = 1;
		}
		else
		{
			printf("Invalid command '%s'\r\n", cmd);
		}
	}
}

int main(int argc, char *argv[])
{
	int c;
	char *pos;
	p2p_transport_cb cb;

	//int max_recv_len = 1024*1024;
	//int max_client_count = 4;
	//int enable_relay = 0;
	int only_relay = 1;


	memset(&cfg, 0, sizeof(cfg));
	memset(&cb, 0, sizeof(cb));
	
	while((c = getopt(argc,argv,"s:u:p:d:r:bv"))!= -1)
	{
		switch (c) 
		{
		case 's':
			if ((pos=strstr(optarg, ":")) != NULL) {
				*pos = '\0';
				cfg.server = optarg;
				cfg.port = (unsigned short)atoi(pos+1);
			} else {
				cfg.server = optarg;
				cfg.port = P2P_SERVER_PORT;
			}
			break;
		case 'u':
			cfg.user = optarg;
			break;
		case 'p':
			cfg.password = optarg;
			break;
		case 'd':
			ds_server = optarg;
			break;
		case 'b':
			deamon = 1;
			break;
		case 'r':
			remote_user = optarg;
			break;
		case 'v':
			printf("%s\r\n", p2p_get_ver());
			return 0;
		default:
			printf("Argument is not valid.\r\n");
			return 1;
		}
	}

	p2p_init(NULL);

	//p2p_set_global_opt(P2P_MAX_RECV_PACKAGE_LEN, &max_recv_len, sizeof(int));
	//p2p_set_global_opt(P2P_MAX_CLIENT_COUNT, &max_client_count, sizeof(int));
	//p2p_set_global_opt(P2P_ENABLE_RELAY, &enable_relay, sizeof(int));
	p2p_set_global_opt(P2P_ONLY_RELAY, &only_relay, sizeof(int));
	

	cb.on_connect_complete = on_connect_complete;
	cb.on_create_complete = on_create_complete;
	cb.on_connection_disconnect = on_connection_disconnect;
	cb.on_accept_remote_connection = on_accept_remote_connection;
	cb.on_connection_recv = on_connection_recv;
	cfg.cb = &cb;
	//cfg.use_tcp_connect_srv = 1;
	//cfg.proxy_addr="127.0.0.1:6666";
	if(cfg.user == NULL)
		cfg.terminal_type = P2P_CLIENT_TERMINAL;
	else
		cfg.terminal_type = P2P_DEVICE_TERMINAL;

    p2p_log_set_level(4);

	if(deamon)
		connect_server();

	p2p_console();

	disconnect_server();

	p2p_uninit();
}
