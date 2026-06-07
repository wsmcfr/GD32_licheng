#ifndef SYSTEM_ALL_H
#define SYSTEM_ALL_H

/* 工程统一公共聚合头文件：集中包含基础库及各层模块头文件，各模块.h通过此文件获得公共依赖。 */

#include "gd32f4xx.h"
#include "boot_app_config.h"
#include "systick.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

/* Driver/Protocol层用SYSTEM_ALL_BASE_ONLY只展开基础依赖；复制为内部标记防止内层包含时被撤销。 */
#if defined(SYSTEM_ALL_BASE_ONLY)
#define SYSTEM_ALL_SKIP_UPPER_LAYERS
#endif

// Driver/Component/Protocol/App 层按序聚合

/* Driver 层头文件。 */
#include "bsp_led.h"
#include "bsp_oled.h"
#include "bsp_usart.h"
#include "bsp_analog.h"
#include "bsp_rtc.h"
#include "bootloader_port.h"

/* Component 层头文件。 */
#include "gd30ad3344.h"
#include "oled.h"

/* Protocol 层头文件。 */
#include "cimc_protocol.h"

#if !defined(SYSTEM_ALL_SKIP_UPPER_LAYERS)
/* App 层头文件。 */
#include "adc_app.h"
#include "cimc_alarm.h"
#include "cimc_params.h"
#include "cimc_power_app.h"
#include "cimc_status.h"
#include "gd30ad3344_pt100_app.h"
#include "led_app.h"
#include "oled_app.h"
#include "rtc_app.h"
#include "usart_app.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
}
#endif

#if defined(SYSTEM_ALL_SKIP_UPPER_LAYERS)
#undef SYSTEM_ALL_SKIP_UPPER_LAYERS
#endif

#endif /* SYSTEM_ALL_H */
