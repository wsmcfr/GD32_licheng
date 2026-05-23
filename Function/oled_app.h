#ifndef __OLED_APP_H__
#define __OLED_APP_H__

#include "system_all.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 函数作用：
 *   以 printf 风格向 OLED 指定坐标输出字符串。
 * 参数说明：
 *   x：OLED 横向像素坐标，当前屏幕建议范围为 0~127。
 *   y：OLED 显示行号，当前 6x8 字符建议范围为 0~3。
 *   format：printf 风格格式字符串，后续可变参数必须与格式占位符匹配。
 * 返回值说明：
 *   非负值：vsnprintf 生成的完整字符数。
 *   负值：格式化失败。
 */
int oled_printf(uint8_t x, uint8_t y, const char *format, ...);

/*
 * 函数作用：
 *   周期性刷新 OLED 上的按键状态、系统时基和 ADC 电压。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void oled_task(void);

#ifdef __cplusplus
}
#endif

#endif


