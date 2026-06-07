#ifndef __ADC_APP_H_
#define __ADC_APP_H_

#include "system_all.h"

float    adc_app_get_ch0_raw_float(void);  /* CH0（电位器）原始电压，单位V */
float    adc_app_get_ch1_raw_float(void);  /* CH1（DAC回读）原始电压，单位V */
uint16_t adc_app_set_dac_raw(uint16_t raw_value);  /* 设置DAC输出（0~4095），返回实际值 */
uint16_t adc_app_get_dac_raw(void);               /* 获取当前DAC目标原始值 */
void     adc_task(void);                          /* 50ms周期：计算变比后的CH0/CH1并告警检查 */

#endif /* __ADC_APP_H_ */
