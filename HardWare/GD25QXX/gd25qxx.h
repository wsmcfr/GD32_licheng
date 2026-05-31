#ifndef GD25QXX_H
#define GD25QXX_H

#include "system_all.h"

/* SPI Flash 页大小，必须与 SMARTFS 移植层的 SMARTFS_FLASH_PAGE_SIZE 保持一致。 */
#define SPI_FLASH_PAGE_SIZE             0x100U

/*
 * 宏作用：
 *   控制是否在启动阶段执行裸地址 SPI Flash 擦写测试。
 * 说明：
 *   SMARTFS 已经管理整片 2MB GD25Q16，不再保留专用裸测扇区。
 *   默认必须关闭；只有排查底层 GD25Qxx 驱动或全新硬件焊接问题时才临时置 1，
 *   且启用后会擦写 0x000000 起始扇区，必然破坏 SMARTFS 元数据。
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
/*
 * 函数作用：
 *   擦除指定 4KB 扇区，并等待 Flash 内部忙状态结束。
 * 参数说明：
 *   sector_addr：目标扇区内任意地址，驱动会按 GD25QXX 扇区擦除命令发送 24 位地址。
 * 返回值说明：
 *   0：表示擦除命令发送完成且 WIP 在超时前清零。
 *  -1：表示 SPI DMA 或 Flash WIP 等待失败。
 */
int spi_flash_sector_erase(uint32_t sector_addr);

/*
 * 函数作用：
 *   擦除整片 GD25QXX Flash，并等待 Flash 内部忙状态结束。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   0：表示整片擦除命令完成。
 *  -1：表示 SPI DMA 或 Flash WIP 等待失败。
 */
int spi_flash_bulk_erase(void);

/*
 * 函数作用：
 *   向 Flash 的同一页内写入最多 256 字节数据。
 * 参数说明：
 *   pbuffer：待写入数据缓冲区，写入长度大于 0 时必须非空。
 *   write_addr：页内写入起始地址。
 *   num_byte_to_write：待写入字节数，调用者应保证不跨越 256B 页边界。
 * 返回值说明：
 *   0：表示页写命令完成。
 *  -1：表示参数非法、SPI DMA 或 Flash WIP 等待失败。
 */
int spi_flash_page_write(uint8_t *pbuffer, uint32_t write_addr, uint16_t num_byte_to_write);

/*
 * 函数作用：
 *   按 256B 页边界自动拆分，把一段连续数据写入 Flash。
 * 参数说明：
 *   pbuffer：待写入数据缓冲区，写入长度大于 0 时必须非空。
 *   write_addr：写入起始地址。
 *   num_byte_to_write：待写入总字节数。
 * 返回值说明：
 *   0：表示全部页写命令完成。
 *  -1：表示任意一页写入失败或参数非法。
 */
int spi_flash_buffer_write(uint8_t *pbuffer, uint32_t write_addr, uint16_t num_byte_to_write);
void spi_flash_buffer_read(uint8_t *pbuffer, uint32_t read_addr, uint16_t num_byte_to_read);
uint32_t spi_flash_read_id(void);
void spi_flash_start_read_sequence(uint32_t read_addr);
/*
 * 函数作用：
 *   发送 GD25QXX 写使能命令，为后续页编程、扇区擦除或整片擦除打开 WEL。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   0：表示 WREN 命令字节已经通过 DMA 发送完成。
 *  -1：表示 SPI DMA 传输超时，调用者不能继续执行写擦命令。
 */
int spi_flash_write_enable(void);
/*
 * 函数作用：
 *   等待 SPI Flash 内部写入或擦除忙状态结束，并带超时保护。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   0：表示 WIP 已在超时前清零。
 *  -1：表示等待超时，Flash 可能仍处于内部忙状态。
 */
int spi_flash_wait_for_write_end(void);
uint8_t spi_flash_send_byte_dma(uint8_t byte);
uint16_t spi_flash_send_halfword_dma(uint16_t half_word);
void spi_flash_transmit_receive_dma(uint8_t *tx_buffer, uint8_t *rx_buffer, uint16_t size);
/*
 * 函数作用：
 *   等待 GD25QXX SPI DMA 收发完成，并带超时保护。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   0：表示 DMA 收发完成。
 *  -1：表示等待超时，调用方应放弃本次 SPI 事务。
 */
int spi_flash_wait_for_dma_end(void);

/*
 * 函数作用：
 *   发送深掉电指令，让 GD25QXX 在深度睡眠期间进入芯片级低功耗状态。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   0：表示 deep power-down 指令已经发送完成。
 *  -1：表示进入深掉电前等待 WIP 清零或 SPI DMA 传输超时。
 */
int spi_flash_enter_deep_power_down(void);

/*
 * 函数作用：
 *   发送释放深掉电指令，让 GD25QXX 从芯片级低功耗状态恢复可访问状态。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   0：表示 release 指令已经发送完成。
 *  -1：表示 SPI DMA 传输超时，Flash 可能仍未可靠退出深掉电。
 */
int spi_flash_release_from_deep_power_down(void);

#endif /* GD25QXX_H */



