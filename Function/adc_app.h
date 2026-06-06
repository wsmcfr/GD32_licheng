#ifndef __ADC_APP_H_
#define __ADC_APP_H_

#include "system_all.h"

/*
 * 函数作用：
 *   获取 CH0（板载电位器）当前原始采样电压浮点值（未乘变比）。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   返回 0~3.3V 范围内的电压值（float），由 ADC 12 位计数按 VREF=3.3V 换算。
 */
float adc_app_get_ch0_raw_float(void);

/*
 * 函数作用：
 *   获取 CH1（DAC 回读，PA4→PC1 跳线）当前原始采样电压浮点值（未乘变比）。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   返回 0~3.3V 范围内的电压值（float）。
 */
float adc_app_get_ch1_raw_float(void);

/*
 * 函数作用：
 *   按赛题 0x0301 控制命令设置 DAC 输出原始值。
 * 参数说明：
 *   raw_value：12 位 DAC 原始值，合法范围为 0~4095；超出范围会被钳制到 4095。
 * 返回值说明：
 *   返回实际写入 DAC 的 12 位原始值。
 */
uint16_t adc_app_set_dac_raw(uint16_t raw_value);

/*
 * 函数作用：
 *   获取当前 DAC 输出原始值缓存。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   返回最近一次通过 adc_app_set_dac_raw() 写入的 12 位 DAC 原始值。
 */
uint16_t adc_app_get_dac_raw(void);

/*
 * 函数作用：
 *   周期性维护 ADC/DAC 应用层状态。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   正式版不再把 ADC CH0 直通 DAC，DAC 只能由赛题控制命令设置。
 */
void adc_task(void);

#endif /* __ADC_APP_H_ */
