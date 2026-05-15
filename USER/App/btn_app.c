#include "btn_app.h"

/*
 * 枚举作用：
 *   定义本模块内部使用的按键位掩码。
 * 说明：
 *   每个按键占用一个独立 bit，便于在简单轮询方案下做按下沿检测，
 *   避免像顺序编号那样在多键切换场景下丢失状态变化。
 */
typedef enum
{
    BTN_KEY1_MASK = (1U << 0),
    BTN_KEY2_MASK = (1U << 1),
    BTN_KEY3_MASK = (1U << 2),
    BTN_KEY4_MASK = (1U << 3),
    BTN_KEY5_MASK = (1U << 4),
    BTN_KEY6_MASK = (1U << 5),
    BTN_KEYW_MASK = (1U << 6),
} btn_key_mask_t;

/*
 * 变量作用：
 *   保存本模块最近一次确认稳定的按键值。
 * 说明：
 *   只要稳定状态发生变化，就会基于该值计算本轮新增的按下沿事件。
 */
static uint8_t g_btn_key_val;

/*
 * 变量作用：
 *   保存本轮检测到的按下沿事件。
 * 说明：
 *   某一位从 0 跳到 1 时，对应 bit 会在本轮被置 1，用于驱动 switch-case 动作分发。
 */
static uint8_t g_btn_key_down;

/*
 * 变量作用：
 *   保存本轮检测到的按键释放沿事件。
 * 说明：
 *   当前业务只响应按下沿，但保留释放沿状态可以让模块逻辑结构与参考实现一致，
 *   后续若要扩展释放动作无需再重构整套扫描骨架。
 */
static uint8_t g_btn_key_up;

/*
 * 变量作用：
 *   保存上一轮确认稳定的按键位图。
 * 说明：
 *   它与当前稳定按键值做异或后，可以得到发生变化的按键集合。
 */
static uint8_t g_btn_key_old;

/*
 * 变量作用：
 *   缓存最近一次采样得到的原始按键值。
 * 说明：
 *   简单轮询仍需要轻量去抖；只有当连续采样值保持一致超过阈值后，
 *   才会更新稳定状态，避免机械抖动触发重复动作。
 */
static uint8_t g_btn_sampled_val;

/*
 * 变量作用：
 *   记录原始采样值已经保持不变的持续时间，单位毫秒。
 */
static uint32_t g_btn_sample_stable_ms;

/*
 * 宏作用：
 *   定义 `btn_task()` 的实际调度周期，单位毫秒。
 * 说明：
 *   该值需要与 `scheduler.c` 中 `btn_task` 的注册周期保持一致，
 *   否则软件去抖累计时间会和真实轮询节拍不一致。
 */
#define BTN_TASK_PERIOD_MS 5U

/*
 * 宏作用：
 *   定义按键稳定判定所需的最小持续时间，单位毫秒。
 * 说明：
 *   当前 `btn_task()` 的调度周期为 5ms，因此 20ms 阈值可以覆盖常见机械按键抖动。
 */
#define BTN_DEBOUNCE_MS 20U

/*
 * 函数作用：
 *   读取全部物理按键当前状态，并编码为按键位图。
 * 主要流程：
 *   1. 逐个读取板级按键输入宏。
 *   2. 将低电平按下语义转换为位掩码。
 *   3. 合并成一个 7 bit 的按键状态值返回给上层轮询逻辑。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   返回当前按下的按键位图；某一位为 1 表示对应按键当前处于按下状态。
 */
static uint8_t prv_btn_read_mask(void)
{
    uint8_t key_mask = 0U;

    if (!KEY1_READ)
    {
        key_mask |= BTN_KEY1_MASK;
    }
    if (!KEY2_READ)
    {
        key_mask |= BTN_KEY2_MASK;
    }
    if (!KEY3_READ)
    {
        key_mask |= BTN_KEY3_MASK;
    }
    if (!KEY4_READ)
    {
        key_mask |= BTN_KEY4_MASK;
    }
    if (!KEY5_READ)
    {
        key_mask |= BTN_KEY5_MASK;
    }
    if (!KEY6_READ)
    {
        key_mask |= BTN_KEY6_MASK;
    }
    if (!KEYW_READ)
    {
        key_mask |= BTN_KEYW_MASK;
    }

    return key_mask;
}

/*
 * 函数作用：
 *   根据稳定的按下沿位图，执行与当前工程一致的按键业务动作。
 * 主要流程：
 *   1. 依次检查每个按键对应的按下沿 bit。
 *   2. 对应 bit 置位时执行 LED 翻转或深度睡眠动作。
 *   3. 保持原先功能映射不变，只替换触发方式。
 * 参数说明：
 *   key_down_mask：本轮确认稳定后新增的按下沿位图。
 * 返回值说明：
 *   无返回值。
 */
static void prv_btn_dispatch(uint8_t key_down_mask)
{
    if ((key_down_mask & BTN_KEY1_MASK) != 0U)
    {
        LED1_TOGGLE;
    }
    if ((key_down_mask & BTN_KEY2_MASK) != 0U)
    {
        LED2_TOGGLE;
        /*
         * 先翻转 LED2 再进入深睡，保持和历史行为一致，
         * 这样用户在按下 KEY2 时仍能先看到一次可见反馈。
         * 该函数会在唤醒恢复后才返回，因此这里直接结束本轮分发，
         * 避免唤醒后继续消费睡前这一次扫描得到的其他按下沿位。
         */
        bsp_enter_deepsleep();
        return;
    }
    if ((key_down_mask & BTN_KEY3_MASK) != 0U)
    {
        LED3_TOGGLE;
    }
    if ((key_down_mask & BTN_KEY4_MASK) != 0U)
    {
        LED4_TOGGLE;
    }
    if ((key_down_mask & BTN_KEY5_MASK) != 0U)
    {
        LED5_TOGGLE;
    }
    if ((key_down_mask & BTN_KEY6_MASK) != 0U)
    {
        LED6_TOGGLE;
    }
    if ((key_down_mask & BTN_KEYW_MASK) != 0U)
    {
        LED6_TOGGLE;
    }
}

/*
 * 函数作用：
 *   初始化按键应用层的本地扫描状态。
 * 主要流程：
 *   1. 读取一次当前原始按键状态，作为初始化基线。
 *   2. 清空按下沿、释放沿和旧状态，避免上电或唤醒后误触发。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void app_btn_init(void)
{
    g_btn_sampled_val = prv_btn_read_mask();
    g_btn_key_val = g_btn_sampled_val;
    g_btn_key_old = g_btn_sampled_val;
    g_btn_key_down = 0U;
    g_btn_key_up = 0U;
    g_btn_sample_stable_ms = 0U;
}

/*
 * 函数作用：
 *   周期性轮询按键状态，并在确认稳定后提取按下沿事件。
 * 主要流程：
 *   1. 读取一次原始按键位图。
 *   2. 只有原始值连续稳定达到去抖阈值后，才更新稳定状态。
 *   3. 基于新旧稳定状态计算按下沿和释放沿。
 *   4. 只对按下沿执行业务动作，避免长按期间重复触发。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void btn_task(void)
{
    uint8_t current_sample = prv_btn_read_mask();

    if (current_sample != g_btn_sampled_val)
    {
        /*
         * 原始采样值一旦发生变化，就重新开始计时。
         * 这样只有在新状态保持稳定足够久后，才会把它升级为有效状态。
         */
        g_btn_sampled_val = current_sample;
        g_btn_sample_stable_ms = 0U;
        return;
    }

    if (g_btn_sample_stable_ms < BTN_DEBOUNCE_MS)
    {
        g_btn_sample_stable_ms += BTN_TASK_PERIOD_MS;
        if (g_btn_sample_stable_ms < BTN_DEBOUNCE_MS)
        {
            return;
        }
    }

    if (g_btn_key_val != current_sample)
    {
        g_btn_key_val = current_sample;
        g_btn_key_down = g_btn_key_val & (uint8_t)(g_btn_key_old ^ g_btn_key_val);
        g_btn_key_up = (uint8_t)(~g_btn_key_val) & (uint8_t)(g_btn_key_old ^ g_btn_key_val);
        g_btn_key_old = g_btn_key_val;

        if (g_btn_key_down != 0U)
        {
            prv_btn_dispatch(g_btn_key_down);
        }
    }
}
