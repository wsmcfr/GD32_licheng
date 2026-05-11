#include "mcu_cmic_gd32f470vet6.h"

extern uint16_t adc_value[2];
extern uint16_t convertarr[CONVERT_NUM];

void adc_task(void)
{
    convertarr[0] = adc_value[0];
}

