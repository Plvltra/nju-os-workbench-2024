#pragma once

#include <common.h>
#include <objs/spinlock.h>

#define P kmt->sem_wait
#define V kmt->sem_signal

// 在信号量初始化时，value 指定了它初始的数值。初始时 value == 1 可以把信号量当互斥锁；
// 初始时 value == 0 可以把信号量作为生产者-消费者缓冲区管理实现。sem_wait 和 sem_signal 分别对应了 P/V 操作。
// 1. 允许在线程中执行信号量的 sem_wait 操作。在 P 操作执行没有相应资源时，线程将被阻塞 (不再被调度执行)。中断没有对应的线程、不能阻塞，因此不能在中断时调用 sem_wait；
// 2. 允许在任意状态下任意执行 sem_signal，包括任何处理器中的任何线程和任何处理器的任何中断。

// TODO: 使用mutex替换spinlock,实现能睡眠的互斥锁和信号量(可以参考xv6 semaphore, p77)
struct semaphore {
    const char *name;
    int value;
    spinlock_t lock;
};

void sem_init(sem_t *sem, const char *name, int value);
void sem_wait(sem_t *sem);
void sem_signal(sem_t *sem);
