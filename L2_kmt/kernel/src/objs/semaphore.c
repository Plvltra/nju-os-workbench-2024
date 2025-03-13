#include <objs/semaphore.h>
#include <objs/task.h>

static void sleep(void *chan, spinlock_t *lk) {
    spin_lock(&mytask->data.lock);
    assert(mytask->data.state = RUNNABLE);
    mytask->data.state = SLEEPING;
    mytask->data.chan = chan;
    spin_unlock(&mytask->data.lock);
    spin_unlock(lk);
    yield();

    spin_lock(&mytask->data.lock);
    mytask->data.chan = 0;
    spin_unlock(&mytask->data.lock);
    spin_lock(lk);
}

static void wakeup(void *chan) {
    for (size_t i = 0; i < MAX_TASK; i++) {
        task_t *t = tasklist[i];
        if (t) {
            spin_lock(&t->data.lock);
            if (t->data.state == SLEEPING && t->data.chan == chan) {
                t->data.state = RUNNABLE;
            }
            spin_unlock(&t->data.lock);
        }
    }
}

void sem_init(sem_t *sem, const char *name, int value) {
    sem->name = name;
    sem->value = value;
    spin_init(&sem->lock, name);
}

void sem_wait(sem_t *sem) {
    CHECK_RSP;
    spin_lock(&sem->lock);
    while (sem->value == 0) {
        sleep(sem, &sem->lock);
    }
    sem->value--;
    spin_unlock(&sem->lock);
}

void sem_signal(sem_t *sem) {
    CHECK_RSP;
    spin_lock(&sem->lock);
    sem->value++;
    wakeup(sem);
    spin_unlock(&sem->lock);
}
