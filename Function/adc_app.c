#include "adc_app.h"

#include "cimc_alarm.h"
#include "cimc_params.h"
#include "rtc_app.h"

#define DAC_RAW_MAX  4095   /* DAC 12位右对齐最大值 */
#define VREF_V       3.3f    /* GD32F470 VDDA参考电压 */
#define FULL_SCALE   4095.0f

static uint16_t g_dac_raw = 0; /* 最近一次设置的DAC原始值 */

/* 设置DAC输出原始值（0~4095），超范围钳制，返回实际写入值 */
uint16_t adc_app_set_dac_raw(uint16_t raw_value)
{
    uint16_t val = raw_value;

    if(val > DAC_RAW_MAX) 
	{
        val = DAC_RAW_MAX;
    }

    g_dac_raw = val;
    convertarr[0] = val;
    dac_data_set(DAC0, DAC_OUT0, DAC_ALIGN_12B_R, val);

    return val;
}

/* 获取当前DAC目标原始值缓存 */
uint16_t adc_app_get_dac_raw(void)
{
    return g_dac_raw;
}

// CH0（电位器）原始电压，单位V，未乘变比
float adc_app_get_ch0_raw_float(void)
{
    return (float)adc_value[0] / FULL_SCALE * VREF_V;
}

// CH1（DAC回读）原始电压，单位V，未乘变比
float adc_app_get_ch1_raw_float(void)
{
    return (float)adc_value[1] / FULL_SCALE * VREF_V;
}

/* 50ms周期：计算变比后的CH0/CH1并检查阈值告警 */
void adc_task(void)
{
    const params_t *p = params_get();
    float ch0 = adc_app_get_ch0_raw_float() * p->ch0_ratio;
    float ch1 = adc_app_get_ch1_raw_float() * p->ch1_ratio;

    alm_check(0, p->ch0_threshold, ch0);
    alm_check(1, p->ch1_threshold, ch1);
}
