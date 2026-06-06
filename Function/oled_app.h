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
 *   y：OLED 显示行号，正式版只使用 0~1 两行。
 *   format：printf 风格格式字符串，后续可变参数必须与格式占位符匹配。
 * 返回值说明：
 *   非负值：vsnprintf 生成的完整字符数。
 *   负值：格式化失败。
 */
int oled_printf(uint8_t x, uint8_t y, const char *format, ...);

/*
 * 函数作用：
 *   清空 OLED 应用层行缓存，强制下一轮 oled_printf 重新写入屏幕内容。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   OLED_Init() 或 OLED_Clear() 会清除物理屏幕，但应用层缓存仍可能保留旧文本。
 *   调用该接口可以避免唤醒恢复后误判“内容未变化”而跳过刷新。
 */
void oled_app_reset_cache(void);

/*
 * 函数作用：
 *   周期性刷新 OLED 双行显示内容。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   第一行显示队伍编号，第二行显示 AutoSample 或 IDLE。
 */
void oled_task(void);

#ifdef __cplusplus
}
#endif

#endif


