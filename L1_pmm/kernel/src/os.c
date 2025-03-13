#include <common.h>
#include <pmm.h>

enum ops
{
    // OP_NONE = 0,
    OP_ALLOC = 1,
    OP_FREE = 2
};
typedef struct malloc_op
{
    enum ops type;
    union
    {
        size_t sz;  // OP_ALLOC: size
        void *addr; // OP_FREE: address
    };
    struct malloc_op *next;
} MallocOp;

typedef struct malloc_block
{
    void *addr;
    struct malloc_block *next;
} MallocBlock;

static MallocBlock      *blocks[8]; // MAX_CPU = 8
static int          block_count[8]; // MAX_CPU = 8

MallocOp random_op()
{
    int thread_id = current_thread_id();
    int count = block_count[thread_id];
    int rand_num = RAND() % 2;
    if (count == 0 || rand_num == 0)
    {
        MallocOp op;
        op.type = OP_ALLOC;
        op.sz = RAND() * (MAX_OBJECT_SIZE / 32768);
        assert(op.sz <= MAX_OBJECT_SIZE);
        return op;
    }
    else
    {
        rand_num = RAND() % count;

        MallocBlock dummy;
        dummy.next = blocks[thread_id];
        MallocBlock *pre = &dummy;
        MallocBlock *current = blocks[thread_id];
        while (rand_num-- > 0)
        {
            assert(pre->next == current);
            pre = current;
            current = current->next;
        }
        pre->next = current->next;
        block_count[thread_id]--;
        blocks[thread_id] = dummy.next;

        MallocOp op;
        op.type = OP_FREE;
        op.addr = current->addr;
        free(current);
        return op;
    }
}

static void os_init() {
    pmm->init();
}

static void os_run() {
    for (const char *s = "Hello World from CPU #*\n"; *s; s++) {
        putch(*s == '*' ? '0' + cpu_current() : *s);
    }

    int thread_id = current_thread_id();
    int times = 100;
    MallocBlock stack_blocks[100];
    for(int i = 0; i < times; i++)
    {
        MallocOp op = random_op();
        switch (op.type)
        {
        case OP_ALLOC:
        {
            printf("OP_ALLOC size: %d\n", (int)op.sz);
            void *ptr = pmm->alloc(op.sz);
            if (ptr != NULL)
            {
                MallocBlock *block = &stack_blocks[i];
                block->addr = ptr;
                block->next = blocks[thread_id];
                blocks[thread_id] = block;
                block_count[thread_id]++;
            }
            break;
        }
        case OP_FREE:
        {
            printf("OP_FREE at: %p\n", op.addr);
            pmm->free(op.addr);
            break;
        }
        }
    }
    while (1) ;
}

MODULE_DEF(os) = {
    .init = os_init,
    .run  = os_run,
};
