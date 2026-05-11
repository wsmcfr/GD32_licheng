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

/* rtc_task 需要复用这个结构读取当前时间，因此在头文件中导出。 */
extern rtc_parameter_struct rtc_initpara;

/*
 * 函数作用：
 *   初始化 RTC 外设并设置默认时间。
 * 返回值说明：
 *   0  表示初始化流程执行完成。
 *  -1  表示写入默认时间失败。
 */
int bsp_rtc_init(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_RTC_H */
