#pragma once

#include <common.h>
#include <objs/spinlock.h>

enum taskstate { UNUSED, SLEEPING, BLOCKED, RUNNABLE, RUNNING };

struct task {
    union {
        struct {
            spinlock_t      lock;
            const char      *name;
            void            (*entry)(void *);
            void            *arg;
            enum taskstate  state;
            void            *chan;          // If non-zero, sleeping on chan(debugging sem)
            mutex_t         *waiting;       // Mutex waiting for(debugging mutex)
            struct cpu      *holder;        // Cpu runs this task, in case of stack overlapping
            Context         context;
            uint64_t        canary;
            char            end[0];
        };
        struct {
            uint8_t         stack[8192];
        };
    } data;
};

extern task_t* tasklist[MAX_TASK];
extern spinlock_t lk_tasklist;
