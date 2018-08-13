#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef WIN32
#include <Windows.h>
#include "ffmpeg_h264_decoder.h"
#include "video_wnd.h"
#include "dd_yuv.h"
#else
#include <pthread.h>
#include <unistd.h>
#endif
#include "av_conn.h"
#include "gss_transport.h" 
#include "h264_reader.h"

static void* dev_av_conn = NULL;
static void* client_av_conn = NULL;

extern int g_deamon;
extern char* g_video_file;
extern char* g_type;

extern int g_fps;

#ifdef WIN32
static void* av_h264_decoder=NULL;
static struct video_wnd* av_wnd;
static struct direct_draw_yuv* av_dd_yuv;
#endif

static void on_dev_av_connect_result(void *transport, void* user_data, int status)
{
	if(status == 0)
	{
		printf("device av connect success!\r\n");
		if(g_deamon && g_video_file) //for deamon test, audio and video connection
			dev_av_send(g_video_file);
	}
	else
	{
		printf("failed to device av connect server, error %d\r\n", status); 
		gss_dev_av_destroy(dev_av_conn);
		dev_av_conn = NULL;
	}
}

static void on_dev_av_disconnect(void *transport, void* user_data, int status)
{
	printf("device av connection disconnect, error %d\r\n", status); 
	gss_dev_av_destroy(dev_av_conn);
	dev_av_conn = NULL;
}

static void on_dev_av_recv(void *transport, void *user_data, char* data, int len)
{
	printf("device av connection receive date, length %d\r\n", len); 
}

void on_dev_av_client_disconnect(void *transport, void *user_data)
{
	printf("device av connection client disconnect\r\n"); 
	gss_dev_av_destroy(dev_av_conn);
	dev_av_conn = NULL;
}


void dev_av_connect_server(char* server, unsigned short port, char* uid, int client_conn)
{
	int result;
	gss_dev_av_cfg cfg;
	gss_dev_av_cb cb;

	if(dev_av_conn)
	{
		printf("device av connection is not null");
		return;
	}

	cfg.server = server;
	cfg.port = port;
	cfg.uid = uid;
	cfg.user_data = NULL;
	cfg.client_conn = client_conn;
	cfg.cb = &cb;

	cb.on_recv = on_dev_av_recv;
	cb.on_disconnect = on_dev_av_disconnect;
	cb.on_connect_result= on_dev_av_connect_result;
	cb.on_client_disconnect= on_dev_av_client_disconnect;


	result = gss_dev_av_connect(&cfg, &dev_av_conn); 
	if(result != 0)
		printf("client_av_connect_server return %d", result);
}

void dev_av_disconnect_server()
{
	if(!dev_av_conn)
	{
		printf("device av connection is null");
		return;
	}
	gss_dev_av_destroy(dev_av_conn);
	dev_av_conn = NULL;
}

int av_on_h264_frame(const char* buf, int len, unsigned int time_stamp)
{
	if(!dev_av_conn)
		return -1;

	return gss_dev_av_send(dev_av_conn, (char*)buf, len, P2P_SEND_BLOCK, GSS_REALPLAY_DATA);
}

#ifdef _WIN32
static DWORD WINAPI av_h264_file_thread(void* arg)
#else
static void* av_h264_file_thread(void* arg)
#endif
{
	char* filepath = (char*)arg;

	read_h264_file(filepath, g_fps, g_deamon, av_on_h264_frame);

	free(filepath);
#ifndef _WIN32
	pthread_detach(pthread_self());
#endif
	return 0;
}

void dev_av_send(const char* filepath)
{
	char* path;
#ifdef _WIN32
	HANDLE thread;
#else
	pthread_t thread;
#endif

	if(!dev_av_conn)
	{
		printf("device av connection is null");
		return;
	}

	path = strdup(filepath);

#ifdef _WIN32
	thread = CreateThread(NULL, 0, av_h264_file_thread, path, 0, NULL);
	CloseHandle(thread);
#else
	pthread_create(&thread, NULL, av_h264_file_thread, path);
#endif
}

static void client_av_destroy()
{
	if(client_av_conn)
	{
		gss_client_av_destroy(client_av_conn);
		client_av_conn = NULL;
	}
#ifdef _WIN32
	if(av_h264_decoder)
	{
		destroy_ffmpeg_video_decoder(av_h264_decoder);
		av_h264_decoder = NULL;
	}

	if(av_dd_yuv)
	{
		destroy_dd_yuv(av_dd_yuv);
		av_dd_yuv = NULL;
	}

	if(av_wnd)
	{
		destroy_video_wnd(av_wnd);
		av_wnd = NULL;
	}
#endif
}

static void on_av_connect_result(void *transport, void* user_data, int status)
{
	if(status == 0)
		printf("client av connect success!\r\n");
	else
	{
		printf("failed to client av connect server, error %d\r\n", status); 
		client_av_destroy();
	}
}

static void on_av_disconnect(void *transport, void* user_data, int status)
{
	printf("client av connection disconnect, error %d\r\n", status); 
	client_av_destroy();
}

static void on_av_recv(void *transport, void *user_data, char* data, int len)
{
	//printf("on_av_recv length %d\r\n", len); 

#ifdef _WIN32
	if(av_h264_decoder)
		ffmpeg_video_decode_frame(av_h264_decoder, data, len, 0); 
#endif
}

static void on_av_device_disconnect(void *transport, void *user_data)
{
	printf("client av connection device disconnect\r\n"); 
	client_av_destroy();
}

#ifdef _WIN32
static void on_av_video_decode(void* decoder, void* user_key, unsigned char** data, int* linesize, int width, int height, unsigned int time_stamp)
{
	if(av_dd_yuv == NULL)
	{
		av_dd_yuv = create_dd_yuv(video_wnd_get_handle(av_wnd), width, height);
		if(av_dd_yuv == NULL)
			printf("failed to create_dd_yuv!!!!!!!!\r\n");
	}
	if(av_dd_yuv)
		dd_yuv_draw(av_dd_yuv, data, linesize);
}
#endif

void client_av_connect_server(char* server, unsigned short port, char* uid)
{
	int result;
	gss_client_conn_cfg cfg;
	gss_client_conn_cb cb;

	if(client_av_conn)
	{
		printf("client av connection is not null");
		return;
	}

#ifdef _WIN32
	av_h264_decoder = create_ffmpeg_video_decoder(NULL, on_av_video_decode, &result);
	if(av_h264_decoder == NULL || result != 0)
		return ;
	av_wnd = create_video_wnd();
#endif

	cfg.server = server;
	cfg.port = port;
	cfg.uid = uid;
	cfg.user_data = NULL;
	cfg.cb = &cb;

	cb.on_recv = on_av_recv;
	cb.on_disconnect = on_av_disconnect;
	cb.on_connect_result= on_av_connect_result;
	cb.on_device_disconnect= on_av_device_disconnect;

	result = gss_client_av_connect(&cfg, &client_av_conn); 
	if(result != 0)
		printf("client_av_connect_server return %d", result);
}

void client_av_disconnect_server()
{
	if(!client_av_conn)
	{
		printf("client av connection is null");
		return;
	}
	client_av_destroy();
}

void client_av_send()
{
	char* text = "client av connection send text";
	if(!client_av_conn)
	{
		printf("client av connection is null");
		return;
	}
	gss_client_av_send(client_av_conn, text, strlen(text), P2P_SEND_BLOCK) ;
}