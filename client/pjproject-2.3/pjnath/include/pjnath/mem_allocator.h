#ifndef __MEMORY_ALLOCATOR_H__
#define __MEMORY_ALLOCATOR_H__

#include <pjlib.h>

#ifdef __cplusplus
extern "C" {
#endif

//固定内存分配器的内存块，每块内存分为有多个连续的固定大小
#define CHUNK_BLOCK_COUNT 8
struct _fixed_size_chunk
{
	unsigned char* begin; //内存开始的位置
	unsigned char* current; //可用内存的位置
	unsigned char* end; //内存结束的位置
	struct _fixed_size_chunk* next;
};
typedef struct _fixed_size_chunk fixed_size_chunk;

//创建固定内存分配器的内存块
fixed_size_chunk* create_fixed_size_chunk(int chunk_size);
//销毁固定内存分配器的内存块
void destroy_fixed_size_chunk(fixed_size_chunk* chunk);
//从块中分配一个固定大小的内存，如果没有就返回0
unsigned char* alloc_block_from_chunk(fixed_size_chunk* chunk, int block_size);

//固定内存分配器，
//分配时如果可用的块的链表里不为空，直接从链表中取出一个内存返回。如果没有就从内存chunk里分配内存
//释放是直接着放入可用块的链表里,在可用块的链表里，每个节点的前四个字节表示下一个可用块的地址
//带锁，支持多线程
struct _fixed_size_allocator
{
	int block_size; //块的大小，创建时指定，其他时候不变

	//可用的块
	unsigned char* first_free_block;
	unsigned char* last_free_block;
	int free_block_count;

	//实际的内存chunk
	fixed_size_chunk* first_chunk;
	fixed_size_chunk* last_chunk;
	unsigned int total_chunk_memory_size; //总共占用了多少内存

	pj_pool_t *pj_pool; /*memory manage poll*/
	pj_mutex_t* mutexDataList;
};
typedef struct _fixed_size_allocator fixed_size_allocator;
//创建固定内存分配器
fixed_size_allocator* create_fixed_size_allocator(int block_size, int init_block_count);
//销毁固定内存分配器
void destroy_fixed_size_allocator(fixed_size_allocator* allocator);
//分配固定内存
unsigned char* alloc_fixed_size_block(fixed_size_allocator* allocator);
//释放固定内存
void free_fixed_size_block(fixed_size_allocator* allocator, unsigned char* data);
   
#ifdef __cplusplus
}
#endif

#endif
