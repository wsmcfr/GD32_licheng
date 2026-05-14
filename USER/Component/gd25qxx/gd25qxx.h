#ifndef GD25QXX_H
#define GD25QXX_H

#include "system_all.h"

/* SPI Flash 页大小，必须与 LittleFS 移植层的 LFS_FLASH_PAGE_SIZE 保持一致。 */
#define SPI_FLASH_PAGE_SIZE             0x100U

/*
 * 宏作用：
 *   控制是否在启动阶段执行裸地址 SPI Flash 擦写测试。
 * 说明：
 *   默认关闭，避免裸擦写破坏 LittleFS 文件系统；只有排查底层 GD25Qxx 驱动
 *   或全新硬件焊接问题时才临时置 1。
 */
#define SPI_FLASH_RAW_TEST_ENABLE       0U

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

/*
 * GD25QXX 使用 SPI0 的 DMA 映射资源。
 * 说明：
 *   当前驱动所有 SPI 收发都通过 DMA1 完成，其中 RX 使用 CH2，TX 使用 CH3，
 *   两个通道都选择 SUBPERI3。时钟、通道和子外设统一放在这里定义，避免
 *   初始化代码和传输代码分别硬编码后出现 DMA 时钟开错的问题。
 */
#define GD25QXX_SPI_DMA_CLOCK         RCU_DMA1
#define GD25QXX_SPI_DMA_PERIPH        DMA1
#define GD25QXX_SPI_DMA_RX_CHANNEL    DMA_CH2
#define GD25QXX_SPI_DMA_TX_CHANNEL    DMA_CH3
#define GD25QXX_SPI_DMA_SUBPERIPH     DMA_SUBPERI3

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



