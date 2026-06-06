#ifndef SYSTEM_ALL_H
#define SYSTEM_ALL_H

/*
 * 文件作用：
 *   这是工程统一使用的公共聚合头文件。
 *   它负责集中包含基础库头文件，以及 App / Driver / Component 常用模块头文件。
 *
 * 设计原则：
 *   1. 各个 .c 文件不直接包含本文件，而是只包含自己的 .h 文件。
 *   2. 各个模块 .h 统一包含本文件，从这里获得公共依赖。
 *   3. 模块自己的宏、引脚映射、函数声明，仍然保留在各自模块头文件中。
 */

#include "gd32f4xx.h"
#include "boot_app_config.h"
#include "systick.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

/*
 * 说明：
 *   Driver/Protocol 头文件会用 SYSTEM_ALL_BASE_ONLY 请求只展开基础依赖。
 *   由于 Driver 头之间会互相包含 system_all.h，临时宏可能被内层头文件撤销，
 *   所以在聚合开始时复制一份内部标记，保证本轮 system_all.h 展开期间判断稳定。
 */
#if defined(SYSTEM_ALL_BASE_ONLY)
#define SYSTEM_ALL_SKIP_UPPER_LAYERS
#endif

/*
 * 说明：
 *   以下头文件按“基础组件 -> Driver -> Component -> Protocol -> App”顺序聚合，
 *   这样上层头文件只需要包含 system_all.h 即可获得所需声明。
 *   Driver 层头文件在自身内部包含本文件时，会定义 SYSTEM_ALL_BASE_ONLY，
 *   从而跳过 Protocol 和 App 层聚合，避免 Driver 头尚未完成定义时，
 *   上层头文件就提前使用它的宏、类型和声明，形成循环依赖。
 */

/* Driver 层头文件。 */
#include "bsp_led.h"
#include "bsp_oled.h"
#include "bsp_storage.h"
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
