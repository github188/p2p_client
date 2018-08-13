#ifndef __FLOW_STAT_H__
#define __FLOW_STAT_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

#define FLOW_STAT_PREV_COUNT (10)

typedef struct flow_stat
{
	unsigned long long prev_val[FLOW_STAT_PREV_COUNT];
	unsigned long long agv_val;
	unsigned long long cur_val;
}flow_stat;

static inline void flow_stat_init(struct flow_stat* fs)
{
	memset(fs, 0, sizeof(struct flow_stat));
}

static inline void flow_stat_calc(struct flow_stat* fs)
{
	unsigned long long total = 0;
	int i;
	for(i=0; i<FLOW_STAT_PREV_COUNT-1; i++)
	{
		total += fs->prev_val[i];
		fs->prev_val[i] = fs->prev_val[i+1];
	}

	total += fs->prev_val[FLOW_STAT_PREV_COUNT-1];
	fs->agv_val = total/FLOW_STAT_PREV_COUNT;

	fs->prev_val[FLOW_STAT_PREV_COUNT-1] = fs->cur_val;
	fs->cur_val = 0;
}

static inline void flow_stat_add(struct flow_stat* fs, unsigned int value)
{
	fs->cur_val += value;
}

static inline unsigned long long flow_stat_get(struct flow_stat* fs)
{
	return fs->agv_val;
}

#define tcp_client_stat_send(client,buf,buf_len) do { \
	flow_stat_add(&g_send_flow_stat, buf_len); \
	tcp_client_send(client, buf, buf_len); \
} while(0) ;

#ifdef __cplusplus
}
#endif

#endif