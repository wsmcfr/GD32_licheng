#include "gd25qxx.h"

#define WRITE 0x02 /* write to memory instruction */
#define WRSR 0x01  /* write status register instruction */
#define WREN 0x06  /* write enable instruction */

#define READ 0x03 /* read from memory instruction */
#define RDSR 0x05 /* read status register instruction  */
#define RDID 0x9F /* read identification */
#define SE 0x20   /* sector erase instruction */
#define BE 0xC7   /* bulk erase instruction */
#define DP 0xB9   /* deep power-down instruction */
#define RDP 0xAB  /* release from deep power-down instruction */

#define WIP_FLAG 0x01 /* write in progress(wip)flag */
#define DUMMY_BYTE 0xA5

/* SPI Flash DMA 临时发送缓冲区。 */
static uint8_t spi1_send_array[GD25QXX_DMA_BUFFER_SIZE];

/* SPI Flash DMA 临时接收缓冲区。 */
static uint8_t spi1_receive_array[GD25QXX_DMA_BUFFER_SIZE];

/*
 * 函数作用：
 *   初始化 GD25QXX 片选状态并使能绑定的 SPI 外设。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   GPIO、SPI 参数和 DMA 时钟由 bsp_gd25qxx_init() 负责配置，本函数只做
 *   Flash 组件级别的收尾初始化，避免组件层重复持有板级资源配置。
 */
void spi_flash_init(void)
{
    SPI_FLASH_CS_HIGH();
    
    /* 使能当前 SPI Flash 绑定的 SPI 外设。 */
    spi_enable(SPI_FLASH);
}

void spi_flash_sector_erase(uint32_t sector_addr)
{
    spi_flash_write_enable();

    SPI_FLASH_CS_LOW();
    spi_flash_send_byte_dma(SE);
    spi_flash_send_byte_dma((sector_addr & 0xFF0000) >> 16);
    spi_flash_send_byte_dma((sector_addr & 0xFF00) >> 8);
    spi_flash_send_byte_dma(sector_addr & 0xFF);
    SPI_FLASH_CS_HIGH();

    spi_flash_wait_for_write_end();
}

void spi_flash_bulk_erase(void)
{
    spi_flash_write_enable();

    SPI_FLASH_CS_LOW();
    spi_flash_send_byte_dma(BE);
    SPI_FLASH_CS_HIGH();

    spi_flash_wait_for_write_end();
}

void spi_flash_page_write(uint8_t *pbuffer, uint32_t write_addr, uint16_t num_byte_to_write)
{
    spi_flash_write_enable();

    SPI_FLASH_CS_LOW();
    spi_flash_send_byte_dma(WRITE);
    spi_flash_send_byte_dma((write_addr & 0xFF0000) >> 16);
    spi_flash_send_byte_dma((write_addr & 0xFF00) >> 8);
    spi_flash_send_byte_dma(write_addr & 0xFF);

    while (num_byte_to_write--)
    {
        spi_flash_send_byte_dma(*pbuffer);
        pbuffer++;
    }

    SPI_FLASH_CS_HIGH();
    spi_flash_wait_for_write_end();
}

void spi_flash_buffer_write(uint8_t *pbuffer, uint32_t write_addr, uint16_t num_byte_to_write)
{
    uint8_t num_of_page = 0, num_of_single = 0, addr = 0, count = 0, temp = 0;

    addr = write_addr % SPI_FLASH_PAGE_SIZE;
    count = SPI_FLASH_PAGE_SIZE - addr;
    num_of_page = num_byte_to_write / SPI_FLASH_PAGE_SIZE;
    num_of_single = num_byte_to_write % SPI_FLASH_PAGE_SIZE;

    if (0 == addr)
    {
        if (0 == num_of_page)
        {
            spi_flash_page_write(pbuffer, write_addr, num_byte_to_write);
        }
        else
        {
            while (num_of_page--)
            {
                spi_flash_page_write(pbuffer, write_addr, SPI_FLASH_PAGE_SIZE);
                write_addr += SPI_FLASH_PAGE_SIZE;
                pbuffer += SPI_FLASH_PAGE_SIZE;
            }
            spi_flash_page_write(pbuffer, write_addr, num_of_single);
        }
    }
    else
    {
        if (0 == num_of_page)
        {
            if (num_of_single > count)
            {
                temp = num_of_single - count;
                spi_flash_page_write(pbuffer, write_addr, count);
                write_addr += count;
                pbuffer += count;
                spi_flash_page_write(pbuffer, write_addr, temp);
            }
            else
            {
                spi_flash_page_write(pbuffer, write_addr, num_byte_to_write);
            }
        }
        else
        {
            num_byte_to_write -= count;
            num_of_page = num_byte_to_write / SPI_FLASH_PAGE_SIZE;
            num_of_single = num_byte_to_write % SPI_FLASH_PAGE_SIZE;

            spi_flash_page_write(pbuffer, write_addr, count);
            write_addr += count;
            pbuffer += count;

            while (num_of_page--)
            {
                spi_flash_page_write(pbuffer, write_addr, SPI_FLASH_PAGE_SIZE);
                write_addr += SPI_FLASH_PAGE_SIZE;
                pbuffer += SPI_FLASH_PAGE_SIZE;
            }

            if (0 != num_of_single)
            {
                spi_flash_page_write(pbuffer, write_addr, num_of_single);
            }
        }
    }
}

void spi_flash_buffer_read(uint8_t *pbuffer, uint32_t read_addr, uint16_t num_byte_to_read)
{
    SPI_FLASH_CS_LOW();
    spi_flash_send_byte_dma(READ);
    spi_flash_send_byte_dma((read_addr & 0xFF0000) >> 16);
    spi_flash_send_byte_dma((read_addr & 0xFF00) >> 8);
    spi_flash_send_byte_dma(read_addr & 0xFF);

    while (num_byte_to_read--)
    {
        *pbuffer = spi_flash_send_byte_dma(DUMMY_BYTE);
        pbuffer++;
    }

    SPI_FLASH_CS_HIGH();
}

uint32_t spi_flash_read_id(void)
{
    uint32_t temp = 0, temp0 = 0, temp1 = 0, temp2 = 0;

    SPI_FLASH_CS_LOW();
    spi_flash_send_byte_dma(RDID);
    temp0 = spi_flash_send_byte_dma(DUMMY_BYTE);
    temp1 = spi_flash_send_byte_dma(DUMMY_BYTE);
    temp2 = spi_flash_send_byte_dma(DUMMY_BYTE);
    SPI_FLASH_CS_HIGH();

    temp = (temp0 << 16) | (temp1 << 8) | temp2;
    return temp;
}

void spi_flash_start_read_sequence(uint32_t read_addr)
{
    SPI_FLASH_CS_LOW();
    spi_flash_send_byte_dma(READ);
    spi_flash_send_byte_dma((read_addr & 0xFF0000) >> 16);
    spi_flash_send_byte_dma((read_addr & 0xFF00) >> 8);
    spi_flash_send_byte_dma(read_addr & 0xFF);
}

void spi_flash_write_enable(void)
{
    SPI_FLASH_CS_LOW();
    spi_flash_send_byte_dma(WREN);
    SPI_FLASH_CS_HIGH();
}

void spi_flash_wait_for_write_end(void)
{
    uint8_t flash_status = 0;

    SPI_FLASH_CS_LOW();
    spi_flash_send_byte_dma(RDSR);

    do
    {
        flash_status = spi_flash_send_byte_dma(DUMMY_BYTE);
    } while ((flash_status & WIP_FLAG) == 0x01);

    SPI_FLASH_CS_HIGH();
}

/*
 * 函数作用：
 *   发送 GD25QXX 深掉电指令，让 Flash 本体在 MCU 深度睡眠期间停止正常待机耗电。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   当前板级没有 Flash 物理断电路径，因此这里采用 JEDEC 通用 deep power-down
 *   指令把芯片切到最省电的软件可达状态。
 */
void spi_flash_enter_deep_power_down(void)
{
    /*
     * 若当前 Flash 仍在页编程/擦除内部忙状态，先等 WIP 清零再进入 deep power-down。
     * 这样可以避免把器件硬切到深掉电时打断内部写流程，导致下一次唤醒后出现
     * 状态寄存器异常或最近一次写入不完整。
     */
    spi_flash_wait_for_write_end();

    SPI_FLASH_CS_LOW();
    spi_flash_send_byte_dma(DP);
    SPI_FLASH_CS_HIGH();
}

/*
 * 函数作用：
 *   发送 GD25QXX 释放深掉电指令，使 Flash 在唤醒后恢复到可通信状态。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   这里额外保留一个很短的阻塞等待，给芯片留出从 deep power-down 返回
 *   standby 的恢复时间，避免唤醒后第一笔访问偶发读到无效值。
 */
void spi_flash_release_from_deep_power_down(void)
{
    SPI_FLASH_CS_LOW();
    spi_flash_send_byte_dma(RDP);
    SPI_FLASH_CS_HIGH();
    delay_1ms(1U);
}

/*
 * 函数作用：
 *   通过 GD25QXX_SPI_DMA_PERIPH 的 TX/RX DMA 通道完成 1 字节 SPI 全双工收发。
 * 参数说明：
 *   byte：需要通过 MOSI 发送给 GD25QXX 的 1 字节数据。
 * 返回值说明：
 *   返回 MISO 同步接收到的 1 字节数据。
 */
uint8_t spi_flash_send_byte_dma(uint8_t byte)
{
    /* 发送前先写入静态 DMA 发送缓冲区，避免 DMA 直接访问栈上临时变量。 */
    spi1_send_array[0] = byte;
    
    /* DMA 配置结构体在 TX/RX 两个通道间复用，后续按方向覆盖必要字段。 */
    dma_single_data_parameter_struct dma_init_struct;
    
    /* 配置 TX 通道：SPI 数据寄存器固定，内存地址按字节自增。 */
    dma_deinit(GD25QXX_SPI_DMA_PERIPH, GD25QXX_SPI_DMA_TX_CHANNEL);
    dma_init_struct.periph_addr         = (uint32_t)&SPI_DATA(SPI_FLASH);
    dma_init_struct.memory0_addr        = (uint32_t)spi1_send_array;
    dma_init_struct.direction           = DMA_MEMORY_TO_PERIPH;
    dma_init_struct.periph_memory_width = DMA_PERIPH_WIDTH_8BIT;
    dma_init_struct.priority            = DMA_PRIORITY_HIGH;
    dma_init_struct.number              = 1; /* 本次只发送 1 字节。 */
    dma_init_struct.periph_inc          = DMA_PERIPH_INCREASE_DISABLE;
    dma_init_struct.memory_inc          = DMA_MEMORY_INCREASE_ENABLE;
    dma_init_struct.circular_mode       = DMA_CIRCULAR_MODE_DISABLE;
    dma_single_data_mode_init(GD25QXX_SPI_DMA_PERIPH, GD25QXX_SPI_DMA_TX_CHANNEL, &dma_init_struct);
    dma_channel_subperipheral_select(GD25QXX_SPI_DMA_PERIPH,
                                     GD25QXX_SPI_DMA_TX_CHANNEL,
                                     GD25QXX_SPI_DMA_SUBPERIPH);
    
    /* 配置 RX 通道：SPI 全双工发送时必须同步接收，否则 RXNE 可能阻塞后续传输。 */
    dma_deinit(GD25QXX_SPI_DMA_PERIPH, GD25QXX_SPI_DMA_RX_CHANNEL);
    dma_init_struct.periph_addr         = (uint32_t)&SPI_DATA(SPI_FLASH);
    dma_init_struct.memory0_addr        = (uint32_t)spi1_receive_array;
    dma_init_struct.direction           = DMA_PERIPH_TO_MEMORY;
    dma_init_struct.priority            = DMA_PRIORITY_HIGH;
    dma_single_data_mode_init(GD25QXX_SPI_DMA_PERIPH, GD25QXX_SPI_DMA_RX_CHANNEL, &dma_init_struct);
    dma_channel_subperipheral_select(GD25QXX_SPI_DMA_PERIPH,
                                     GD25QXX_SPI_DMA_RX_CHANNEL,
                                     GD25QXX_SPI_DMA_SUBPERIPH);
    
    /* 先打开 RX，再打开 TX，避免首字节回读数据来不及搬运。 */
    dma_channel_enable(GD25QXX_SPI_DMA_PERIPH, GD25QXX_SPI_DMA_RX_CHANNEL);
    dma_channel_enable(GD25QXX_SPI_DMA_PERIPH, GD25QXX_SPI_DMA_TX_CHANNEL);
    
    /* 打开 SPI 的 DMA 请求后，SPI 数据收发由 DMA 通道触发完成。 */
    spi_dma_enable(SPI_FLASH, SPI_DMA_RECEIVE);
    spi_dma_enable(SPI_FLASH, SPI_DMA_TRANSMIT);
    
    /* Wait for DMA transfer complete */
    while(RESET == dma_flag_get(GD25QXX_SPI_DMA_PERIPH, GD25QXX_SPI_DMA_RX_CHANNEL, DMA_FLAG_FTF));
    
    /* 传输结束立即关闭 SPI DMA 请求和通道，避免影响下一次重新配置。 */
    spi_dma_disable(SPI_FLASH, SPI_DMA_RECEIVE);
    spi_dma_disable(SPI_FLASH, SPI_DMA_TRANSMIT);
    dma_channel_disable(GD25QXX_SPI_DMA_PERIPH, GD25QXX_SPI_DMA_RX_CHANNEL);
    dma_channel_disable(GD25QXX_SPI_DMA_PERIPH, GD25QXX_SPI_DMA_TX_CHANNEL);
    
    /* Clear DMA flags */
    dma_flag_clear(GD25QXX_SPI_DMA_PERIPH, GD25QXX_SPI_DMA_RX_CHANNEL, DMA_FLAG_FTF);
    dma_flag_clear(GD25QXX_SPI_DMA_PERIPH, GD25QXX_SPI_DMA_TX_CHANNEL, DMA_FLAG_FTF);
    
    /* 返回同步收到的字节，用于 JEDEC ID、状态寄存器等读操作。 */
    return spi1_receive_array[0];
}

/*
 * 函数作用：
 *   通过 GD25QXX 的 DMA 收发链路连续发送 2 字节，并组合返回同步接收的半字。
 * 参数说明：
 *   half_word：需要发送的 16 位数据，高 8 位先发送，低 8 位后发送。
 * 返回值说明：
 *   返回 MISO 同步接收到的 16 位数据，高 8 位对应第一字节。
 */
uint16_t spi_flash_send_halfword_dma(uint16_t half_word)
{
    uint16_t rx_data;
    
    /* SPI Flash 按 MSB 先行发送半字，高 8 位先进入 DMA 发送缓冲区。 */
    spi1_send_array[0] = (uint8_t)(half_word >> 8);
    spi1_send_array[1] = (uint8_t)half_word;
    
    /* DMA 配置结构体在 TX/RX 两个通道间复用，后续按方向覆盖必要字段。 */
    dma_single_data_parameter_struct dma_init_struct;
    
    /* 配置 TX 通道，发送缓冲区连续提供 2 字节。 */
    dma_deinit(GD25QXX_SPI_DMA_PERIPH, GD25QXX_SPI_DMA_TX_CHANNEL);
    dma_init_struct.periph_addr         = (uint32_t)&SPI_DATA(SPI_FLASH);
    dma_init_struct.memory0_addr        = (uint32_t)spi1_send_array;
    dma_init_struct.direction           = DMA_MEMORY_TO_PERIPH;
    dma_init_struct.periph_memory_width = DMA_PERIPH_WIDTH_8BIT;
    dma_init_struct.priority            = DMA_PRIORITY_HIGH;
    dma_init_struct.number              = 2; /* 本次发送 2 字节。 */
    dma_init_struct.periph_inc          = DMA_PERIPH_INCREASE_DISABLE;
    dma_init_struct.memory_inc          = DMA_MEMORY_INCREASE_ENABLE;
    dma_init_struct.circular_mode       = DMA_CIRCULAR_MODE_DISABLE;
    dma_single_data_mode_init(GD25QXX_SPI_DMA_PERIPH, GD25QXX_SPI_DMA_TX_CHANNEL, &dma_init_struct);
    dma_channel_subperipheral_select(GD25QXX_SPI_DMA_PERIPH,
                                     GD25QXX_SPI_DMA_TX_CHANNEL,
                                     GD25QXX_SPI_DMA_SUBPERIPH);
    
    /* 配置 RX 通道，同步接收 2 字节回读数据。 */
    dma_deinit(GD25QXX_SPI_DMA_PERIPH, GD25QXX_SPI_DMA_RX_CHANNEL);
    dma_init_struct.periph_addr         = (uint32_t)&SPI_DATA(SPI_FLASH);
    dma_init_struct.memory0_addr        = (uint32_t)spi1_receive_array;
    dma_init_struct.direction           = DMA_PERIPH_TO_MEMORY;
    dma_init_struct.priority            = DMA_PRIORITY_HIGH;
    dma_single_data_mode_init(GD25QXX_SPI_DMA_PERIPH, GD25QXX_SPI_DMA_RX_CHANNEL, &dma_init_struct);
    dma_channel_subperipheral_select(GD25QXX_SPI_DMA_PERIPH,
                                     GD25QXX_SPI_DMA_RX_CHANNEL,
                                     GD25QXX_SPI_DMA_SUBPERIPH);
    
    /* 先打开 RX，再打开 TX，保证 SPI 全双工回读不会丢首字节。 */
    dma_channel_enable(GD25QXX_SPI_DMA_PERIPH, GD25QXX_SPI_DMA_RX_CHANNEL);
    dma_channel_enable(GD25QXX_SPI_DMA_PERIPH, GD25QXX_SPI_DMA_TX_CHANNEL);
    
    /* 打开 SPI 的 DMA 请求后，SPI 数据收发由 DMA 通道触发完成。 */
    spi_dma_enable(SPI_FLASH, SPI_DMA_RECEIVE);
    spi_dma_enable(SPI_FLASH, SPI_DMA_TRANSMIT);
    
    /* 等 RX 完成代表 2 字节全双工收发已经完成。 */
    while(RESET == dma_flag_get(GD25QXX_SPI_DMA_PERIPH, GD25QXX_SPI_DMA_RX_CHANNEL, DMA_FLAG_FTF));
    
    /* 传输结束后关闭 DMA 请求和通道，下一次调用会重新配置通道参数。 */
    spi_dma_disable(SPI_FLASH, SPI_DMA_RECEIVE);
    spi_dma_disable(SPI_FLASH, SPI_DMA_TRANSMIT);
    dma_channel_disable(GD25QXX_SPI_DMA_PERIPH, GD25QXX_SPI_DMA_RX_CHANNEL);
    dma_channel_disable(GD25QXX_SPI_DMA_PERIPH, GD25QXX_SPI_DMA_TX_CHANNEL);
    
    /* 清除 RX/TX 完成标志，避免下一次传输误判。 */
    dma_flag_clear(GD25QXX_SPI_DMA_PERIPH, GD25QXX_SPI_DMA_RX_CHANNEL, DMA_FLAG_FTF);
    dma_flag_clear(GD25QXX_SPI_DMA_PERIPH, GD25QXX_SPI_DMA_TX_CHANNEL, DMA_FLAG_FTF);
    
    /* 按发送顺序组合接收到的两个字节。 */
    rx_data = (uint16_t)(spi1_receive_array[0] << 8);
    rx_data |= spi1_receive_array[1];
    
    return rx_data;
}

/*
 * 函数作用：
 *   通过 GD25QXX 的 DMA 收发链路执行最多 GD25QXX_DMA_BUFFER_SIZE 字节的 SPI 全双工传输。
 * 参数说明：
 *   tx_buffer：发送数据缓冲区指针，函数会先复制到内部静态 DMA 缓冲区。
 *   rx_buffer：接收数据输出缓冲区指针，传输完成后写入同步接收到的数据。
 *   size：请求传输字节数，超过 GD25QXX_DMA_BUFFER_SIZE 时会被截断。
 * 返回值说明：
 *   无返回值。
 */
void spi_flash_transmit_receive_dma(uint8_t *tx_buffer, uint8_t *rx_buffer, uint16_t size)
{
    /* 该驱动使用固定静态 DMA 缓冲区，超长请求必须截断，避免越界写。 */
    if (size > GD25QXX_DMA_BUFFER_SIZE) {
        size = GD25QXX_DMA_BUFFER_SIZE;
    }
    
    /* 先复制到内部 DMA 缓冲区，保证 DMA 访问的内存生命周期稳定。 */
    for (uint16_t i = 0; i < size; i++) {
        spi1_send_array[i] = tx_buffer[i];
    }
    
    /* DMA 配置结构体在 TX/RX 两个通道间复用，后续按方向覆盖必要字段。 */
    dma_single_data_parameter_struct dma_init_struct;
    
    /* 配置 TX 通道，按 size 连续发送内部缓冲区数据。 */
    dma_deinit(GD25QXX_SPI_DMA_PERIPH, GD25QXX_SPI_DMA_TX_CHANNEL);
    dma_init_struct.periph_addr         = (uint32_t)&SPI_DATA(SPI_FLASH);
    dma_init_struct.memory0_addr        = (uint32_t)spi1_send_array;
    dma_init_struct.direction           = DMA_MEMORY_TO_PERIPH;
    dma_init_struct.periph_memory_width = DMA_PERIPH_WIDTH_8BIT;
    dma_init_struct.priority            = DMA_PRIORITY_HIGH;
    dma_init_struct.number              = size;
    dma_init_struct.periph_inc          = DMA_PERIPH_INCREASE_DISABLE;
    dma_init_struct.memory_inc          = DMA_MEMORY_INCREASE_ENABLE;
    dma_init_struct.circular_mode       = DMA_CIRCULAR_MODE_DISABLE;
    dma_single_data_mode_init(GD25QXX_SPI_DMA_PERIPH, GD25QXX_SPI_DMA_TX_CHANNEL, &dma_init_struct);
    dma_channel_subperipheral_select(GD25QXX_SPI_DMA_PERIPH,
                                     GD25QXX_SPI_DMA_TX_CHANNEL,
                                     GD25QXX_SPI_DMA_SUBPERIPH);
    
    /* 配置 RX 通道，按 size 接收同步回读数据。 */
    dma_deinit(GD25QXX_SPI_DMA_PERIPH, GD25QXX_SPI_DMA_RX_CHANNEL);
    dma_init_struct.periph_addr         = (uint32_t)&SPI_DATA(SPI_FLASH);
    dma_init_struct.memory0_addr        = (uint32_t)spi1_receive_array;
    dma_init_struct.direction           = DMA_PERIPH_TO_MEMORY;
    dma_init_struct.priority            = DMA_PRIORITY_HIGH;
    dma_single_data_mode_init(GD25QXX_SPI_DMA_PERIPH, GD25QXX_SPI_DMA_RX_CHANNEL, &dma_init_struct);
    dma_channel_subperipheral_select(GD25QXX_SPI_DMA_PERIPH,
                                     GD25QXX_SPI_DMA_RX_CHANNEL,
                                     GD25QXX_SPI_DMA_SUBPERIPH);
    
    /* 先启用 RX，再启用 TX，避免 SPI 开始传输后 RX DMA 尚未就绪。 */
    dma_channel_enable(GD25QXX_SPI_DMA_PERIPH, GD25QXX_SPI_DMA_RX_CHANNEL);
    dma_channel_enable(GD25QXX_SPI_DMA_PERIPH, GD25QXX_SPI_DMA_TX_CHANNEL);
    
    spi_dma_enable(SPI_FLASH, SPI_DMA_RECEIVE);
    spi_dma_enable(SPI_FLASH, SPI_DMA_TRANSMIT);
    
    /* 等 RX 完成代表本次多字节全双工收发已经完成。 */
    while(RESET == dma_flag_get(GD25QXX_SPI_DMA_PERIPH, GD25QXX_SPI_DMA_RX_CHANNEL, DMA_FLAG_FTF));
    
    /* 传输结束后关闭 DMA 请求和通道，释放给下一次重新配置。 */
    spi_dma_disable(SPI_FLASH, SPI_DMA_RECEIVE);
    spi_dma_disable(SPI_FLASH, SPI_DMA_TRANSMIT);
    dma_channel_disable(GD25QXX_SPI_DMA_PERIPH, GD25QXX_SPI_DMA_RX_CHANNEL);
    dma_channel_disable(GD25QXX_SPI_DMA_PERIPH, GD25QXX_SPI_DMA_TX_CHANNEL);
    
    /* 清除 RX/TX 完成标志，避免下一次传输误判。 */
    dma_flag_clear(GD25QXX_SPI_DMA_PERIPH, GD25QXX_SPI_DMA_RX_CHANNEL, DMA_FLAG_FTF);
    dma_flag_clear(GD25QXX_SPI_DMA_PERIPH, GD25QXX_SPI_DMA_TX_CHANNEL, DMA_FLAG_FTF);
    
    /* 最后把内部 DMA 接收缓冲区的数据复制给调用者。 */
    for (uint16_t i = 0; i < size; i++) {
        rx_buffer[i] = spi1_receive_array[i];
    }
}

/*
 * 函数作用：
 *   等待 GD25QXX SPI DMA 接收通道完成，并清理 RX/TX 通道完成标志。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void spi_flash_wait_for_dma_end(void)
{
    /* 等待 RX 通道完成，RX 完成代表 SPI 全双工收发已经闭环。 */
    while(RESET == dma_flag_get(GD25QXX_SPI_DMA_PERIPH, GD25QXX_SPI_DMA_RX_CHANNEL, DMA_FLAG_FTF));
    
    /* 清除 RX/TX 完成标志，为后续 DMA 传输留下干净状态。 */
    dma_flag_clear(GD25QXX_SPI_DMA_PERIPH, GD25QXX_SPI_DMA_RX_CHANNEL, DMA_FLAG_FTF);
    dma_flag_clear(GD25QXX_SPI_DMA_PERIPH, GD25QXX_SPI_DMA_TX_CHANNEL, DMA_FLAG_FTF);
}

/*
 * 函数作用：
 *   执行 GD25QXX 裸地址擦写读回测试，用于排查底层 SPI Flash 驱动和硬件链路。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   该测试会擦除一个 4KB 扇区，只能在 SPI_FLASH_RAW_TEST_ENABLE 打开时调用，
 *   并且必须使用 LittleFS 管理区之外的 LFS_FLASH_RAW_TEST_START_ADDR。
 */
void test_spi_flash(void)
{
    uint32_t flash_id;
    uint8_t write_buffer[SPI_FLASH_PAGE_SIZE];
    uint8_t read_buffer[SPI_FLASH_PAGE_SIZE];

    /*
     * 裸 Flash 测试会擦除 4KB 扇区，必须固定使用 LittleFS 管理区之外的
     * 末尾保留扇区，不能再使用 0x000000，避免破坏 LittleFS 超级块。
     */
    uint32_t test_addr = LFS_FLASH_RAW_TEST_START_ADDR;

    my_printf(DEBUG_USART, "SPI FLASH Test Start\r\n");

    /* 第一步：重新确认 SPI Flash 驱动处于片选释放和 SPI 使能状态。 */
    spi_flash_init();
    my_printf(DEBUG_USART, "SPI Flash Initialized.\r\n");

    /* 第二步：读取 JEDEC ID，确认 SPI 通信链路和芯片响应正常。 */
    flash_id = spi_flash_read_id();
    my_printf(DEBUG_USART, "Flash ID: 0x%lX\r\n", flash_id);

    /*
     * 第三步：擦除专用裸测保留扇区。
     * 注意：扇区擦除耗时较长，底层函数会阻塞等待 WIP 清零后再返回。
     */
    my_printf(DEBUG_USART,
              "Erasing raw-test reserved sector at address 0x%lX...\r\n",
              test_addr);
    spi_flash_sector_erase(test_addr);
    my_printf(DEBUG_USART, "Sector erased.\r\n");

    /* 擦除后先读回一页确认全为 0xFF，避免在擦除失败的扇区上继续写入。 */
    spi_flash_buffer_read(read_buffer, test_addr, SPI_FLASH_PAGE_SIZE);
    int erased_check_ok = 1;
    for (int i = 0; i < SPI_FLASH_PAGE_SIZE; i++)
    {
        if (read_buffer[i] != 0xFF)
        {
            erased_check_ok = 0;
            break;
        }
    }
    if (erased_check_ok)
    {
        my_printf(DEBUG_USART, "Erase check PASSED. Sector is all 0xFF.\r\n");
    }
    else
    {
        my_printf(DEBUG_USART, "Erase check FAILED.\r\n");
    }

    /* 第四步：准备一页测试数据，后续整页写入并读回比较。 */
    const char *message = "Hello from GD32 raw SPI FLASH reserved sector.";
    uint16_t data_len = strlen(message);
    if (data_len >= SPI_FLASH_PAGE_SIZE)
    {
        data_len = SPI_FLASH_PAGE_SIZE - 1;
    }
    memset(write_buffer, 0, SPI_FLASH_PAGE_SIZE);
    memcpy(write_buffer, message, data_len);
    write_buffer[data_len] = '\0';

    my_printf(DEBUG_USART, "Writing data to address 0x%lX: \"%s\"\r\n", test_addr, write_buffer);
    /* 写入完整一页，尾部填 0，便于读回后做定长 memcmp。 */
    spi_flash_buffer_write(write_buffer, test_addr, SPI_FLASH_PAGE_SIZE);
    my_printf(DEBUG_USART, "Data written.\r\n");

    /* 第五步：重新从 Flash 读回，验证数据确实落到外部存储芯片。 */
    my_printf(DEBUG_USART, "Reading data from address 0x%lX...\r\n", test_addr);
    memset(read_buffer, 0x00, SPI_FLASH_PAGE_SIZE);
    spi_flash_buffer_read(read_buffer, test_addr, SPI_FLASH_PAGE_SIZE);
    my_printf(DEBUG_USART, "Data read: \"%.*s\"\r\n", SPI_FLASH_PAGE_SIZE, read_buffer);

    /* 第六步：按整页比较，任何字节不一致都视为裸 Flash 驱动测试失败。 */
    if (memcmp(write_buffer, read_buffer, SPI_FLASH_PAGE_SIZE) == 0)
    {
        my_printf(DEBUG_USART, "Data VERIFIED! Write and Read successful.\r\n");
    }
    else
    {
        my_printf(DEBUG_USART, "Data VERIFICATION FAILED!\r\n");
    }

    my_printf(DEBUG_USART, "SPI FLASH Test End\r\n");
}

