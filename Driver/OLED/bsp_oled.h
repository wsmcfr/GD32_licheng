#ifndef BSP_OLED_H
#define BSP_OLED_H

#define SYSTEM_ALL_BASE_ONLY
#include "system_all.h"
#undef SYSTEM_ALL_BASE_ONLY

#ifdef __cplusplus
extern "C" {
#endif

/* OLED I2C */
#define I2C0_OWN_ADDRESS7              0x72U
#define I2C0_DATA_ADDRESS              ((uint32_t)&I2C_DATA(I2C0))

/* OLED 批量 DMA 发送缓冲区定义： */
#define OLED_TX_DATA_MAX_SIZE          128U
#define OLED_TX_BUFFER_SIZE            (OLED_TX_DATA_MAX_SIZE + 1U)

#define OLED_PORT                      GPIOB
#define OLED_GPIO_CLOCK                RCU_GPIOB
#define OLED_DAT_PIN                   GPIO_PIN_9
#define OLED_CLK_PIN                   GPIO_PIN_8
#define AF_OLED_I2C0                   GPIO_AF_4

/* OLED 批量发送缓冲区*/
extern __IO uint8_t oled_data_buf[OLED_TX_BUFFER_SIZE];

void bsp_oled_init(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_OLED_H */
