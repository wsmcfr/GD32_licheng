#ifndef GD25QXX_H
#define GD25QXX_H

#include "system_all.h"

/* SPI Flash 页大小。 */
#define SPI_FLASH_PAGE_SIZE             0x100U

/* SPI Flash 所在 SPI0 引脚资源定义。 */
#define GD25QXX_SPI_GPIO_PORT          GPIOB
#define GD25QXX_SPI_GPIO_CLOCK         RCU_GPIOB
#define GD25QXX_SPI_CS_GPIO_PORT       GPIOA
#define GD25QXX_SPI_CS_GPIO_CLOCK      RCU_GPIOA
#define GD25QXX_SPI_SCK_PIN            GPIO_PIN_3
#define GD25QXX_SPI_MISO_PIN           GPIO_PIN_4
#define GD25QXX_SPI_MOSI_PIN           GPIO_PIN_5
#define GD25QXX_SPI_CS_PIN             GPIO_PIN_15
#define GD25QXX_SPI_AF                 GPIO_AF_5

/* SPI Flash 使用的 SPI 外设和 DMA 临时缓冲区长度。 */
#define SPI_FLASH                     SPI0
#define GD25QXX_DMA_BUFFER_SIZE       12U

/* 片选控制宏定义。 */
#define SPI_FLASH_CS_LOW()            gpio_bit_reset(GD25QXX_SPI_CS_GPIO_PORT, GD25QXX_SPI_CS_PIN)
#define SPI_FLASH_CS_HIGH()           gpio_bit_set(GD25QXX_SPI_CS_GPIO_PORT, GD25QXX_SPI_CS_PIN)

/*
 * 函数作用：
 *   对 SPI Flash 做基础初始化，并确保片选保持非激活态。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void test_spi_flash(void);
void spi_flash_init(void);
void spi_flash_sector_erase(uint32_t sector_addr);
void spi_flash_bulk_erase(void);
void spi_flash_page_write(uint8_t *pbuffer, uint32_t write_addr, uint16_t num_byte_to_write);
void spi_flash_buffer_write(uint8_t *pbuffer, uint32_t write_addr, uint16_t num_byte_to_write);
void spi_flash_buffer_read(uint8_t *pbuffer, uint32_t read_addr, uint16_t num_byte_to_read);
uint32_t spi_flash_read_id(void);
void spi_flash_start_read_sequence(uint32_t read_addr);
void spi_flash_write_enable(void);
void spi_flash_wait_for_write_end(void);
uint8_t spi_flash_send_byte_dma(uint8_t byte);
uint16_t spi_flash_send_halfword_dma(uint16_t half_word);
void spi_flash_transmit_receive_dma(uint8_t *tx_buffer, uint8_t *rx_buffer, uint16_t size);
void spi_flash_wait_for_dma_end(void);

/*
 * 函数作用：
 *   发送深掉电指令，让 GD25QXX 在深度睡眠期间进入芯片级低功耗状态。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void spi_flash_enter_deep_power_down(void);

/*
 * 函数作用：
 *   发送释放深掉电指令，让 GD25QXX 从芯片级低功耗状态恢复可访问状态。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void spi_flash_release_from_deep_power_down(void);

#endif /* GD25QXX_H */



