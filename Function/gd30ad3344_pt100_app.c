#include "gd30ad3344_pt100_app.h"

/*
 * 宏作用：
 *   指定 PT100 调理板输出接入 GD30AD3344 的默认通道。
 * 说明：
 *   当前采样板由仪表放大器输出单端电压，因此默认读取 AIN0~GND。
 */
#define PT100_ADC_CHANNEL              GD30AD3344_Channel_4

/*
 * 宏作用：
 *   指定 GD30AD3344 的默认量程。
 * 说明：
 *   商业版 PT100 调理模块的 Vout 带约 0.9617V 偏置，常用温区输出落在
 *   ±4.096V 量程的有效区间内，同时给异常高输出保留余量。
 */
#define PT100_ADC_PGA                  GD30AD3344_PGA_4V096

/*
 * 宏作用：
 *   商业版 PT100 调理模块在 0Ω 等效输入附近的输出偏置，单位 V。
 * 说明：
 *   用户实测标定公式为 R测=(Vout-0.9617)/0.001957，这里把 0.9617V
 *   单独命名，便于后续按实测两点标定重新修正。
 */
#define PT100_COMMERCIAL_OFFSET_V      0.9617f

/*
 * 宏作用：
 *   商业版 PT100 调理模块输出电压相对 PT100 电阻的斜率，单位 V/Ω。
 * 说明：
 *   该值来自用户给出的商业版模块公式 R测=(Vout-0.9617)/0.001957，
 *   表示电阻每增加 1Ω，模块输出约增加 1.957mV。
 */
#define PT100_COMMERCIAL_RESISTANCE_SLOPE_V_PER_OHM 0.001957f

/*
 * 宏作用：
 *   商业版 PT100 调理模块从电阻换算到温度的线性增益，单位 ℃/Ω。
 * 说明：
 *   用户给出的公式为 T≈2.635*R测-263.5，适合当前商业版模块的现场标定。
 */
#define PT100_COMMERCIAL_TEMPERATURE_GAIN 2.635f

/*
 * 宏作用：
 *   商业版 PT100 调理模块线性温度公式中的常量偏移，单位 ℃。
 * 说明：
 *   公式写作 T≈2.635*R测-263.5，因此实现中会减去该偏移值。
 */
#define PT100_COMMERCIAL_TEMPERATURE_OFFSET_C 263.5f

/* 本应用按题目要求覆盖的最低温度，单位 ℃。 */
#define PT100_TEMPERATURE_MIN_C        (-50.0f)

/* 本应用按题目要求覆盖的最高温度，单位 ℃。 */
#define PT100_TEMPERATURE_MAX_C        150.0f

/*
 * 变量作用：
 *   缓存最近一次 PT100 测量结果。
 * 说明：
 *   调度任务负责更新该缓存，显示、串口或其他应用逻辑通过 getter 读取结构体副本。
 */
static pt100_measurement_t s_pt100_latest;

/*
 * 变量作用：
 *   标记下一次采样是否只用于刷新 GD30AD3344 通道/量程配置。
 * 说明：
 *   GD30AD3344 切换 MUX/PGA 后，首帧可能对应旧转换结果；固定通道应用启动后也先
 *   丢弃一次，避免上电初始化过程中的旧数据直接参与温度显示。
 */
static uint8_t s_pt100_discard_next_sample = 1U;

/*
 * 函数作用：
 *   判断商业版模块线性公式换算出的温度是否落在应用支持范围内。
 * 参数说明：
 *   temperature_c：由商业版模块线性公式得到的温度，单位 ℃。
 *   range_valid：输出参数；为 1 表示温度位于 -50℃~150℃，为 0 表示越界。
 * 返回值说明：
 *   返回限制后的温度；低于下限时返回 -50℃，高于上限时返回 150℃。
 */
static float prv_pt100_clamp_temperature(float temperature_c, uint8_t *range_valid)
{
    if(NULL != range_valid) {
        *range_valid = 1U;
    }

    if(temperature_c <= PT100_TEMPERATURE_MIN_C) {
        /*
         * 商业版模块公式是现场线性标定，超出应用温区时只发布边界值，
         * 并通过 range_valid 提醒上层不要把边界值当作精确温度。
         */
        if(NULL != range_valid) {
            *range_valid = 0U;
        }
        return PT100_TEMPERATURE_MIN_C;
    }

    if(temperature_c >= PT100_TEMPERATURE_MAX_C) {
        /*
         * 高于题目/应用温区时同样限制到上边界，便于 OLED 或串口显示保持稳定。
         */
        if(NULL != range_valid) {
            *range_valid = 0U;
        }
        return PT100_TEMPERATURE_MAX_C;
    }

    return temperature_c;
}

/*
 * 函数作用：
 *   初始化 GD30AD3344 PT100 应用层缓存和首帧丢弃状态。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void gd30ad3344_pt100_app_init(void)
{
    memset(&s_pt100_latest, 0, sizeof(s_pt100_latest));
    s_pt100_discard_next_sample = 1U;
}

/*
 * 函数作用：
 *   周期性读取 GD30AD3344 的 AIN0~GND 电压，并换算成 PT100 电阻和温度。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void gd30ad3344_pt100_task(void)
{
    float adc_voltage_v;
    float module_signal_v;
    float resistance_ohm;
    float temperature_c;
    uint8_t range_valid = 0U;

    adc_voltage_v = GD30AD3344_AD_Read(PT100_ADC_CHANNEL, PT100_ADC_PGA);

    if(0U != s_pt100_discard_next_sample) {
        /*
         * 首帧只用于把 ADC 内部 MUX/PGA 推到目标配置，下一周期再发布测量值。
         */
        s_pt100_discard_next_sample = 0U;
        return;
    }

    /*
     * GD30AD3344 读到的是商业版 PT100 模块的 Vout，先扣除模块零点偏置，
     * 再按用户实测斜率反算电阻，避免沿用工业版 16 倍放大链路导致温度偏高。
     */
    module_signal_v = adc_voltage_v - PT100_COMMERCIAL_OFFSET_V;
    resistance_ohm = module_signal_v / PT100_COMMERCIAL_RESISTANCE_SLOPE_V_PER_OHM;
    temperature_c = (PT100_COMMERCIAL_TEMPERATURE_GAIN * resistance_ohm) -
                    PT100_COMMERCIAL_TEMPERATURE_OFFSET_C;

    s_pt100_latest.adc_voltage_v = adc_voltage_v;
    s_pt100_latest.pt100_voltage_v = module_signal_v;
    s_pt100_latest.resistance_ohm = resistance_ohm;
    s_pt100_latest.temperature_c = prv_pt100_clamp_temperature(temperature_c, &range_valid);
    s_pt100_latest.range_valid = range_valid;
    s_pt100_latest.sample_ready = 1U;

    /*
     * 任务末尾把本次换算结果发到 USART0 调试口，便于现场直接观察商业版模块
     * 标定公式的输出效果；valid=0 表示温度已经被限制到应用边界。
     */
    my_printf(DEBUG_USART,
              "PT100: Vout=%.4fV signal=%.4fV R=%.2fohm T=%.2fC valid=%u\r\n",
              s_pt100_latest.adc_voltage_v,
              s_pt100_latest.pt100_voltage_v,
              s_pt100_latest.resistance_ohm,
              s_pt100_latest.temperature_c,
              s_pt100_latest.range_valid);
}

/*
 * 函数作用：
 *   获取最近一次 PT100 采样和温度换算结果。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   返回测量结果结构体副本；如果 sample_ready 为 0，表示当前尚无可用采样。
 */
pt100_measurement_t gd30ad3344_pt100_get_latest(void)
{
    return s_pt100_latest;
}
