#include "st_queue.h"
#include <memory.h>

st_queue* create_st_queue(){
	st_queue* queue = malloc(sizeof(st_queue));
	
	queue->count = 0;
	queue->first_item = NULL;
	queue->last_item = NULL;

#ifdef _WIN32
	InitializeCriticalSection(&queue->mutex);
	queue->sem = CreateSemaphore(NULL, 0, LONG_MAX, NULL);
#else
	pthread_mutex_init(&queue->mutex, NULL);
	sem_init(&queue->sem, 0, 0);
#endif

	return queue;
}

void st_queue_push_item(st_queue* queue, queue_item* item){
	if(!queue)
		return;

#ifdef _WIN32
	EnterCriticalSection(&queue->mutex);
#else
	pthread_mutex_lock(&queue->mutex);
#endif	
	
	if(queue->first_item){
		queue->last_item->next = item;
		queue->last_item = item;
	}
	else{
		queue->first_item = queue->last_item = item;
	}
	++queue->count;

#ifdef _WIN32
	LeaveCriticalSection(&queue->mutex);

	/*wake up st_queue_pop*/
	ReleaseSemaphore(&queue->sem, 1, 0);
#else
	pthread_mutex_unlock(&queue->mutex);

	/*wake up st_queue_pop*/
	sem_post(&queue->sem);
#endif

}

void st_queue_push(st_queue* queue, const char* buf, int buf_len){
	queue_item* item;
	if(!queue)
		return;
	
	/*copy data , push to item list*/
	item = malloc(sizeof(queue_item)+buf_len);
	item->length = buf_len;
	item->next = NULL;
	memcpy(item+1, buf, buf_len);

	st_queue_push_item(queue, item);
}

queue_item* st_queue_pop(st_queue* queue){
	queue_item* item = NULL;

	if(!queue)
		return NULL;
#ifdef _WIN32
	WaitForSingleObject(&queue->sem, INFINITE);
	
	EnterCriticalSection(&queue->mutex);
#else
	sem_wait(&queue->sem);

	pthread_mutex_lock(&queue->mutex);
#endif	
	if(queue->count > 0){
		item = queue->first_item;
		if(--queue->count == 0)
			queue->first_item = queue->last_item = NULL;
		else
			queue->first_item = queue->first_item->next;
	}			
#ifdef _WIN32
	LeaveCriticalSection(&queue->mutex);
#else
	pthread_mutex_unlock(&queue->mutex);
#endif

	return item;
}

void  destroy_st_queue(st_queue* queue){
	queue_item* item = NULL;
	queue_item* next_item = NULL;

	if(!queue)
		return;
	
#ifdef _WIN32
	EnterCriticalSection(&queue->mutex);
#else
	pthread_mutex_lock(&queue->mutex);
#endif	
	item = queue->first_item ;
	while(item){
		next_item = item->next;
		free(item);
		item = next_item;
	}

#ifdef _WIN32
	LeaveCriticalSection(&queue->mutex);

	CloseHandle(queue->sem);
	DeleteCriticalSection(&queue->mutex);
#else
	pthread_mutex_unlock(&queue->mutex);

	sem_destroy(&queue->sem);
	pthread_mutex_destroy(&queue->mutex);
#endif
	
	free(queue);
}

void st_queue_wakeup(st_queue* queue){
	/*wake up st_queue_pop*/
#ifdef _WIN32
	ReleaseSemaphore(&queue->sem, 1, 0);
#else
	sem_post(&queue->sem);
#endif
}

