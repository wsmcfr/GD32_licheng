#ifndef BOOT_APP_CONFIG_H
#define BOOT_APP_CONFIG_H

#include "gd32f4xx.h"

/*
 * 宏作用：
 *   定义当前工程作为 BootLoader App 时的 Flash 起始地址。
 * 说明：
 *   该地址必须同时满足：
 *   1. 与 Keil IROM1 / scatter 文件中的 LR_IROM1 起始地址一致。
 *   2. 与官方 BootLoader 跳转时使用的 appStartAddr 一致。
 *   3. 满足 Cortex-M 向量表对齐要求。
 */
#define BOOT_APP_START_ADDRESS          (0x0800D000UL)

/*
 * 宏作用：
 *   定义当前 App 可使用的 Flash 最大空间。
 * 说明：
 *   官方 BootLoader_Two_Stage 例程规划 App 区为 0x0800D000 起，
 *   长度 0x00063000，末尾到 0x08070000 之前，避免覆盖下载缓存区。
 */
#define BOOT_APP_FLASH_SIZE             (0x00063000UL)

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 函数作用：
 *   将 Cortex-M4 的中断向量表切换到当前 App 的 Flash 起始地址。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void boot_app_vector_table_init(void);

/*
 * 函数作用：
 *   接管 BootLoader 跳转到 App 后的最小启动现场。
 * 说明：
 *   这个函数解决的不是“如何运行 App 主逻辑”，而是“BootLoader 已经跳过来了，
 *   但中断和向量表现场还不完全属于 App”这个交接问题。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void boot_app_handoff_init(void);

#ifdef __cplusplus
}
#endif

#endif /* BOOT_APP_CONFIG_H */
