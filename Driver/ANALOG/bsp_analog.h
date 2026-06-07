#ifndef BSP_ANALOG_H
#define BSP_ANALOG_H

#define SYSTEM_ALL_BASE_ONLY
#include "system_all.h"
#undef SYSTEM_ALL_BASE_ONLY

#ifdef __cplusplus
extern "C" {
#endif

/* ADC DMA 刷新采样数组， */
extern __IO uint16_t adc_value[2];

void bsp_adc_init(void);

void bsp_dac_init(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_ANALOG_H */
