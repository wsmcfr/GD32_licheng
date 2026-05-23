#include "led_app.h"

/*
 * 变量作用：
 *   6 个 LED 的应用层状态缓存，索引 0~5 分别对应 LED1~LED6。
 * 说明：
 *   1 表示应用层期望点亮，0 表示应用层期望熄灭；实际高低电平极性由 LED_WRITE 宏统一处理。
 */
uint8_t ucLed[6] = {1,0,1,0,1,0};

/*
 * 函数作用：
 *   根据传入的 LED 逻辑状态数组刷新 6 个板载 LED 的实际输出。
 * 主要流程：
 *   1. 检查状态数组指针，避免空指针导致异常访问。
 *   2. 将 6 个逻辑状态压缩成一个位图，便于统一比较和输出。
 *   3. 仅在位图发生变化时写 GPIO，减少周期任务中的重复寄存器操作。
 * 参数说明：
 *   ucLed：长度至少为 6 的 LED 状态数组，非 0 表示对应 LED 点亮，0 表示熄灭。
 * 返回值说明：
 *   无返回值。
 */
static void led_disp(uint8_t *ucLed)
{
    uint8_t temp = 0x00;
    static uint8_t temp_old = 0xff;

    if (ucLed == NULL)
    {
        return;
    }

    /* 这里按逻辑状态拼出位图，再交给底层宏统一处理电平极性。 */
    for (int i = 0; i < 6; i++)
    {
        if (ucLed[i])
        {
            temp |= (1 << i);
        }
    }

    if (temp_old != temp)
    {
        /* 只有状态变化时才写 GPIO，避免高频调度下反复刷新相同 LED 电平。 */
        LED1_SET(temp & 0x01);
        LED2_SET(temp & 0x02);
        LED3_SET(temp & 0x04);
        LED4_SET(temp & 0x08);
        LED5_SET(temp & 0x10);
        LED6_SET(temp & 0x20);

        temp_old = temp;
    }
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
