#ifndef __SOCKET_PAIR_H__
#define __SOCKET_PAIR_H__

PJ_BEGIN_DECL

typedef struct p2p_socket_pair p2p_socket_pair;

typedef struct p2p_socket_pair_item
{
	void* data;
	void (*cb)(void* data);
}p2p_socket_pair_item;

pj_status_t create_socket_pair(p2p_socket_pair** sock_pair, pj_pool_t *pool);
void destroy_socket_pair(p2p_socket_pair* sock_pair);
void schedule_socket_pair(p2p_socket_pair* sock_pair, p2p_socket_pair_item* item);
void run_socket_pair(p2p_socket_pair *sock_pair);

PJ_END_DECL

#endif /* __SOCKET_PAIR_H__ */