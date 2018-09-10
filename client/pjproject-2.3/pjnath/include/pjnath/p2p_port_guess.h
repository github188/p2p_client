#ifndef __PJNATH_P2P_PORT_GUESS_H__
#define __PJNATH_P2P_PORT_GUESS_H__

#define GUESS_MIN_PORT (1024)
#define GUESS_MAX_PORT (65535)

#include <pjlib.h>
#include <pjnath/p2p_tcp.h>

#ifdef USE_P2P_TCP

#define USE_P2P_PORT_GUESS 1
#ifdef USE_P2P_PORT_GUESS

PJ_BEGIN_DECL

struct pj_ice_strans_p2p_conn;
struct p2p_port_guess;


#define PORT_BITS_COUNT ((65536-GUESS_MIN_PORT)/8)

#define P2P_GUESS_REQUEST (0xB1)  
#define P2P_GUESS_RESPONSE (0xB2)  

#define PORT_GUESS_HOLE_TOTAL_COUNT (192)
#define PORT_GUESS_HOLE_INIT_COUNT (64)
#define PORT_GUESS_HOLE_ADD_COUNT (4)

#pragma pack(1)
	/**
	 * This structure p2p port guess package. All the fields are in network byte
	 * order when it's on the wire.
	 */
	typedef struct p2p_port_guess_data
	{
		pj_uint8_t type;
		pj_uint16_t port;
		pj_uint8_t padding;
		pj_int32_t conn_id;
	}p2p_port_guess_data;

#pragma pack()

typedef struct p2p_port_guess
{
	pj_pool_t *pool; /* pj memory pool*/

	pj_sock_t sock; /*ice session stun socket handle*/

	pj_sockaddr remote_addr; /* peer Internet address*/

	struct pj_ice_strans_p2p_conn* p2p_conn; /*user data*/

	pj_timer_entry timer; /*guess timer*/

	pj_uint16_t guess_times; /*guess times,add 1 per timer*/

	char port_bits[PORT_BITS_COUNT]; /*if bit is 1, port had used*/

	p2p_port_guess_data guess_data;

	pj_grp_lock_t  *grp_lock;

	pj_uint16_t port_low;
	pj_uint16_t port_high;
	pj_uint16_t port_peer; //stun port

	pj_sockaddr response_addr; /* peer response address*/
	int response_times; /*send response times */

	//SymmetricNAT holes
	pj_sock_t holes[PORT_GUESS_HOLE_TOTAL_COUNT];
	pj_activesock_t *holes_activesock[PORT_GUESS_HOLE_TOTAL_COUNT];
	p2p_port_guess_data holes_recv_data[PORT_GUESS_HOLE_TOTAL_COUNT];
	int valid_holes;
	int total_holes;
	//for receive p2p_tcp data,size is P2P_TCP_MSS
	char* valid_hole_recv_buf; 

	unsigned char too_many_files;

	unsigned char guess_success;
}p2p_port_guess;

struct p2p_port_guess* p2p_create_port_guess(pj_sock_t sock, 
	pj_sockaddr* remote_addr,
	struct pj_ice_strans_p2p_conn* p2p_conn);

void p2p_destroy_port_guess(struct p2p_port_guess* guess);

void p2p_port_guess_on_request(struct p2p_port_guess* guess, 
							   unsigned char* buf, 
							   pj_size_t len,
							   const pj_sockaddr_t *src_addr,
							   unsigned src_addr_len);

void p2p_port_guess_on_response(struct p2p_port_guess* guess, 
							   unsigned char* buf, 
							   pj_size_t len,
							   const pj_sockaddr_t *src_addr,
							   unsigned src_addr_len);

PJ_END_DECL

#endif //end of USE_P2P_TCP

#endif //end of USE_P2P_PORT_GUESS

#endif //end of __PJNATH_P2P_PORT_GUESS_H__