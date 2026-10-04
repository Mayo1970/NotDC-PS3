#pragma once
// PC stand-in for PSL1GHT sys/sem.h (mutex + condition, see harness.cpp).
#include <stdint.h>
#define SYS_SEM_ATTR_PROTOCOL 0x0002
#define SYS_SEM_ATTR_PSHARED  0x0200
typedef uint32_t sys_sem_t;
typedef struct sys_sem_attr
{
	uint32_t attr_protocol;
	uint32_t attr_pshared;
	uint64_t key;
	int32_t flags;
	uint32_t pad;
	char name[8];
} sys_sem_attr_t;
int sysSemCreate(sys_sem_t* sem, const sys_sem_attr_t* attr, int32_t initial, int32_t max);
int sysSemDestroy(sys_sem_t sem);
int sysSemWait(sys_sem_t sem, uint64_t timeout_usec);
int sysSemPost(sys_sem_t sem, int32_t count);
