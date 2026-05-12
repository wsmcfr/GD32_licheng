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
 *   根据 ebtn 框架传入的按钮逻辑 ID，读取对应物理按键的当前按下状态。
 * 主要流程：
 *   1. 通过 btn->key_id 区分 7 个按键。
 *   2. 调用板级按键读取宏获取 GPIO 电平。
 *   3. 将低电平按下的硬件语义转换成 ebtn 需要的 1/0 状态。
 * 参数说明：
 *   btn：ebtn 框架维护的按键对象指针，必须指向 btns[] 中的有效对象。
 * 返回值说明：
 *   1：表示当前按键处于按下状态。
 *   0：表示当前按键未按下，或 key_id 不在本工程定义范围内。
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
 *   处理 ebtn 框架派发的按键事件，并把单击事件映射成 LED 翻转或低功耗动作。
 * 主要流程：
 *   1. 仅响应 EBTN_EVT_ONCLICK，忽略当前未使用的长按、连击等事件。
 *   2. 根据 key_id 执行对应 LED 翻转。
 *   3. KEY2 单击后额外进入深度睡眠，用于验证低功耗唤醒流程。
 * 参数说明：
 *   btn：产生事件的按键对象指针，key_id 决定本次事件对应的物理按键。
 *   evt：ebtn 框架派发的事件类型，本函数当前只处理单击事件。
 * 返回值说明：
 *   无返回值。
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
 *   初始化按键事件框架，并注册本模块维护的静态按键数组和回调函数。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void app_btn_init(void)
{
    ebtn_init(btns, EBTN_ARRAY_SIZE(btns), NULL, 0, prv_btn_get_state, prv_btn_event);
}

/*
 * 函数作用：
 *   周期性推进 ebtn 状态机，使按键去抖、点击识别和事件回调持续运行。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void btn_task(void)
{
    ebtn_process(get_system_ms());
}
