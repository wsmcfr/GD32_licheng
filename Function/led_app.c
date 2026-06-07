#include "led_app.h"

static uint8_t  g_old_mask = 0x00U; /* 上次位图 */
static uint8_t  g_cached   = 0;     /* 缓存有效 */
static uint32_t g_sys_ms   = 0;     /* LED1计时 */
static uint8_t  g_sys_st   = 0;     /* LED1状态 */

/* 按变化位写LED。 */
static void write_leds(uint8_t mask, uint8_t changed)
{
    if((changed & 0x01U) != 0) 
	{
        LED1_SET((mask & 0x01U) != 0);
    }

    if((changed & 0x02U) != 0) 
	{
        LED2_SET((mask & 0x02U) != 0);
    }
}

/* 生成LED位图。 */
static uint8_t build_mask(void)
{
    uint8_t mask = 0;

    if(g_sys_st != 0) 
	{
        mask |= 0x01U;
    }

    if(sts_sampling() != 0) 
	{
        mask |= 0x02U;
    }

    return mask;
}

/* 同步LED位图。 */
static void led_refresh(uint8_t mask)
{
    uint8_t changed;

    if(g_cached == 0) 
	{
        /* 首次强制写两路。 */
        changed = 0x03U;
        g_cached = 1;
    } 
	else 
	{
        changed = (uint8_t)((mask ^ g_old_mask) & 0x03U);
    }

    if(changed == 0) 
	{
        return;
    }

    write_leds(mask, changed);
    g_old_mask = mask;
}

/* 睡眠前熄灯并复位缓存。 */
void led_app_blank_for_sleep(void)
{
    g_sys_st = 0;
    write_leds(0, 0x03U);
    led_app_reset_cache();
}

/* 复位LED缓存。 */
void led_app_reset_cache(void)
{
    g_old_mask = 0;
    g_cached   = 0;
}

/* LED周期任务。 */
void led_task(void)
{
    uint32_t now = timebase_get_ms32();

    if((uint32_t)(now - g_sys_ms) >= 1000) 
	{
        g_sys_ms = now;
        g_sys_st = (g_sys_st == 0) ? 1 : 0;
    }

    led_refresh(build_mask());
}
