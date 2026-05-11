#ifndef __OLED_APP_H__
#define __OLED_APP_H__

#include "system_all.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 函数作用：
 *   以 printf 风格向 OLED 指定坐标输出字符串。
 */
int oled_printf(uint8_t x, uint8_t y, const char *format, ...);

/*
 * 函数作用：
 *   周期性刷新 OLED 上的按键状态、系统时基和 ADC 电压。
 */
void oled_task(void);

#ifdef __cplusplus
}
#endif

#endif


