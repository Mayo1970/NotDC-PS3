#pragma once
// PC stand-in for PSL1GHT sys/mutex.h (pthreads, see harness.cpp).
#include <stdint.h>
#include <string.h>
typedef uint32_t sys_mutex_t;
typedef struct sys_mutex_attr
{
	uint32_t attr_protocol, attr_recursive, attr_pshared, attr_adaptive;
	uint64_t key;
	int32_t flags;
	uint32_t pad;
	char name[8];
} sys_mutex_attr_t;
#define sysMutexAttrInitialize(x) memset(&(x), 0, sizeof(x))
int sysMutexCreate(sys_mutex_t* m, const sys_mutex_attr_t* attr);
int sysMutexDestroy(sys_mutex_t m);
int sysMutexLock(sys_mutex_t m, uint64_t timeout);
int sysMutexUnlock(sys_mutex_t m);
