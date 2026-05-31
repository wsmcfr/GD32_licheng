#include "led_app.h"

/*
 * 变量作用：
 *   6 个 LED 的应用层状态缓存，索引 0~5 分别对应 LED1~LED6。
 * 说明：
 *   1 表示应用层期望点亮，0 表示应用层期望熄灭；实际高低电平极性由 LED_WRITE 宏统一处理。
 */
uint8_t ucLed[6] = {1,0,1,0,1,0};

/*
 * 宏作用：
 *   定义 LED 应用层处理的灯数量和有效位范围。
 * 说明：
 *   LED_APP_VALID_MASK 的 bit0~bit5 分别对应 LED1~LED6，
 *   用于屏蔽无关高位，避免状态比较受到脏数据影响。
 */
#define LED_APP_COUNT           6U
#define LED_APP_VALID_MASK      0x3fU

/*
 * 变量作用：
 *   保存上一次已经同步到硬件的 LED 逻辑位图。
 * 说明：
 *   该缓存只由 led_app 内部维护。外部模块需要在低功耗或 GPIO 重初始化后调用
 *   led_app_reset_cache()，让下一轮刷新不依赖睡前硬件状态。
 */
static uint8_t g_led_mask_old = 0x00U;

/*
 * 变量作用：
 *   标记 g_led_mask_old 是否已经对应过真实硬件输出。
 * 说明：
 *   上电、唤醒或直接硬件熄灯后该标志必须清零，下一次 led_task() 会强制写入全部 LED。
 */
static uint8_t g_led_cache_valid = 0U;

/*
 * 函数作用：
 *   按位图直接写入 6 路 LED 硬件输出。
 * 参数说明：
 *   led_mask：bit0~bit5 分别表示 LED1~LED6 的目标逻辑状态，1 表示点亮。
 *   changed_mask：bit0~bit5 表示需要实际写 GPIO 的 LED，0 表示该路保持不动。
 * 返回值说明：
 *   无返回值。
 */
static void led_app_write_mask(uint8_t led_mask, uint8_t changed_mask)
{
    if ((changed_mask & 0x01U) != 0U)
    {
        LED1_SET((led_mask & 0x01U) != 0U);
    }

    if ((changed_mask & 0x02U) != 0U)
    {
        LED2_SET((led_mask & 0x02U) != 0U);
    }

    if ((changed_mask & 0x04U) != 0U)
    {
        LED3_SET((led_mask & 0x04U) != 0U);
    }

    if ((changed_mask & 0x08U) != 0U)
    {
        LED4_SET((led_mask & 0x08U) != 0U);
    }

    if ((changed_mask & 0x10U) != 0U)
    {
        LED5_SET((led_mask & 0x10U) != 0U);
    }

    if ((changed_mask & 0x20U) != 0U)
    {
        LED6_SET((led_mask & 0x20U) != 0U);
    }
}

/*
 * 函数作用：
 *   把 ucLed[] 中的 6 路逻辑状态压缩成位图。
 * 参数说明：
 *   led_state：长度至少为 6 的 LED 状态数组，非 0 表示对应 LED 点亮。
 * 返回值说明：
 *   bit0~bit5 分别对应 LED1~LED6 的逻辑状态；输入为空时返回 0。
 */
static uint8_t led_app_build_mask(const uint8_t *led_state)
{
    uint8_t led_mask = 0x00U;
    uint8_t i;

    if (led_state == NULL)
    {
        return 0U;
    }

    /* 这里只压缩应用层逻辑状态，硬件有效电平仍由 LED_SET 宏统一处理。 */
    for (i = 0U; i < LED_APP_COUNT; i++)
    {
        if (led_state[i] != 0U)
        {
            led_mask |= (uint8_t)(1U << i);
        }
    }

    return (uint8_t)(led_mask & LED_APP_VALID_MASK);
}

/*
 * 函数作用：
 *   根据传入的 LED 逻辑状态数组刷新 6 个板载 LED 的实际输出。
 * 主要流程：
 *   1. 检查状态数组指针，避免空指针导致异常访问。
 *   2. 将 6 个逻辑状态压缩成一个位图，便于统一比较和输出。
 *   3. 通过异或计算变化位，只刷新状态发生变化的 LED，减少周期任务中的重复 GPIO 写入。
 * 参数说明：
 *   ucLed：长度至少为 6 的 LED 状态数组，非 0 表示对应 LED 点亮，0 表示熄灭。
 * 返回值说明：
 *   无返回值。
 */
static void led_disp(const uint8_t *ucLed)
{
    uint8_t led_mask;
    uint8_t changed_mask;

    if (ucLed == NULL)
    {
        return;
    }

    led_mask = led_app_build_mask(ucLed);

    /*
     * 首次刷新时还没有可信的历史状态，必须强制写入 6 路 LED。
     * 这样可以覆盖 bsp_led_init() 之后的默认关闭状态，也能避免历史缓存假设影响上电显示。
     */
    if (g_led_cache_valid == 0U)
    {
        changed_mask = LED_APP_VALID_MASK;
        g_led_cache_valid = 1U;
    }
    else
    {
        /* 异或结果中为 1 的位表示该 LED 逻辑状态变化，需要刷新对应 GPIO。 */
        changed_mask = (uint8_t)((led_mask ^ g_led_mask_old) & LED_APP_VALID_MASK);
    }

    if (changed_mask == 0U)
    {
        return;
    }

    led_app_write_mask(led_mask, changed_mask);

    /* 保存本次逻辑位图，作为下一轮调度的变化检测基线。 */
    g_led_mask_old = led_mask;
}

/*
 * 函数作用：
 *   设置单个 LED 的应用层逻辑状态。
 * 参数说明：
 *   led_index：LED 索引，0~5 分别对应 LED1~LED6；超出范围时忽略。
 *   state：非 0 表示点亮，0 表示熄灭。
 * 返回值说明：
 *   无返回值。
 */
void led_app_set(uint8_t led_index, uint8_t state)
{
    if (led_index >= LED_APP_COUNT)
    {
        return;
    }

    ucLed[led_index] = (state != 0U) ? 1U : 0U;
    led_disp(ucLed);
}

/*
 * 函数作用：
 *   翻转单个 LED 的应用层逻辑状态。
 * 参数说明：
 *   led_index：LED 索引，0~5 分别对应 LED1~LED6；超出范围时忽略。
 * 返回值说明：
 *   无返回值。
 */
void led_app_toggle(uint8_t led_index)
{
    if (led_index >= LED_APP_COUNT)
    {
        return;
    }

    ucLed[led_index] = (ucLed[led_index] == 0U) ? 1U : 0U;
    led_disp(ucLed);
}

/*
 * 函数作用：
 *   将所有 LED 的应用层状态设置为熄灭，并立即同步到硬件输出。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void led_app_all_off(void)
{
    uint8_t i;

    for (i = 0U; i < LED_APP_COUNT; i++)
    {
        ucLed[i] = 0U;
    }

    led_disp(ucLed);
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
    uint8_t i;

    for (i = 0U; i < LED_APP_COUNT; i++)
    {
        ucLed[i] = 0U;
    }

    /*
     * 低功耗入口不能完全相信运行态缓存，因为 GPIO 可能刚被板级初始化或异常路径改写。
     * 这里直接强制写 6 路熄灭，再把缓存置为无效，唤醒后下一轮仍会完整重刷。
     */
    led_app_write_mask(0U, LED_APP_VALID_MASK);
    led_app_reset_cache();
}

/*
 * 函数作用：
 *   复位 LED 应用层的硬件刷新缓存，让下一次 led_task() 强制写入 6 路 LED。
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
 *   调度器周期调用的 LED 刷新任务，将应用层 ucLed 状态同步到硬件 LED。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void led_task(void)
{
    led_disp(ucLed);
}
