#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "system_all.h"

#ifdef __cplusplus
extern "C" {
#endif

void system_init(void);

void scheduler_run(void);

void scheduler_reset_runtime(void);

#ifdef __cplusplus
}
#endif

#endif
