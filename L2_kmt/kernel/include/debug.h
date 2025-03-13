#pragma once

#ifdef TRACE_F
    #define TRACE_ENTRY printf("[trace] %s:entry\n", __func__)
    #define TRACE_EXIT printf("[trace] %s:exit\n", __func__)
#else
    #define TRACE_ENTRY ((void)0)
    #define TRACE_EXIT ((void)0)
#endif

#ifdef __x86_64
    #define RSP "rsp"
#else
    #define RSP "esp"
#endif

#define CHECK_RSP \
    ({ uintptr_t rsp_addr; \
    __asm__ volatile ("movq %%" RSP ", %0" : "=r" (rsp_addr)); \
    if (mytask) { \
        uintptr_t lower = (uintptr_t)&mytask->data.end; \
        uintptr_t upper = (uintptr_t)(mytask + 1); \
        fpanic_on(rsp_addr <= lower || rsp_addr >= upper, \
            "stack overflow! rsp:%p, lower bound:%p, upper_bound: %p.", rsp_addr, lower, upper); \
    } })
