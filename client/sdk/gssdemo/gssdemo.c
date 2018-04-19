#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#ifdef WIN32
#include "getopt.h"
#include <Windows.h>
#include "libavformat/avformat.h"
#else
#include<unistd.h>
#include <pthread.h>
#endif
#include "gss_common.h"
#include "p2p_dispatch.h"
#include "signaling_conn.h"
#include "av_conn.h"
#include "live_conn.h"

#define GSS_SERVER_PORT (6000)

static char* g_ds_server = NULL;

int g_deamon = 0;

char* g_video_file = NULL;

char* g_type = NULL;

char* g_uid = NULL;
char* g_server = NULL;
unsigned short g_port = GSS_SERVER_PORT;

int g_fps = 25;

void ds_callback(void* dispatcher, int status, void* user_data, char* server, unsigned short port, unsigned int server_id)
{
	if(status == P2P_SUCCESS)
	{
		if(!g_deamon)
			printf("ds_callback successed, %s, %d, %d\r\n", server, port, server_id);
	}
	else
		printf("ds_callback failed, uid %s, %d\r\n", g_uid, status);
	destroy_gss_dispatch_requester(dispatcher);
}

void request_dispatch()
{
	void* dispatcher = NULL;
	if(g_ds_server)
		gss_request_dispatch_server(g_uid, "", g_ds_server, 0, ds_callback, &dispatcher);
}

void query_dispatch()
{
	void* dispatcher = NULL;
	if(g_ds_server)
		gss_query_dispatch_server(g_uid, g_ds_server, 0, ds_callback, &dispatcher);
}


#ifdef _WIN32
static DWORD WINAPI dispatch_deamon_thread(void* arg)
#else
static void* dispatch_deamon_thread(void* arg)
#endif
{
	while(1)
	{
		query_dispatch();
		request_dispatch();
		usleep(1000*1000); //one second
	}

#ifndef _WIN32
	pthread_detach(pthread_self());
#endif
	return 0;
}

void dispatch_deamon()
{
#ifdef _WIN32
	HANDLE thread;
#else
	pthread_t thread;
#endif

	device_main_connect_server(g_server, g_port, g_uid);

	usleep(1000*1000); //one second

#ifdef _WIN32
	thread = CreateThread(NULL, 0, dispatch_deamon_thread, NULL, 0, NULL);
	CloseHandle(thread);
#else
	pthread_create(&thread, NULL, dispatch_deamon_thread, NULL);
#endif
}

/*
 * Main console loop.
 */
static void gss_console(void)
{
	int app_quit = 0;
	while (!app_quit)
	{
		char input[80], *cmd;
		const char *SEP = " \t\r\n";
		int len;

		if(g_deamon)
		{
			usleep(1000*1000);
			continue;
		}


		printf("\r\n"
			"r : request from dispatch server\r\n"
			"qu : query from dispatch server\r\n"
			"m : device main connect server\r\n"
			"md : disconnect device main connection\r\n"
			"ms : device main connection send\r\n"
			"s : client signaling connect server\r\n"
			"sd : disconnect signaling connection\r\n"
			"ss : signaling connection send\r\n"
			"v : client video connect server\r\n"
			"vd : disconnect client video connection\r\n"
			"vs : client video connection send\r\n"
			"dvd : disconnect device video connection\r\n"
			"dvs : device video connection send\r\n"
			"p : client pull connect server\r\n"
			"pd : disconnect pull connection\r\n"
			"dp : device push connect server\r\n"
			"dpd : disconnect device video connection\r\n"
			"dps : device video connection send\r\n"
			"q : quit\r\n"
			"Please input command:\r\n");
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
		else if (strcmp(cmd, "main")==0 || strcmp(cmd, "m")==0)
		{
			device_main_connect_server(g_server, g_port, g_uid);
		}
		else if (strcmp(cmd, "signaling")==0 || strcmp(cmd, "s")==0)
		{	
			signaling_connect_server(g_server, g_port, g_uid);
		}
		else if (strcmp(cmd, "md")==0)
		{
			device_main_disconnect_server();
		}
		else if (strcmp(cmd, "sd")==0)
		{
			signaling_disconnect_server();
		}
		else if (strcmp(cmd, "ms")==0)
		{
			char* idx = strtok(NULL, SEP);
			if(!idx)
			{
				printf("please input client index!!!!!!\r\n");
				continue;
			}
			device_main_send(atoi(idx));
		}
		else if (strcmp(cmd, "ss")==0)
		{
			signaling_connection_send();
		}
		else if (strcmp(cmd, "video")==0 || strcmp(cmd, "v")==0)
		{
			client_av_connect_server(g_server, g_port, g_uid);
		}
		else if (strcmp(cmd, "vd")==0)
		{
			client_av_disconnect_server();
		}
		else if (strcmp(cmd, "vs")==0)
		{
			client_av_send();
		}
		else if (strcmp(cmd, "dvd")==0)
		{
			dev_av_disconnect_server();
		}
		else if (strcmp(cmd, "dvs")==0)
		{
			char* filepath = strtok(NULL, SEP);
			if(!filepath)
			{
				if(!g_video_file)
				{
					printf("please input video file path!!!!!!\r\n");
					continue;
				}
				filepath = g_video_file;
			}
			dev_av_send(filepath);
		}
		else if (strcmp(cmd, "pull")==0 || strcmp(cmd, "p")==0)
		{
			client_pull_connect_server(g_server, g_port, g_uid);
		}
		else if (strcmp(cmd, "pd")==0)
		{
			client_pull_disconnect_server();
		}
		else if (strcmp(cmd, "dp")==0)
		{
			dev_push_connect_server(g_server, g_port, g_uid);
		}
		else if (strcmp(cmd, "dpd")==0)
		{
			dev_push_disconnect_server();
		}
		else if (strcmp(cmd, "dps")==0)
		{
			char* filepath = strtok(NULL, SEP);
			if(!filepath)
			{
				if(!g_video_file)
				{
					printf("please input video file path!!!!!!\r\n");
					continue;
				}
				filepath = g_video_file;
			}
			dev_push_send(filepath);
		}
		else if (strcmp(cmd, "rtmp")==0)
		{
			char* url = strtok(NULL, SEP);
			if(!url)
			{
				printf("please input url!!!!!!\r\n");
				continue;
			}
			dev_push_rtmp(url);
		}
		else if (strcmp(cmd, "quit")==0 || strcmp(cmd, "q")==0)
		{
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

#ifdef WIN32
	av_register_all();
#endif

	while((c = getopt(argc,argv,"f:s:u:d:bt:vr:"))!= -1)
	{
		switch (c) 
		{
		case 'f':
			g_video_file = optarg;
			break;
		case 's':
			if ((pos=strstr(optarg, ":")) != NULL) 
			{
				*pos = '\0';
				g_server = optarg;
				g_port = (unsigned short)atoi(pos+1);
			} 
			else 
			{
				g_server = optarg;
				g_port = GSS_SERVER_PORT;
			}
			break;
		case 'u':
			g_uid = optarg;
			break;
		case 'd':
			g_ds_server = optarg;
			break;
		case 'b':
			g_deamon = 1;
			break;
		case 't':
			g_type = optarg;
			break;
		case 'r':
			g_fps = atoi(optarg);
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

	if(g_deamon)
	{
		p2p_log_set_level(0);

		if(strcmp(g_type, "main") == 0)
			device_main_connect_server(g_server, g_port, g_uid);

		if(strcmp(g_type, "signaling") == 0)
			signaling_connect_server(g_server, g_port, g_uid);

		if(strcmp(g_type, "video") == 0)
			client_av_connect_server(g_server, g_port, g_uid);

		if(strcmp(g_type, "push") == 0)
			dev_push_connect_server(g_server, g_port, g_uid);

		if(strcmp(g_type, "pull") == 0)
			client_pull_connect_server(g_server, g_port, g_uid);

		if(strcmp(g_type, "dispatch") == 0)
			dispatch_deamon();
	}
	else
		p2p_log_set_level(4);
	gss_console();

	p2p_uninit();

	return 0;
}

