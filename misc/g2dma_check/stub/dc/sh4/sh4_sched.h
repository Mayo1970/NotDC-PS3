#pragma once
#include "types.h"
typedef int sh4_sched_callback(int tag, int sch_cycl, int jitter);
int sh4_sched_register(int tag, sh4_sched_callback* ssc);
void sh4_sched_request(int id, int cycles);
