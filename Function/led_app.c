#include "led_app.h"

/*
 * 变量作用：
 *   保存上一次已经同步到硬件的两个 LED 逻辑位图。
 * 说明：
 *   bit0 对应系统状态灯 LED1，bit1 对应采集工作灯 LED2。
 */
static uint8_t g_led_mask_old = 0x00U;

/*
 * 变量作用：
 *   标记 g_led_mask_old 是否已经对应过真实硬件输出。
 * 说明：
 *   上电、唤醒或直接硬件熄灯后该标志必须清零，下一次 led_task() 会强制写入两个 LED。
 */
static uint8_t g_led_cache_valid = 0U;

/*
 * 变量作用：
 *   LED1 系统状态灯的闪烁节拍缓存。
 * 说明：
 *   赛题要求进入 APP 后以 1s 为单位闪烁，这里用毫秒 timebase 做无阻塞翻转。
 */
static uint32_t g_led_system_last_toggle_ms = 0U;
static uint8_t g_led_system_state = 0U;

/*
 * 函数作用：
 *   按位图直接写入两个正式 LED 硬件输出。
 * 参数说明：
 *   led_mask：bit0 表示 LED1，bit1 表示 LED2，1 表示点亮。
 *   changed_mask：bit0/bit1 表示需要实际写 GPIO 的 LED，0 表示该路保持不动。
 * 返回值说明：
 *   无返回值。
 */
static void led_app_write_mask(uint8_t led_mask, uint8_t changed_mask)
{
    if((changed_mask & 0x01U) != 0U) {
        LED1_SET((led_mask & 0x01U) != 0U);
    }

    if((changed_mask & 0x02U) != 0U) {
        LED2_SET((led_mask & 0x02U) != 0U);
    }
}

/*
 * 函数作用：
 *   根据当前应用状态构造两个正式 LED 的目标位图。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   bit0：系统状态灯 LED1。
 *   bit1：采集工作灯 LED2。
 */
static uint8_t led_app_build_mask(void)
{
    uint8_t led_mask = 0U;

    if(g_led_system_state != 0U) {
        led_mask |= 0x01U;
    }

    if(cimc_status_is_auto_sample_active() != 0U) {
        led_mask |= 0x02U;
    }

    return led_mask;
}

/*
 * 函数作用：
 *   将两个正式 LED 的目标位图同步到硬件。
 * 参数说明：
 *   led_mask：bit0 表示 LED1，bit1 表示 LED2，1 表示点亮。
 * 返回值说明：
 *   无返回值。
 */
static void led_app_refresh(uint8_t led_mask)
{
    uint8_t changed_mask;

    if(g_led_cache_valid == 0U) {
        /*
         * 首次刷新时没有可信历史状态，强制写入两个 LED，覆盖 bsp_led_init() 后的默认关闭状态。
         */
        changed_mask = 0x03U;
        g_led_cache_valid = 1U;
    } else {
        changed_mask = (uint8_t)((led_mask ^ g_led_mask_old) & 0x03U);
    }

    if(changed_mask == 0U) {
        return;
    }

    led_app_write_mask(led_mask, changed_mask);
    g_led_mask_old = led_mask;
}

/*
 * 函数作用：
 *   低功耗或复位前关闭正式版两个 LED 指示灯。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void led_app_all_off(void)
{
    g_led_system_state = 0U;
    led_app_refresh(0U);
}

/*
 * 函数作用：
 *   低功耗入口专用熄灯接口，关闭硬件 LED 并复位 LED 刷新缓存。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void led_app_blank_for_sleep(void)
{
    g_led_system_state = 0U;
    led_app_write_mask(0U, 0x03U);
    led_app_reset_cache();
}

/*
 * 函数作用：
 *   复位 LED 应用层缓存，让下一次 led_task() 强制刷新两个正式指示灯。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void led_app_reset_cache(void)
{
    g_led_mask_old = 0U;
    g_led_cache_valid = 0U;
}

/*
 * 函数作用：
 *   调度器周期调用的 LED 任务。
 * 主要流程：
 *   1. 每 1000ms 翻转一次 LED1 系统状态灯。
 *   2. 根据自动采集状态刷新 LED2 采集工作灯。
 *   3. 仅当目标位图变化时写 GPIO，减少无意义寄存器操作。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void led_task(void)
{
    uint32_t now_ms = timebase_get_ms32();

    if((uint32_t)(now_ms - g_led_system_last_toggle_ms) >= 1000U) {
        g_led_system_last_toggle_ms = now_ms;
        g_led_system_state = (g_led_system_state == 0U) ? 1U : 0U;
    }

    led_app_refresh(led_app_build_mask());
}
