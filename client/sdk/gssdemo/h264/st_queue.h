#ifndef __ST_QUEUE_H__
#define __ST_QUEUE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#ifdef _WIN32 
#include <Windows.h>
#else
#include <pthread.h>
#include <semaphore.h>
#endif


	typedef struct queue_item{
		int  length;
		struct queue_item* next;
	}queue_item;

	/*thread safe queue*/
	typedef struct st_queue{
#ifdef _WIN32 
		CRITICAL_SECTION mutex;
		HANDLE sem;
#else
		pthread_mutex_t mutex;
		sem_t sem;
#endif

		queue_item* first_item;
		queue_item* last_item;
		unsigned int count;
	}st_queue;

	st_queue* create_st_queue();

	void st_queue_push(st_queue* queue, const char* buf, int buf_len);

	void st_queue_push_item(st_queue* queue, queue_item* item);

	queue_item* st_queue_pop(st_queue* queue);

	void  destroy_st_queue(st_queue* queue);

	void st_queue_wakeup(st_queue* queue);

#ifdef __cplusplus
}
#endif

#endif
