#ifndef BSP_RTC_H
#define BSP_RTC_H

#define SYSTEM_ALL_BASE_ONLY
#include "system_all.h"
#undef SYSTEM_ALL_BASE_ONLY

#ifdef __cplusplus
extern "C" {
#endif

/* 当前工程选择 LXTAL 作为 RTC 时钟源。 */
#define RTC_CLOCK_SOURCE_LXTAL

#ifndef RTC_CLOCK_FALLBACK_IRC32K_ENABLE
#define RTC_CLOCK_FALLBACK_IRC32K_ENABLE 1U
#endif

#define BKP_VALUE                       0x32F0U

typedef enum
{
    RTC_STATUS_SOURCE_NONE = 0U,
    RTC_STATUS_SOURCE_LXTAL = 1U,
    RTC_STATUS_SOURCE_IRC32K = 2U,
    RTC_STATUS_SOURCE_HXTAL_DIV = 3U,
} bsp_rtc_clock_source_t;

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

typedef struct
{
    uint8_t clock_ready;
    uint8_t backup_valid;
    uint8_t lxtal_recovered;
    bsp_rtc_clock_source_t clock_source;
    uint16_t prescaler_a;
    uint16_t prescaler_s;
    uint32_t bdctl;
    uint32_t hrfc;
    uint32_t cosc;
} bsp_rtc_status_t;

/* rtc_task 需要复用这个结构读取当前时间，因此在头文件中导出。 */
extern rtc_parameter_struct rtc_initpara;

int bsp_rtc_init(void);

int bsp_rtc_get_datetime(bsp_rtc_datetime_t *datetime);

int bsp_rtc_get_epoch_seconds(uint32_t *epoch_seconds);

int bsp_rtc_get_status(bsp_rtc_status_t *status);

int bsp_rtc_set_datetime(const bsp_rtc_datetime_t *datetime);

#ifdef __cplusplus
}
#endif

#endif /* BSP_RTC_H */
