/* Licence
* Company: MCUSTUDIO
* Auther: Ahypnis.
* Version: V0.10
* Time: 2025/06/05
* Note:
*/
#include "btn_app.h"

/*
 * 枚举作用：
 *   给每个物理按键分配一个逻辑 ID，供 ebtn 框架内部区分。
 */
typedef enum
{
    USER_BUTTON_0 = 0,
    USER_BUTTON_1,
    USER_BUTTON_2,
    USER_BUTTON_3,
    USER_BUTTON_4,
    USER_BUTTON_5,
    USER_BUTTON_6,
    USER_BUTTON_MAX,
} user_button_t;

/*
 * 变量作用：
 *   定义所有按键共用的默认去抖和点击参数。
 * 说明：
 *   这里使用单击事件为主，保持与当前业务逻辑一致。
 */
static const ebtn_btn_param_t defaul_ebtn_param = EBTN_PARAMS_INIT(20, 0, 20, 1000, 0, 1000, 10);

/*
 * 变量作用：
 *   静态按键对象数组，索引与 user_button_t 一一对应。
 */
static ebtn_btn_t btns[] = {
    EBTN_BUTTON_INIT(USER_BUTTON_0, &defaul_ebtn_param),
    EBTN_BUTTON_INIT(USER_BUTTON_1, &defaul_ebtn_param),
    EBTN_BUTTON_INIT(USER_BUTTON_2, &defaul_ebtn_param),
    EBTN_BUTTON_INIT(USER_BUTTON_3, &defaul_ebtn_param),
    EBTN_BUTTON_INIT(USER_BUTTON_4, &defaul_ebtn_param),
    EBTN_BUTTON_INIT(USER_BUTTON_5, &defaul_ebtn_param),
    EBTN_BUTTON_INIT(USER_BUTTON_6, &defaul_ebtn_param),
};

/*
 * 函数作用：
 *   根据按钮逻辑 ID 读取当前按下状态。
 * 返回值说明：
 *   1 表示当前按键处于按下状态，0 表示未按下。
 */
static uint8_t prv_btn_get_state(struct ebtn_btn *btn)
{
    switch (btn->key_id)
    {
    case USER_BUTTON_0:
        return !KEY1_READ;
    case USER_BUTTON_1:
        return !KEY2_READ;
    case USER_BUTTON_2:
        return !KEY3_READ;
    case USER_BUTTON_3:
        return !KEY4_READ;
    case USER_BUTTON_4:
        return !KEY5_READ;
    case USER_BUTTON_5:
        return !KEY6_READ;
    case USER_BUTTON_6:
        return !KEYW_READ;
    default:
        return 0;
    }
}

/*
 * 函数作用：
 *   处理按键单击事件，并执行对应的 LED 或低功耗动作。
 */
static void prv_btn_event(struct ebtn_btn *btn, ebtn_evt_t evt)
{
    if (evt == EBTN_EVT_ONCLICK)
    {
        switch (btn->key_id)
        {
        case USER_BUTTON_0:
            LED1_TOGGLE;
            break;
        case USER_BUTTON_1:
            LED2_TOGGLE;
            bsp_enter_deepsleep();
            break;
        case USER_BUTTON_2:
            LED3_TOGGLE;
            break;
        case USER_BUTTON_3:
            LED4_TOGGLE;
            break;
        case USER_BUTTON_4:
            LED5_TOGGLE;
            break;
        case USER_BUTTON_5:
            LED6_TOGGLE;
            break;
        case USER_BUTTON_6:
            LED6_TOGGLE;
            break;
        default:
            break;
        }
    }
}

/*
 * 函数作用：
 *   初始化按键事件框架，并注册静态按键数组。
 */
void app_btn_init(void)
{
    ebtn_init(btns, EBTN_ARRAY_SIZE(btns), NULL, 0, prv_btn_get_state, prv_btn_event);
}

/*
 * 函数作用：
 *   周期性推进 ebtn 状态机。
 */
void btn_task(void)
{
    ebtn_process(get_system_ms());
}
