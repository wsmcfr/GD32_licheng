#ifndef __GD30AD3344_PT100_APP_H_
#define __GD30AD3344_PT100_APP_H_

#include "system_all.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t sample_ready;
    uint8_t range_valid;
    float adc_voltage_v;
    float pt100_voltage_v;
    float resistance_ohm;
    float temperature_c;
} pt100_measurement_t;

void gd30ad3344_pt100_app_init(void);

void gd30ad3344_pt100_task(void);

pt100_measurement_t gd30ad3344_pt100_get_latest(void);

#ifdef __cplusplus
}
#endif

#endif /* __GD30AD3344_PT100_APP_H_ */
