#ifndef BSP_OLED_H
#define BSP_OLED_H

/*
 * 文件作用：
 *   定义 OLED 所在 I2C、DMA、GPIO 资源以及初始化接口。
 *   OLED 初始化使用的命令/数据 DMA 缓冲区也在这里声明。
 */

#define SYSTEM_ALL_BASE_ONLY
#include "system_all.h"
#undef SYSTEM_ALL_BASE_ONLY

#ifdef __cplusplus
extern "C" {
#endif

/* OLED I2C 硬件资源定义。 */
#define I2C0_OWN_ADDRESS7              0x72U
#define I2C0_SLAVE_ADDRESS7            0x82U
#define I2C0_DATA_ADDRESS              ((uint32_t)&I2C_DATA(I2C0))

#define OLED_PORT                      GPIOB
#define OLED_GPIO_CLOCK                RCU_GPIOB
#define OLED_DAT_PIN                   GPIO_PIN_9
#define OLED_CLK_PIN                   GPIO_PIN_8
#define AF_OLED_I2C0                   GPIO_AF_4

/* OLED 发送命令和数据时复用的两字节 DMA 缓冲区。 */
extern __IO uint8_t oled_cmd_buf[2];
extern __IO uint8_t oled_data_buf[2];

/*
 * 函数作用：
 *   初始化 OLED 所依赖的 GPIO、I2C0 和 DMA 发送通道。
 */
void bsp_oled_init(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_OLED_H */
