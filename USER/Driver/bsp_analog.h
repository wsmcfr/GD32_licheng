#ifndef BSP_ANALOG_H
#define BSP_ANALOG_H

/*
 * 文件作用：
 *   定义 ADC/DAC 相关引脚资源、共享数据缓冲区以及初始化接口。
 */

#define SYSTEM_ALL_BASE_ONLY
#include "system_all.h"
#undef SYSTEM_ALL_BASE_ONLY

#ifdef __cplusplus
extern "C" {
#endif

/* ADC 引脚定义。 */
#define ADC1_PORT                       GPIOC
#define ADC1_CLK_PORT                   RCU_GPIOC
#define ADC1_PIN                        GPIO_PIN_0
#define ADC_VREF_PIN                    GPIO_PIN_2

/* DAC 引脚定义。 */
#define CONVERT_NUM                     1U
#define DAC0_R12DH_ADDRESS              0x40007408U
#define DAC1_PORT                       GPIOA
#define DAC1_CLK_PORT                   RCU_GPIOA
#define DAC1_PIN                        GPIO_PIN_4

/* ADC 采样值和 DAC 输出缓存由驱动层统一提供。 */
extern uint16_t adc_value[2];
extern uint16_t convertarr[CONVERT_NUM];

/*
 * 函数作用：
 *   初始化 ADC0 双通道采样及 DMA 循环搬运。
 */
void bsp_adc_init(void);

/*
 * 函数作用：
 *   初始化 DAC0 通道 0 以及触发它的 TIMER5。
 */
void bsp_dac_init(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_ANALOG_H */
