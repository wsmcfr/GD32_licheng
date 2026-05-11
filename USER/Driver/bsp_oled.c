#include "bsp_oled.h"

/* OLED DMA 发送缓冲区：
 * 第 1 字节为控制字，第 2 字节为命令或数据本体。
 */
__IO uint8_t oled_cmd_buf[2] = {0x00U, 0x00U};
__IO uint8_t oled_data_buf[2] = {0x40U, 0x00U};

/*
 * 函数作用：
 *   初始化 OLED 使用的 I2C0 外设、GPIO 复用功能以及 DMA 发送通道。
 * 主要流程：
 *   1. 打开 GPIOB、I2C0、DMA0 时钟。
 *   2. 将 PB8/PB9 配置为 I2C0 复用开漏输出。
 *   3. 初始化 I2C0 工作频率和本机地址。
 *   4. 预先配置 DMA0 CH6，为 OLED 命令/数据发送做准备。
 */
void bsp_oled_init(void)
{
    dma_single_data_parameter_struct dma_init_struct;

    rcu_periph_clock_enable(OLED_GPIO_CLOCK);
    rcu_periph_clock_enable(RCU_I2C0);
    rcu_periph_clock_enable(RCU_DMA0);

    gpio_af_set(OLED_PORT, AF_OLED_I2C0, OLED_DAT_PIN);
    gpio_af_set(OLED_PORT, AF_OLED_I2C0, OLED_CLK_PIN);

    gpio_mode_set(OLED_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, OLED_DAT_PIN);
    gpio_output_options_set(OLED_PORT, GPIO_OTYPE_OD, GPIO_OSPEED_50MHZ, OLED_DAT_PIN);

    gpio_mode_set(OLED_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, OLED_CLK_PIN);
    gpio_output_options_set(OLED_PORT, GPIO_OTYPE_OD, GPIO_OSPEED_50MHZ, OLED_CLK_PIN);

    i2c_clock_config(I2C0, 400000U, I2C_DTCY_2);
    i2c_mode_addr_config(I2C0, I2C_I2CMODE_ENABLE, I2C_ADDFORMAT_7BITS, I2C0_OWN_ADDRESS7);
    i2c_enable(I2C0);
    i2c_ack_config(I2C0, I2C_ACK_ENABLE);

    dma_deinit(DMA0, DMA_CH6);
    dma_single_data_para_struct_init(&dma_init_struct);
    dma_init_struct.direction = DMA_MEMORY_TO_PERIPH;
    dma_init_struct.memory0_addr = (uint32_t)oled_data_buf;
    dma_init_struct.memory_inc = DMA_MEMORY_INCREASE_ENABLE;
    dma_init_struct.periph_memory_width = DMA_PERIPH_WIDTH_8BIT;
    dma_init_struct.number = 2U;
    dma_init_struct.periph_addr = I2C0_DATA_ADDRESS;
    dma_init_struct.periph_inc = DMA_PERIPH_INCREASE_DISABLE;
    dma_init_struct.priority = DMA_PRIORITY_ULTRA_HIGH;
    dma_single_data_mode_init(DMA0, DMA_CH6, &dma_init_struct);

    dma_circulation_disable(DMA0, DMA_CH6);
    dma_channel_subperipheral_select(DMA0, DMA_CH6, DMA_SUBPERI1);
}
