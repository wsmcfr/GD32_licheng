#ifndef BSP_RTC_H
#define BSP_RTC_H

/*
 * 文件作用：
 *   定义 RTC 初始化相关的常量、共享时间结构和初始化接口。
 */

#define SYSTEM_ALL_BASE_ONLY
#include "system_all.h"
#undef SYSTEM_ALL_BASE_ONLY

#ifdef __cplusplus
extern "C" {
#endif

/* 当前工程选择 LXTAL 作为 RTC 时钟源。 */
#define RTC_CLOCK_SOURCE_LXTAL
#define BKP_VALUE                       0x32F0U

/*
 * 结构体作用：
 *   以十进制形式描述一组完整的 RTC 日期时间，供串口命令层和其它应用层模块使用。
 * 成员说明：
 *   year：完整年份，当前接口约定范围为 2000~2099。
 *   month：月份，十进制范围为 1~12。
 *   date：日期，十进制范围为 1~31，具体上限受月份和闰年影响。
 *   hour：小时，24 小时制，十进制范围为 0~23。
 *   minute：分钟，十进制范围为 0~59。
 *   second：秒，十进制范围为 0~59。
 *   day_of_week：星期枚举，读取时返回 `RTC_MONDAY`~`RTC_SUNDAY`；设置时由驱动内部自动计算。
 */
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

/* rtc_task 需要复用这个结构读取当前时间，因此在头文件中导出。 */
extern rtc_parameter_struct rtc_initpara;

/*
 * 函数作用：
 *   初始化 RTC 外设；若备份域已有有效时间则直接恢复，否则写入默认时间。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   0：表示初始化流程执行完成，且 RTC 当前时间已可读取。
 *  -1：表示首次建表时写入默认 RTC 时间失败。
 */
int bsp_rtc_init(void);

/*
 * 函数作用：
 *   读取当前 RTC 的完整年月日时分秒，并转换成十进制结构体输出。
 * 参数说明：
 *   datetime：输出时间结构体指针，必须非空；成功时写入当前 RTC 快照。
 * 返回值说明：
 *   0：表示读取成功。
 *  -1：表示参数无效，或 RTC 阴影寄存器同步失败导致当前时间不可可靠读取。
 */
int bsp_rtc_get_datetime(bsp_rtc_datetime_t *datetime);

/*
 * 函数作用：
 *   按十进制年月日时分秒设置 RTC 当前时间，并自动计算星期字段。
 * 参数说明：
 *   datetime：输入时间结构体指针，必须非空；年份范围当前约定为 2000~2099。
 * 返回值说明：
 *   0：表示设置成功，RTC 当前时间已经更新。
 *  -1：表示参数无效、RTC 同步失败，或底层 rtc_init() 写入失败。
 */
int bsp_rtc_set_datetime(const bsp_rtc_datetime_t *datetime);

#ifdef __cplusplus
}
#endif

#endif /* BSP_RTC_H */
