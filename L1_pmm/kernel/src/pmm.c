#include <pmm.h>
#ifndef TEST
#include <spinlock.h>
#else
#include <thread.h>
#endif

#define nullptr 0
#define ALIGN_UP(value, alignment) (((value) + (alignment) - 1) & ~((alignment) - 1))
#define ALIGN_DOWN(value, alignment) ((value) & ~((alignment) - 1))

// ~Begin global variables
#ifndef TEST
spinlock_t lock_central[MAX_OBJECT_LEVEL + 1];
spinlock_t lock_heap = spin_init("lock_heap");
spinlock_t lock_shadow = spin_init("lock_shadow");
#else
pthread_mutex_t lock_central[MAX_OBJECT_LEVEL + 1] = { [0 ... MAX_OBJECT_LEVEL] = PTHREAD_MUTEX_INITIALIZER };
pthread_mutex_t lock_heap = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t lock_shadow = PTHREAD_MUTEX_INITIALIZER;
#endif

#ifdef TEST
// heap without am
typedef struct {
    void *start, *end;
} Area;
static Area heap;
#endif

static ThreadCache thread_caches[MAX_THREAD];
static CentralCache central_cache;
static Span spans[MAX_SPAN];
static size_t SPAN_NUM;
static uintptr_t heap_head = nullptr;
static unsigned char shadows[MAX_HEAP_SIZE / 8]; // TODO: big shadow memory cause qemu crash
// ~End global variables

int current_thread_id() {
#ifndef TEST
    return cpu_current();
#else
    pthread_t pthread_id = pthread_self();
    for (int i = 0; i < 4096; i++)
    {
        if (threads_[i].thread == pthread_id)
            return i;
    }
    return -1;
#endif
}

#ifndef TEST
    void mutex_lock(spinlock_t *__mutex) {  spin_lock(__mutex); }
    void mutex_unlock(spinlock_t *__mutex) { spin_unlock(__mutex); }
#else
    int mutex_lock(pthread_mutex_t *__mutex) { return pthread_mutex_lock(__mutex); }
    int mutex_unlock(pthread_mutex_t *__mutex) { return pthread_mutex_unlock(__mutex); }
#endif

// Available area
uintptr_t heap_start() { return ALIGN_UP((uintptr_t)heap.start, PAGE_SIZE); }
uintptr_t heap_end() { return ALIGN_DOWN((uintptr_t)heap.end, PAGE_SIZE); } // exclusive

#define CHECK_HEAP(ptr) \
    ({ assert(heap_start() <= (ptr) && (ptr) <= heap_end()); })

#define CHECK_SPAN(span) \
    ({ assert( \
        ((span).status == ON_HEAP && (span).central_size == 0) \
        || ((span).status == IN_USE && 0 <= (span).central_size && (span).central_size <= PAGE_SIZE) ); })

#define CHECK_LEVEL(level) \
    ({ assert(MIN_OBJECT_LEVEL <= (level) && (level) <= MAX_OBJECT_LEVEL); })

#define CHECK_FREELIST(freelist, level) \
    ({ \
        CHECK_LEVEL((level)); \
        int _expected = (freelist)->count[(level)]; \
        int _got = 0; \
        uintptr_t _loop_ptr = (freelist)->head[(level)]; \
        while (_loop_ptr != nullptr) \
        { \
            _loop_ptr = next(_loop_ptr); \
            if (_loop_ptr != nullptr) \
                CHECK_HEAP(_loop_ptr); \
            _got++; \
        } \
        assert(_expected == _got); \
    })

#define CHECK_WHOLE_FREELIST(freelist) \
    ({ \
        for (int _level = MIN_OBJECT_LEVEL; _level <= MAX_OBJECT_LEVEL; _level++) \
        { \
            CHECK_FREELIST((freelist), _level); \
        } \
    })

static unsigned char *addr2shadow(uintptr_t addr)
{
    CHECK_HEAP(addr);
    assert((addr & 0x7) == 0);

    int offset = (addr >> 3) - (heap_start() >> 3);
    assert(offset <= LENGTH(shadows));
    return shadows + offset;
}

static uintptr_t next(uintptr_t ptr) {
    assert(ptr != nullptr);
    uintptr_t new_ptr = *((uintptr_t*)ptr);
    return new_ptr;
}

// link address that ptr and next point to
static void link_ptr(uintptr_t pre, uintptr_t next) {
    assert(pre != nullptr);
    uintptr_t next_address = (uintptr_t)next;
    *((uintptr_t *)pre) = next_address;
}

static size_t level_of(size_t size) 
{
    if (size <= 0 || size > MAX_OBJECT_SIZE)
        return 0;
    if (size <= MIN_OBJECT_SIZE)
        return MIN_OBJECT_LEVEL;

    size = size - 1;
    int result = 0;
    while (size)
    {
        size = size >> 1;
        result++;
    }
    assert(MIN_OBJECT_LEVEL <= result && result <= MAX_OBJECT_LEVEL);
    return result;
}

static int pow_of2(int level)
{
    return 1 << level;
}

// Number of objects to move from central cache to local cache at this level
static size_t move_count(int level) {
    assert(MIN_OBJECT_LEVEL <= level && level <= MAX_OBJECT_LEVEL);
    return 1 << (MAX_OBJECT_LEVEL - level);
}

static void move(FreeList *from, FreeList *to, int level, int count) {
    CHECK_FREELIST(from, level);
    CHECK_FREELIST(to, level);
    assert(from->count[level] >= count);
    uintptr_t first = from->head[level];
    uintptr_t last = first;
    int number = count;
    while (--number > 0)
    {
        last = next(last);
        assert(last != nullptr);
    }
    from->head[level] = next(last);
    from->count[level] -= count;
    link_ptr(last, to->head[level]);
    to->head[level] = first;
    to->count[level] += count;
    CHECK_FREELIST(from, level);
    CHECK_FREELIST(to, level);
}

static int span_idx(uintptr_t ptr) {
    CHECK_HEAP(ptr);
    int index = (ptr - heap_start()) / PAGE_SIZE;
    return index;
}

static Span* span_of(uintptr_t ptr) {
    CHECK_HEAP(ptr);
    int index = span_idx(ptr);
    return &spans[index];
}

static void pmm_init() {
#ifndef TEST
    uintptr_t pmsize = (
        (uintptr_t)heap.end
        - (uintptr_t)heap.start
    );

    printf(
        "Got %d MiB heap: [%p, %p)\n",
        pmsize >> 20, heap.start, heap.end
    );
#else
    int HEAP_SIZE = MAX_HEAP_SIZE;
    char *ptr  = malloc(HEAP_SIZE);
    heap.start = ptr;
    heap.end   = ptr + HEAP_SIZE;
    printf("Got %d MiB heap: [%p, %p)\n", HEAP_SIZE >> 20, heap.start, heap.end);
#endif
    assert(heap_start() < heap_end());

    // Init locks
#ifndef TEST
    for (int i = MIN_OBJECT_LEVEL; i <= MAX_OBJECT_LEVEL; i++) {

        char name[30];
        sprintf(name, "lock_central_%d", i);
        lock_central[i] = spin_init(name);
    }
#endif

    // Init shadows
    memset(shadows, 0x0, sizeof(shadows));

    // Init spans
    SPAN_NUM = (heap_end() - heap_start()) / PAGE_SIZE;
    for (int i = 0; i < SPAN_NUM; i++) {
        spans[i].status = ON_HEAP;
        spans[i].level = 0;
    }

    // Init heap list
    heap_head = heap_start();
    uintptr_t heap_tail = heap_start();
    for (uintptr_t ptr = heap_start() + PAGE_SIZE; ptr < heap_end(); ptr += PAGE_SIZE)
    {
        link_ptr(heap_tail, ptr);
        heap_tail = ptr;
    }
}

// Allocate memory: heap -> central
static int alloc_heap(int level)
{
    mutex_lock(&lock_heap);
    if (heap_head == nullptr)
    {
        mutex_unlock(&lock_heap);
        return 0;
    }

    // Heap is enough for allocating
    assert(heap_head != nullptr);
    uintptr_t alloc_addr = heap_head;
    Span *span = span_of(alloc_addr);
    assert(span->status == ON_HEAP && span->central_size == 0);
    span->status = IN_USE;
    span->central_size = PAGE_SIZE;
    span->level = level;
    printf("span: %d, status: IN_USE\n", span_idx(alloc_addr));
    heap_head = next(heap_head);
    FreeList *central_freelist = &central_cache.freelist;
    for (uintptr_t loop_ptr = alloc_addr; loop_ptr < alloc_addr + PAGE_SIZE; loop_ptr += pow_of2(level))
    {
        link_ptr(loop_ptr, central_freelist->head[level]);
        central_freelist->head[level] = loop_ptr;
    }
    central_freelist->count[level] += move_count(level);
    CHECK_FREELIST(central_freelist, level);

    mutex_unlock(&lock_heap);
    return 1;
}

// Allocate memory: central -> thread cache
static int alloc_central(int level)
{
    mutex_lock(&lock_central[level]);

    int thread_id = current_thread_id();
    ThreadCache *thread_cache = &thread_caches[thread_id];
    FreeList *freelist = &thread_cache->freelist;
    FreeList *central_freelist = &central_cache.freelist;
    if (central_freelist->count[level] < move_count(level))
    {
        int result = alloc_heap(level);
        if (result == 0)
        {
            mutex_unlock(&lock_central[level]);
            return 0;
        }
    }

    // Central cache is enough for allocating
    assert(central_freelist->count[level] >= move_count(level));
    uintptr_t dummy_memory;
    uintptr_t loop_ptr = (uintptr_t)&dummy_memory;
    link_ptr(loop_ptr, central_freelist->head[level]);
    int number = move_count(level);
    while (number-- > 0)
    {
        // Update span central size
        loop_ptr = next(loop_ptr);
        CHECK_HEAP(loop_ptr);
        Span *span = span_of(loop_ptr); // TODO: span不加锁是否有问题
        assert(0 <= span->central_size && span->central_size <= PAGE_SIZE);
        span->central_size -= pow_of2(level);
        CHECK_SPAN(*span);
    }
    move(central_freelist, freelist, level, move_count(level));

    mutex_unlock(&lock_central[level]);
    return 1;
}

static void *kalloc(size_t size) 
{
    const size_t level = level_of(size);
    if (level == 0)
    {
        printf("Allocated memory at NULL\n");
        return NULL;
    }
    int thread_id = current_thread_id();
    assert(thread_id != -1);

    // Thread cache is enough for allocating
    ThreadCache *thread_cache = &thread_caches[thread_id];
    FreeList *freelist = &thread_cache->freelist;
    if (freelist->head[level] == nullptr)
    {
        int result = alloc_central(level);
        if (result == 0)
        {
            printf("Allocated memory at NULL\n");
            return nullptr;
        }
    }
    assert(freelist->head[level] != nullptr);

    uintptr_t alloc_start = freelist->head[level];
    freelist->head[level] = next(freelist->head[level]);
    freelist->count[level]--;
    CHECK_FREELIST(freelist, level);
    printf("Allocated memory at %p, span id: %d\n", (void *)alloc_start, span_idx(alloc_start));

    // Use shadow memory to check validation
    mutex_lock(&lock_shadow);
    unsigned char *shadow_start = addr2shadow(alloc_start);
    uintptr_t alloc_end = alloc_start + pow_of2(level);
    CHECK_HEAP(alloc_start);
    CHECK_HEAP(alloc_end);
    unsigned char *shadow_end = addr2shadow(alloc_end);
    for (unsigned char *ptr = shadow_start; ptr < shadow_end; ptr++)
        assert((*ptr) == 0);
    memset(shadow_start, 0xff, pow_of2(level) / 8);
    mutex_unlock(&lock_shadow);

    return (void *)alloc_start;
}

void free2heap(int level)
{
    mutex_lock(&lock_heap);
    // If central cache is oversized, return memory to heap
    int CENTRAL_THREASHOLD = 4;
    int free_span_count = 0;
    for (int i = 0; i < SPAN_NUM; i++)
    {
        if (spans[i].central_size == PAGE_SIZE)
        {
            if (free_span_count < CENTRAL_THREASHOLD)
            {
                free_span_count++;
            }
            else
            {
                FreeList *central_freelist = &central_cache.freelist;
                int level = spans[i].level;
                uintptr_t dummy_head;
                uintptr_t dummy_ptr = (uintptr_t)&dummy_head;
                uintptr_t loop_ptr = dummy_ptr;
                link_ptr(loop_ptr, central_freelist->head[level]);
                // Clean up current span's memory in level
                int freed_count = 0;
                while (loop_ptr != nullptr)
                {
                    uintptr_t next_ptr = next(loop_ptr);
                    if (next_ptr != nullptr && span_idx(next_ptr) == i)
                    {
                        freed_count++;
                        link_ptr(loop_ptr, next(next_ptr));
                    }
                    else
                    {
                        loop_ptr = next(loop_ptr);
                    }
                }
                assert(freed_count == move_count(level));
                central_freelist->count[level] -= freed_count;
                central_freelist->head[level] = next(dummy_ptr);
                CHECK_FREELIST(central_freelist, level);
                // Return memory to heap
                assert(spans[i].status == IN_USE && spans[i].central_size == PAGE_SIZE);
                spans[i].status = ON_HEAP;
                spans[i].central_size = 0;
                spans[i].level = 0;
                printf("span id: %d, status: ON_HEAP\n", i);
                uintptr_t span_addr = heap_start() + PAGE_SIZE * i;
                link_ptr(span_addr, heap_head);
                heap_head = span_addr;
            }
        }
    }
    mutex_unlock(&lock_heap);
}

void free2central(int level)
{
    int thread_id = current_thread_id();
    ThreadCache *thread_cache = &thread_caches[thread_id];
    FreeList *freelist = &thread_cache->freelist;

    mutex_lock(&lock_central[level]);

    int LOCAL_THREASHOLD = 2 * move_count(level);
    if (freelist->count[level] < LOCAL_THREASHOLD)
    {
        mutex_unlock(&lock_central[level]);
        return;
    }

    // If local cache is oversized, move freelist to central_freelist
    FreeList *central_freelist = &central_cache.freelist;
    move(freelist, central_freelist, level, move_count(level));
    // update span central size
    uintptr_t dummy_memory;
    uintptr_t loop_ptr = (uintptr_t)&dummy_memory;
    link_ptr(loop_ptr, central_freelist->head[level]);
    int number = move_count(level);
    while (number-- > 0)
    {
        loop_ptr = next(loop_ptr);
        CHECK_HEAP(loop_ptr);
        Span *span = span_of(loop_ptr);
        assert(0 <= span->central_size && span->central_size <= PAGE_SIZE);
        span->central_size += pow_of2(level);
        CHECK_SPAN(*span);
    }

    free2heap(level);
    mutex_unlock(&lock_central[level]);
}

static void kfree(void *ptr) 
{
    uintptr_t ptr_addr = (uintptr_t)ptr;
    Span* span = span_of(ptr_addr);
    int level = span->level;
    printf("Free memory at: %p, size: %x, span id: %d\n", ptr, pow_of2(level), span_idx(ptr_addr));
    assert(span->status == IN_USE);
    int thread_id = current_thread_id();
    assert(thread_id != -1);

    // Use shadow memory to check validation
    mutex_lock(&lock_shadow);
    unsigned char *shadow_start = addr2shadow(ptr_addr);
    uintptr_t free_end = ptr_addr + pow_of2(level);
    CHECK_HEAP(ptr_addr);
    CHECK_HEAP(free_end);
    unsigned char *shadow_end = addr2shadow(free_end);
    for (unsigned char *ptr = shadow_start; ptr < shadow_end; ptr++)
        assert((*ptr) == 0xff);
    memset(shadow_start, 0x0, pow_of2(level) / 8);
    mutex_unlock(&lock_shadow);

    ThreadCache *thread_cache = &thread_caches[thread_id];
    FreeList *freelist = &thread_cache->freelist;
    link_ptr(ptr_addr, freelist->head[level]);
    freelist->head[level] = ptr_addr;
    freelist->count[level]++;
    CHECK_FREELIST(freelist, level);

    free2central(level);

    // // If local cache is oversized, move freelist to central_freelist
    // int LOCAL_THREASHOLD = 2 * move_count(level);
    // if (freelist->count[level] >= LOCAL_THREASHOLD)
    // {
    //     mutex_lock(&lock_central[level]);
    //     FreeList *central_freelist = &central_cache.freelist;
    //     move(freelist, central_freelist, level, move_count(level));
    //     // update span central size
    //     uintptr_t dummy_memory;
    //     uintptr_t loop_ptr = (uintptr_t)&dummy_memory;
    //     link_ptr(loop_ptr, central_freelist->head[level]);
    //     int number = move_count(level);
    //     while (number-- > 0)
    //     {
    //         loop_ptr = next(loop_ptr);
    //         CHECK_HEAP(loop_ptr);
    //         Span *span = span_of(loop_ptr);
    //         assert(0 <= span->central_size && span->central_size <= PAGE_SIZE);
    //         span->central_size += pow_of2(level);
    //         CHECK_SPAN(*span);
    //     }
        
    //     // If central cache is oversized, return memory to heap
    //     int CENTRAL_THREASHOLD = 4;
    //     int free_span_count = 0;
    //     for (int i = 0; i < SPAN_NUM; i++)
    //     {
    //         if (spans[i].central_size == PAGE_SIZE)
    //         {
    //             if (free_span_count < CENTRAL_THREASHOLD)
    //             {
    //                 free_span_count++;
    //             }
    //             else
    //             {
    //                 int level = spans[i].level;
    //                 uintptr_t dummy_head;
    //                 uintptr_t dummy_ptr = (uintptr_t)&dummy_head;
    //                 loop_ptr = dummy_ptr;
    //                 link_ptr(loop_ptr, central_freelist->head[level]);
    //                 // Clean up current span's memory in level
    //                 int freed_count = 0;
    //                 while (loop_ptr != nullptr)
    //                 {
    //                     uintptr_t next_ptr = next(loop_ptr);
    //                     if (next_ptr != nullptr && span_idx(next_ptr) == i)
    //                     {
    //                         freed_count++;
    //                         link_ptr(loop_ptr, next(next_ptr));
    //                     }
    //                     else
    //                     {
    //                         loop_ptr = next(loop_ptr);
    //                     }
    //                 }
    //                 assert(freed_count == move_count(level));
    //                 central_freelist->count[level] -= freed_count;
    //                 central_freelist->head[level] = next(dummy_ptr);
    //                 CHECK_FREELIST(central_freelist, level);
    //                 // Return memory to heap
    //                 assert(spans[i].status == IN_USE && spans[i].central_size == PAGE_SIZE);
    //                 spans[i].status = ON_HEAP;
    //                 spans[i].central_size = 0;
    //                 spans[i].level = 0;
    //                 printf("span id: %d, status: ON_HEAP\n", i);
    //                 uintptr_t span_addr = heap_start() + PAGE_SIZE * i;
    //                 link_ptr(span_addr, heap_head);
    //                 heap_head = span_addr;
    //             }
    //         }
    //     }
    //     mutex_unlock(&lock_central[level]);
    // }
}

MODULE_DEF(pmm) = {
    .init  = pmm_init,
    .alloc = kalloc,
    .free  = kfree,
};
