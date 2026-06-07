#include "adc_app.h"

#include "cimc_alarm.h"
#include "cimc_params.h"

#define DAC_RAW_MAX  4095   /* DAC最大值 */
#define VREF_V       3.3f    /* 参考电压 */
#define FULL_SCALE   4095.0f

/* 设置DAC输出，超限钳制。 */
uint16_t adc_app_set_dac_raw(uint16_t raw_value)
{
    uint16_t val = raw_value;

    if(val > DAC_RAW_MAX) 
	{
        val = DAC_RAW_MAX;
    }

    dac_data_set(DAC0, DAC_OUT0, DAC_ALIGN_12B_R, val);

    return val;
}

// CH0原始电压。
float adc_app_get_ch0_raw_float(void)
{
    return (float)adc_value[0] / FULL_SCALE * VREF_V;
}

// CH1原始电压。
float adc_app_get_ch1_raw_float(void)
{
    return (float)adc_value[1] / FULL_SCALE * VREF_V;
}

/* ADC采样告警任务。 */
void adc_task(void)
{
    const params_t *p = params_get();
    float ch0 = adc_app_get_ch0_raw_float() * p->ch0_ratio;
    float ch1 = adc_app_get_ch1_raw_float() * p->ch1_ratio;

    alm_check(0, p->ch0_threshold, ch0);
    alm_check(1, p->ch1_threshold, ch1);
}
