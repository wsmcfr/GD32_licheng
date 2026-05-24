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
 *   1mA 激励、R5=6.49kΩ 对应约 16.41 倍前端增益，-50℃~150℃ 输出约
 *   1.32V~2.58V，落在 ±4.096V 量程的有效区间内。
 */
#define PT100_ADC_PGA                  GD30AD3344_PGA_4V096

/*
 * 宏作用：
 *   PT100 恒流源激励电流，单位 A。
 * 说明：
 *   该值来自当前硬件设计目标 1mA；若实际标定电流不同，应同步修改本值和文档。
 */
#define PT100_EXCITATION_CURRENT_A     0.001f

/*
 * 宏作用：
 *   PT100 前端仪表放大器电压增益。
 * 说明：
 *   U3 采用 R5=6.49kΩ 作为增益电阻，按 INA333/NA333 兼容公式
 *   G=1+100kΩ/RG 得到约 16.41。
 */
#define PT100_FRONTEND_GAIN            16.41f

/* PT100 在 0℃ 时的标称电阻，单位 Ω。 */
#define PT100_R0_OHM                   100.0f

/* PT100 Callendar-Van Dusen A 系数。 */
#define PT100_CVD_A                    3.9083e-3f

/* PT100 Callendar-Van Dusen B 系数。 */
#define PT100_CVD_B                    (-5.775e-7f)

/* PT100 Callendar-Van Dusen C 系数，仅在 0℃ 以下参与计算。 */
#define PT100_CVD_C                    (-4.183e-12f)

/* 本应用按题目要求覆盖的最低温度，单位 ℃。 */
#define PT100_TEMPERATURE_MIN_C        (-50.0f)

/* 本应用按题目要求覆盖的最高温度，单位 ℃。 */
#define PT100_TEMPERATURE_MAX_C        150.0f

/* 二分法求解温度的迭代次数，24 次足够让 200℃ 区间收敛到远小于 0.01℃。 */
#define PT100_SOLVE_ITERATIONS         24U

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
 *   根据给定温度计算 PT100 理论电阻。
 * 参数说明：
 *   temperature_c：待换算的温度，单位 ℃，本应用主要使用 -50℃~150℃。
 * 返回值说明：
 *   返回该温度下 PT100 的理论电阻，单位 Ω。
 */
static float prv_pt100_temperature_to_resistance(float temperature_c)
{
    float resistance_ratio;

    resistance_ratio = 1.0f +
                       (PT100_CVD_A * temperature_c) +
                       (PT100_CVD_B * temperature_c * temperature_c);

    if(temperature_c < 0.0f) {
        /*
         * 0℃ 以下需要加入 C 项，避免负温区简单二次模型带来系统误差。
         * 题目范围最低到 -50℃，该分支会覆盖低温测试点。
         */
        resistance_ratio += PT100_CVD_C *
                            (temperature_c - 100.0f) *
                            temperature_c *
                            temperature_c *
                            temperature_c;
    }

    return PT100_R0_OHM * resistance_ratio;
}

/*
 * 函数作用：
 *   将 PT100 电阻反算成温度。
 * 参数说明：
 *   resistance_ohm：由 ADC 电压、前端增益和激励电流反算出的 PT100 电阻，单位 Ω。
 *   range_valid：输出参数；为 1 表示输入电阻落在 -50℃~150℃ 理论范围内，为 0 表示越界。
 * 返回值说明：
 *   返回二分求解得到的温度，单位 ℃；越界时返回被限制在边界内的估算温度。
 */
static float prv_pt100_resistance_to_temperature(float resistance_ohm, uint8_t *range_valid)
{
    uint8_t i;
    float low_c = PT100_TEMPERATURE_MIN_C;
    float high_c = PT100_TEMPERATURE_MAX_C;
    float mid_c;
    float min_resistance;
    float max_resistance;

    min_resistance = prv_pt100_temperature_to_resistance(PT100_TEMPERATURE_MIN_C);
    max_resistance = prv_pt100_temperature_to_resistance(PT100_TEMPERATURE_MAX_C);

    if(NULL != range_valid) {
        *range_valid = 1U;
    }

    if(resistance_ohm <= min_resistance) {
        /*
         * 电阻低于 -50℃ 理论值时仍返回边界温度，调用方可通过 range_valid
         * 识别断线、短路、激励电流偏差或测试电阻越界等异常。
         */
        if(NULL != range_valid) {
            *range_valid = 0U;
        }
        return PT100_TEMPERATURE_MIN_C;
    }

    if(resistance_ohm >= max_resistance) {
        /*
         * 电阻高于 150℃ 理论值时返回上边界，避免上层误把越界值当成精确温度。
         */
        if(NULL != range_valid) {
            *range_valid = 0U;
        }
        return PT100_TEMPERATURE_MAX_C;
    }

    for(i = 0U; i < PT100_SOLVE_ITERATIONS; i++) {
        mid_c = (low_c + high_c) * 0.5f;

        /*
         * PT100 在该温区内电阻随温度单调增加，因此可以用电阻大小决定二分方向。
         */
        if(prv_pt100_temperature_to_resistance(mid_c) < resistance_ohm) {
            low_c = mid_c;
        } else {
            high_c = mid_c;
        }
    }

    return (low_c + high_c) * 0.5f;
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
    float pt100_voltage_v;
    float resistance_ohm;
    uint8_t range_valid = 0U;

    adc_voltage_v = GD30AD3344_AD_Read(PT100_ADC_CHANNEL, PT100_ADC_PGA);

    if(0U != s_pt100_discard_next_sample) {
        /*
         * 首帧只用于把 ADC 内部 MUX/PGA 推到目标配置，下一周期再发布测量值。
         */
        s_pt100_discard_next_sample = 0U;
        return;
    }

    pt100_voltage_v = adc_voltage_v / PT100_FRONTEND_GAIN;
    resistance_ohm = pt100_voltage_v / PT100_EXCITATION_CURRENT_A;

    s_pt100_latest.adc_voltage_v = adc_voltage_v;
    s_pt100_latest.pt100_voltage_v = pt100_voltage_v;
    s_pt100_latest.resistance_ohm = resistance_ohm;
    s_pt100_latest.temperature_c = prv_pt100_resistance_to_temperature(resistance_ohm, &range_valid);
    s_pt100_latest.range_valid = range_valid;
    s_pt100_latest.sample_ready = 1U;
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
