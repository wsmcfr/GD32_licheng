#include "oled_app.h"

#define OLED_APP_LINE_COUNT             4U
#define OLED_APP_LINE_BUFFER_SIZE       32U
#define OLED_APP_VISIBLE_CHARS          16U

/*
 * 变量作用：
 *   缓存 OLED 每一行上一次已经写入的文本内容，用于脏行判断。
 * 说明：
 *   128x32 OLED 使用 8 像素步进字符时每行可显示 16 个字符。缓存按 32 字节保留，
 *   既覆盖当前格式化文本，也显著小于旧版 512 字节栈缓冲。
 */
static char g_oled_line_cache[OLED_APP_LINE_COUNT][OLED_APP_LINE_BUFFER_SIZE];

/*
 * 函数作用：
 *   清空 OLED 应用层行缓存，让下一次 oled_printf 必定刷新对应行。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void oled_app_reset_cache(void)
{
    memset(g_oled_line_cache, 0, sizeof(g_oled_line_cache));
}

/*
 * 函数作用：
 *   使用 printf 风格格式化文本，并把结果显示到 OLED 指定坐标。
 * 主要流程：
 *   1. 使用 vsnprintf 将可变参数格式化到本地缓冲区，避免无界写入。
 *   2. 将显示行裁剪/补齐到当前屏幕一行可见宽度。
 *   3. 与行缓存比较，仅当文本变化时调用 OLED_ShowStr 刷新。
 * 参数说明：
 *   x：OLED 横向像素坐标，当前 128x32 屏建议范围为 0~127。
 *   y：OLED 行号，当前 6x8 字符显示建议范围为 0~3。
 *   format：printf 风格格式字符串，后续可变参数必须与格式占位符匹配。
 * 返回值说明：
 *   非负值：vsnprintf 计算出的格式化字符串长度，不包含结尾 '\0'。
 *   负值：格式化失败，返回值由 vsnprintf 决定。
 */
int oled_printf(uint8_t x, uint8_t y, const char *format, ...)
{
    char buffer[OLED_APP_LINE_BUFFER_SIZE];
    va_list arg;
    int len;
    uint8_t i;

    va_start(arg, format);
    /* 使用行级有界格式化，避免调试显示占用过大的任务栈。 */
    len = vsnprintf(buffer, sizeof(buffer), format, arg);
    va_end(arg);

    if(y >= OLED_APP_LINE_COUNT) {
        return len;
    }

    /*
     * OLED_ShowStr 不会主动清掉上一帧较长字符串残留，因此先把可见区补齐空格。
     * 超过一行的内容按当前 128 像素屏宽裁剪，避免自动换行改写下一页内容。
     */
    for(i = 0U; i < OLED_APP_VISIBLE_CHARS; i++) {
        if('\0' == buffer[i]) {
            break;
        }
    }
    while(i < OLED_APP_VISIBLE_CHARS) {
        buffer[i] = ' ';
        i++;
    }
    buffer[OLED_APP_VISIBLE_CHARS] = '\0';

    if(0 != memcmp(g_oled_line_cache[y], buffer, OLED_APP_VISIBLE_CHARS + 1U)) {
        memcpy(g_oled_line_cache[y], buffer, OLED_APP_VISIBLE_CHARS + 1U);
        OLED_ShowStr(x, y, buffer, 8);
    }

    return len;
}

/*
 * 函数作用：
 *   周期性刷新 OLED 上的按键状态、系统运行时间和 ADC 电压值。
 * 主要流程：
 *   1. 第 1 行显示 6 个普通按键的实时 GPIO 电平。
 *   2. 第 2 行显示系统毫秒时基，便于观察调度器是否运行。
 *   3. 第 3 行把 ADC 原始值换算成电压显示。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void oled_task(void)
{
    oled_printf(0, 0, "KEY STA: %d%d%d%d%d%d", KEY1_READ, KEY2_READ, KEY3_READ, KEY4_READ, KEY5_READ, KEY6_READ);
    oled_printf(0, 1, "uwTick:%lld", (long long)get_system_ms());
    oled_printf(0, 2, "A0:%.2fV V:%.2f", adc_value[0] / 4095.0f * 3.3f, adc_value[1] / 4095.0f * 3.3f);
	
}
