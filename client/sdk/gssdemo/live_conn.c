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
#endif
#include "live_conn.h"
#include "gss_transport.h" 
#include "h264_reader.h"
#include "gss_common.h"

static void* dev_push_conn = NULL;
static void* client_pull_conn = NULL;

extern int g_fps;
extern int g_deamon;
extern char* g_video_file;
extern char* g_type;

#ifdef WIN32
static void* pull_h264_decoder=NULL;
static struct video_wnd* pull_wnd=NULL;
static struct direct_draw_yuv* pull_dd_yuv=NULL;

static DWORD first_frame_time = 0;

static unsigned int first_frame_ts = 0;
#endif

static void on_dev_push_connect_result(void *transport, void* user_data, int status)
{
	if(status == 0)
	{
		printf("device push connect success!\r\n");
		if(g_deamon && g_video_file) //for deamon test, push audio and video to gss
			dev_push_send(g_video_file);
	}		
	else
	{
		printf("failed to device push connect server, error %d\r\n", status); 
		gss_dev_push_destroy(dev_push_conn);
		dev_push_conn = NULL;
	}
}

static void on_dev_push_disconnect(void *transport, void* user_data, int status)
{
	printf("device push connection disconnect, error %d\r\n", status); 
	gss_dev_push_destroy(dev_push_conn);
	dev_push_conn = NULL;
}

static void on_rtmp_event(void *transport, void* user_data, int event)
{
	printf("on_rtmp_event, event %d\r\n", event); 
}


void dev_push_connect_server(char* server, unsigned short port, char* uid)
{
	int result;
	gss_dev_push_conn_cfg cfg;
	gss_dev_push_cb cb;

	if(dev_push_conn)
	{
		printf("device push connection is not null");
		return;
	}

	cfg.server = server;
	cfg.port = port;
	cfg.uid = uid;
	cfg.user_data = NULL;
	cfg.cb = &cb;

	cb.on_disconnect = on_dev_push_disconnect;
	cb.on_connect_result= on_dev_push_connect_result;
	cb.on_rtmp_event = on_rtmp_event;

	result = gss_dev_push_connect(&cfg, &dev_push_conn); 
	if(result != 0)
		printf("dev_push_connect_server return %d", result);
}

void dev_push_disconnect_server()
{
	if(!dev_push_conn)
	{
		printf("device push connection is null");
		return;
	}
	gss_dev_push_destroy(dev_push_conn);
	dev_push_conn = NULL;
}

int dev_push_on_h264_frame(const char* buf, int len, unsigned int time_stamp)
{
	if(dev_push_conn)
	{
		unsigned char nut = buf[4] & 0x1f;
		return gss_dev_push_send(dev_push_conn, (char*)buf, len, GSS_VIDEO_DATA, time_stamp, nut == 7, P2P_SEND_BLOCK);
	}
	else
		return -1;
}

#ifdef _WIN32
static DWORD WINAPI live_h264_file_thread(void* arg)
#else
static void* live_h264_file_thread(void* arg)
#endif
{
	char* filepath = (char*)arg;

	read_h264_file(filepath, g_fps, g_deamon, dev_push_on_h264_frame);

	free(filepath);

#ifndef _WIN32
	pthread_detach(pthread_self());
#endif
	return 0;
}

void dev_push_send(const char* filepath)
{
#ifdef _WIN32
	HANDLE thread;
#else
	pthread_t thread;
#endif
	char* path;

	if(!dev_push_conn)
	{
		printf("device push connection is null");
		return;
	}

	path = strdup(filepath);

#ifdef _WIN32
	thread = CreateThread(NULL, 0, live_h264_file_thread, path, 0, NULL);
	CloseHandle(thread);
#else
	pthread_create(&thread, NULL, live_h264_file_thread, path);
#endif	
		
}

void dev_push_rtmp(char* url)
{
	if(!dev_push_conn)
	{
		printf("device push connection is null");
		return;
	}

	gss_dev_push_rtmp(dev_push_conn, url);
}

static void destory_pull()
{
	if(client_pull_conn)
	{
		gss_client_pull_destroy(client_pull_conn);
		client_pull_conn = NULL;
	}
#ifdef _WIN32
	if(pull_h264_decoder)
	{
		destroy_ffmpeg_video_decoder(pull_h264_decoder);
		pull_h264_decoder = NULL;
	}

	if(pull_dd_yuv)
	{
		destroy_dd_yuv(pull_dd_yuv);
		pull_dd_yuv = NULL;
	}

	if(pull_wnd)
	{
		destroy_video_wnd(pull_wnd);
		pull_wnd = NULL;
	}
#endif
}

static void on_pull_connect_result(void *transport, void* user_data, int status)
{
	if(status == 0)
		printf("client pull connect success!\r\n");
	else
	{
		printf("failed to client pull connect server, error %d\r\n", status); 
		destory_pull();
	}
}

static void on_pull_disconnect(void *transport, void* user_data, int status)
{
	printf("on_pull_disconnect, error %d\r\n", status); 
	destory_pull();
}

static void on_pull_recv(void *transport, void *user_data, char* data, int len, char type, unsigned int time_stamp)
{
#ifdef _WIN32
	DWORD now_time = timeGetTime();
	printf("on_pull_recv length %d,type %d,stamp %u,now time %u\r\n", len, type, time_stamp, now_time);
	if(pull_h264_decoder)
		ffmpeg_video_decode_frame(pull_h264_decoder, data, len, time_stamp); 
#else
	//printf("on_pull_recv length %d,type %d,stamp %u\r\n", len, type, time_stamp); 
#endif
}

static void on_pull_device_disconnect(void *transport, void *user_data)
{
	printf("client pull connection device disconnect\r\n"); 
	destory_pull();
}

#ifdef _WIN32
static void on_pull_video_decode(void* decoder, void* user_key, unsigned char** data, int* linesize, int width, int height, unsigned int time_stamp)
{
	DWORD now_time = timeGetTime();

	if(first_frame_time == 0)
	{
		first_frame_time = now_time;
		first_frame_ts = time_stamp;
	}
	else
	{
		DWORD time_span = now_time - first_frame_time;
		DWORD draw_time = time_stamp - first_frame_ts;
		if(draw_time > time_span)
			usleep(1000*(draw_time-time_span));
	}

	if(pull_dd_yuv == NULL)
	{
		pull_dd_yuv = create_dd_yuv(video_wnd_get_handle(pull_wnd), width, height);
		if(pull_dd_yuv == NULL)
			printf("failed to create_dd_yuv!!!!!!!!\r\n");
	}
	if(pull_dd_yuv)
		dd_yuv_draw(pull_dd_yuv, data, linesize);
}
#endif

void client_pull_connect_server(char* server, unsigned short port, char* uid)
{
	int result;
	gss_pull_conn_cfg cfg;
	gss_pull_conn_cb cb;

	if(client_pull_conn)
	{
		printf("client pull connection is not null");
		return;
	}

#ifdef _WIN32
	first_frame_time = 0 ;
	first_frame_ts = 0;
	pull_h264_decoder = create_ffmpeg_video_decoder(NULL, on_pull_video_decode, &result);
	if(pull_h264_decoder == NULL || result != 0)
		return ;
	pull_wnd = create_video_wnd();

#endif

	cfg.server = server;
	cfg.port = port;
	cfg.uid = uid;
	cfg.user_data = NULL;
	cfg.cb = &cb;

	cb.on_recv = on_pull_recv;
	cb.on_disconnect = on_pull_disconnect;
	cb.on_connect_result= on_pull_connect_result;
	cb.on_device_disconnect= on_pull_device_disconnect;

	result = gss_client_pull_connect(&cfg, &client_pull_conn); 
	if(result != 0)
		printf("client_pull_connect_server return %d", result);
}

void client_pull_disconnect_server()
{
	if(!client_pull_conn)
	{
		printf("client pull connection is null");
		return;
	}
	destory_pull();
}

