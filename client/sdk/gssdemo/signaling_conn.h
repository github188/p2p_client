#ifndef SIGNALING_CONN_H
#define SIGNALING_CONN_H

#ifdef __cplusplus
extern "C" {
#endif
	
void device_main_connect_server(char* server, unsigned short port, char* uid);
void signaling_connect_server(char* server, unsigned short port, char* uid);

void device_main_disconnect_server();
void signaling_disconnect_server();

void device_main_send(int client_index);
void signaling_connection_send();

#ifdef __cplusplus
}    
#endif

#endif

