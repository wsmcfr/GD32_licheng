#include "adc_app.h"

#include "cimc_alarm.h"
#include "cimc_params.h"
#include "rtc_app.h"

/* DAC 为 12 位右对齐输出，赛题 0x0301 命令内容范围是 0x0000~0x0FFF。 */
#define ADC_APP_DAC_RAW_MAX    4095U
/* ADC 参考电压：GD32F470 VDDA = 3.3V。 */
#define ADC_APP_VREF_V         3.3f
#define ADC_APP_FULL_SCALE     4095.0f

/*
 * 变量作用：
 *   缓存最近一次按赛题命令设置的 DAC 原始值。
 * 说明：
 *   CH1 回读由 ADC DMA 采样 PA4-PC1 跳线后的电压；该缓存只表示 MCU 下发给 DAC 的目标值。
 */
static uint16_t g_adc_app_dac_raw = 0U;

/*
 * 函数作用：
 *   按赛题 0x0301 控制命令设置 DAC 输出原始值。
 * 主要流程：
 *   1. 将输入值钳制到 12 位 DAC 合法范围 0~4095。
 *   2. 更新应用层 DAC 目标缓存 convertarr[0] 和 g_adc_app_dac_raw。
 *   3. 直接写 DAC0 OUT0 12 位右对齐数据寄存器，使输出立即按命令变化。
 * 参数说明：
 *   raw_value：12 位 DAC 原始值，合法范围为 0~4095；超出范围会被钳制到 4095。
 * 返回值说明：
 *   返回实际写入 DAC 的 12 位原始值。
 */
uint16_t adc_app_set_dac_raw(uint16_t raw_value)
{
    uint16_t limited_value = raw_value;

    if(limited_value > ADC_APP_DAC_RAW_MAX) {
        limited_value = ADC_APP_DAC_RAW_MAX;
    }

    g_adc_app_dac_raw = limited_value;
    convertarr[0] = limited_value;
    dac_data_set(DAC0, DAC_OUT0, DAC_ALIGN_12B_R, limited_value);

    return limited_value;
}

/*
 * 函数作用：
 *   获取当前 DAC 输出原始值缓存。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   返回最近一次通过 adc_app_set_dac_raw() 写入的 12 位 DAC 原始值。
 */
uint16_t adc_app_get_dac_raw(void)
{
    return g_adc_app_dac_raw;
}

/*
 * 函数作用：
 *   周期性维护 ADC/DAC 应用层状态。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   ADC DMA 在驱动层持续刷新 adc_value[]；DAC 输出由 0x0301 命令通过
 *   adc_app_set_dac_raw() 设置。这里不能再把 adc_value[0] 写入 convertarr[0]，
 *   否则电位器会覆盖上位机设置的 DAC 输出，导致 G-01 DAC 联动评分失败。
 */
/*
 * 函数作用：
 *   获取 CH0（电位器）原始电压浮点值（未乘变比）。
 * 返回值：0~3.3V 范围浮点数。
 */
float adc_app_get_ch0_raw_float(void)
{
    return (float)adc_value[0] / ADC_APP_FULL_SCALE * ADC_APP_VREF_V;
}

/*
 * 函数作用：
 *   获取 CH1（DAC 回读）原始电压浮点值（未乘变比）。
 * 返回值：0~3.3V 范围浮点数。
 */
float adc_app_get_ch1_raw_float(void)
{
    return (float)adc_value[1] / ADC_APP_FULL_SCALE * ADC_APP_VREF_V;
}

/*
 * 函数作用：
 *   周期性维护 ADC 应用层状态：计算变比后的 CH0/CH1 值并执行阈值告警检查。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void adc_task(void)
{
    const cimc_params_t *p = cimc_params_get();
    float ch0 = adc_app_get_ch0_raw_float() * p->ch0_ratio;
    float ch1 = adc_app_get_ch1_raw_float() * p->ch1_ratio;

    /*
     * 每次采集后判断是否超过阈值，由 cimc_alarm 模块决定是否记录和主动上报。
     * 0 = CH0（电位器），1 = CH1（DAC 回读）。
     */
    cimc_alarm_check(0U, p->ch0_threshold, ch0);
    cimc_alarm_check(1U, p->ch1_threshold, ch1);
}
