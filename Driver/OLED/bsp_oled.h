#ifndef BSP_OLED_H
#define BSP_OLED_H

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

/* OLED 批量 DMA 发送缓冲区定义：
 * 第 1 字节固定为 SSD1306 控制字，后续最多承载一整页 128 字节显存数据。
 */
#define OLED_TX_DATA_MAX_SIZE          128U
#define OLED_TX_BUFFER_SIZE            (OLED_TX_DATA_MAX_SIZE + 1U)

#define OLED_PORT                      GPIOB
#define OLED_GPIO_CLOCK                RCU_GPIOB
#define OLED_DAT_PIN                   GPIO_PIN_9
#define OLED_CLK_PIN                   GPIO_PIN_8
#define AF_OLED_I2C0                   GPIO_AF_4

/* OLED 命令仍使用两字节包；数据缓冲扩展为一页批量写，减少 I2C 事务数量。 */
extern __IO uint8_t oled_cmd_buf[2];
extern __IO uint8_t oled_data_buf[OLED_TX_BUFFER_SIZE];

void bsp_oled_init(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_OLED_H */
