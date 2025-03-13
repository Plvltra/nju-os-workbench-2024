#include <objs/spinlock.h>
#include <objs/task.h>
#include <am.h>

// This is a ported version of spin-lock
// from xv6-riscv to AbstractMachine:
// https://github.com/mit-pdos/xv6-riscv

struct cpu cpus[16];

void spin_init(spinlock_t *lk, const char *name) {
    lk->name = name;
    lk->status = UNLOCKED;
    lk->cpu = NULL;
}

void spin_lock(spinlock_t *lk) {
    assert(lk);
    // Disable interrupts to avoid deadlock.
    push_off();

    // This is a deadlock.
    if (holding(lk)) {
        fpanic("acquire %s", lk->name);
        // panic("acquire");
    }

    // This our main body of spin lock.
    int got;
    int threashold = 1 << 30, timer = 0; // 30 is too long
    do {
        got = atomic_xchg(&lk->status, LOCKED);
        timer++;
        if (timer > threashold)
            fpanic("maybe deadlock: %s occur!", lk->name);
            // panic("maybe deadlock occur!");
    } while (got != UNLOCKED);

    __sync_synchronize();

    lk->cpu = mycpu;
}

void spin_unlock(spinlock_t *lk) {
    assert(lk);
    if (!holding(lk)) {
        fpanic("release %s", lk->name);
        // panic("release");
    }

    lk->cpu = NULL;

    __sync_synchronize();

    atomic_xchg(&lk->status, UNLOCKED);

    pop_off();
}

static int is_empty(waitlist_t* list) {
    return list->rear == list->front;
}

static void enqueue(waitlist_t* list, task_t *task) {
    list->tasks[list->rear] = task;
    list->rear = (list->rear + 1) % MAX_TASK;
    panic_on(is_empty(list), "waitlist is full!");
}

static task_t* dequeue(waitlist_t* list) {
    panic_on(is_empty(list), "waitlist is empty!");
    task_t *ret = list->tasks[list->front];
    list->front = (list->front + 1) % MAX_TASK;
    return ret;
}

void mutex_init(mutex_t *lk, const char *name) {
    lk->name = name;
    lk->locked = UNLOCKED;
    spin_init(&lk->spinlock, name);
}

void mutex_lock(mutex_t *lk) {
    CHECK_RSP;
    assert(lk);
    int acquired = 0;
    spin_lock(&lk->spinlock);
    waitlist_t *waitlist = &lk->waitlist;
    for (int i = waitlist->front; i != waitlist->rear; i = (i + 1) % MAX_TASK) {
        if (mytask == waitlist->tasks[i]) {
            fpanic("AA deadlock: %s acquire %s", mytask->data.name, lk->name);
        }
    }
    if (lk->locked != 0) {
        enqueue(&lk->waitlist, mytask);

        spin_lock(&mytask->data.lock);
        mytask->data.state = BLOCKED;
        mytask->data.waiting = lk;
        spin_unlock(&mytask->data.lock);
    } else {
        lk->locked = 1;
        acquired = 1;
    }
    spin_unlock(&lk->spinlock);
    if (!acquired) yield();
}

void mutex_unlock(mutex_t *lk) {
    CHECK_RSP;
    assert(lk);
    spin_lock(&lk->spinlock);
    assert(lk->locked);
    if (!is_empty(&lk->waitlist)) {
        task_t *task = dequeue(&lk->waitlist);
        spin_lock(&task->data.lock);
        assert(task->data.waiting == lk);
        task->data.state = RUNNABLE;
        task->data.waiting = NULL;
        spin_unlock(&task->data.lock);
    } else {
        lk->locked = 0;
    }
    spin_unlock(&lk->spinlock);
}

// Check whether this cpu is holding the lock.
// Interrupts must be off.
bool holding(spinlock_t *lk) {
    return (
        lk->status == LOCKED &&
        lk->cpu == &cpus[cpu_current()]
    );
}

// push_off/pop_off are like intr_off()/intr_on()
// except that they are matched:
// it takes two pop_off()s to undo two push_off()s.
// Also, if interrupts are initially off, then
// push_off, pop_off leaves them off.
void push_off() {
    int old = ienabled();
    struct cpu *c = mycpu;

    iset(false);
    if (c->noff == 0) {
        c->intena = old;
    }
    c->noff += 1;
}

void pop_off() {
    struct cpu *c = mycpu;

    // Never enable interrupt when holding a lock.
    if (ienabled()) {
        panic("pop_off - interruptible");
    }

    if (c->noff < 1) {
        panic("pop_off");
    }

    c->noff -= 1;
    if (c->noff == 0 && c->intena) {
        iset(true);
    }
}
