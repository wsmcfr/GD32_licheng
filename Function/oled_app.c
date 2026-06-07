#include "oled_app.h"

#define LINE_COUNT     2
#define LINE_BUF_SIZE  32
#define VISIBLE_CHARS  16

/* 每行已显示内容缓存，用于脏字符检测 */
static char g_cache[LINE_COUNT][LINE_BUF_SIZE];

/* 从左找第一个差异位置 */
static uint8_t diff_start(const char *old, const char *new)
{
    uint8_t i;
    for(i = 0; i < VISIBLE_CHARS; i++)
        if(old[i] != new[i]) return i;
    return VISIBLE_CHARS;
}

/* 从右找最后一个差异位置的下一位 */
static uint8_t diff_end(const char *old, const char *new, uint8_t start)
{
    uint8_t i;
    if(start >= VISIBLE_CHARS) return start;
    i = VISIBLE_CHARS;
    while(i > start) 
	{
        i--;
        if(old[i] != new[i]) return (uint8_t)(i + 1);
    }
    return start;
}

void oled_app_reset_cache(void)
{
    memset(g_cache, 0, sizeof(g_cache));
}

int oled_printf(uint8_t x, uint8_t y, const char *format, ...)
{
    char buf[LINE_BUF_SIZE], diff_buf[LINE_BUF_SIZE];
    va_list arg;
    int len;
    uint8_t i, ds, de, dlen, seg_x;

    va_start(arg, format);
    len = vsnprintf(buf, sizeof(buf), format, arg);
    va_end(arg);

    if(y >= LINE_COUNT) return len;

    /* 可见区不足时补空格，覆盖上一帧残留 */
    for(i = 0; i < VISIBLE_CHARS; i++)
        if(buf[i] == '\0') break;
    while(i < VISIBLE_CHARS) buf[i++] = ' ';
    buf[VISIBLE_CHARS] = '\0';

    ds = diff_start(g_cache[y], buf);
    de = diff_end(g_cache[y], buf, ds);
    if(de > ds) 
	{
        dlen  = (uint8_t)(de - ds);
        seg_x = (uint8_t)(x + ds * 8);
        memcpy(diff_buf, &buf[ds], dlen);
        diff_buf[dlen] = '\0';
        /* 16px字体，逻辑行0→物理页0，逻辑行1→物理页2 */
        if(OLED_ShowStr(seg_x, (uint8_t)(y * 2), diff_buf))
		{
            memcpy(&g_cache[y][ds], &buf[ds], dlen);
            g_cache[y][VISIBLE_CHARS] = '\0';
        }
    }

    return len;
}

void oled_task(void)
{
    oled_printf(0, 0, "%s", sts_team_id());
    if(sts_sampling())
        oled_printf(0, 1, "AutoSample");
    else
        oled_printf(0, 1, "IDLE");
}
