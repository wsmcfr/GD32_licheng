#ifndef BSP_ANALOG_H
#define BSP_ANALOG_H

#define SYSTEM_ALL_BASE_ONLY
#include "system_all.h"
#undef SYSTEM_ALL_BASE_ONLY

#ifdef __cplusplus
extern "C" {
#endif

/* ADC 引脚定义。 */
#define ADC1_PORT                       GPIOC
#define ADC1_CLK_PORT                   RCU_GPIOC
#define ADC1_PIN                        GPIO_PIN_0   /* PC0: CH0 电位器输入，对应 ADC_CHANNEL_10 */
#define ADC2_PIN                        GPIO_PIN_1   /* PC1: CH1 DAC 回读，通过 PA4→PC1 跳线与 DAC 输出连通，对应 ADC_CHANNEL_11 */
#define ADC_VREF_PIN                    GPIO_PIN_2   /* PC2: 参考电压引脚，配置为模拟浮空，不参与 ADC 扫描序列 */

/* DAC 引脚定义。 */
#define CONVERT_NUM                     1U
#define DAC1_PORT                       GPIOA
#define DAC1_CLK_PORT                   RCU_GPIOA
#define DAC1_PIN                        GPIO_PIN_4

/* ADC 采样值和 DAC 输出缓存由驱动层统一提供。 */
extern __IO uint16_t adc_value[2];
extern uint16_t convertarr[CONVERT_NUM];

void bsp_adc_init(void);

void bsp_dac_init(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_ANALOG_H */
