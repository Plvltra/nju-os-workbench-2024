#include <common.h>
#include <objs/objs.h>

#define CANARY 0xdeadbeef

static Context *kmt_context_save(Event ev, Context *context) {
    TRACE_ENTRY;
    CHECK_RSP;
    if (mytask) {
        // acquire task's stack ownership
        spin_lock(&mytask->data.lock);
        mytask->data.context = *context;
        mytask->data.holder = mycpu;
        spin_unlock(&mytask->data.lock);
    }
    TRACE_EXIT;
    return NULL;
}

// 其中 create 在系统中创建一个线程 (task_t 应当事先被分配好)，这个线程立即就可以被调度执行 (但调用 create 时中断可能处于关闭状态，在打开中断后它才获得被调度执行的权利)。
// 我们假设 create 创建的线程永不返回——但它有可能在永远不会被调度执行的情况下被调用 kmt->teardown 回收。
// TODO: 此处不对tasklist加锁可能导致其他线程teardown产生数据竞争
static Context *kmt_schedule(Event ev, Context *context) {
    TRACE_ENTRY;
    CHECK_RSP;

    task_t *old_task = mytask;
    if (mytask) { // TODO: maybe wrong
        spin_lock(&mytask->data.lock);
        if (mytask->data.state == RUNNING)
            mytask->data.state = RUNNABLE;
        spin_unlock(&mytask->data.lock);
    }

    int has_sched = 0;
    int threashold = 1 << 20, timer = 0;
    while (1) {
        int liveness = 0;
        for (size_t i = 0; i < MAX_TASK && !has_sched; i++) { // TODO: Optimize Round Robin
            task_t *t = tasklist[i];
            if (t == NULL)
                continue;
            spin_lock(&t->data.lock);
            if (t->data.state == RUNNABLE && (t->data.holder == NULL || t->data.holder == mycpu)) {
                mycpu->current = t;
                t->data.state = RUNNING;
                has_sched = 1;
                liveness = 1;
            }
            spin_unlock(&t->data.lock);
        }
        // panic_on(!liveness, "maybe deadlock occur!");
        if (has_sched)
            break;

        if (timer++ > threashold)
            panic("maybe schedule error occur!");
    }
    panic_on(mytask->data.canary != CANARY, "stack overflow!");

    if (old_task) {
        // release task's stack ownership
        spin_lock(&old_task->data.lock);
        old_task->data.holder = NULL;
        spin_unlock(&old_task->data.lock);
    }

    TRACE_EXIT;
    return &(mytask->data.context);
}

void kmt_init() {
    spin_init(&lk_tasklist, "lk_task_list");

    os->on_irq(INT_MIN, EVENT_NULL, kmt_context_save);
    os->on_irq(INT_MAX, EVENT_NULL, kmt_schedule);
}

// Should be thread safety
int kmt_create(task_t *task, const char *name, void (*entry)(void *arg), void *arg) {
    TRACE_ENTRY;
    spin_init(&task->data.lock, name);
    task->data.name     = name;
    task->data.entry    = entry;
    task->data.arg      = arg;
    task->data.state    = RUNNABLE;
    task->data.chan     = NULL;
    task->data.waiting  = NULL;
    task->data.holder   = NULL;
    task->data.canary   = CANARY;

    // set thread intial register status
    task->data.context = *kcontext(
        (Area) { .start = &task->data.end, .end = task + 1, },
        task->data.entry, arg
    );

    spin_lock(&lk_tasklist);
    for (size_t i = 0; i < MAX_TASK; i++) {
        if (!tasklist[i]) {
            tasklist[i] = task;
            break;
        }
    }
    spin_unlock(&lk_tasklist);
    TRACE_EXIT;
    return -1;
}

// Should be thread safety
void kmt_teardown(task_t *task) {
    TRACE_ENTRY;
    // mutex_lock(&lk_tasklist);
    // TODO: 回收为线程分配的资源, 清理task list, 设置null
    // mutex_unlock(&lk_tasklist);
    TRACE_EXIT;
}

MODULE_DEF(kmt) = {
    .init           = kmt_init,
    .create         = kmt_create,
    .teardown       = kmt_teardown,
    .spin_init      = spin_init,
    .spin_lock      = spin_lock,
    .spin_unlock    = spin_unlock,
    .sem_init       = sem_init,
    .sem_wait       = sem_wait,
    .sem_signal     = sem_signal,
};
