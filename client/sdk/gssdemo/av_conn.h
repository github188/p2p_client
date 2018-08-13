#ifndef AV_CONN_H
#define AV_CONN_H

#ifdef __cplusplus
extern "C" {
#endif
	
void client_av_connect_server(char* server, unsigned short port, char* uid);
void client_av_disconnect_server();
void client_av_send();

void dev_av_connect_server(char* server, unsigned short port, char* uid, int client_conn);
void dev_av_disconnect_server();
void dev_av_send(const char* filepath);

#ifdef __cplusplus
}    
#endif

#endif

