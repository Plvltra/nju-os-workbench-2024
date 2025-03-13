#include <objs/task.h>

task_t* tasklist[MAX_TASK];
spinlock_t lk_tasklist;