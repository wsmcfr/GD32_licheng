#include "led_app.h"

static uint8_t  g_old_mask = 0x00U; /* 上次同步到硬件的LED位图 */
static uint8_t  g_cached   = 0;    /* 缓存是否有效，0=强制刷新 */
static uint32_t g_sys_ms   = 0;    /* LED1上次翻转时刻 */
static uint8_t  g_sys_st   = 0;    /* LED1当前电平 */

/* 按位图写两路LED硬件，changed_mask中置1的位才操作GPIO */
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

/* 构造LED目标位图：bit0=LED1系统灯，bit1=LED2采集灯 */
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

/* 将LED位图同步到硬件，仅变化的位写GPIO */
static void led_refresh(uint8_t mask)
{
    uint8_t changed;

    if(g_cached == 0) 
	{
        /* 首次刷新无可信历史，强制写两路 */
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

/* 低功耗或复位前关闭两路LED */
void led_app_all_off(void)
{
    g_sys_st = 0;
    led_refresh(0);
}

/* 低功耗入口熄灯，并复位刷新缓存，唤醒后首次led_task强制刷新 */
void led_app_blank_for_sleep(void)
{
    g_sys_st = 0;
    write_leds(0, 0x03U);
    led_app_reset_cache();
}

/* 复位LED缓存，下次led_task强制刷新两路 */
void led_app_reset_cache(void)
{
    g_old_mask = 0;
    g_cached   = 0;
}

/* 调度器20ms周期：LED1每1s翻转，LED2跟随采集状态，仅变化时写GPIO */
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
