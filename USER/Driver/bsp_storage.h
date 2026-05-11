#ifndef BSP_STORAGE_H
#define BSP_STORAGE_H

/*
 * 文件作用：
 *   提供板级 SPI Flash 和 GD30AD3344 的底层外设初始化接口。
 */

#define SYSTEM_ALL_BASE_ONLY
#include "system_all.h"
#undef SYSTEM_ALL_BASE_ONLY

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 函数作用：
 *   初始化 SPI Flash 所使用的 GPIO、SPI0、DMA 等底层资源。
 */
void bsp_gd25qxx_init(void);

/*
 * 函数作用：
 *   初始化 GD30AD3344 所使用的 GPIO、SPI3、DMA 等底层资源。
 */
void bsp_gd30ad3344_init(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_STORAGE_H */
