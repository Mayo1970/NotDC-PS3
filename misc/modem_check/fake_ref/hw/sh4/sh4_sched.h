#pragma once
// modem_check: Flycast core/hw/sh4/sh4_sched.h, for the reference build; ref_glue.cpp has the scheduler.
#include "types.h"
#define SH4_MAIN_CLOCK (200 * 1000 * 1000)
typedef int sh4_sched_callback(int tag, int sch_cycl, int jitter, void *arg);
int sh4_sched_register(int tag, sh4_sched_callback* ssc, void *arg = nullptr);
void sh4_sched_unregister(int id);
void sh4_sched_request(int id, int cycles);
u64 sh4_sched_now64();
void sh4_sched_serialize(Serializer& ser, int id);
void sh4_sched_deserialize(Deserializer& deser, int id);
