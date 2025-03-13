#pragma once
#include <common.h>

#define PAGE_SIZE (16*1024*1024)        // 16MB
#define MIN_OBJECT_SIZE 8               // 8Byte
#define MIN_OBJECT_LEVEL 3
#define MAX_OBJECT_SIZE (16*1024*1024)  // 16MB
#define MAX_OBJECT_LEVEL 24
#define SMALL_OBJECT_LEVEL 20           // 1MB
#ifndef TEST
    #define MAX_THREAD 8                // MAX_CPU = 8
#else
    #define MAX_THREAD 16
#endif
#define MAX_SPAN 256                    // 4GB/256MB
#define MAX_HEAP_SIZE (256*1024*1024)    // TODO: 64MB


typedef struct {
    uintptr_t head  [MAX_OBJECT_LEVEL + 1];
    int count       [MAX_OBJECT_LEVEL + 1];
} FreeList;

typedef struct {
    FreeList freelist;
} ThreadCache;

typedef struct {
    FreeList freelist;
} CentralCache;

enum span_status { ON_HEAP, IN_USE };

typedef struct {
    enum span_status status;
    size_t central_size; // TODO: 初始16MB, 进线程减少，离开线程增加，用来归还
    int level;
} Span;

/** @return: tid - 1 */
int current_thread_id();

#ifndef TEST
    #define RAND() (rand())
#else
    #define RAND() (rand() % 32768)
#endif
