#include <pjnath/mem_allocator.h>
#include <pjnath/p2p_global.h>


#define DATA_ALIGN(size, boundary) (((size) + ((boundary) - 1)) & ~((boundary) - 1))
//�����̶��ڴ���������ڴ��
fixed_size_chunk* create_fixed_size_chunk(int chunk_size)
{
	fixed_size_chunk* chunk = (fixed_size_chunk*)malloc(sizeof(fixed_size_chunk));
	chunk->begin = chunk->current = (unsigned char*)malloc(chunk_size);
	chunk->end = chunk->begin + chunk_size;
	chunk->next = 0;
	return chunk;
}
//���ٹ̶��ڴ���������ڴ��
void destroy_fixed_size_chunk(fixed_size_chunk* chunk)
{
	free(chunk->begin);
	free(chunk);
}
//�ӿ��з���һ���̶���С���ڴ棬���û�оͷ���0
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

//�ӵ�ǰ�ڴ�chunk�У�����һ���ڴ棬�����ǰchunk��û�ڴ��ˣ�����������һ���µ�chunk
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

//�����̶��ڴ������
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
	//�����ʼ��
	allocator->first_free_block = allocator->last_free_block = alloc_block(allocator);
	for(i=1; i<init_block_count; i++)
	{
		block = alloc_block(allocator);
		//�ڿ��ÿ�������ÿ���ڵ��ǰ�ĸ��ֽڱ�ʾ��һ�����ÿ�ĵ�ַ
		*((long*)allocator->last_free_block) = (long)block;
		allocator->last_free_block = block;
	}

	return allocator;
}
//���ٹ̶��ڴ������
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
//����̶��ڴ棬����ʱ������õĿ�������ﲻΪ�գ�ֱ�Ӵ�������ȡ��һ���ڴ淵�ء����û�оʹ��ڴ�chunk������ڴ�
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
		//�ڿ��ÿ�������ÿ���ڵ��ǰ�ĸ��ֽڱ�ʾ��һ�����ÿ�ĵ�ַ
		allocator->first_free_block = (unsigned char*)(*((long*)allocator->first_free_block));
		allocator->free_block_count--;
	}
	pj_mutex_unlock(allocator->mutexDataList);

	return block;
}
//�ͷŹ̶��ڴ棬ֱ���ŷ�����ÿ��������
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
		//�ڿ��ÿ�������ÿ���ڵ��ǰ�ĸ��ֽڱ�ʾ��һ�����ÿ�ĵ�ַ
		*((long*)allocator->last_free_block) = (long)data;
		allocator->last_free_block = data;
	}
	allocator->free_block_count++;
	pj_mutex_unlock(allocator->mutexDataList);
}