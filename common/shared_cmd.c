#include "shared_cmd.h"
#include "log.h"
#ifndef _WIN32
#include <arpa/inet.h>
#endif

//free shared commands
TAILQ_HEAD(free_shared_cmds, shared_cmd_tailq) free_shared_cmd = TAILQ_HEAD_INITIALIZER(free_shared_cmd);

TAILQ_HEAD(free_shared_cmd_tailqs, shared_cmd_tailq) free_sc_tailq = TAILQ_HEAD_INITIALIZER(free_sc_tailq);

static pthread_mutex_t shared_cmd_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t shared_cmd_tailq_mutex = PTHREAD_MUTEX_INITIALIZER;
static unsigned int free_shared_cmd_count=0;

void shared_cmd_startup()
{
	TAILQ_INIT(&free_shared_cmd);
	TAILQ_INIT(&free_sc_tailq);
}

void shared_cmd_clean()
{
	shared_cmd_tailq* sc_tailq;
	TAILQ_FOREACH(sc_tailq, &free_shared_cmd, tailq)
	{
		gss_free(sc_tailq->sc->cmd);
		gss_free(sc_tailq->sc);
		gss_free(sc_tailq);
	}

	TAILQ_FOREACH(sc_tailq, &free_sc_tailq, tailq)
	{
		gss_free(sc_tailq);
	}
}

shared_cmd* malloc_shared_cmd(const char* buf, unsigned int len)
{
	shared_cmd* sc = NULL;
	shared_cmd_tailq* sc_tailq = NULL;

	pthread_mutex_lock(&shared_cmd_mutex);
	sc_tailq = TAILQ_FIRST(&free_shared_cmd);
	if(sc_tailq)
	{
		TAILQ_REMOVE(&free_shared_cmd, sc_tailq, tailq);
		sc = sc_tailq->sc;
		free_shared_cmd_count --;
		//LOG(LOG_LEVEL_TRACE, "malloc_shared_cmd %d", free_shared_cmd_count);
	}
	pthread_mutex_unlock(&shared_cmd_mutex);

	if(sc_tailq)
		free_shared_cmd_tailq(sc_tailq);

	if(!sc)
	{
		sc = (shared_cmd*)gss_malloc(sizeof(shared_cmd));
		sc->cmd = (char*)gss_malloc(GSS_MAX_CMD_LEN);
	}
	memcpy(sc->cmd, buf, len);
	sc->cmd_len = len;

#ifdef _WIN32
	sc->ref = 0;
#else

#ifdef USE_ATOMIC_T
	atomic_set(&sc->ref, 0);
#else
	sc->ref = 0;
#endif

#endif
	sc->time_stamp = ntohl(*(unsigned int*)(buf + sizeof(GSS_DATA_HEADER)));
	return sc;
}

void shared_cmd_add_ref(shared_cmd* sc)
{
#ifdef _WIN32
	InterlockedIncrement(&sc->ref);
#else
#ifdef USE_ATOMIC_T
	atomic_inc(&sc->ref);
#else
	__sync_add_and_fetch(&sc->ref,1); 
#endif
#endif

}

void shared_cmd_release(shared_cmd* sc)
{
	int ref;
#ifdef _WIN32
	ref = InterlockedDecrement(&sc->ref);
#else
#ifdef USE_ATOMIC_T
	ref = !atomic_sub_and_test(1, &sc->ref);
#else
	ref = __sync_sub_and_fetch(&sc->ref, 1);
#endif
#endif
	if(ref == 0)
	{
		shared_cmd_tailq* sc_tailq = malloc_shared_cmd_tailq(sc);

		pthread_mutex_lock(&shared_cmd_mutex);
		TAILQ_INSERT_TAIL(&free_shared_cmd, sc_tailq, tailq);
		free_shared_cmd_count++;
		//LOG(LOG_LEVEL_TRACE, "shared_cmd_release %d", free_shared_cmd_count);
		pthread_mutex_unlock(&shared_cmd_mutex);
	}
}

shared_cmd_tailq* malloc_shared_cmd_tailq(shared_cmd* sc)
{
	shared_cmd_tailq* sc_tailq = NULL;

	pthread_mutex_lock(&shared_cmd_tailq_mutex);
	sc_tailq = TAILQ_FIRST(&free_sc_tailq);
	if(sc_tailq)
		TAILQ_REMOVE(&free_sc_tailq, sc_tailq, tailq);
	pthread_mutex_unlock(&shared_cmd_tailq_mutex);

	if(sc_tailq == NULL)
		sc_tailq = (shared_cmd_tailq*)gss_malloc(sizeof(shared_cmd_tailq));

	sc_tailq->sc = sc;
	return sc_tailq;
}

void free_shared_cmd_tailq(shared_cmd_tailq* sc_tailq)
{
	pthread_mutex_lock(&shared_cmd_tailq_mutex);
	TAILQ_INSERT_TAIL(&free_sc_tailq, sc_tailq, tailq);
	pthread_mutex_unlock(&shared_cmd_tailq_mutex);
}
