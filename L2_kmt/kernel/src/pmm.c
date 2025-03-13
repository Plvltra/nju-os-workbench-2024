#include <pmm.h>
#include <common.h>
#include <objs/objs.h>

#define nullptr 0
#define ALIGN_UP(value, alignment) (((value) + (alignment) - 1) & ~((alignment) - 1))
#define ALIGN_DOWN(value, alignment) ((value) & ~((alignment) - 1))

// ~Begin global variables
static spinlock_t lk_central[MAX_OBJECT_LEVEL + 1];
static spinlock_t lk_heap;
static spinlock_t lk_shadow;

static ThreadCache thread_caches[MAX_THREAD];
static CentralCache central_cache;
static Span spans[MAX_SPAN];
static size_t SPAN_NUM;
static uintptr_t heap_head = nullptr;
static unsigned char shadows[MAX_HEAP_SIZE / 8]; // TODO: big shadow memory cause qemu crash
// ~End global variables

int current_thread_id() {
    return cpu_current();
}

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
    uintptr_t pmsize = (
        (uintptr_t)heap.end
        - (uintptr_t)heap.start
    );

    printf(
        "Got %d MiB heap: [%p, %p)\n",
        pmsize >> 20, heap.start, heap.end
    );
    assert(heap_start() < heap_end());

    // Init locks
    kmt->spin_init(&lk_heap, "lk_heap");
    kmt->spin_init(&lk_shadow, "lk_shadow");
    for (int i = MIN_OBJECT_LEVEL; i <= MAX_OBJECT_LEVEL; i++) {
        char name[30];
        sprintf(name, "lk_central_%d", i);
        kmt->spin_init(&lk_central[i], name);
    }

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
    kmt->spin_lock(&lk_heap);
    if (heap_head == nullptr)
    {
        kmt->spin_unlock(&lk_heap);
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

    kmt->spin_unlock(&lk_heap);
    return 1;
}

// Allocate memory: central -> thread cache
static int alloc_central(int level)
{
    kmt->spin_lock(&lk_central[level]);

    int thread_id = current_thread_id();
    ThreadCache *thread_cache = &thread_caches[thread_id];
    FreeList *freelist = &thread_cache->freelist;
    FreeList *central_freelist = &central_cache.freelist;
    if (central_freelist->count[level] < move_count(level))
    {
        int result = alloc_heap(level);
        if (result == 0)
        {
            kmt->spin_unlock(&lk_central[level]);
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

    kmt->spin_unlock(&lk_central[level]);
    return 1;
}

static void *kalloc(size_t size) 
{
#ifdef SIMPLE_PMM
    kmt->spin_lock(&lk_heap);
    static bool has_init = false;
    static uintptr_t heap_top;
    if (!has_init) {
        heap_top = (uintptr_t)heap.start;
        has_init = true;
    }

    size_t aligned_size = pow_of2(level_of(size));
    uintptr_t result = ALIGN_UP(heap_top, aligned_size);
    panic_on(result + size > (uintptr_t)heap.end, "memory out of bound");
    heap_top = result + size;
    kmt->spin_unlock(&lk_heap);
    return (void *)result;
#else
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
    kmt->spin_lock(&lk_shadow);
    unsigned char *shadow_start = addr2shadow(alloc_start);
    uintptr_t alloc_end = alloc_start + pow_of2(level);
    CHECK_HEAP(alloc_start);
    CHECK_HEAP(alloc_end);
    unsigned char *shadow_end = addr2shadow(alloc_end);
    for (unsigned char *ptr = shadow_start; ptr < shadow_end; ptr++)
        assert((*ptr) == 0);
    memset(shadow_start, 0xff, pow_of2(level) / 8);
    kmt->spin_unlock(&lk_shadow);

    return (void *)alloc_start;
#endif // SIMPLE_PMM
}

void free2heap(int level)
{
    kmt->spin_lock(&lk_heap);
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
    kmt->spin_unlock(&lk_heap);
}

void free2central(int level)
{
    int thread_id = current_thread_id();
    ThreadCache *thread_cache = &thread_caches[thread_id];
    FreeList *freelist = &thread_cache->freelist;

    kmt->spin_lock(&lk_central[level]);

    int LOCAL_THREASHOLD = 2 * move_count(level);
    if (freelist->count[level] < LOCAL_THREASHOLD)
    {
        kmt->spin_unlock(&lk_central[level]);
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
    kmt->spin_unlock(&lk_central[level]);
}

static void kfree(void *ptr) 
{
#ifdef SIMPLE_PMM
    return;
#else
    uintptr_t ptr_addr = (uintptr_t)ptr;
    Span* span = span_of(ptr_addr);
    int level = span->level;
    printf("Free memory at: %p, size: %x, span id: %d\n", ptr, pow_of2(level), span_idx(ptr_addr));
    assert(span->status == IN_USE);
    int thread_id = current_thread_id();
    assert(thread_id != -1);

    // Use shadow memory to check validation
    kmt->spin_lock(&lk_shadow);
    unsigned char *shadow_start = addr2shadow(ptr_addr);
    uintptr_t free_end = ptr_addr + pow_of2(level);
    CHECK_HEAP(ptr_addr);
    CHECK_HEAP(free_end);
    unsigned char *shadow_end = addr2shadow(free_end);
    for (unsigned char *ptr = shadow_start; ptr < shadow_end; ptr++)
        assert((*ptr) == 0xff);
    memset(shadow_start, 0x0, pow_of2(level) / 8);
    kmt->spin_unlock(&lk_shadow);

    ThreadCache *thread_cache = &thread_caches[thread_id];
    FreeList *freelist = &thread_cache->freelist;
    link_ptr(ptr_addr, freelist->head[level]);
    freelist->head[level] = ptr_addr;
    freelist->count[level]++;
    CHECK_FREELIST(freelist, level);

    free2central(level);
#endif // SIMPLE_PMM
}

MODULE_DEF(pmm) = {
    .init  = pmm_init,
    .alloc = kalloc,
    .free  = kfree,
};
