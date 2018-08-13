#ifndef __MEMORY_ALLOCATOR_H__
#define __MEMORY_ALLOCATOR_H__

#include <pjlib.h>

#ifdef __cplusplus
extern "C" {
#endif

//�̶��ڴ���������ڴ�飬ÿ���ڴ��Ϊ�ж�������Ĺ̶���С
#define CHUNK_BLOCK_COUNT 8
struct _fixed_size_chunk
{
	unsigned char* begin; //�ڴ濪ʼ��λ��
	unsigned char* current; //�����ڴ��λ��
	unsigned char* end; //�ڴ������λ��
	struct _fixed_size_chunk* next;
};
typedef struct _fixed_size_chunk fixed_size_chunk;

//�����̶��ڴ���������ڴ��
fixed_size_chunk* create_fixed_size_chunk(int chunk_size);
//���ٹ̶��ڴ���������ڴ��
void destroy_fixed_size_chunk(fixed_size_chunk* chunk);
//�ӿ��з���һ���̶���С���ڴ棬���û�оͷ���0
unsigned char* alloc_block_from_chunk(fixed_size_chunk* chunk, int block_size);

//�̶��ڴ��������
//����ʱ������õĿ�������ﲻΪ�գ�ֱ�Ӵ�������ȡ��һ���ڴ淵�ء����û�оʹ��ڴ�chunk������ڴ�
//�ͷ���ֱ���ŷ�����ÿ��������,�ڿ��ÿ�������ÿ���ڵ��ǰ�ĸ��ֽڱ�ʾ��һ�����ÿ�ĵ�ַ
//������֧�ֶ��߳�
struct _fixed_size_allocator
{
	int block_size; //��Ĵ�С������ʱָ��������ʱ�򲻱�

	//���õĿ�
	unsigned char* first_free_block;
	unsigned char* last_free_block;
	int free_block_count;

	//ʵ�ʵ��ڴ�chunk
	fixed_size_chunk* first_chunk;
	fixed_size_chunk* last_chunk;
	unsigned int total_chunk_memory_size; //�ܹ�ռ���˶����ڴ�

	pj_pool_t *pj_pool; /*memory manage poll*/
	pj_mutex_t* mutexDataList;
};
typedef struct _fixed_size_allocator fixed_size_allocator;
//�����̶��ڴ������
fixed_size_allocator* create_fixed_size_allocator(int block_size, int init_block_count);
//���ٹ̶��ڴ������
void destroy_fixed_size_allocator(fixed_size_allocator* allocator);
//����̶��ڴ�
unsigned char* alloc_fixed_size_block(fixed_size_allocator* allocator);
//�ͷŹ̶��ڴ�
void free_fixed_size_block(fixed_size_allocator* allocator, unsigned char* data);
   
#ifdef __cplusplus
}
#endif

#endif
