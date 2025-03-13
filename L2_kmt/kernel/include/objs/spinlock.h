#pragma once

#include <common.h>

#define UNLOCKED  0
#define LOCKED    1

struct spinlock {
    const char *name;
    int status;
    struct cpu *cpu;
};

void spin_init(spinlock_t *lk, const char *name);
void spin_lock(spinlock_t *lk);
void spin_unlock(spinlock_t *lk);

typedef struct {
    task_t* tasks[MAX_TASK];
    int front, rear;
} waitlist_t;

typedef struct mutex {
    const char *name;
    spinlock_t spinlock;
    int locked;
    waitlist_t waitlist;
    // struct cpu *cpu; // for debugging
} mutex_t;

void mutex_init(mutex_t *lk, const char *name);
void mutex_lock(mutex_t *lk);
void mutex_unlock(mutex_t *lk);

bool holding(spinlock_t *lk);
void push_off();
void pop_off();

struct cpu {
    int noff;
    int intena;
    task_t *current;
};

extern struct cpu cpus[16];
#define mycpu (&cpus[cpu_current()])
#define mytask (mycpu->current)
