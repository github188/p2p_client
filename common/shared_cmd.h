#ifndef __SHARED_CMD__
#define __SHARED_CMD__

#ifdef __cplusplus
extern "C"
{
#endif

#include "queue.h"
#include <stdlib.h>
#include <memory.h>
#include "gss_mem.h"
#include "gss_protocol.h"
#include <pthread.h>
#ifdef WIN32
	#include <winsock2.h>
#else

#ifdef USE_ATOMIC_T
	#include <alsa/iatomic.h> 
#endif

#endif

typedef struct shared_cmd
{
	//a full command
	char* cmd;

	//command length
	unsigned int cmd_len;
	
	// the reference count
#ifdef _WIN32
	long ref;
#else

#ifdef USE_ATOMIC_T
	atomic_t ref;
#else
	int ref;
#endif

#endif
	//time stamp
	unsigned int time_stamp;
}shared_cmd;

typedef struct shared_cmd_tailq
{
	TAILQ_ENTRY(shared_cmd_tailq) tailq;
	shared_cmd* sc;
}shared_cmd_tailq;

TAILQ_HEAD(shared_cmd_tailq_list, shared_cmd_tailq);

void shared_cmd_startup();
void shared_cmd_clean();

shared_cmd* malloc_shared_cmd(const char* buf, unsigned int len);

void shared_cmd_add_ref(shared_cmd* sc);

void shared_cmd_release(shared_cmd* sc);

shared_cmd_tailq* malloc_shared_cmd_tailq(shared_cmd* sc);

void free_shared_cmd_tailq(shared_cmd_tailq* sc_tailq);

#ifdef __cplusplus
}; //end of extern "C" {
#endif

#endif
