#include <pjnath/p2p_pool.h>

#ifdef USE_P2P_POOL

#include <pjlib.h>
#include <pjlib-util.h>
#include <pjnath.h>

static unsigned char p2p_pool_inited = 0;

#ifdef USE_BLOCK_POOL

#define SYSTEM_MALLOC_INDEX 0xFF

typedef struct p2p_pool_item
{
	unsigned char index; //index in pool blocks, SYSTEM_MALLOC_INDEX is system malloc
	struct p2p_pool_item* next;
	unsigned char padding[3];//for align 4
}p2p_pool_item;

typedef struct p2p_pool_block
{
	size_t free_count;
	p2p_pool_item* begin_item;
	p2p_pool_item* end_item;
	void* memory;
}p2p_pool_block;

size_t pool_item_size[]={64-sizeof(p2p_pool_item), 
	128-sizeof(p2p_pool_item),
	256-sizeof(p2p_pool_item), 
	512-sizeof(p2p_pool_item), 
	1024-sizeof(p2p_pool_item),
	1280-sizeof(p2p_pool_item), 
	2048-sizeof(p2p_pool_item), 
	4096-sizeof(p2p_pool_item), 
	8192-sizeof(p2p_pool_item), 
	11264-sizeof(p2p_pool_item), 
	32768-sizeof(p2p_pool_item)
};

size_t pool_block_item_count[]={2048, 256, 64, 64, 864, 128, 32, 64, 32, 96, 8};

#define POOL_BLOCK_COUNT (sizeof(pool_block_item_count)/sizeof(size_t))
#define POOL_ITEM_MALLOC_SIZE(index) (pool_item_size[index]+sizeof(p2p_pool_item))

typedef struct p2p_pool  
{
#ifdef WIN32
	HANDLE lock;
#else
	pthread_mutex_t lock;
#endif

	p2p_pool_block blocks[POOL_BLOCK_COUNT]; //pool blocks for cache
}p2p_pool;

static p2p_pool g_p2p_pool;

int init_p2p_pool()
{
	unsigned char block;
	size_t total = 0;
	if(p2p_pool_inited)
		return 0;
	
	p2p_pool_inited = 1;

#ifndef WIN32
	pthread_mutex_init(&g_p2p_pool.lock, NULL);
#else
	g_p2p_pool.lock = CreateMutex(NULL, 0, NULL);
#endif

	//malloc memory,and init block free list
	for(block=0; block<POOL_BLOCK_COUNT; block++)
	{
		void* memory;
		p2p_pool_item* item;
		size_t i=0;
		int block_size = POOL_ITEM_MALLOC_SIZE(block)*pool_block_item_count[block];
		g_p2p_pool.blocks[block].free_count = pool_block_item_count[block];
		memory = malloc(block_size);
		memset(memory, 0, block_size);
		g_p2p_pool.blocks[block].memory = memory;
		item = g_p2p_pool.blocks[block].begin_item = memory;
		
		for(i=0; i<pool_block_item_count[block]; i++)
		{
			item->index = block;
			item->next = (p2p_pool_item*)((char*)item+POOL_ITEM_MALLOC_SIZE(block));
			item = item->next;
		}

		g_p2p_pool.blocks[block].end_item = (p2p_pool_item*)((char*)item-POOL_ITEM_MALLOC_SIZE(block));

		total += block_size;
	}
	PJ_LOG(4, ("p2p_pool", "p2p_pool total memory size: %ld", total));
	return 0;
}

void uninit_p2p_pool()
{
	size_t i;

	if(!p2p_pool_inited)
		return;

	for(i=0; i<POOL_BLOCK_COUNT; i++)
	{
		free(g_p2p_pool.blocks[i].memory);
	}

#ifndef WIN32
	pthread_mutex_destroy(&g_p2p_pool.lock);
#else
	CloseHandle(g_p2p_pool.lock);
#endif

	p2p_pool_inited = 0;
}

void* p2p_malloc(size_t size)
{
	unsigned char index = 0;
	p2p_pool_item* item;
	if(!p2p_is_inited())
	{
		item = (p2p_pool_item*)malloc(sizeof(p2p_pool_item) + size);
		item->index = SYSTEM_MALLOC_INDEX;
		return item+1;
	}
	for(index=0; index<POOL_BLOCK_COUNT; index++)
	{
		if(size < pool_item_size[index])
			break;
	}
	if(index == POOL_BLOCK_COUNT || g_p2p_pool.blocks[index].free_count == 0)//so large , call system malloc
	{
		item = (p2p_pool_item*)malloc(sizeof(p2p_pool_item) + size);
		item->index = SYSTEM_MALLOC_INDEX;
		PJ_LOG(4, ("p2p_pool","p2p_pool call system malloc %d %d\r\n", index, size));
		return item+1;
	}

#ifndef WIN32
	pthread_mutex_lock(&g_p2p_pool.lock);
#else
	WaitForSingleObject(g_p2p_pool.lock, INFINITE);
#endif
	if(g_p2p_pool.blocks[index].free_count == 0)//no free pool item, all cache is using, call system malloc
	{
		item = (p2p_pool_item*)malloc(sizeof(p2p_pool_item) + size);
		item->index = SYSTEM_MALLOC_INDEX;

		PJ_LOG(4, ("p2p_pool","p2p_pool cache free count 0, %d\r\n", index));
	}
	else//get memory from block free list
	{
		item = g_p2p_pool.blocks[index].begin_item;
		g_p2p_pool.blocks[index].begin_item = item->next;
		g_p2p_pool.blocks[index].free_count--;
	}
#ifndef WIN32
	pthread_mutex_unlock(&g_p2p_pool.lock);
#else
	ReleaseMutex(g_p2p_pool.lock);
#endif
	return item+1;
}

void p2p_free(void* memory)
{
	p2p_pool_item* item;
	unsigned char index;
	if(memory == NULL)
		return;

	item = (p2p_pool_item*)((char*)memory - sizeof(p2p_pool_item));
	index = item->index;
	if(index == SYSTEM_MALLOC_INDEX)
	{
		free(item);//the memory is system malloc,so free
		return;
	}
	//put the memory to block free list
#ifndef WIN32
	pthread_mutex_lock(&g_p2p_pool.lock);
#else
	WaitForSingleObject(g_p2p_pool.lock, INFINITE);
#endif
	if(g_p2p_pool.blocks[index].free_count == 0)
	{
		g_p2p_pool.blocks[index].begin_item = g_p2p_pool.blocks[item->index].end_item = item;
	}
	else
	{
		g_p2p_pool.blocks[index].end_item->next = item;
		g_p2p_pool.blocks[index].end_item = item;
	}
	g_p2p_pool.blocks[index].free_count++;
#ifndef WIN32
	pthread_mutex_unlock(&g_p2p_pool.lock);
#else
	ReleaseMutex(g_p2p_pool.lock);
#endif
}

void p2p_pool_dump()
{
	int index;
	unsigned long total = 0;
	for(index=0; index<POOL_BLOCK_COUNT; index++)
	{
		PJ_LOG(4, ("p2p_pool", "p2p_pool dump %d %d", index, g_p2p_pool.blocks[index].free_count));
		total += (g_p2p_pool.blocks[index].free_count * POOL_ITEM_MALLOC_SIZE(index));
	}
	PJ_LOG(4, ("p2p_pool", "p2p_pool dump total %ld", total));

}
#else

//#define P2P_POOL_USE_CPLUSPLUS

#ifdef P2P_POOL_USE_CPLUSPLUS
#include <map>
#else
extern "C" {
#include <pjnath/rbtree.h>
}
#endif

#include <pjnath/p2p_tcp.h>
#ifdef USE_P2P_TCP
#define P2P_POOL_TOTAL_SIZE (1024*1024*2)
#else
#define P2P_POOL_TOTAL_SIZE (1024*1024*4)
#endif

#define SYSTEM_MALLOC_INDEX 0xFFFFFFFF

#define P2P_POOL_BOUNDARY 4
#define P2P_POOL_ALIGN(size, boundary) (((size) + ((boundary) - 1)) & ~((boundary) - 1))

typedef struct p2p_pool_item
{
#ifndef P2P_POOL_USE_CPLUSPLUS
	struct rb_node data_node;  
#endif

	size_t size;
}p2p_pool_item;


#ifdef P2P_POOL_USE_CPLUSPLUS
//use malloc for STL
template <class T> class malloc_allocator
{
public:
	typedef T                 value_type;
	typedef value_type*       pointer;
	typedef const value_type* const_pointer;
	typedef value_type&       reference;
	typedef const value_type& const_reference;
	typedef std::size_t       size_type;
	typedef std::ptrdiff_t    difference_type;

	template <class U>
	struct rebind { typedef malloc_allocator<U> other; };

	malloc_allocator() {}
	malloc_allocator(const malloc_allocator&) {}
	template <class U>
	malloc_allocator(const malloc_allocator<U>&) {}
	~malloc_allocator() {}

	pointer address(reference x) const { return &x; }
	const_pointer address(const_reference x) const {
		return x;
	}

	pointer allocate(size_type n, const_pointer = 0) {
		void* p = malloc(n * sizeof(T));
		if (!p)
			throw std::bad_alloc();
		return static_cast<pointer>(p);
	}

	void deallocate(pointer p, size_type) { free(p); }

	size_type max_size() const {
		return static_cast<size_type>(-1) / sizeof(T);
	}

	void construct(pointer p, const value_type& x) {
		new(p) value_type(x);
	}
	void destroy(pointer p) { p;(p)->~value_type(); }

private:
	void operator=(const malloc_allocator&);
};

template<> class malloc_allocator<void>
{
	typedef void        value_type;
	typedef void*       pointer;
	typedef const void* const_pointer;

	template <class U>
	struct rebind { typedef malloc_allocator<U> other; };
};


template <class T>
inline bool operator==(const malloc_allocator<T>&,
					   const malloc_allocator<T>&) {
						   return true;
}

template <class T>
inline bool operator!=(const malloc_allocator<T>&,
					   const malloc_allocator<T>&) {
						   return false;
}

#define P2P_POOL_MAP std::multimap<size_t, p2p_pool_item*, std::less<size_t>, malloc_allocator<std::pair<const size_t, p2p_pool_item*> > > 

#else

static void rb_pool_item_insert(struct rb_root *root, struct p2p_pool_item *data)  
{  
	struct rb_node **tmp = &(root->rb_node);  
	struct rb_node *parent = NULL;  

	while (*tmp)  
	{  
		struct p2p_pool_item *cur = (struct p2p_pool_item *)*tmp;//container_of(*tmp, struct p2p_pool_item, data_node);  

		parent = *tmp;  
		if(data->size < cur->size)  
			tmp = &((*tmp)->rb_left);  
		else 
			tmp = &((*tmp)->rb_right);  
	}  

	rb_link_node(&(data->data_node), parent, tmp);  
	rb_insert_color(&(data->data_node), root);  
}  

// find leftmost node not less than size in rb tree
static struct p2p_pool_item* rb_pool_item_search(struct rb_root *root, size_t size)  
{  
	struct rb_node *node = root->rb_node;  
	struct rb_node * where_node = NULL;
	while(node)  
	{  
		struct p2p_pool_item *data = (struct p2p_pool_item *)node;//container_of(node, struct rbt_data, data_node);  

		if (size < data->size)  
		{
			where_node = node;
			node = node->rb_left;
		}
		else if (size > data->size)  
			node = node->rb_right;  
		else  
			return data;  
	}  

	return (struct p2p_pool_item *)where_node;  
} 

#endif //end of P2P_POOL_USE_CPLUSPLUS

typedef struct p2p_pool  
{
#ifdef WIN32
	HANDLE lock;
#else
	pthread_mutex_t lock;
#endif
	char* memory;
	char* current;

#ifdef P2P_POOL_USE_CPLUSPLUS
	P2P_POOL_MAP free_map;
#else
	rb_root free_map;
#endif
	
	size_t total_map_size;
}p2p_pool;

static p2p_pool g_p2p_pool;

int init_p2p_pool()
{
	if(p2p_pool_inited)
		return 0;

	p2p_pool_inited = 1;

#ifndef WIN32
	pthread_mutex_init(&g_p2p_pool.lock, NULL);
#else
	g_p2p_pool.lock = CreateMutex(NULL, 0, NULL);
#endif
	
	g_p2p_pool.total_map_size = 0;
	g_p2p_pool.memory = (char*)malloc(P2P_POOL_TOTAL_SIZE);
	g_p2p_pool.current = g_p2p_pool.memory + P2P_POOL_TOTAL_SIZE;

#ifndef P2P_POOL_USE_CPLUSPLUS
	g_p2p_pool.free_map.rb_node = NULL;
#endif
	PJ_LOG(4, ("p2p_pool", "p2p_pool total memory size: %d", P2P_POOL_TOTAL_SIZE));

	return 0;
}

void uninit_p2p_pool()
{
	if(!p2p_pool_inited)
		return;

#ifdef P2P_POOL_USE_CPLUSPLUS
	g_p2p_pool.free_map.clear();
#else
	g_p2p_pool.free_map.rb_node = NULL;
#endif
	
	free(g_p2p_pool.memory);
#ifndef WIN32
	pthread_mutex_destroy(&g_p2p_pool.lock);
#else
	CloseHandle(g_p2p_pool.lock);
#endif

	p2p_pool_inited = 0;
}

void p2p_free(void* memory)
{
	p2p_pool_item* item;
	size_t size;
	if(memory == NULL)
		return;

	item = (p2p_pool_item*)((char*)memory - sizeof(p2p_pool_item));
	size = item->size;
	if(size == SYSTEM_MALLOC_INDEX)
	{
		free(item);//the memory is system malloc,so free
		return;
	}

	//put memory to free map
#ifndef WIN32
	pthread_mutex_lock(&g_p2p_pool.lock);
#else
	WaitForSingleObject(g_p2p_pool.lock, INFINITE);
#endif

	if((char*)item == g_p2p_pool.current)//put the memory to big memory
	{
		g_p2p_pool.current += size;
	}
	else
	{
#ifdef P2P_POOL_USE_CPLUSPLUS
		g_p2p_pool.free_map.insert(P2P_POOL_MAP::value_type(size,item));
#else
		rb_pool_item_insert(&g_p2p_pool.free_map, item);
#endif
		g_p2p_pool.total_map_size += size;
	}
#ifndef WIN32
	pthread_mutex_unlock(&g_p2p_pool.lock);
#else
	ReleaseMutex(g_p2p_pool.lock);
#endif	
}

void* p2p_malloc(size_t size)
{
	p2p_pool_item* item = NULL;
	size_t real_size;

#ifndef P2P_POOL_USE_CPLUSPLUS
	p2p_pool_item *node = NULL;  
#endif

	if(!p2p_is_inited())
	{
		item = (p2p_pool_item*)malloc(sizeof(p2p_pool_item) + size);
		item->size = SYSTEM_MALLOC_INDEX;
		return item+1;
	}
	//align to P2P_POOL_BOUNDARY
	real_size = P2P_POOL_ALIGN(size, P2P_POOL_BOUNDARY)+sizeof(p2p_pool_item);

#ifndef WIN32
	pthread_mutex_lock(&g_p2p_pool.lock);
#else
	WaitForSingleObject(g_p2p_pool.lock, INFINITE);
#endif


#ifdef P2P_POOL_USE_CPLUSPLUS
	P2P_POOL_MAP::iterator it = g_p2p_pool.free_map.lower_bound(real_size);
	if(it != g_p2p_pool.free_map.end())
	{
		//less then double real size or memory all used
		if(it->second->size < real_size * 2 
			|| (size_t)(g_p2p_pool.current-g_p2p_pool.memory) < real_size)
		{
			item = it->second;
			g_p2p_pool.total_map_size -= item->size;
			g_p2p_pool.free_map.erase(it);
		}
	}
#else
	node = rb_pool_item_search(&g_p2p_pool.free_map, real_size); 
	if(node)
	{
		//less then double real size or memory all used
		if(node->size < real_size * 2 
			|| (size_t)(g_p2p_pool.current-g_p2p_pool.memory) < real_size)
		{
			item = node;
			g_p2p_pool.total_map_size -= item->size;
			rb_erase(&node->data_node, &g_p2p_pool.free_map);  
		}
	}
#endif
	if(item == NULL)
	{
		if((size_t)(g_p2p_pool.current-g_p2p_pool.memory) > real_size)
		{
			g_p2p_pool.current -= real_size;
			item = (p2p_pool_item*)g_p2p_pool.current;
			item->size = real_size;
		}
		else//call system malloc
		{
			item = (p2p_pool_item*)malloc(real_size);
			item->size = SYSTEM_MALLOC_INDEX;
			PJ_LOG(4, ("p2p_pool","p2p_pool call system malloc %d\r\n", size));
		}
	}

#ifndef WIN32
	pthread_mutex_unlock(&g_p2p_pool.lock);
#else
	ReleaseMutex(g_p2p_pool.lock);
#endif

	return item+1;
}

void p2p_pool_gc()
{
#ifndef P2P_POOL_USE_CPLUSPLUS
	struct rb_node *node;  
	struct rb_node *next;
#endif

#ifndef WIN32
	pthread_mutex_lock(&g_p2p_pool.lock);
#else
	WaitForSingleObject(g_p2p_pool.lock, INFINITE);
#endif

#ifdef P2P_POOL_USE_CPLUSPLUS
	for(P2P_POOL_MAP::iterator it = g_p2p_pool.free_map.begin();
		it != g_p2p_pool.free_map.end();)
	{
		p2p_pool_item* item = it->second;
		if((char*)item == g_p2p_pool.current)
		{
			g_p2p_pool.current += item->size;
			g_p2p_pool.total_map_size -= item->size;
			g_p2p_pool.free_map.erase(it++);
		}
		else
		{
			it++;
		}
	}
#else
	for(node = rb_first(&g_p2p_pool.free_map); node;)  
	{
		p2p_pool_item* item = (struct p2p_pool_item *)node;
		next = rb_next(node);
		
		if((char*)item == g_p2p_pool.current)
		{
			g_p2p_pool.current += item->size;
			g_p2p_pool.total_map_size -= item->size;
			rb_erase(node, &g_p2p_pool.free_map);  
		}

		node = next;
	}
#endif

#ifndef WIN32
	pthread_mutex_unlock(&g_p2p_pool.lock);
#else
	ReleaseMutex(g_p2p_pool.lock);
#endif
}

void p2p_pool_dump()
{
	//p2p_pool_gc();
	PJ_LOG(4, ("p2p_pool", "p2p_pool dump total %d, %d",
		g_p2p_pool.current-g_p2p_pool.memory,
		g_p2p_pool.total_map_size));
}

#endif

static void p2p_pool_callback(pj_pool_t *pool, pj_size_t size)
{
	PJ_CHECK_STACK();
	PJ_UNUSED_ARG(pool);
	PJ_UNUSED_ARG(size);

	PJ_THROW(PJ_NO_MEMORY_EXCEPTION);
}

static void p2p_block_free(pj_pool_factory *factory, void *mem, 
			       pj_size_t size)
{
    PJ_CHECK_STACK();

    if (factory->on_block_free) 
        factory->on_block_free(factory, size);

    p2p_free(mem);
}

static void *p2p_block_alloc(pj_pool_factory *factory, pj_size_t size)
{
    PJ_CHECK_STACK();

    if (factory->on_block_alloc) 
	{
		int rc;
		rc = factory->on_block_alloc(factory, size);
		if (!rc)
			return NULL;
    }

    return p2p_malloc(size);
}
/* pj pool factory memory policy*/
struct pj_pool_factory_policy p2p_policy = 
{
	&p2p_block_alloc,
	&p2p_block_free,
	&p2p_pool_callback,
	0
};

struct pj_pool_factory_policy* pj_pool_factory_p2p_policy()
{
	return &p2p_policy;
}

#endif