#include <pjnath/mem_allocator.h>
#include <pjnath/p2p_global.h>


#define DATA_ALIGN(size, boundary) (((size) + ((boundary) - 1)) & ~((boundary) - 1))
//创建固定内存分配器的内存块
fixed_size_chunk* create_fixed_size_chunk(int chunk_size)
{
	fixed_size_chunk* chunk = (fixed_size_chunk*)malloc(sizeof(fixed_size_chunk));
	chunk->begin = chunk->current = (unsigned char*)malloc(chunk_size);
	chunk->end = chunk->begin + chunk_size;
	chunk->next = 0;
	return chunk;
}
//销毁固定内存分配器的内存块
void destroy_fixed_size_chunk(fixed_size_chunk* chunk)
{
	free(chunk->begin);
	free(chunk);
}
//从块中分配一个固定大小的内存，如果没有就返回0
unsigned char* alloc_block_from_chunk(fixed_size_chunk* chunk, int block_size)
{
	unsigned char* result = 0;
	if(chunk->current + block_size <= chunk->end)
	{
		result = chunk->current;
		chunk->current += block_size;
	}
	return result;
}

//从当前内存chunk中，分配一个内存，如果当前chunk里没内存了，就重新申请一个新的chunk
unsigned char* alloc_block(fixed_size_allocator* allocator)
{
	fixed_size_chunk* chunk;
	unsigned char* result = alloc_block_from_chunk(allocator->last_chunk, allocator->block_size);
	if(result == 0)
	{
		chunk = create_fixed_size_chunk(CHUNK_BLOCK_COUNT*allocator->block_size);
		allocator->total_chunk_memory_size += CHUNK_BLOCK_COUNT*allocator->block_size;
		allocator->last_chunk->next = chunk;
		allocator->last_chunk = chunk;
		result = alloc_block_from_chunk(allocator->last_chunk, allocator->block_size);
	}
	return result;
}

//创建固定内存分配器
fixed_size_allocator* create_fixed_size_allocator(int block_size, int init_block_count)
{
	int i;
	unsigned char* block;

	fixed_size_allocator* allocator = (fixed_size_allocator*)malloc(sizeof(fixed_size_allocator));

	allocator->pj_pool = pj_pool_create(&get_p2p_global()->caching_pool.factory, 
		"p2p_memc%p", 
		PJNATH_POOL_LEN_ICE_STRANS,
		PJNATH_POOL_INC_ICE_STRANS, 
		NULL);

	pj_mutex_create_recursive(allocator->pj_pool, NULL, &allocator->mutexDataList);

	allocator->block_size = DATA_ALIGN(block_size, sizeof(long));
	allocator->free_block_count = init_block_count;
	allocator->first_chunk = allocator->last_chunk = create_fixed_size_chunk(CHUNK_BLOCK_COUNT*block_size);
	allocator->total_chunk_memory_size = CHUNK_BLOCK_COUNT*allocator->block_size;
	if(init_block_count == 0)
	{
		allocator->first_free_block = allocator->last_free_block = 0;
		return allocator;
	}
	//分配初始块
	allocator->first_free_block = allocator->last_free_block = alloc_block(allocator);
	for(i=1; i<init_block_count; i++)
	{
		block = alloc_block(allocator);
		//在可用块的链表里，每个节点的前四个字节表示下一个可用块的地址
		*((long*)allocator->last_free_block) = (long)block;
		allocator->last_free_block = block;
	}

	return allocator;
}
//销毁固定内存分配器
void destroy_fixed_size_allocator(fixed_size_allocator* allocator)
{
	fixed_size_chunk* chunk;
	pj_mutex_lock(allocator->mutexDataList);
	chunk = allocator->first_chunk;
	while(chunk)
	{
		chunk = allocator->first_chunk->next;
		destroy_fixed_size_chunk(allocator->first_chunk);
		allocator->first_chunk = chunk;
	}
	pj_mutex_unlock(allocator->mutexDataList);

	pj_mutex_destroy(allocator->mutexDataList);
	delay_destroy_pool(allocator->pj_pool);
	free(allocator);
}
//分配固定内存，分配时如果可用的块的链表里不为空，直接从链表中取出一个内存返回。如果没有就从内存chunk里分配内存
unsigned char* alloc_fixed_size_block(fixed_size_allocator* allocator)
{
	unsigned char* block;
	pj_mutex_lock(allocator->mutexDataList);
	if(allocator->free_block_count == 0)
	{
		allocator->first_free_block = allocator->last_free_block = 0;
		block = alloc_block(allocator);
	}
	else
	{
		block = allocator->first_free_block;
		//在可用块的链表里，每个节点的前四个字节表示下一个可用块的地址
		allocator->first_free_block = (unsigned char*)(*((long*)allocator->first_free_block));
		allocator->free_block_count--;
	}
	pj_mutex_unlock(allocator->mutexDataList);

	return block;
}
//释放固定内存，直接着放入可用块的链表里
void free_fixed_size_block(fixed_size_allocator* allocator, unsigned char* data)
{
	*((long*)data) = 0;
	pj_mutex_lock(allocator->mutexDataList);
	if(allocator->free_block_count == 0)
	{
		allocator->first_free_block = allocator->last_free_block = data;
	}
	else
	{
		//在可用块的链表里，每个节点的前四个字节表示下一个可用块的地址
		*((long*)allocator->last_free_block) = (long)data;
		allocator->last_free_block = data;
	}
	allocator->free_block_count++;
	pj_mutex_unlock(allocator->mutexDataList);
}