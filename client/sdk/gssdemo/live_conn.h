#ifndef LIVE_CONN_H
#define LIVE_CONN_H

#ifdef __cplusplus
extern "C" {
#endif
	
void client_pull_connect_server(char* server, unsigned short port, char* uid);
void client_pull_disconnect_server();
void client_pull_send();

void dev_push_connect_server(char* server, unsigned short port, char* uid);
void dev_push_disconnect_server();
void dev_push_send(const char* filepath);
void dev_push_rtmp(char* url);

#ifdef __cplusplus
}    
#endif

#endif

