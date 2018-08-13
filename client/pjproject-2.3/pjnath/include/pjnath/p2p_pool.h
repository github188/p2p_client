#ifndef __P2P_POOL_H__
#define __P2P_POOL_H__

#ifdef __cplusplus
	extern "C" {
#endif

#ifdef WIN32
#include <windows.h>
#else
	#include <pthread.h>
#endif
#include <stdio.h>

#ifdef PJ_ARM_MIPS
#define USE_P2P_POOL 1 //only in arm linux device,use this macro definition
#endif

#ifdef USE_P2P_POOL

int init_p2p_pool();
void uninit_p2p_pool();

void* p2p_malloc(size_t size);
void p2p_free(void* memory);
void p2p_pool_dump();

struct pj_pool_factory_policy* pj_pool_factory_p2p_policy();

#else
#define p2p_malloc malloc
#define p2p_free free
#endif

#ifdef __cplusplus
	}
#endif

#endif