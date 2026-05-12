#include "adc_app.h"

/*
 * 函数作用：
 *   将 ADC 第一个通道的采样结果写入 DAC 输出缓存。
 * 主要流程：
 *   1. 读取驱动层 DMA 持续更新的 adc_value[0]。
 *   2. 写入 convertarr[0]，供 DAC 输出链路使用。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   当前工程只演示最简单的数据直通，因此只更新 convertarr[0]。
 */
void adc_task(void)
{
    convertarr[0] = adc_value[0];
}

