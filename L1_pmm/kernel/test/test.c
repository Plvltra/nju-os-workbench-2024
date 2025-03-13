#include <kernel.h>
#include <thread.h>
#include <pmm.h>

static void entry(int tid)
{
    void *pos = pmm->alloc(128);
}

static void smoke_test(int thread_count)
{
    printf("Begin smoke test.\n");
    for (int i = 0; i < thread_count; i++)
    {
        create(entry);
    }
    create(entry);
    join();
    printf("End smoke test.\n");
}

static void infinite_entry(int tid)
{
    while (1)
    {
        void *pos = pmm->alloc(128);
    }
}

static void infinite_test()
{
    for (int i = 0; i < 4; i++)
    {
        create(infinite_entry);
    }
    join();
}

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

static MallocBlock      *blocks[LENGTH(threads_)];
static int          block_count[LENGTH(threads_)];

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

void stress_test_entry(int tid)
{
    int thread_id = current_thread_id();

    int times = -1;
    while (times--)
    {
        MallocOp op = random_op();
        switch (op.type)
        {
        case OP_ALLOC:
        {
            printf("OP_ALLOC size: %zu\n", op.sz);
            void *ptr = pmm->alloc(op.sz);
            if (ptr != NULL)
            {
                MallocBlock *block = (MallocBlock *)malloc(sizeof(MallocBlock));
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
}

static void create_test(void *entry, size_t thread_count)
{
    for (int i = 0; i < thread_count; i++)
    {
        create(entry);
    }
    join();
}

int main(int argc, char *argv[])
{
    setvbuf(stdout, NULL, _IOLBF, 0);

    pmm->init();
    srand((unsigned int)time(NULL));
    switch (atoi(argv[1]))
    {
    case 0:
        // printf("~Begin smoke test.\n");
        // create_test(entry, 1);
        // printf("~End smoke test.\n");
        break;
    case 1:
        // printf("~Begin infinite test.\n");
        // create_test(infinite_entry, 4);
        // printf("~End infinite test.\n");
        break;
    case 2:
        printf("~Begin stress test.\n");
        create_test(stress_test_entry, 8);
        printf("~End stress test.\n");
        break;
    default:
        break;
    }
    printf("End.\n");
}
