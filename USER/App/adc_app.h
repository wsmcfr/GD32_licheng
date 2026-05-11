#ifndef __ADC_APP_H_
#define __ADC_APP_H_

#include "system_all.h"

/*
 * 函数作用：
 *   周期性同步 ADC 采样值到 DAC 输出缓存。
 */
void adc_task(void);

#endif /* __ADC_APP_H_ */
