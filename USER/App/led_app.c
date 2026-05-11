/* Licence
* Company: MCUSTUDIO
* Auther: Ahypnis.
* Version: V0.10
* Time: 2025/06/05
* Note:
*/
#include "led_app.h"

/*
 * 变量作用：
 *   6 个 LED 的应用层状态缓存。
 */
uint8_t ucLed[6] = {1,0,1,0,1,0};

/**
 * @brief Display or close LEDs by state array.
 * @param ucLed LED state array.
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
        LED1_SET(temp & 0x01);
        LED2_SET(temp & 0x02);
        LED3_SET(temp & 0x04);
        LED4_SET(temp & 0x08);
        LED5_SET(temp & 0x10);
        LED6_SET(temp & 0x20);

        temp_old = temp;
    }
}

/**
 * @brief LED display task.
 */
void led_task(void)
{
    led_disp(ucLed);
}
