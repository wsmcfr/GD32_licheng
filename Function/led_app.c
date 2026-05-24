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
    uint8_t led_mask = 0x00;
    uint8_t changed_mask;
    static uint8_t led_mask_old = 0x00;
    static uint8_t led_cache_valid = 0U;

    if (ucLed == NULL)
    {
        return;
    }

    /* 这里按逻辑状态拼出位图，再交给底层宏统一处理电平极性。 */
    for (uint8_t i = 0U; i < LED_APP_COUNT; i++)
    {
        if (ucLed[i] != 0U)
        {
            led_mask |= (uint8_t)(1U << i);
        }
    }

    /* 只保留 LED1~LED6 的有效位，确保后续变化检测只针对实际硬件输出。 */
    led_mask &= LED_APP_VALID_MASK;

    /*
     * 首次刷新时还没有可信的历史状态，必须强制写入 6 路 LED。
     * 这样可以覆盖 bsp_led_init() 之后的默认关闭状态，也能避免历史缓存假设影响上电显示。
     */
    if (led_cache_valid == 0U)
    {
        changed_mask = LED_APP_VALID_MASK;
        led_cache_valid = 1U;
    }
    else
    {
        /* 异或结果中为 1 的位表示该 LED 逻辑状态变化，需要刷新对应 GPIO。 */
        changed_mask = (uint8_t)((led_mask ^ led_mask_old) & LED_APP_VALID_MASK);
    }

    if (changed_mask == 0U)
    {
        return;
    }

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

    /* 保存本次逻辑位图，作为下一轮调度的变化检测基线。 */
    led_mask_old = led_mask;
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
