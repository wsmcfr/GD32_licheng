#ifndef __ADC_APP_H_
#define __ADC_APP_H_

#include "system_all.h"

float    adc_app_get_ch0_raw_float(void); /* CH0电压 */
float    adc_app_get_ch1_raw_float(void); /* CH1电压 */
uint16_t adc_app_set_dac_raw(uint16_t raw_value); /* 设置DAC */
void     adc_task(void); /* ADC任务 */

#endif /* __ADC_APP_H_ */
