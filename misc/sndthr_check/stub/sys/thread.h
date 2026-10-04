#pragma once
// PC stand-in for PSL1GHT sys/thread.h (pthreads, see harness.cpp).
#include <stdint.h>
typedef uint64_t sys_ppu_thread_t;
#define THREAD_JOINABLE 1
int sysThreadCreate(sys_ppu_thread_t* tid, void (*entry)(void*), void* arg, int32_t prio, uint64_t stack, uint64_t flags,
                    char* name);
int sysThreadJoin(sys_ppu_thread_t tid, uint64_t* rv);
void sysThreadExit(uint64_t rv);
int sysThreadGetId(sys_ppu_thread_t* tid);
int sysThreadGetPriority(sys_ppu_thread_t tid, int32_t* prio);
int sysThreadSetPriority(sys_ppu_thread_t tid, int32_t prio);
