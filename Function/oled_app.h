#ifndef __OLED_APP_H__
#define __OLED_APP_H__

#include "system_all.h"

#ifdef __cplusplus
extern "C" {
#endif

int oled_printf(uint8_t x, uint8_t y, const char *format, ...);

void oled_app_reset_cache(void);

void oled_task(void);

#ifdef __cplusplus
}
#endif

#endif


