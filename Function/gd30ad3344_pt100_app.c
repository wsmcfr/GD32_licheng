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
 *   商业版 PT100 调理模块的 Vout 带约 0.94235185V 偏置，常用温区输出落在
 *   ±4.096V 量程的有效区间内，同时给异常高输出保留余量。
 */
#define PT100_ADC_PGA                  GD30AD3344_PGA_4V096

/*
 * 宏作用：
 *   商业版 PT100 调理模块在 0Ω 等效输入附近的输出偏置，单位 V。
 * 说明：
 *   用户按固件 ADC 读数 100Ω->1.1355V、154Ω->1.2398V 两点重新标定得到
 *   R测=(Vout-0.94235185)/0.0019314815，这里把零点偏置单独命名，
 *   避免万用表 TP4 电压与 ADC 换算电压之间的毫伏级差异继续带入电阻计算。
 */
#define PT100_COMMERCIAL_OFFSET_V      0.94235185f

/*
 * 宏作用：
 *   商业版 PT100 调理模块输出电压相对 PT100 电阻的斜率，单位 V/Ω。
 * 说明：
 *   该值来自用户按固件 ADC 读数中的 100Ω 和 154Ω 两个点重新计算的公式，
 *   表示电阻每增加 1Ω，固件 Vout 读数约增加 1.9314815mV。
 */
#define PT100_COMMERCIAL_RESISTANCE_SLOPE_V_PER_OHM 0.0019314815f

/* 本应用按题目要求覆盖的最低温度，单位 ℃。 */
#define PT100_TEMPERATURE_MIN_C        (-50.0f)

/* 本应用按题目要求覆盖的最高温度，单位 ℃。 */
#define PT100_TEMPERATURE_MAX_C        150.0f

/*
 * 宏作用：
 *   计算测试板电阻-温度标定表的元素个数。
 * 说明：
 *   标定表只在本文件内部使用，计数宏集中定义可以避免插值循环手写表长度。
 */
#define PT100_TEMPERATURE_TABLE_COUNT  \
    (sizeof(s_pt100_temperature_table) / sizeof(s_pt100_temperature_table[0]))

/*
 * 结构体作用：
 *   描述测试板一个已知电阻点和它对应的标称温度。
 * 成员说明：
 *   resistance_ohm：测试板丝印或实测采用的标称电阻，单位 Ω，表内必须按升序排列。
 *   temperature_c：该电阻在测试板上标注的理论温度，单位 ℃。
 */
typedef struct {
    float resistance_ohm;
    float temperature_c;
} pt100_calibration_point_t;

/*
 * 表作用：
 *   保存测试板可切换电阻点对应的温度，用于把反算电阻转换成更贴合测试板的温度。
 * 说明：
 *   这些点来自测试板丝印/原理图。任务先用两点标定把 Vout 反算成电阻，
 *   再在本表中做分段线性插值；这样比单条 T=2.635*R-263.5 直线更贴合全量程。
 */
static const pt100_calibration_point_t s_pt100_temperature_table[] = {
    {80.6f,  -49.27f},
    {82.5f,  -44.49f},
    {100.0f,   0.0f},
    {107.79f, 20.0f},
    {113.0f,  33.44f},
    {115.0f,  38.61f},
    {115.54f, 40.0f},
    {123.24f, 60.0f},
    {130.90f, 80.0f},
    {138.51f, 100.0f},
    {150.0f,  130.45f},
    {154.0f,  141.11f},
};

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
 *   根据测试板电阻-温度标定表，把 PT100 电阻换算为温度。
 * 主要流程：
 *   1. 若电阻低于或等于表首点，返回表首温度，后续统一限幅。
 *   2. 若电阻高于或等于表尾点，返回表尾温度，避免外推放大误差。
 *   3. 在相邻两个标定点之间执行线性插值，得到更贴合测试板丝印的温度。
 * 参数说明：
 *   resistance_ohm：由 Vout 两点标定公式反算出的 PT100 电阻，单位 Ω。
 * 返回值说明：
 *   返回插值后的温度，单位 ℃；超出表范围时返回最近边界标定温度。
 */
static float prv_pt100_resistance_to_temperature(float resistance_ohm)
{
    uint32_t index;

    if(resistance_ohm <= s_pt100_temperature_table[0].resistance_ohm) {
        return s_pt100_temperature_table[0].temperature_c;
    }

    for(index = 1U; index < PT100_TEMPERATURE_TABLE_COUNT; index++) {
        const pt100_calibration_point_t *lower = &s_pt100_temperature_table[index - 1U];
        const pt100_calibration_point_t *upper = &s_pt100_temperature_table[index];

        if(resistance_ohm <= upper->resistance_ohm) {
            float span_ohm = upper->resistance_ohm - lower->resistance_ohm;
            float position = (resistance_ohm - lower->resistance_ohm) / span_ohm;

            /*
             * 测试板标定点不是完全等间隔，使用相邻点插值可以把每个旋钮档位附近的
             * 温度误差限制在局部区间，而不是让单条全局直线牵连整个量程。
             */
            return lower->temperature_c +
                   (position * (upper->temperature_c - lower->temperature_c));
        }
    }

    return s_pt100_temperature_table[PT100_TEMPERATURE_TABLE_COUNT - 1U].temperature_c;
}

/*
 * 函数作用：
 *   判断测试板分段插值换算出的温度是否落在应用支持范围内。
 * 参数说明：
 *   temperature_c：由测试板电阻-温度表分段插值得到的温度，单位 ℃。
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
         * 测试板插值仍可能遇到表外电阻，超出应用温区时只发布边界值，
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

    if(0 != GD30AD3344_AD_Read(PT100_ADC_CHANNEL, PT100_ADC_PGA, &adc_voltage_v)) {
        /*
         * SPI/DMA 异常时不能把 0xFFFF 等失败哨兵值换算成满量程温度。
         * 这里保留上一帧数值，但显式清除可用标志，提示显示/上报层本轮采样无效。
         */
        s_pt100_latest.sample_ready = 0U;
        s_pt100_latest.range_valid = 0U;
        return;
    }

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
    temperature_c = prv_pt100_resistance_to_temperature(resistance_ohm);

    s_pt100_latest.adc_voltage_v = adc_voltage_v;
    s_pt100_latest.pt100_voltage_v = module_signal_v;
    s_pt100_latest.resistance_ohm = resistance_ohm;
    s_pt100_latest.temperature_c = prv_pt100_clamp_temperature(temperature_c, &range_valid);
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
