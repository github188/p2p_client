#ifndef __P2P_TCP_H__
#define __P2P_TCP_H__

#include <pjlib.h>
#include <pjlib-util.h>
#include <pjnath.h>

#pragma pack(1)
	/**
	 * This structure p2p data header. All the fields are in network byte
	 * order when it's on the wire.
	 */
	typedef struct p2p_tcp_header
	{
		pj_uint8_t type;
		//if type is P2P_TCP_TYPE_DATA,seq is send sequence number,
		//if type is P2P_TCP_TYPE_ACK,seq is last receive sequence number or recved_last_order_seq
		pj_uint32_t seq; 
		pj_uint16_t len;
		pj_uint16_t wnd_size;
		pj_uint32_t ack;
		pj_uint8_t send_times;
		pj_uint8_t padding[2];
	}p2p_tcp_header;
#pragma pack()

#pragma pack(4) //for 64 bit system(ios..), 8 byte align, 
	typedef struct p2p_tcp_snd_data
	{
		pj_uint8_t send_times;
		pj_time_val last_send_time;
		struct p2p_tcp_snd_data* next;
		/*in p2p_tcp_send function
		split a long send buffer to multiple P2P_TCP_MSS size short buffer
		pkg_index is short buffer index*/
		pj_uint32_t pkg_index;
		p2p_tcp_header header;		
	}p2p_tcp_snd_data;
#pragma pack()

#define P2P_TCP_MSS 1360
#define P2P_TCP_MAX_DATA_LEN (P2P_TCP_MSS-sizeof(p2p_tcp_header))
#define P2P_TCP_MAX_ACK_LEN ((sizeof(p2p_tcp_header)+MAX_SACK_COUNT*sizeof(pj_uint32_t)*2)/4)

#define USE_P2P_TCP 1

#ifdef USE_P2P_TCP



#ifdef __cplusplus
extern "C" {
#endif

struct sockaddr;

#define P2P_TCP_SEND_BUFFER_COUNT 256
#define P2P_TCP_RECV_BUFFER_COUNT 256

#define MAX_SACK_COUNT P2P_TCP_SEND_BUFFER_COUNT

	typedef struct p2p_tcp_recved_data
	{
		pj_uint16_t data_offset;
		pj_time_val t;
		struct p2p_tcp_recved_data* next;
	}p2p_tcp_recved_data;


	struct p2p_tcp_sock;
	typedef struct p2p_tcp_cb
	{
		void* user_data;

		int (*send)(const struct sockaddr* addr, const char* buffer, int buffer_len, void *user_data, pj_uint8_t force_relay);
		void (*on_close)(void *user_data);
		void (*on_recved)(void *user_data);
		void (*on_send)(void *user_data);
		void (*on_noresned_recved)(void *user_data, const char* buffer, int buffer_len);
	}p2p_tcp_cb;

	typedef struct p2p_tcp_sock 
	{
		pj_pool_t		*pool;	/**< Pool used by this object.	*/
		p2p_tcp_cb cb; /*p2p tcp callback*/

		//cache user send data
		p2p_tcp_snd_data* send_data_begin;
		p2p_tcp_snd_data* send_data_end;
		p2p_tcp_snd_data* send_data_cur;
		pj_uint16_t send_data_count;

		//last send time
		pj_time_val last_send_time;

		//last receive user data time
		pj_time_val last_recv_data_time;

		//last receive package time, maybe ack, user data, window probe, heart
		pj_time_val last_recv_time;

		//user data ordered 
		p2p_tcp_recved_data* recved_user_data_begin;
		p2p_tcp_recved_data* recved_user_data_end;
		pj_uint16_t recved_user_data_count;
		//receive last order sequence number
		pj_uint32_t recved_last_order_seq;

		//user data disorder
		p2p_tcp_recved_data* recved_disorder_begin;
		pj_uint16_t recved_disorder_count;

		//slow start,fast resend,congestion control
		pj_uint8_t send_state;

		//congestion windows
		pj_uint16_t cwnd;

		//Slow Start Threshold
		pj_uint16_t ssthresh;
		
		//send sequence number
		pj_uint32_t send_seq;

		/* RTT measurement */  
		pj_uint32_t srtt; /* smoothed round trip time << 3 */  
		pj_uint32_t mdev; /* medium deviation */  
		pj_uint32_t mdev_max; /* maximal mdev for the last rtt period */  
		pj_uint32_t rttvar; /* smoothed mdev_max */ 
		pj_uint32_t rto;
		pj_uint32_t rtt_req; /*last rrt sequence number*/
		pj_time_val rto_time;

		//p2p tcp resend timer
		pj_timer_entry resend_timer;

		pj_uint16_t avoid_congestion_ack;

		pj_uint16_t peer_wnd_size; //remote receive window remain size

		//delay ack or fast ack
		pj_uint8_t delay_ack;

		//last send ack sequence number
		pj_uint32_t last_send_ack;
		pj_uint32_t sack[2*MAX_SACK_COUNT];
		pj_uint16_t sack_len;
		//last received ack
		pj_uint32_t last_recv_ack;
		pj_uint16_t dup_ack;

		//delay ack timeout
		pj_uint32_t ato; 

		//fast ack count
		pj_uint32_t fask_ack_count;

		//zero window probe timer
		pj_uint8_t is_zero_wnd_probe;
		pj_timer_entry zero_wnd_probe_timer;

		pj_sock_t sock;
		pj_sockaddr peer_addr;

		//ack buffer
		pj_uint32_t ack_buffer[P2P_TCP_MAX_ACK_LEN];
		
		//delay ack timer
		pj_timer_entry delay_ack_timer;

		//p2p tcp heart timer
		pj_timer_entry heart_timer;

		pj_grp_lock_t  *grp_lock;

		pj_uint16_t dup_ack_wnd;

		pj_uint8_t force_relay; //force use relay socket
	}p2p_tcp_sock;

	p2p_tcp_sock* p2p_tcp_create(p2p_tcp_cb* cb, pj_sock_t sock, pj_sockaddr* remote_addr,pj_grp_lock_t  *grp_lock);
	void p2p_tcp_destory(p2p_tcp_sock* sock);
	int p2p_tcp_send(p2p_tcp_sock* sock, const void* buffer, int buffer_len);
	int p2p_tcp_recv(p2p_tcp_sock* sock, void* buffer, int buffer_len);
	void p2p_tcp_data_recved(p2p_tcp_sock* sock, const void* buffer, int buffer_len);

	//immediately sendto,no cache, no resend
	pj_status_t p2p_tcp_no_resend(p2p_tcp_sock* sock, const void* buffer, int buffer_len);

	int p2p_tcp_get_send_remain(p2p_tcp_sock* sock);

	//port guess success,change udp socket handle and source address
	void p2p_tcp_sock_guess_port(p2p_tcp_sock* sock,
		pj_sock_t handle,
		const pj_sockaddr_t *src_addr,
		unsigned src_addr_len);

	//clear all send buffer
	void p2p_tcp_clear_send_buf(p2p_tcp_sock* sock);

#ifdef __cplusplus
}
#endif

#endif //USE_P2P_TCP

#endif //__P2P_TCP_H__