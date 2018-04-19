#ifndef __DISP_CLIENT__H__
#define __DISP_CLIENT__H__

struct event_base;

#define MAX_DISP_SERVER_LEN (256)

#define MAX_DISP_SERVER_COUNT (64)

typedef struct disp_svr_info{
	char addr[MAX_DISP_SERVER_LEN];
	unsigned short port;
}disp_svr_info;

typedef void (*send_disp_info_func)(void* tcp_client);
typedef void (*reload_disp_svr_info_func)(struct disp_svr_info* disp_svr, int* count);

int start_disp_client(struct event_base* evb, send_disp_info_func send_func,  reload_disp_svr_info_func reload_func);

void stop_disp_client(void);

#endif