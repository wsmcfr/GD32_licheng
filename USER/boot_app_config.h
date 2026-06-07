#ifndef BOOT_APP_CONFIG_H
#define BOOT_APP_CONFIG_H

#include "gd32f4xx.h"

#define BOOT_APP_START_ADDRESS          (0x08011000UL)

#ifdef __cplusplus
extern "C" {
#endif

void boot_app_vector_table_init(void);

void boot_app_handoff_init(void);

#ifdef __cplusplus
}
#endif

#endif /* BOOT_APP_CONFIG_H */
