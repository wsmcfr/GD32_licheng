#include "adc_app.h"

/*
 * 函数作用：
 *   将 ADC 第一个通道的采样结果写入 DAC 输出缓存。
 * 说明：
 *   当前工程只演示最简单的数据直通，因此只更新 convertarr[0]。
 */
void adc_task(void)
{
    convertarr[0] = adc_value[0];
}

