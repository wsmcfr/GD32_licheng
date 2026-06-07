#ifndef BSP_RTC_H
#define BSP_RTC_H

#define SYSTEM_ALL_BASE_ONLY
#include "system_all.h"
#undef SYSTEM_ALL_BASE_ONLY

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint16_t year;
    uint8_t month;
    uint8_t date;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t day_of_week;
} bsp_rtc_datetime_t;

int bsp_rtc_init(void);

int bsp_rtc_get_datetime(bsp_rtc_datetime_t *datetime);

int bsp_rtc_set_datetime(const bsp_rtc_datetime_t *datetime);

#ifdef __cplusplus
}
#endif

#endif /* BSP_RTC_H */
