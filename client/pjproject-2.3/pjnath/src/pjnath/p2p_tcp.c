#include <pjnath/p2p_tcp.h>

#ifdef USE_P2P_TCP

#ifdef WIN32
#include <WinSock2.h>
#else
#include <sys/time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#endif


//minimal rto time,ms
#define MIN_RTO (200)

#define RESEND_RTO_ADD_VAL (100)

//initialize rtt value,ms
#define INIT_RTT (MIN_RTO/3)

/* minimal time to delay before sending an ACK,ms*/  
#define DELAY_ACK_MIN (40)

/* maximal time to delay before sending an ACK,ms */  
#define DELAY_ACK_MAX (200)

//minimal fast ack count
#define MIN_FAST_ACK (2)

//maximal fast ack count
#define MAX_FAST_ACK (4)

//initialize congestion windows
#define CWND_INIT_VAL (24)

#define CWND_RESEND_VAL (4)

//initialize ssthresh
#define INIT_SSTHRESH (64)
#define MIN_DUP_SSTHRESH (48)

enum{SEND_STATE_SLOW_START=0, SEND_STATE_AVOID_CONGESTION, SEND_STATE_FAST_RECOVERY};

#define P2P_TCP_TYPE_DATA (0xA1) 
#define P2P_TCP_TYPE_ACK (0xA2)
#define P2P_TCP_TYPE_ZERO_WND (0xA3)
#define P2P_TCP_TYPE_HEART (0xA4)
#define P2P_TCP_TYPE_DATA_NORESEND (0xA5)

#define P2P_TCP_CLOSE_TIMEOUT (8) //second

#define P2P_TCP_HEART_TIMEOUT (1) //second

#define P2P_TCP_WND_MULTIPLE (2)

#define CONGESTION_CWND_SSTHRESH (64)

#define P2P_TCP_MAX_CWND (128)

#define MAX_ONCE_SEND_COUNT (32)

#define BAD_ONCE_SEND_COUNT (24)

#define BAD2_ONCE_SEND_COUNT (16)

#define BAD_SEND_COUNT (48)

#define BAD_MIN_SEND_SPAN (120)

#define PJ_TIME_VAL_ADD_MS(t,ms)	do{ \
										 (t).msec+=(ms); \
										 if((t).msec>=1000) \
										 {  \
											(t).sec+=((t).msec/1000); \
											(t).msec=((t).msec%1000); \
										 } \
									} while(0)

static void p2p_tcp_send_data(p2p_tcp_sock* sk, pj_time_val now);

#ifndef MAX
#   define MAX(a,b) (a > b ? a : b)
#endif

//#define P2P_TCP_DEBUG_LOG 1

//#define TCP_GRP_LOCK_LOG 1

#ifdef TCP_GRP_LOCK_LOG
static pj_status_t tcp_grp_lock_acquire_log(const char* file, int line, pj_grp_lock_t *grp_lock)
{
	PJ_LOG(3, ("grp_lock", "tcp_grp_lock_acquire %s %d %p", file, line, grp_lock));
	return pj_grp_lock_acquire(grp_lock);
}
static pj_status_t tcp_grp_lock_release_log(const char* file, int line, pj_grp_lock_t *grp_lock)
{
	PJ_LOG(3, ("grp_lock", "tcp_grp_lock_release %s %d %p", file, line, grp_lock));
	return pj_grp_lock_release(grp_lock);
}

#define tcp_grp_lock_acquire(grp_lock) tcp_grp_lock_acquire_log(__FILE__, __LINE__, grp_lock)
#define tcp_grp_lock_release(grp_lock) tcp_grp_lock_release_log(__FILE__, __LINE__, grp_lock)
#else

#define tcp_grp_lock_acquire pj_grp_lock_acquire
#define tcp_grp_lock_release pj_grp_lock_release

#endif

//to see linux tcp tcp_rtt_estimator function source code
/*
SRTT = SRTT+0.125(RTT-SRTT)
DevRTT = (1-0.25)*DevRTT+ 0.25*(|RTT-SRTT|)
RTO = SRTT + 4*DevRTT

p2p_tcp_sock->srtt = SRTT*8
p2p_tcp_sock->mdev = DevRTT*4
p2p_tcp_sock->mdev_max is max p2p_tcp_sock->mdev in cwnd
*/
static void tcp_rtt_estimator (p2p_tcp_sock* tp, pj_uint32_t mrtt)  
{  
	long m = mrtt;
	if (m == 0)
		m = 1;   

	if (tp->srtt != 0) {
		m -= (tp->srtt >> 3); /* m is |RTT-SRTT| */
		tp->srtt += m;  /* srtt = 7/8 srtt + 1/8 new */

		if (m < 0) {
			m = -m; 
			m -= (tp->mdev >> 2); 
			if (m > 0)
				m >>= 3;
		} else {
			m -= (tp->mdev >> 2); 
		}  

		tp->mdev += m; /* mdev = 3/4 mdev + 1/4 new */

		if (tp->mdev > tp->mdev_max) {  
			tp->mdev_max = tp->mdev;  
			if (tp->mdev_max > tp->rttvar )  
				tp->rttvar = tp->mdev_max;  
		}

		if (tp->last_recv_ack >= tp->rtt_req) {  
			if (tp->mdev_max < tp->rttvar)
				tp->rttvar -= (tp->rttvar - tp->mdev_max) >> 2;   
			tp->mdev_max = MIN_RTO; 
			tp->rtt_req = tp->last_recv_ack+ tp->cwnd;
		}
	} 
	else {   
		tp->srtt = m << 3;  
		tp->mdev = m << 1;
		tp->mdev_max = tp->rttvar = MAX(tp->mdev, MIN_RTO);
		tp->rtt_req = tp->last_recv_ack + tp->cwnd;
	}
	tp->rto = (tp->srtt >> 3) + tp->rttvar;
}

PJ_INLINE(void) p2p_tcp_header_hton(p2p_tcp_header* data)
{
	data->seq = htonl(data->seq);
	data->len = htons(data->len);
	data->wnd_size = htons(data->wnd_size);
	data->ack = htonl(data->ack);
}

PJ_INLINE(void) p2p_tcp_header_ntoh(p2p_tcp_header* data)
{
	data->seq = ntohl(data->seq);
	data->len = ntohs(data->len);
	data->wnd_size = ntohs(data->wnd_size);
	data->ack = ntohl(data->ack);
}

PJ_INLINE(void) p2p_tcp_enter_delay_ack(p2p_tcp_sock* sk)
{
	pj_time_val t;

	if(sk->delay_ack == 1)
		return;
	sk->delay_ack = 1;

	t.sec = sk->ato / 1000;
	t.msec = sk->ato % 1000;
	pj_timer_heap_schedule_w_grp_lock(get_p2p_global()->timer_heap, 
		&sk->delay_ack_timer,
		&t, 
		0, 
		NULL);
}

PJ_INLINE(void) p2p_tcp_enter_fast_ack(p2p_tcp_sock* sk)
{
	if(!sk->delay_ack)
		return;
	sk->delay_ack = 0;
	sk->ato = DELAY_ACK_MIN;
	//half of receive window
	sk->fask_ack_count = (P2P_TCP_RECV_BUFFER_COUNT- sk->recved_user_data_count)/2;

	if(sk->fask_ack_count < MIN_FAST_ACK)
		sk->fask_ack_count = MIN_FAST_ACK;

	if(sk->fask_ack_count > MAX_FAST_ACK)
		sk->fask_ack_count = MAX_FAST_ACK;

	pj_timer_heap_cancel_if_active(get_p2p_global()->timer_heap, &sk->delay_ack_timer, 0);
}

PJ_INLINE(pj_uint8_t) seq_in_sack(p2p_tcp_sock* sk, pj_uint32_t seq)
{
	int i;
	pj_uint8_t is_begin = 1;

	for(i=0; i<sk->sack_len; i++)
	{
		if(is_begin)
		{
			if(seq < sk->sack[i])
				return 0;
			if(seq == sk->sack[i])
				return 1;
			is_begin = 0;
		}
		else
		{
			if(seq <= sk->sack[i])
				return 1;
			is_begin = 1;
		}
	}
	return 0;
}

PJ_INLINE(void) p2p_tcp_cwnd_on_resend(p2p_tcp_sock* sk)
{
	sk->ssthresh = sk->cwnd / 2;
	if(sk->ssthresh < INIT_SSTHRESH)
		sk->ssthresh = INIT_SSTHRESH;
	sk->dup_ack = 0;
	PJ_LOG(4,("p2p_tcp", "p2p_tcp_cwnd_on_resend cwnd %d send_data_count %d rto %d ssthresh %d %p", sk->cwnd, sk->send_data_count, sk->rto, sk->ssthresh, sk));
	sk->cwnd = CWND_RESEND_VAL;
	sk->send_state = SEND_STATE_SLOW_START;
}

static void p2p_tcp_rto_resend(pj_timer_heap_t *th, pj_timer_entry *e)
{
	p2p_tcp_sock* sk = (p2p_tcp_sock*) e->user_data;
	pj_time_val now;

	PJ_UNUSED_ARG(th);
	
	tcp_grp_lock_acquire(sk->grp_lock);

	if(sk->send_data_count > 0 && sk->send_data_begin->send_times > 0)
	{
		pj_time_val rto_time = sk->send_data_begin->last_send_time;
		PJ_TIME_VAL_ADD_MS(rto_time, sk->rto); 
		if(PJ_TIME_VAL_GTE(rto_time, sk->rto_time))
			sk->rto_time = rto_time;
	}
	else
	{
		tcp_grp_lock_release(sk->grp_lock);
		return;
	}

	pj_gettickcount(&now);
	if(PJ_TIME_VAL_GTE(now, sk->rto_time))
	{
		if(sk->rto < (MIN_RTO+2*RESEND_RTO_ADD_VAL)) //rto too small
			sk->rto += RESEND_RTO_ADD_VAL;
		p2p_tcp_cwnd_on_resend(sk);
		sk->send_data_cur = sk->send_data_begin;
		p2p_tcp_send_data(sk, now);
	}
	else
	{
		pj_time_val t = sk->rto_time;
		PJ_TIME_VAL_SUB(t, now);
		pj_timer_heap_schedule_w_grp_lock(get_p2p_global()->timer_heap, 
			&sk->resend_timer,
			&t, 
			0, 
			NULL);
	}

	tcp_grp_lock_release(sk->grp_lock);
}

PJ_INLINE(void) zero_wnd_probe_start(p2p_tcp_sock* sk)
{
	// start zero windows probe 
	if(sk->peer_wnd_size == 0)
	{
		if(sk->is_zero_wnd_probe == 0)
		{
			pj_time_val t;
			
			sk->is_zero_wnd_probe = 1;

			t.sec = sk->rto / 1000;
			t.msec = sk->rto % 1000;
			pj_timer_heap_schedule_w_grp_lock(get_p2p_global()->timer_heap, 
				&sk->zero_wnd_probe_timer,
				&t, 
				0, 
				NULL);

			PJ_LOG(3,("p2p_tcp", "zero_wnd_probe_start %p", sk));
		}
	}
}

PJ_INLINE(void) p2p_tcp_sendto(p2p_tcp_sock* sk, const char* buffer, int buffer_len)
{
	if(sk->sock)
	{
		int sended ;

		sended = sendto(sk->sock,
			buffer,
			buffer_len, 
			0,
			(const struct sockaddr*)&sk->peer_addr,
			pj_sockaddr_get_len(&sk->peer_addr));
		if(sended != buffer_len)
		{
			int err = pj_get_native_netos_error();
			PJ_LOG(4,("p2p_tcp", "p2p_tcp_sendto %d %d %d %p", sended, buffer_len, err, sk));
		}
	}
	else
	{
		sk->cb.send((const struct sockaddr*)&sk->peer_addr,	buffer,	buffer_len,	sk->cb.user_data);
	}
}

PJ_INLINE(void) p2p_tcp_sendto_peer(p2p_tcp_sock* sk, const char* buffer, int buffer_len)
{
	p2p_tcp_sendto(sk, buffer, buffer_len);
	pj_gettickcount(&sk->last_send_time);
}

PJ_INLINE(int) p2p_tcp_get_interval(p2p_tcp_sock* sk, p2p_tcp_snd_data* begin, p2p_tcp_snd_data* end)
{
	int interval = 0;
	if(sk->sack_len)
	{
		while(begin != end)
		{
			interval++;
			begin = begin->next;
		}
	}
	else
	{
		interval = end->header.seq - begin->header.seq;
	}
	return interval;
}

static pj_uint32_t p2p_tcp_send_ctrl_wnd(p2p_tcp_sock* sk, pj_uint32_t wnd)
{
	wnd *= P2P_TCP_WND_MULTIPLE;
	//send list too long
	if(sk->send_data_count > BAD_SEND_COUNT)
	{
		if(sk->send_data_count > (2*BAD_SEND_COUNT))
		{
			if(wnd > BAD2_ONCE_SEND_COUNT)
				wnd = BAD2_ONCE_SEND_COUNT;
		}
		else
		{
			if(wnd > BAD_ONCE_SEND_COUNT)
				wnd = BAD_ONCE_SEND_COUNT;
		}
	}
	else
	{
		if(wnd > MAX_ONCE_SEND_COUNT)
			wnd = MAX_ONCE_SEND_COUNT;
	}

	return wnd;
}

static void p2p_tcp_send_data(p2p_tcp_sock* sk, pj_time_val now)
{
	pj_uint32_t fly_count;
	pj_uint32_t i;
	pj_uint32_t wnd = sk->peer_wnd_size < sk->cwnd ? sk->peer_wnd_size : sk->cwnd;
	if(wnd == 0)//window is zero 
	{
		zero_wnd_probe_start(sk);
		return;
	}

	if(!sk->send_data_cur || sk->send_data_count == 0)
		return;

	fly_count = p2p_tcp_get_interval(sk, sk->send_data_begin, sk->send_data_cur);
	if(fly_count >= wnd)
		return;
	else
		wnd -= fly_count;

	wnd = p2p_tcp_send_ctrl_wnd(sk, wnd);

#ifdef P2P_TCP_DEBUG_LOG
	PJ_LOG(4,("p2p_tcp", "p2p_tcp_send_data send_data_count %d send_data_cur %d send_data_begin %d wnd %d", sk->send_data_count, sk->send_data_cur->header.seq, sk->send_data_begin->header.seq, wnd));
#endif

	for(i=0; i<wnd && sk->send_data_cur; i++)
	{
		//if data in sack,do not send
		int send_len;
		p2p_tcp_header* header;
		if(sk->sack_len)
		{
			while(sk->send_data_cur)
			{
				if(!seq_in_sack(sk, sk->send_data_cur->header.seq))
					break;

				sk->send_data_cur->send_times++;
				sk->send_data_cur->last_send_time = now;

				sk->send_data_cur = sk->send_data_cur->next;
			}
			if(!sk->send_data_cur)
				return;
		}
		
		sk->send_data_cur->send_times++;
		sk->send_data_cur->last_send_time = now;
		
		header = &sk->send_data_cur->header;
		header->wnd_size = P2P_TCP_RECV_BUFFER_COUNT>sk->recved_user_data_count? P2P_TCP_RECV_BUFFER_COUNT - sk->recved_user_data_count : 0;
		header->ack = sk->recved_last_order_seq+1;
		header->send_times = sk->send_data_cur->send_times;

		if(sk->send_data_cur == sk->send_data_begin)//if first in send list, set next rto time
		{
			pj_time_val t;

			sk->rto_time = now;
			PJ_TIME_VAL_ADD_MS(sk->rto_time, sk->rto); 

			t.sec = sk->rto / 1000;
			t.msec = sk->rto % 1000;
			pj_timer_heap_schedule_w_grp_lock(get_p2p_global()->timer_heap, 
				&sk->resend_timer,
				&t, 
				0, 
				NULL);
		}
			
		send_len = header->len + sizeof(p2p_tcp_header);

#ifdef P2P_TCP_DEBUG_LOG
		/*PJ_LOG(4,("p2p_tcp", "p2p_tcp_send_data cwnd %d seq %d %p i %d, send_times %d", sk->cwnd, header->seq, sk, i, header->send_times));*/
#endif

		p2p_tcp_header_hton(header);	
		p2p_tcp_sendto(sk, (const char*)header, send_len);
		p2p_tcp_header_ntoh(header);	

		sk->send_data_cur = sk->send_data_cur->next;
	}

	sk->last_send_time = now;

	//PJ_LOG(4,("p2p_tcp", "p2p_tcp_send_data cwnd %d,i %d, send_data_cur %p", sk->cwnd, i, sk->send_data_cur));

	//if last send time minus last receive time less then ato
	if(sk->last_recv_time.sec != 0)
	{
		pj_time_val now1 = now;
		PJ_TIME_VAL_SUB(now1, sk->last_recv_data_time);
		if(PJ_TIME_VAL_MSEC(now1) < (long)sk->ato)
			p2p_tcp_enter_delay_ack(sk);
	}
}

PJ_INLINE(void) zero_wnd_probe_on_recved(p2p_tcp_sock* sk, pj_time_val* now)
{
	if(sk->is_zero_wnd_probe)
	{
		if(sk->peer_wnd_size != 0) //disable zero window probe
		{
			sk->is_zero_wnd_probe = 0;
			pj_timer_heap_cancel_if_active(get_p2p_global()->timer_heap, &sk->zero_wnd_probe_timer, 0);
			PJ_LOG(3,("p2p_tcp", "zero_wnd_probe_on_recved %p, send_data_count %d,peer_wnd_size %d", sk, sk->send_data_count, sk->peer_wnd_size));
			p2p_tcp_send_data(sk, *now);
		}
	}
}

PJ_INLINE(void) p2p_tcp_dup_ack_ssthresh(p2p_tcp_sock* sk)
{
	if (sk->cwnd <= CONGESTION_CWND_SSTHRESH) 
	{
		if(sk->cwnd <= MIN_DUP_SSTHRESH)//too small
			sk->ssthresh = sk->cwnd;
		else
		{
			sk->ssthresh = sk->cwnd/4*3;
			if(sk->ssthresh < MIN_DUP_SSTHRESH)
				sk->ssthresh = MIN_DUP_SSTHRESH;
		}
	}
	else
	{
		sk->ssthresh = sk->cwnd/3*2;
	}
}

PJ_INLINE(void) p2p_tcp_avoid_congestion_ack(p2p_tcp_sock* sk, pj_uint32_t ack)
{
	pj_bool_t add_wnd = PJ_FALSE;
	sk->avoid_congestion_ack += (pj_uint16_t)(ack - sk->last_recv_ack);
	if (sk->cwnd <= CONGESTION_CWND_SSTHRESH) 
	{
		if(sk->avoid_congestion_ack >= sk->cwnd/2)//every rtt/2 cwnd + 1
			add_wnd = PJ_TRUE;
	}
	else
	{
		if(sk->avoid_congestion_ack >= sk->cwnd)//every rtt cwnd + 1
			add_wnd = PJ_TRUE;
	}
	if(add_wnd)
	{
		sk->avoid_congestion_ack = 0;
		sk->cwnd ++;
		if(sk->cwnd >= P2P_TCP_MAX_CWND)
			sk->cwnd = P2P_TCP_MAX_CWND;
	}
}

static pj_bool_t p2p_tcp_is_dup_ack(p2p_tcp_sock* sk, p2p_tcp_header* h)
{
	//ignore user real data,only ack valid
	if(h->type == P2P_TCP_TYPE_ACK && sk->send_data_count)
	{			
		/*
		ack seq must greater than send_data_begin seq
		if resend,ack seq may be less than send_data_begin seq
		send_times must be equal, else SEND_STATE_SLOW_START status will resend many times 
		*/
		if( sk->send_data_begin->header.seq < h->seq 
			&& sk->send_data_begin->header.send_times == h->send_times
			&& sk->last_recv_ack == h->ack )
		{
			sk->dup_ack++;
			if(sk->dup_ack >= 3) //receive 3 duplicate ACK, enter fast recovery
				return PJ_TRUE;
		}
		else
			sk->dup_ack = 0;
	}
	return PJ_FALSE;
}

static void p2p_tcp_cwnd_on_acked(p2p_tcp_sock* sk, p2p_tcp_header* h, pj_time_val now)
{
	pj_uint32_t ack = h->ack;

	if(ack < sk->last_recv_ack)
		return;

#ifdef P2P_TCP_DEBUG_LOG	
	PJ_LOG(4,("p2p_tcp", 
			  "send_state %d, cwnd %d,send_data_count %d,ack %d,last_recv_ack %d,sack_len %d,seq %d, send_times %d",
			sk->send_state, sk->cwnd, sk->send_data_count,	ack, sk->last_recv_ack, 
			sk->sack_len, h->seq, h->send_times));
#endif

	switch(sk->send_state)
	{
	case SEND_STATE_SLOW_START:
		if(sk->last_recv_ack < ack) //every ack, cwnd+1
		{
			sk->dup_ack = 0;
			sk->cwnd += (pj_uint16_t)(ack-sk->last_recv_ack);
			if(sk->cwnd >= sk->ssthresh)
			{
				sk->send_state = SEND_STATE_AVOID_CONGESTION;
				sk->avoid_congestion_ack = 0;
				if(sk->cwnd >= P2P_TCP_MAX_CWND)
					sk->cwnd = P2P_TCP_MAX_CWND;
			}
		}
		else if(p2p_tcp_is_dup_ack(sk, h))
		{
			p2p_tcp_snd_data* old_cur;
			pj_uint16_t old_cwnd ;

			PJ_LOG(4,("p2p_tcp", "dup_ack2 %d %d %p %d %p", 
				sk->dup_ack, sk->cwnd, sk->send_data_cur, sk->send_data_count, sk));

			//resend sk->send_data_begin + P2P_TCP_WND_MULTIPLE
			old_cwnd = sk->cwnd;
			sk->cwnd = (pj_uint16_t)(h->seq-sk->send_data_begin->header.seq)/P2P_TCP_WND_MULTIPLE+1;
			old_cur = sk->send_data_cur;
			sk->send_data_cur = sk->send_data_begin;
			p2p_tcp_send_data(sk, now);
			sk->send_data_cur = old_cur;
			sk->cwnd = old_cwnd;
		}
		break;
	case SEND_STATE_AVOID_CONGESTION:
		{
			p2p_tcp_avoid_congestion_ack(sk, ack);
			
			if(p2p_tcp_is_dup_ack(sk, h))
			{
				p2p_tcp_snd_data* old_cur;
				PJ_LOG(4,("p2p_tcp", "dup_ack1 %d %d %p %d %p", 
					sk->dup_ack, sk->cwnd, sk->send_data_cur, sk->send_data_count, sk));

				sk->send_state = SEND_STATE_FAST_RECOVERY;
				
				p2p_tcp_dup_ack_ssthresh(sk);

				sk->dup_ack_wnd = sk->cwnd;

				//resend send_data_begin and others, sk->cwnd*P2P_TCP_WND_MULTIPLE		
				sk->cwnd = (pj_uint16_t)(h->seq-sk->send_data_begin->header.seq)/P2P_TCP_WND_MULTIPLE+1;					
				if(sk->cwnd < 3)
					sk->cwnd = 3;
				
				old_cur = sk->send_data_cur;
				sk->send_data_cur = sk->send_data_begin;
				p2p_tcp_send_data(sk, now);
				sk->send_data_cur = old_cur;
			}
		}
		break;
	case SEND_STATE_FAST_RECOVERY:
		{
			if(sk->last_recv_ack == ack)
			{
				sk->dup_ack++;
				if(sk->send_data_count && sk->send_data_cur)
				{
					//fly count + 1
					sk->cwnd = (pj_uint16_t)p2p_tcp_get_interval(sk, sk->send_data_begin, sk->send_data_cur)+1;
					//PJ_LOG(4,("p2p_tcp", "SEND_STATE_FAST_RECOVERY p2p_tcp_send_data"));
					p2p_tcp_send_data(sk, now);
				}
			}
			else //receive new ack, enter avoid congestion
			{
				sk->cwnd = sk->ssthresh + sk->dup_ack;
				if(sk->cwnd > sk->dup_ack_wnd)
					sk->cwnd = sk->dup_ack_wnd;
				sk->dup_ack = 0;
				sk->avoid_congestion_ack = 0;
				if(sk->cwnd <= INIT_SSTHRESH) //cwnd too small
				{
					sk->send_state = SEND_STATE_SLOW_START;
					sk->ssthresh  = INIT_SSTHRESH;
				}
				else
				{
					sk->send_state = SEND_STATE_AVOID_CONGESTION;
				}
			}
		}
		break;
	}
	sk->last_recv_ack = ack;
}

static void p2p_tcp_free_sack(p2p_tcp_sock* sk)
{
	p2p_tcp_snd_data* prev = NULL, *cur = NULL;
	if(sk->send_data_count == 0 || sk->sack_len == 0)
		return;
	
	prev = sk->send_data_begin;
	cur = prev->next;

	while(cur)
	{
		if(cur->header.seq > sk->sack[sk->sack_len-1])
			break;
		if(cur->header.seq < sk->sack[0])
		{
			prev = cur;
			cur = cur->next;
			continue;
		}
		if(seq_in_sack(sk, cur->header.seq))
		{
			prev->next = cur->next;
			//PJ_LOG(4,("p2p_tcp", "p2p_tcp_free_sack free send_data_count %d, seq %d", sk->send_data_count, cur->header.seq));
			
			if(cur == sk->send_data_end)
				sk->send_data_end = prev;
			if(cur == sk->send_data_cur)
				sk->send_data_cur = cur->next;

			p2p_free(cur);

			cur = prev->next;
			sk->send_data_count--;
		}
		else
		{
			prev = cur;
			cur = cur->next;
		}
	}
}

static void p2p_tcp_acked(p2p_tcp_sock* sk, p2p_tcp_header* h, pj_time_val now, pj_uint8_t* call_on_sended)
{
	pj_uint32_t begin_seq, count=0;
	p2p_tcp_snd_data* begin, *acked = NULL;

	//delayed repeat ack
	if(sk->send_data_count == 0)	
		return;

	begin = sk->send_data_begin;
	begin_seq = begin->header.seq;

	if(h->ack < begin_seq)//repeat ack
		return;
	
	//adjust cwnd and send status
	p2p_tcp_cwnd_on_acked(sk, h, now);

	while(sk->send_data_begin)
	{
		if(sk->send_data_begin->header.seq >= h->ack)
		{
			acked = sk->send_data_begin;
			break;
		}
		count++;
		sk->send_data_begin = sk->send_data_begin->next;
	}	
	sk->send_data_count -= (pj_uint16_t)count;

	//update rtt timeout to last ack+rto
	if(sk->send_data_count > 0)
	{
		if(sk->send_data_cur == NULL 
			|| h->ack < sk->send_data_cur->header.seq)
		{
			sk->rto_time = sk->send_data_begin->last_send_time;
			PJ_TIME_VAL_ADD_MS(sk->rto_time, sk->rto); 
		}
	}
	else
	{
		sk->rto_time.sec = sk->rto_time.msec = 0;
		pj_timer_heap_cancel_if_active(get_p2p_global()->timer_heap, &sk->resend_timer, 0);
	}

	//when resend data, maybe send_data_cur < send_data_begin
	if(sk->send_data_cur 
		&& sk->send_data_cur->header.seq < h->ack)
		sk->send_data_cur = sk->send_data_begin;
	
	//if resend time is 1, get new rtt 
	if(count > 0 && acked && acked->send_times == 1)
	{
		pj_time_val now1 = now;
		PJ_TIME_VAL_SUB(now1, acked->last_send_time);
		tcp_rtt_estimator(sk, PJ_TIME_VAL_MSEC(now1));
	}

	if(count > 0 && sk->send_data_count && sk->send_data_cur)
	{
		//PJ_LOG(4,("p2p_tcp", "p2p_tcp_acked p2p_tcp_send_data"));
		p2p_tcp_send_data(sk, now);
	}
	//peer received, free send data
	while(count)
	{
		p2p_tcp_snd_data* next = begin->next;
		//PJ_LOG(4,("p2p_tcp", "p2p_tcp_acked free seq %d", begin->header.seq));
		p2p_free(begin);
		begin = next;
		count--;
	}

	p2p_tcp_free_sack(sk);

	//notify user continue send
	if(sk->send_data_count < P2P_TCP_SEND_BUFFER_COUNT*7/8)
		*call_on_sended = 1;
}

static void p2p_tcp_recv_ack(p2p_tcp_sock* sk, p2p_tcp_header* h, pj_time_val now, pj_uint8_t* call_on_sended)
{
	sk->sack_len = (h->len >> 2); //">>2" is sizeof(pj_uint32_t)
	if(h->len != 0) //sack
	{
		int* recv_sack = (int*)(h+1);
		int i;
		for(i=0; i<sk->sack_len; i++)
		{
			sk->sack[i] = pj_ntohl(*recv_sack);
			recv_sack++;
		}
	}

	p2p_tcp_acked(sk, h, now, call_on_sended);
}

//check disorder list,add disorder to order
static void p2p_tcp_disorder_to_order(p2p_tcp_sock* sk)
{
	while(sk->recved_disorder_count)
	{
		p2p_tcp_header* h = (p2p_tcp_header*)(sk->recved_disorder_begin+1);
		if(sk->recved_last_order_seq+1 == h->seq)//disorder to order
		{
			//recved_user_data_count be sure greater than zero
			sk->recved_user_data_end->next = sk->recved_disorder_begin;
			sk->recved_user_data_end = sk->recved_disorder_begin;
			sk->recved_user_data_count++;

			sk->recved_disorder_begin = sk->recved_disorder_begin->next;
			sk->recved_disorder_count--;

			sk->recved_last_order_seq++;
		}
		else if(sk->recved_last_order_seq+1 > h->seq) //repeat receive data ?? never go to here?
		{
			p2p_tcp_recved_data* data = sk->recved_disorder_begin;
			sk->recved_disorder_begin = sk->recved_disorder_begin->next;
			sk->recved_disorder_count--;

			p2p_free(data);
		}
		else
			break;
	}
}

PJ_INLINE(p2p_tcp_recved_data*) p2p_tcp_malloc_recved_data(p2p_tcp_header* header, pj_time_val now)
{
	p2p_tcp_recved_data* data = p2p_malloc(P2P_TCP_MSS + sizeof(p2p_tcp_recved_data));
	data->next = NULL;
	data->data_offset = 0;
	data->t = now;
	pj_memcpy(data+1, header, header->len+sizeof(p2p_tcp_header));
	return data;
}

static void p2p_tcp_recv_ordered_data(p2p_tcp_sock* sk, p2p_tcp_header* header, pj_time_val now)
{
	p2p_tcp_recved_data* data = p2p_tcp_malloc_recved_data(header, now);
	
	if(sk->recved_user_data_count == 0)
	{
		sk->recved_user_data_begin = sk->recved_user_data_end = data;
	}
	else
	{
		sk->recved_user_data_end->next = data;
		sk->recved_user_data_end = data;
	}
	sk->recved_user_data_count++;
	sk->recved_last_order_seq++;

	p2p_tcp_disorder_to_order(sk);
}

static void p2p_tcp_recv_disorder_data(p2p_tcp_sock* sk, p2p_tcp_header* header, pj_time_val now, pj_uint8_t* repeat_recv)
{
	if(sk->recved_disorder_count == 0)
	{
		sk->recved_disorder_begin = p2p_tcp_malloc_recved_data(header, now);
		sk->recved_disorder_count++;
	}
	else
	{
		p2p_tcp_recved_data* prev;
		p2p_tcp_recved_data* cur = sk->recved_disorder_begin;
		p2p_tcp_header* h = (p2p_tcp_header*)(cur+1);
		if(h->seq == header->seq)//repeat receive data
		{
			*repeat_recv = 1;
			return;
		}
		//order insert to disorder list begin
		if(header->seq < h->seq)
		{
			p2p_tcp_recved_data* recved_data = p2p_tcp_malloc_recved_data(header, now);
			recved_data->next = cur;
			sk->recved_disorder_begin = recved_data;
			sk->recved_disorder_count++;
			return;
		}
		prev = cur;
		cur = cur->next;
		while(cur)
		{	
			h = (p2p_tcp_header*)(cur+1);
			if(h->seq == header->seq)//repeat receive data
			{
				*repeat_recv = 1;
				return;
			}
			if(header->seq < h->seq)
			{
				p2p_tcp_recved_data* recved_data = p2p_tcp_malloc_recved_data(header, now);
				prev->next = recved_data;
				recved_data->next = cur;
				break;
			}
			prev = cur;
			cur = cur->next;
		}
		if(cur == NULL)//insert to end
			prev->next = p2p_tcp_malloc_recved_data(header, now);
		sk->recved_disorder_count++;
	}
}

PJ_INLINE(void) p2p_tcp_fast_ack_decrease(p2p_tcp_sock* sk)
{
	pj_uint32_t dec = sk->recved_last_order_seq - sk->last_send_ack;
	if(dec > sk->fask_ack_count)
	{
		sk->fask_ack_count = 0;
		p2p_tcp_enter_delay_ack(sk);
	}
	else
	{
		sk->fask_ack_count -= dec;
	}
}

static void p2p_tcp_calculate_ato(p2p_tcp_sock* sk, pj_time_val recved_time)
{
	pj_uint32_t delta;
	//first receive data
	if(sk->last_recv_data_time.sec == 0 &&
		sk->last_recv_data_time.msec == 0)
		return;
	PJ_TIME_VAL_SUB(recved_time, sk->last_recv_data_time);
	delta = PJ_TIME_VAL_MSEC(recved_time);

	/*to see linux source tcp_event_data_recv  */  
    if (delta <= DELAY_ACK_MIN / 2) 
	{
		sk->ato = (sk->ato >> 1) + DELAY_ACK_MIN / 2;  
	} 
	else if (delta < sk->ato)
	{  
		sk->ato = (sk->ato >> 1) + delta;  
		if (sk->ato > sk->rto)  
			sk->ato = sk->rto;
	} 
	else if (delta > sk->ato)
	{
		p2p_tcp_enter_fast_ack(sk);
	}  
}

static void p2p_tcp_recv_user_data(p2p_tcp_sock* sk, 
								   p2p_tcp_header* header,
								   pj_uint8_t* repeat_recv,
								   pj_time_val now,
								   pj_uint8_t* call_on_sended)
{
#ifdef P2P_TCP_DEBUG_LOG
	//PJ_LOG(4,("p2p_tcp", "p2p_tcp_recv_user_data %p seq %d len %d delay_ack %d fask_ack_count %d", sk, header->seq, header->len, sk->delay_ack, sk->fask_ack_count));
#endif

	p2p_tcp_acked(sk, header, now, call_on_sended);
	
	p2p_tcp_calculate_ato(sk, now);
	
	sk->last_recv_data_time = now;
	if(header->seq <= sk->recved_last_order_seq) //repeat receive data
	{
		*repeat_recv = 1;
		return;
	}

	if(header->seq-sk->recved_last_order_seq == 1)//ordered data, put to user data list
	{
		p2p_tcp_recv_ordered_data(sk, header, now);
	}
	else//disorder data
	{
		p2p_tcp_recv_disorder_data(sk, header, now, repeat_recv);
	}
}

static void p2p_tcp_send_ack(p2p_tcp_sock* sk, char* ack_buffer, pj_uint32_t seq, pj_uint8_t send_times)
{
	pj_uint16_t send_ack_len;
	sk->last_send_ack = sk->recved_last_order_seq;
	if(sk->recved_disorder_count == 0) //send pure ack
	{
		p2p_tcp_header* h = (p2p_tcp_header*)ack_buffer;
		h->ack = sk->last_send_ack+1;
		h->type = P2P_TCP_TYPE_ACK;
		h->seq = seq;
		h->len = 0;
		h->send_times = send_times;
		h->wnd_size = P2P_TCP_RECV_BUFFER_COUNT>sk->recved_user_data_count? P2P_TCP_RECV_BUFFER_COUNT - sk->recved_user_data_count : 0;
#ifdef P2P_TCP_DEBUG_LOG
		PJ_LOG(4,("p2p_tcp", "p2p_tcp_send_ack pure %p %d", sk, h->ack));
#endif
		p2p_tcp_header_hton(h);
		send_ack_len = sizeof(p2p_tcp_header);
	}
	else//send sack
	{
		p2p_tcp_recved_data* cur = sk->recved_disorder_begin;
		p2p_tcp_header* h = (p2p_tcp_header*)(cur+1);
		pj_uint32_t cur_seq = h->seq;
		pj_uint32_t* begin_sack = (pj_uint32_t*)(ack_buffer+sizeof(p2p_tcp_header));
		pj_uint32_t* cur_sack = begin_sack;
	
		*cur_sack = pj_htonl(cur_seq);
		cur_sack++;
		cur = cur->next;
		while(cur)
		{
			h = (p2p_tcp_header*)(cur+1);
			if(h->seq != cur_seq+1 ) //disorder
			{	
				*cur_sack = pj_htonl(cur_seq);
				cur_sack++;

				*cur_sack = pj_htonl(h->seq);
				cur_sack++;
			}
			cur_seq = h->seq;
			cur = cur->next;
		}
		*cur_sack = pj_htonl(cur_seq);//last
		cur_sack++;
	
		h = (p2p_tcp_header*)ack_buffer;
		h->ack = sk->last_send_ack+1;
		h->type = P2P_TCP_TYPE_ACK;
		h->seq = seq;
		h->len = (pj_uint16_t)((cur_sack-begin_sack) << 2); // "<<2" is sizeof(pj_uint32_t)
		h->wnd_size = P2P_TCP_RECV_BUFFER_COUNT>sk->recved_user_data_count? P2P_TCP_RECV_BUFFER_COUNT - sk->recved_user_data_count : 0;
		h->send_times = send_times;

		send_ack_len = sizeof(p2p_tcp_header)+h->len;
#ifdef P2P_TCP_DEBUG_LOG
		PJ_LOG(4,("p2p_tcp", "p2p_tcp_send_ack sack %p %d", sk, h->ack));
#endif

		p2p_tcp_header_hton(h);		
	}
	p2p_tcp_sendto_peer(sk, (const char*)sk->ack_buffer, send_ack_len);
}

static void p2p_tcp_try_ack_on_recv(p2p_tcp_sock* sk, pj_uint8_t repeat_recv, pj_uint32_t seq, pj_uint8_t send_times)
{
	if(repeat_recv)
	{
		if(sk->delay_ack)//repeat receive data, change to fast ack
			p2p_tcp_enter_fast_ack(sk);
		p2p_tcp_send_ack(sk, (char*)sk->ack_buffer, seq, send_times);

	}
	else
	{
		//fast ack state,immediately send ack and decrease fast ack count
		if(sk->delay_ack == 0)
			p2p_tcp_fast_ack_decrease(sk);

		//disorder is not empty or receive more than 2 user data, immediately send ack
		if(sk->delay_ack == 0 
			|| sk->recved_disorder_count 
			|| sk->recved_user_data_count >= 2)
			p2p_tcp_send_ack(sk, (char*)sk->ack_buffer, seq, send_times);
	}
}

static void p2p_tcp_delay_ack(pj_timer_heap_t *th, pj_timer_entry *e)
{
	p2p_tcp_sock* sk = (p2p_tcp_sock*) e->user_data;
	PJ_UNUSED_ARG(th);
	tcp_grp_lock_acquire(sk->grp_lock);
	if(sk->delay_ack == 0){
		tcp_grp_lock_release(sk->grp_lock);
		return;
	}

	if(sk->recved_last_order_seq == sk->last_send_ack
		&& sk->recved_disorder_count == 0)
	{
		pj_time_val t;
		t.sec = sk->ato / 1000;
		t.msec = sk->ato % 1000;
		pj_timer_heap_schedule_w_grp_lock(get_p2p_global()->timer_heap, 
			&sk->delay_ack_timer,
			&t, 
			0, 
			NULL);
		tcp_grp_lock_release(sk->grp_lock);
		return;
	}

	p2p_tcp_send_ack(sk, (char*)sk->ack_buffer, sk->recved_last_order_seq, 1);
	p2p_tcp_enter_fast_ack(sk);

	tcp_grp_lock_release(sk->grp_lock);
}

static void p2p_tcp_zero_wnd_probe(pj_timer_heap_t *th, pj_timer_entry *e)
{
	p2p_tcp_sock* sk = (p2p_tcp_sock*) e->user_data;
	p2p_tcp_header h;
	pj_time_val t;
		
	PJ_UNUSED_ARG(th);
	tcp_grp_lock_acquire(sk->grp_lock);
	if(sk->is_zero_wnd_probe == 0)
	{
		tcp_grp_lock_release(sk->grp_lock);
		return ;
	}

	h.type = P2P_TCP_TYPE_ZERO_WND;
	h.seq = 0;
	h.ack = sk->last_send_ack+1;
	h.len = 0;
	h.wnd_size = P2P_TCP_RECV_BUFFER_COUNT>sk->recved_user_data_count? P2P_TCP_RECV_BUFFER_COUNT - sk->recved_user_data_count : 0;
	p2p_tcp_header_hton(&h);
	p2p_tcp_sendto_peer(sk, (const char*)&h, sizeof(h));

	t.sec = sk->rto / 1000;
	t.msec = sk->rto % 1000;
	pj_timer_heap_schedule_w_grp_lock(get_p2p_global()->timer_heap, 
		&sk->zero_wnd_probe_timer,
		&t, 
		0, 
		NULL);
	tcp_grp_lock_release(sk->grp_lock);
}

static void p2p_tcp_send_heart(p2p_tcp_sock* sk)
{
	p2p_tcp_header h;
	h.type = P2P_TCP_TYPE_HEART;
	h.seq = 0;
	h.ack = sk->last_send_ack+1;
	h.len = 0;
	h.wnd_size = P2P_TCP_RECV_BUFFER_COUNT>sk->recved_user_data_count? P2P_TCP_RECV_BUFFER_COUNT - sk->recved_user_data_count : 0;
	p2p_tcp_header_hton(&h);	
	p2p_tcp_sendto_peer(sk, (const char*)&h, sizeof(p2p_tcp_header));

	//PJ_LOG(4,("p2p_tcp", "p2p_tcp_check_heart send P2P_TCP_TYPE_HEART"));
}

static void p2p_tcp_check_heart(pj_timer_heap_t *th, pj_timer_entry *e)
{
	pj_time_val now;
	p2p_tcp_sock* sk = (p2p_tcp_sock*) e->user_data;
	pj_time_val t;

	PJ_UNUSED_ARG(th);

	pj_gettickcount(&now);
	tcp_grp_lock_acquire(sk->grp_lock);
	//check close timeout
	if(now.sec - sk->last_recv_time.sec > P2P_TCP_CLOSE_TIMEOUT)
	{
		sk->cb.on_close(sk->cb.user_data);
		tcp_grp_lock_release(sk->grp_lock);
		return;
	}

	if(now.sec - sk->last_send_time.sec >= P2P_TCP_HEART_TIMEOUT)
	{
		p2p_tcp_send_heart(sk);
	}

	t.sec = P2P_TCP_HEART_TIMEOUT;
	t.msec = 0;
	pj_timer_heap_schedule_w_grp_lock(get_p2p_global()->timer_heap, 
		&sk->heart_timer,
		&t, 
		0, 
		NULL);
	tcp_grp_lock_release(sk->grp_lock);
}

static void p2p_tcp_set_sock_buf(p2p_tcp_sock* sk)
{
#ifdef WIN32
	int buf_size = sizeof(int);
#else
	socklen_t buf_size = sizeof(int);
#endif
	int send_buf=0;
	int recv_buf=0;
	const int buflen = P2P_TCP_MSS*P2P_TCP_SEND_BUFFER_COUNT;

	getsockopt(sk->sock, SOL_SOCKET, SO_RCVBUF, (char*)&recv_buf, &buf_size);
	if(recv_buf<buflen)
		setsockopt(sk->sock, SOL_SOCKET, SO_RCVBUF, (char*)&buflen, buf_size);

	getsockopt(sk->sock, SOL_SOCKET, SO_SNDBUF, (char*)&send_buf, &buf_size);
	if(send_buf<buflen)
		setsockopt(sk->sock, SOL_SOCKET, SO_SNDBUF, (char*)&buflen, buf_size);

	PJ_LOG(4,("p2p_tcp", "p2p_tcp_create socket buffer %d %d", recv_buf, send_buf));
}

p2p_tcp_sock* p2p_tcp_create(p2p_tcp_cb* cb, pj_sock_t sock, pj_sockaddr* remote_addr,pj_grp_lock_t  *grp_lock)
{
	pj_pool_t *pool;
	p2p_tcp_sock* sk;
	pj_time_val t;

	pool = pj_pool_create(&get_p2p_global()->caching_pool.factory, 
		"p2p_tcp%p", 
		4096,
		256, 
		NULL);

	sk = PJ_POOL_ZALLOC_T(pool, p2p_tcp_sock);
	pj_bzero(sk, sizeof(p2p_tcp_sock));
	
	sk->pool = pool;
	pj_memcpy(&sk->cb, cb, sizeof(p2p_tcp_cb));

	sk->sock = sock;
	pj_sockaddr_cp(&sk->peer_addr, remote_addr);

	sk->send_state = SEND_STATE_SLOW_START;
	sk->srtt = 0;
	sk->dup_ack_wnd = sk->cwnd = CWND_INIT_VAL;
	sk->ssthresh = INIT_SSTHRESH;
	sk->send_seq = 1;
	sk->peer_wnd_size = P2P_TCP_RECV_BUFFER_COUNT;
	sk->rto = MIN_RTO*2;
	sk->ato = DELAY_ACK_MIN;
	sk->grp_lock = grp_lock;

	pj_timer_entry_init(&sk->delay_ack_timer, 0, sk, &p2p_tcp_delay_ack);
	pj_timer_entry_init(&sk->zero_wnd_probe_timer, 0, sk, &p2p_tcp_zero_wnd_probe);
	pj_timer_entry_init(&sk->heart_timer, 0, sk, &p2p_tcp_check_heart);
	pj_timer_entry_init(&sk->resend_timer, 0, sk, &p2p_tcp_rto_resend);

	pj_gettickcount(&sk->last_send_time);
	sk->last_recv_time = sk->last_send_time;
	p2p_tcp_enter_fast_ack(sk);

	p2p_tcp_send_heart(sk);
	t.sec = P2P_TCP_HEART_TIMEOUT;
	t.msec = 0;
	pj_timer_heap_schedule_w_grp_lock(get_p2p_global()->timer_heap, 
		&sk->heart_timer,
		&t, 
		0, 
		NULL);
	if(sock != 0)
	{
		p2p_tcp_set_sock_buf(sk);
	}
	PJ_LOG(4,("p2p_tcp", "p2p_tcp_create %p %d", sk, sock));

	return sk;
}

PJ_INLINE(void) free_tcp_recved_data(p2p_tcp_recved_data* data, pj_uint16_t count)
{
	int i;
	for(i=0; i<count; i++)
	{
		p2p_tcp_recved_data* next = data->next;
		p2p_free(data);
		data = next;
	}
}

PJ_INLINE(void) free_tcp_send_data(p2p_tcp_snd_data* data, pj_uint16_t count)
{
	int i;
	for(i=0; i<count; i++)
	{
		p2p_tcp_snd_data* next = data->next;
		p2p_free(data);
		data = next;
	}
}

void p2p_tcp_destory(p2p_tcp_sock* sock)
{
	PJ_LOG(4,("p2p_tcp", "p2p_tcp_destory %p begin", sock));

	if(sock == NULL)
		return;

	pj_timer_heap_cancel_if_active(get_p2p_global()->timer_heap, &sock->heart_timer, 0);
	pj_timer_heap_cancel_if_active(get_p2p_global()->timer_heap, &sock->delay_ack_timer, 0);
	pj_timer_heap_cancel_if_active(get_p2p_global()->timer_heap, &sock->zero_wnd_probe_timer, 0);
	pj_timer_heap_cancel_if_active(get_p2p_global()->timer_heap, &sock->resend_timer, 0);

	free_tcp_send_data(sock->send_data_begin, sock->send_data_count);
	free_tcp_recved_data(sock->recved_user_data_begin, sock->recved_user_data_count);
	free_tcp_recved_data(sock->recved_disorder_begin, sock->recved_disorder_count);

	delay_destroy_pool(sock->pool);
	PJ_LOG(4,("p2p_tcp", "p2p_tcp_destory %p end", sock));
}

int p2p_tcp_get_send_remain(p2p_tcp_sock* sock)
{
	int remain = 0;
	if(sock == NULL)
		return 0;

	tcp_grp_lock_acquire(sock->grp_lock);

	remain = (P2P_TCP_SEND_BUFFER_COUNT-sock->send_data_count)*P2P_TCP_MAX_DATA_LEN;

	tcp_grp_lock_release(sock->grp_lock);

	return remain;
}

static void on_io_thead_tcp_send(void* data)
{
	p2p_tcp_sock* sock = data;
	pj_time_val now;

	pj_gettickcount(&now);
	//PJ_LOG(4,("p2p_tcp", "on_io_thead_tcp_send p2p_tcp_send_data"));

	if(sock->send_state == SEND_STATE_FAST_RECOVERY)
	{
		//fly count + BAD2_ONCE_SEND_COUNT
		sock->cwnd = (pj_uint16_t)p2p_tcp_get_interval(sock, sock->send_data_begin, sock->send_data_cur)+BAD2_ONCE_SEND_COUNT;
	}
	p2p_tcp_send_data(sock, now);

	pj_grp_lock_dec_ref(sock->grp_lock); //decrease reference, add in p2p_tcp_send
}

PJ_INLINE(void) p2p_tcp_schedule_send(p2p_tcp_sock* sock)
{
	//send data list too long, do not send
	if(sock->send_data_count > BAD_SEND_COUNT)
	{
		pj_time_val now;
		pj_time_val last_send_time = sock->last_send_time;

		pj_gettickcount(&now);
		PJ_TIME_VAL_ADD_MS(last_send_time, BAD_MIN_SEND_SPAN);

		if(PJ_TIME_VAL_GTE(last_send_time, now))
			return;
	}

	if(sock->sock)
	{
		pj_time_val now;
		pj_gettickcount(&now);
		//PJ_LOG(4,("p2p_tcp", "p2p_tcp_schedule_send p2p_tcp_send_data"));

		if(sock->send_state == SEND_STATE_FAST_RECOVERY)
		{
			//fly count + BAD2_ONCE_SEND_COUNT
			sock->cwnd = (pj_uint16_t)p2p_tcp_get_interval(sock, sock->send_data_begin, sock->send_data_cur)+BAD2_ONCE_SEND_COUNT;
		}
		p2p_tcp_send_data(sock, now);
	}
	else //prevent deadlock, call p2p_tcp_send_data in io thread
	{
		p2p_socket_pair_item item;
		item.cb = on_io_thead_tcp_send;
		item.data = sock;
		pj_grp_lock_add_ref(sock->grp_lock); //add reference, release in on_io_thead_tcp_send
		schedule_socket_pair(get_p2p_global()->sock_pair, &item);
	}
}

int p2p_tcp_send(p2p_tcp_sock* sock, const void* buffer, int buffer_len)
{
	int cached = 0;
	pj_uint16_t pkg_index = 0;
	if(sock == NULL)
		return 0;

	//PJ_LOG(4,("p2p_tcp", "p2p_tcp_send %p %d", sock, buffer_len));

	tcp_grp_lock_acquire(sock->grp_lock);
	//cache data to send buffer
	while(sock->send_data_count < P2P_TCP_SEND_BUFFER_COUNT 
		&& buffer_len > cached)
	{
		p2p_tcp_snd_data* snd_data;
		int len = buffer_len-cached;
		if(len > P2P_TCP_MAX_DATA_LEN)
			len = P2P_TCP_MAX_DATA_LEN;

		snd_data = p2p_malloc(P2P_TCP_MAX_DATA_LEN+sizeof(p2p_tcp_snd_data));

		snd_data->last_send_time.sec = snd_data->last_send_time.msec = 0;
		snd_data->send_times = 0;
		snd_data->next = NULL;
		snd_data->header.type = P2P_TCP_TYPE_DATA;
		snd_data->header.len = (pj_uint16_t)len;
		snd_data->header.seq = sock->send_seq++;
		snd_data->pkg_index = pkg_index++;
		memset(snd_data->header.padding, 0, sizeof(snd_data->header.padding));
		pj_memcpy(snd_data+1, (char*)buffer+cached, len);//

		if(sock->send_data_count == 0)
		{
			sock->send_data_begin = sock->send_data_end = snd_data;
		}
		else
		{
			sock->send_data_end->next = snd_data;
			sock->send_data_end = snd_data;
		}

		if(sock->send_data_cur == NULL)
			sock->send_data_cur = snd_data;
		sock->send_data_count++;

		cached += len;
	}

	if(cached == 0)
	{
		tcp_grp_lock_release(sock->grp_lock);
		return -1;
	}
	else
	{
		p2p_tcp_schedule_send(sock);
		tcp_grp_lock_release(sock->grp_lock);
		return cached;
	}
}
static int p2p_tcp_recv_data(void* buffer, 
							 int buffer_len, 
							 p2p_tcp_recved_data** recved_data,
							 pj_uint16_t* recved_count)
{
	int recved_len = 0;
	p2p_tcp_recved_data* data;

	data = *recved_data;

	while(recved_len < buffer_len 
		&& *recved_count > 0)
	{
		p2p_tcp_header* header = (p2p_tcp_header*)(data+1);
		int copy_len = header->len - data->data_offset;
		int remain = buffer_len-recved_len;
		if(copy_len > remain)
		{
			pj_memcpy((char*)buffer+recved_len, (char*)(header+1)+data->data_offset, remain);
			recved_len += remain;
			data->data_offset += (pj_uint16_t)remain;
			break;
		}
		else
		{
			p2p_tcp_recved_data* free_data;

			pj_memcpy((char*)buffer+recved_len, (char*)(header+1)+data->data_offset, copy_len);
			recved_len += copy_len;
			free_data = data;
			data = data->next;
			(*recved_count)--;

			p2p_free(free_data);
		}
	}
	*recved_data = data;

	return recved_len;
}

int p2p_tcp_recv(p2p_tcp_sock* sock, void* buffer, int buffer_len)
{
	int old_recved_count;
	int recved_len = 0;

	if(sock == NULL || sock->recved_user_data_count == 0)
		return -1;

	//PJ_LOG(4,("p2p_tcp", "p2p_tcp_recv %p %d", sock, buffer_len));

	tcp_grp_lock_acquire(sock->grp_lock);
	old_recved_count = sock->recved_user_data_count;

	recved_len = p2p_tcp_recv_data(buffer, buffer_len, &sock->recved_user_data_begin, &sock->recved_user_data_count);
	
	//if zero window changed, send ack
	if(old_recved_count >= P2P_TCP_RECV_BUFFER_COUNT 
		&& sock->recved_user_data_count < P2P_TCP_RECV_BUFFER_COUNT)
	{
		p2p_tcp_send_ack(sock, (char*)sock->ack_buffer, sock->recved_last_order_seq, 1);
	}
	tcp_grp_lock_release(sock->grp_lock);
	return recved_len;
}

void p2p_tcp_data_recved(p2p_tcp_sock* sock, const void* buffer, int buffer_len)
{
	pj_uint8_t repeat_recv = 0;
	p2p_tcp_header* header = (p2p_tcp_header*)(buffer);
	pj_time_val now;
	pj_uint8_t call_on_sended = 0;

	if(sock == NULL || buffer_len <= 0)
		return ;

	//PJ_LOG(4,("p2p_tcp", "p2p_tcp_data_recved %p %d", sock, buffer_len));

	p2p_tcp_header_ntoh(header);

	pj_gettickcount(&now);

	tcp_grp_lock_acquire(sock->grp_lock);

	sock->peer_wnd_size = header->wnd_size;
	sock->last_recv_time = now;	

	zero_wnd_probe_on_recved(sock, &now);

	switch(header->type)
	{
	case P2P_TCP_TYPE_ACK:
		p2p_tcp_recv_ack(sock, header, now, &call_on_sended);
		break;

	case P2P_TCP_TYPE_DATA:
		{
			p2p_tcp_recv_user_data(sock, header, &repeat_recv, now, &call_on_sended);

			//if receive user data, try to send ack
			p2p_tcp_try_ack_on_recv(sock, repeat_recv, header->seq, header->send_times);

			//notify user to receive data
			if(sock->recved_user_data_count>0)
				sock->cb.on_recved(sock->cb.user_data);
		}
		break;

	case P2P_TCP_TYPE_ZERO_WND:
		p2p_tcp_send_ack(sock, (char*)sock->ack_buffer, sock->recved_last_order_seq, 1);
		break;

	case P2P_TCP_TYPE_DATA_NORESEND:
		sock->cb.on_noresned_recved(sock->cb.user_data, (const char*)(header+1), header->len);
		break;

	case P2P_TCP_TYPE_HEART: //nothing to do
		break;
	}

	if(call_on_sended)
		sock->cb.on_send(sock->cb.user_data);

	tcp_grp_lock_release(sock->grp_lock);
}

static void on_io_thread_tcp_no_resend(void* data)
{
	p2p_tcp_snd_data* snd_data = data;
	p2p_tcp_sock* sock = (p2p_tcp_sock*)(snd_data->next);
	pj_uint16_t len = snd_data->header.len;

	p2p_tcp_header_hton(&snd_data->header);	
	p2p_tcp_sendto(sock, (const char*)&snd_data->header, len+sizeof(p2p_tcp_header));
	p2p_free(snd_data);

	pj_grp_lock_dec_ref(sock->grp_lock); //decrease reference, add in p2p_tcp_no_resend
}

//immediately sendto,no cache, no resend 
pj_status_t p2p_tcp_no_resend(p2p_tcp_sock* sock, const void* buffer, int buffer_len)
{
	p2p_tcp_snd_data* snd_data;

	if(sock == NULL)
		return PJ_EGONE;
	if(buffer_len > P2P_TCP_MAX_DATA_LEN)
		return PJ_ETOOBIG;

	snd_data = p2p_malloc(P2P_TCP_MAX_DATA_LEN+sizeof(p2p_tcp_snd_data));
	snd_data->last_send_time.sec = snd_data->last_send_time.msec = 0;
	snd_data->send_times = 0;

	snd_data->header.type = P2P_TCP_TYPE_DATA_NORESEND;
	snd_data->header.len = (pj_uint16_t)buffer_len;
	snd_data->header.seq = 0;
	snd_data->header.wnd_size = P2P_TCP_RECV_BUFFER_COUNT>sock->recved_user_data_count? P2P_TCP_RECV_BUFFER_COUNT - sock->recved_user_data_count : 0;
	snd_data->header.ack = sock->recved_last_order_seq+1;
	snd_data->header.send_times = 1;
	memset(snd_data->header.padding, 0, sizeof(snd_data->header.padding));

	pj_memcpy(snd_data+1, (char*)buffer, buffer_len);
	
	if(sock->sock)
	{
		snd_data->next = NULL;
		p2p_tcp_header_hton(&snd_data->header);	
		p2p_tcp_sendto(sock, (const char*)&snd_data->header, buffer_len+sizeof(p2p_tcp_header));
		p2p_free(snd_data);
	}
	else //prevent deadlock, call on_io_thead_tcp_no_resend in io thread
	{
		p2p_socket_pair_item item;
		item.cb = on_io_thread_tcp_no_resend;
		item.data = snd_data;
		snd_data->next = (struct p2p_tcp_snd_data*)sock;
		pj_grp_lock_add_ref(sock->grp_lock); //add reference, release in on_io_thead_tcp_no_resend
		schedule_socket_pair(get_p2p_global()->sock_pair, &item);
	}

	return PJ_SUCCESS;
}

//port guess success,change udp socket handle and source address
void p2p_tcp_sock_guess_port(p2p_tcp_sock* sock,
							 pj_sock_t handle,
							 const pj_sockaddr_t *src_addr,
							 unsigned src_addr_len)
{
	if(sock == NULL )
		return ;

	PJ_UNUSED_ARG(src_addr_len);

	tcp_grp_lock_acquire(sock->grp_lock);
	
	sock->sock = handle;
	p2p_tcp_set_sock_buf(sock);
	pj_sockaddr_cp(&sock->peer_addr, src_addr);

	tcp_grp_lock_release(sock->grp_lock);
}

//clear all send buffer
void p2p_tcp_clear_send_buf(p2p_tcp_sock* sock)
{
	p2p_tcp_snd_data* cur = NULL, *prev = NULL, *next = NULL;
	pj_uint16_t i;
	pj_uint16_t send_data_count;

	if(sock == NULL || sock->send_data_count == 0)
		return ;

	cur = sock->send_data_begin;
	send_data_count = sock->send_data_count;

	tcp_grp_lock_acquire(sock->grp_lock);

	//find send times is zero(never no send to peer) and first package index
	for(i=0; i<send_data_count; i++)
	{
		if(cur->send_times == 0 && cur->pkg_index == 0)
			break;
		prev = cur;
		cur = cur->next;
	}

	//reset send sequence number and send_data_count
	if(i < send_data_count)
	{
		sock->send_seq = cur->header.seq;
		sock->send_data_count = i;

		//reset send_data_begin,send_data_end, send_data_cur
		if(sock->send_data_count == 0)
			sock->send_data_begin = sock->send_data_end = sock->send_data_cur = NULL;
		else
		{
			sock->send_data_end = prev;
			sock->send_data_end->next = NULL;
			if(sock->send_data_cur 
				&& sock->send_data_cur->header.seq > sock->send_data_end->header.seq)
				sock->send_data_cur = NULL;
		}
	}

	//free all send zero times data
	for(; i<send_data_count; i++)
	{
		next = cur->next;
		p2p_free(cur);
		cur = next;
	}

	tcp_grp_lock_release(sock->grp_lock);
}
#endif
