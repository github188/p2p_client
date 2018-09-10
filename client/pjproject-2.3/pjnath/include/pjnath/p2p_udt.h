#ifndef __PJNATH_P2P_UDT_H__
#define __PJNATH_P2P_UDT_H__

typedef struct p2p_udt_connector p2p_udt_connector;
typedef struct p2p_udt_listener p2p_udt_listener;
typedef struct p2p_udt_accepter p2p_udt_accepter;

#define P2P_RECV_BUFFER_SIZE (1024*1024)
#define P2P_SEND_BUFFER_SIZE (1024*1024)
#define P2P_MIN_BUFFER_SIZE (1024*64)

#define P2P_DATA_COMMON (0)
#define P2P_DATA_AV (1)
#define P2P_DATA_AV_NO_RESEND (2)

typedef struct p2p_udt_cb
{
	pj_status_t (*udt_send)(void* user_data, const pj_sockaddr_t* addr, const char* buffer, size_t buffer_len, pj_uint8_t force_relay);
	void (*udt_on_recved)(void* user_data, const char* buffer, size_t buffer_len);
	pj_status_t (*udt_on_accept)(void* user_data, void* udt_sock, pj_sockaddr_t* addr);
	void (*get_sock_addr)(pj_sockaddr_t* addr, void* user_data);
	void (*get_peer_addr)(pj_sockaddr_t* addr, void* user_data);
	void (*udt_on_connect)(void* user_data, pj_bool_t success);
	void (*udt_pause_send)(void* user_data, pj_bool_t pause);
	void (*udt_on_close)(void* user_data);
	void (*udt_on_noresend_recved)(void* user_data, const char* buffer, size_t buffer_len);
}p2p_udt_cb;

struct udt_recved_base;
typedef struct udt_recved_data{	char* buffer;
	int buffer_len;
	struct udt_recved_base* base;
	struct udt_recved_data* next;
}udt_recved_data;

PJ_BEGIN_DECL

pj_status_t p2p_udt_init();
void p2p_udt_uninit();

pj_status_t create_p2p_udt_connector(p2p_udt_cb* cb, void* user_data, int send_buf_size, int recv_buf_size, pj_sock_t sock, p2p_udt_connector** connector);

pj_status_t p2p_udt_connector_send(p2p_udt_connector* connector, const char* buffer, size_t buffer_len);

pj_status_t p2p_udt_connector_on_recved(p2p_udt_connector* connector, 
										const char* buffer, 
										size_t buffer_len,
										const pj_sockaddr_t *src_addr,
										unsigned src_addr_len);

void destroy_p2p_udt_connector(p2p_udt_connector* connector);

pj_status_t create_p2p_udt_accepter(p2p_udt_cb* cb, void* user_data, int send_buf_size, int recv_buf_size, void* udt_sock,  pj_sock_t sock, p2p_udt_accepter** accepter);

void destroy_p2p_udt_accepter(p2p_udt_accepter* accepter);

pj_status_t p2p_udt_accepter_send(p2p_udt_accepter* accepter, const char* buffer, size_t buffer_len);

pj_status_t create_p2p_udt_listener(p2p_udt_cb* cb, void* user_data, const pj_sockaddr_t *bind_addr, p2p_udt_listener** listener);

pj_status_t p2p_udt_listener_on_recved(p2p_udt_listener* listener, 
									   const char* buffer, 
									   size_t buffer_len,
									   const pj_sockaddr_t *src_addr,
									   unsigned src_addr_len);

pj_status_t p2p_udt_accepter_on_recved(p2p_udt_accepter* accepter, 
									   const char* buffer, 
									   size_t buffer_len,
									   const pj_sockaddr_t *src_addr,
									   unsigned src_addr_len);

void destroy_p2p_udt_listener(p2p_udt_listener* listener);

pj_status_t p2p_udt_connector_model_send(p2p_udt_connector* connector, const char* buffer, size_t buffer_len, p2p_send_model model, int type);
pj_status_t p2p_udt_accepter_model_send(p2p_udt_accepter* accepter, const char* buffer, size_t buffer_len,p2p_send_model model,int type);

pj_status_t p2p_udt_connector_set_opt(p2p_udt_connector* connector, p2p_opt opt, const void* optval, int optlen);
pj_status_t p2p_udt_accepter_set_opt(p2p_udt_accepter* accepter, p2p_opt opt, const void* optval, int optlen);

pj_bool_t p2p_udt_connector_sock_valid(p2p_udt_connector* connector);

void p2p_udt_connector_wakeup_send(p2p_udt_connector* connector);
void p2p_udt_accepter_wakeup_send(p2p_udt_accepter* accepter);

void p2p_udt_connector_guess_port(p2p_udt_connector* connector,
								  pj_sock_t sock,
								  const pj_sockaddr_t *src_addr,
								  unsigned src_addr_len);
void p2p_udt_accepter_guess_port(p2p_udt_accepter* accepter,
								 pj_sock_t sock,
								 const pj_sockaddr_t *src_addr,
								 unsigned src_addr_len);

//clear all send buffer
void p2p_udt_accepter_clear_send_buf(p2p_udt_accepter* accepter);	

PJ_END_DECL
#endif	/* __PJNATH_P2P_UDT_H__ */