#include "co.h"
#include <stdlib.h>
#include <stdio.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#define STACK_SIZE 64 * 1024 // 64kb


typedef struct context {
    jmp_buf env;
} Context;

enum co_status {
    CO_NEW = 1,
    CO_RUNNINIG,
    CO_WAITING,
    CO_DEAD,
};

typedef struct co {
    const char *name;
    void (*func)(void *);
    void *arg;

    enum co_status  status;
    struct co       *waiter;
    struct context  context;
    uint8_t         stack[STACK_SIZE];

    struct co       *next;      // circular linked list
    struct co       *pre;       // circular linked list
} Co;
Co *current = NULL;

void switch_to(Co *picked);
void coroutine_wrapper(void (*func)(void *), void *arg);
Co* random_pick();


static inline void stack_switch_call(void *sp, void *entry, uintptr_t arg) {
  asm volatile (
#if __x86_64__
    "movq %0, %%rsp; movq %2, %%rdi; movq %3, %%rsi; jmp *%1" : : "b"((uintptr_t)sp),     "d"(coroutine_wrapper), "r"(entry), "a"(arg)
#else
    "movl %0, %%esp; movl %2, 4(%0); movl %3, 8(%0); jmp *%1" : : "b"((uintptr_t)sp - 12), "d"(coroutine_wrapper), "r"(entry), "a"(arg)
#endif
  );
}

// static inline void stack_switch_call(void *sp, void *entry, uintptr_t arg) {
//   asm volatile (
// #if __x86_64__
//     "movq %0, %%rsp; movq %2, %%rdi; jmp *%1" : : "b"((uintptr_t)sp),     "d"(entry), "a"(arg)
// #else
//     "movl %0, %%esp; movl %2, 4(%0); jmp *%1" : : "b"((uintptr_t)sp - 8), "d"(entry), "a"(arg)
// #endif
//   );
// }

void link(Co *co0, Co *co1)
{
    co0->next = co1;
    co1->pre = co0;
}

void coroutine_wrapper(void (*func)(void *), void *arg) {
    func(arg);

    current->status = CO_DEAD;
    Co* waiter = current->waiter;
    link(current->pre, current->next);

    if (waiter != NULL)
    {
        waiter->status = CO_RUNNINIG;
        current = waiter;
        longjmp(waiter->context.env, 1);
    }
    else
    {
        // coroutine may finish without being awaited
        current = current->next; // to avoid an infinite loop
        Co *picked = random_pick();
        switch_to(picked);
    }
}

Co *co_start(const char *name, void (*func)(void *), void *arg) {
    Co *new_co = malloc(sizeof(Co));
    new_co->name = name;
    new_co->func = func;
    new_co->arg = arg;
    new_co->status = CO_NEW;

    bool is_first = current == NULL;
    if (!is_first)
    {
        Co *co0 = current;
        Co *co1 = current->next;
        link(co0, new_co);
        link(new_co, co1);
    }
    return new_co;
}

void co_wait(Co *co) {
    if (co->status == CO_DEAD)
    {
        free(co);
        return;
    }
    
    assert(current != NULL);
    co->waiter = current;
    current->status = CO_WAITING;
    co_yield();
}

Co* random_pick()
{
    assert(current != NULL);

    Co *co = current;
    Co *reservoir = NULL;
    int n = 0;
    do {
        if (co->status == CO_NEW || co->status == CO_RUNNINIG) {
            n++;
            if (rand() % n == 0) {
                reservoir = co;
            }
        }
        co = co->next;
    } while (co != current);
    assert(reservoir != NULL);
    return reservoir;
}

void switch_to(Co *picked)
{
    current = picked;
    assert(picked->status == CO_NEW || picked->status == CO_RUNNINIG);
    if (picked->status == CO_NEW)
    {
        picked->status = CO_RUNNINIG;
        char* sp = (char*)(picked->stack);
        sp = sp + STACK_SIZE;
#if __x86_64__
        sp = sp - 8; // to align stack, push rbp occupies 8 bytes
#else
        sp = sp - 16;
#endif
        stack_switch_call(sp, picked->func, (uintptr_t)picked->arg);
    }
    else
    {
        longjmp(picked->context.env, 1);
    }
}

void co_yield() {
    int val = setjmp(current->context.env);
    if (val == 0)
    {
        // schedule
        Co *picked = random_pick();
        switch_to(picked);
    }
}

/** hook main coroutine logic */
static __attribute__((constructor)) void before_main()
{
    Co* co_main = co_start("main", NULL, NULL);
    link(co_main, co_main);
    current = co_main;
}

static __attribute__((destructor)) void after_main()
{
    assert(current->pre == current && current->next == current);
    free(current);
}
