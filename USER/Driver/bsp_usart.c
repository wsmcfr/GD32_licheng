#include "bsp_usart.h"

/* 各串口 DMA 接收缓冲区定义。 */
uint8_t usart0_rxbuffer[BSP_USART0_RX_BUFFER_SIZE];
uint8_t usart1_rxbuffer[BSP_USART1_RX_BUFFER_SIZE];
uint8_t usart2_rxbuffer[BSP_USART2_RX_BUFFER_SIZE];
uint8_t usart5_rxbuffer[BSP_USART5_RX_BUFFER_SIZE];

/*
 * 函数作用：
 *   初始化 USART0 及其 DMA 空闲中断接收链路。
 * 主要流程：
 *   1. 配置 DMA 为串口到内存方向。
 *   2. 配置 GPIO 复用功能。
 *   3. 配置串口波特率和收发能力。
 *   4. 打开 IDLE 中断用于帧接收完成判定。
 */
void bsp_usart0_init(void)
{
    dma_single_data_parameter_struct dma_init_struct;

    rcu_periph_clock_enable(RCU_DMA1);
    rcu_periph_clock_enable(USART0_CLK_PORT);
    rcu_periph_clock_enable(RCU_USART0);

    dma_deinit(USART0_RX_DMA_PERIPH, USART0_RX_DMA_CHANNEL);
    dma_init_struct.direction = DMA_PERIPH_TO_MEMORY;
    dma_init_struct.memory0_addr = (uint32_t)usart0_rxbuffer;
    dma_init_struct.memory_inc = DMA_MEMORY_INCREASE_ENABLE;
    dma_init_struct.number = sizeof(usart0_rxbuffer);
    dma_init_struct.periph_addr = USART0_RDATA_ADDRESS;
    dma_init_struct.periph_inc = DMA_PERIPH_INCREASE_DISABLE;
    dma_init_struct.periph_memory_width = DMA_PERIPH_WIDTH_8BIT;
    dma_init_struct.priority = DMA_PRIORITY_ULTRA_HIGH;
    dma_single_data_mode_init(USART0_RX_DMA_PERIPH, USART0_RX_DMA_CHANNEL, &dma_init_struct);

    dma_circulation_disable(USART0_RX_DMA_PERIPH, USART0_RX_DMA_CHANNEL);
    dma_channel_subperipheral_select(USART0_RX_DMA_PERIPH,
                                     USART0_RX_DMA_CHANNEL,
                                     USART0_RX_DMA_SUBPERI);
    dma_channel_enable(USART0_RX_DMA_PERIPH, USART0_RX_DMA_CHANNEL);

    gpio_af_set(USART0_TX_PORT, USART0_AF, USART0_TX_PIN);
    gpio_af_set(USART0_RX_PORT, USART0_AF, USART0_RX_PIN);

    gpio_mode_set(USART0_TX_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, USART0_TX_PIN);
    gpio_output_options_set(USART0_TX_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, USART0_TX_PIN);

    gpio_mode_set(USART0_RX_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, USART0_RX_PIN);
    gpio_output_options_set(USART0_RX_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, USART0_RX_PIN);

    usart_deinit(USART0);
    usart_baudrate_set(USART0, 115200U);
    usart_receive_config(USART0, USART_RECEIVE_ENABLE);
    usart_transmit_config(USART0, USART_TRANSMIT_ENABLE);
    usart_dma_receive_config(USART0, USART_RECEIVE_DMA_ENABLE);
    usart_enable(USART0);

    nvic_irq_enable(USART0_IRQn, 0U, 0U);
    usart_interrupt_enable(USART0, USART_INT_IDLE);
}

/*
 * 函数作用：
 *   兼容当前工程的单串口初始化入口。
 * 说明：
 *   当前系统默认仅把 USART0 当作调试串口，因此该接口只初始化 USART0。
 */
void bsp_usart_init(void)
{
    bsp_usart0_init();
}

/*
 * 函数作用：
 *   初始化 USART1 及其 DMA 接收链路。
 */
void bsp_usart1_init(void)
{
    dma_single_data_parameter_struct dma_init_struct;

    rcu_periph_clock_enable(RCU_DMA0);
    rcu_periph_clock_enable(USART1_CLK_PORT);
    rcu_periph_clock_enable(RCU_USART1);

    dma_deinit(USART1_RX_DMA_PERIPH, USART1_RX_DMA_CHANNEL);
    dma_init_struct.direction = DMA_PERIPH_TO_MEMORY;
    dma_init_struct.memory0_addr = (uint32_t)usart1_rxbuffer;
    dma_init_struct.memory_inc = DMA_MEMORY_INCREASE_ENABLE;
    dma_init_struct.number = sizeof(usart1_rxbuffer);
    dma_init_struct.periph_addr = USART1_RDATA_ADDRESS;
    dma_init_struct.periph_inc = DMA_PERIPH_INCREASE_DISABLE;
    dma_init_struct.periph_memory_width = DMA_PERIPH_WIDTH_8BIT;
    dma_init_struct.priority = DMA_PRIORITY_ULTRA_HIGH;
    dma_single_data_mode_init(USART1_RX_DMA_PERIPH, USART1_RX_DMA_CHANNEL, &dma_init_struct);

    dma_circulation_disable(USART1_RX_DMA_PERIPH, USART1_RX_DMA_CHANNEL);
    dma_channel_subperipheral_select(USART1_RX_DMA_PERIPH,
                                     USART1_RX_DMA_CHANNEL,
                                     USART1_RX_DMA_SUBPERI);
    dma_channel_enable(USART1_RX_DMA_PERIPH, USART1_RX_DMA_CHANNEL);

    gpio_af_set(USART1_TX_PORT, USART1_AF, USART1_TX_PIN | USART1_RX_PIN);
    gpio_mode_set(USART1_TX_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, USART1_TX_PIN | USART1_RX_PIN);
    gpio_output_options_set(USART1_TX_PORT,
                            GPIO_OTYPE_PP,
                            GPIO_OSPEED_50MHZ,
                            USART1_TX_PIN | USART1_RX_PIN);

    usart_deinit(USART1);
    usart_baudrate_set(USART1, 115200U);
    usart_receive_config(USART1, USART_RECEIVE_ENABLE);
    usart_transmit_config(USART1, USART_TRANSMIT_ENABLE);
    usart_dma_receive_config(USART1, USART_RECEIVE_DMA_ENABLE);
    usart_enable(USART1);
}

/*
 * 函数作用：
 *   初始化 USART2 及其 DMA 接收链路。
 */
void bsp_usart2_init(void)
{
    dma_single_data_parameter_struct dma_init_struct;

    rcu_periph_clock_enable(RCU_DMA0);
    rcu_periph_clock_enable(USART2_CLK_PORT);
    rcu_periph_clock_enable(RCU_USART2);

    dma_deinit(USART2_RX_DMA_PERIPH, USART2_RX_DMA_CHANNEL);
    dma_init_struct.direction = DMA_PERIPH_TO_MEMORY;
    dma_init_struct.memory0_addr = (uint32_t)usart2_rxbuffer;
    dma_init_struct.memory_inc = DMA_MEMORY_INCREASE_ENABLE;
    dma_init_struct.number = sizeof(usart2_rxbuffer);
    dma_init_struct.periph_addr = USART2_RDATA_ADDRESS;
    dma_init_struct.periph_inc = DMA_PERIPH_INCREASE_DISABLE;
    dma_init_struct.periph_memory_width = DMA_PERIPH_WIDTH_8BIT;
    dma_init_struct.priority = DMA_PRIORITY_ULTRA_HIGH;
    dma_single_data_mode_init(USART2_RX_DMA_PERIPH, USART2_RX_DMA_CHANNEL, &dma_init_struct);

    dma_circulation_disable(USART2_RX_DMA_PERIPH, USART2_RX_DMA_CHANNEL);
    dma_channel_subperipheral_select(USART2_RX_DMA_PERIPH,
                                     USART2_RX_DMA_CHANNEL,
                                     USART2_RX_DMA_SUBPERI);
    dma_channel_enable(USART2_RX_DMA_PERIPH, USART2_RX_DMA_CHANNEL);

    gpio_af_set(USART2_TX_PORT, USART2_AF, USART2_TX_PIN | USART2_RX_PIN);
    gpio_mode_set(USART2_TX_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, USART2_TX_PIN | USART2_RX_PIN);
    gpio_output_options_set(USART2_TX_PORT,
                            GPIO_OTYPE_PP,
                            GPIO_OSPEED_50MHZ,
                            USART2_TX_PIN | USART2_RX_PIN);

    usart_deinit(USART2);
    usart_baudrate_set(USART2, 115200U);
    usart_receive_config(USART2, USART_RECEIVE_ENABLE);
    usart_transmit_config(USART2, USART_TRANSMIT_ENABLE);
    usart_dma_receive_config(USART2, USART_RECEIVE_DMA_ENABLE);
    usart_enable(USART2);
}

/*
 * 函数作用：
 *   初始化 USART5 及其 DMA 接收链路。
 */
void bsp_usart5_init(void)
{
    dma_single_data_parameter_struct dma_init_struct;

    rcu_periph_clock_enable(RCU_DMA1);
    rcu_periph_clock_enable(USART5_CLK_PORT);
    rcu_periph_clock_enable(RCU_USART5);

    dma_deinit(USART5_RX_DMA_PERIPH, USART5_RX_DMA_CHANNEL);
    dma_init_struct.direction = DMA_PERIPH_TO_MEMORY;
    dma_init_struct.memory0_addr = (uint32_t)usart5_rxbuffer;
    dma_init_struct.memory_inc = DMA_MEMORY_INCREASE_ENABLE;
    dma_init_struct.number = sizeof(usart5_rxbuffer);
    dma_init_struct.periph_addr = USART5_RDATA_ADDRESS;
    dma_init_struct.periph_inc = DMA_PERIPH_INCREASE_DISABLE;
    dma_init_struct.periph_memory_width = DMA_PERIPH_WIDTH_8BIT;
    dma_init_struct.priority = DMA_PRIORITY_ULTRA_HIGH;
    dma_single_data_mode_init(USART5_RX_DMA_PERIPH, USART5_RX_DMA_CHANNEL, &dma_init_struct);

    dma_circulation_disable(USART5_RX_DMA_PERIPH, USART5_RX_DMA_CHANNEL);
    dma_channel_subperipheral_select(USART5_RX_DMA_PERIPH,
                                     USART5_RX_DMA_CHANNEL,
                                     USART5_RX_DMA_SUBPERI);
    dma_channel_enable(USART5_RX_DMA_PERIPH, USART5_RX_DMA_CHANNEL);

    gpio_af_set(USART5_TX_PORT, USART5_AF, USART5_TX_PIN | USART5_RX_PIN);
    gpio_mode_set(USART5_TX_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, USART5_TX_PIN | USART5_RX_PIN);
    gpio_output_options_set(USART5_TX_PORT,
                            GPIO_OTYPE_PP,
                            GPIO_OSPEED_50MHZ,
                            USART5_TX_PIN | USART5_RX_PIN);

    usart_deinit(USART5);
    usart_baudrate_set(USART5, 115200U);
    usart_receive_config(USART5, USART_RECEIVE_ENABLE);
    usart_transmit_config(USART5, USART_TRANSMIT_ENABLE);
    usart_dma_receive_config(USART5, USART_RECEIVE_DMA_ENABLE);
    usart_enable(USART5);
}

/*
 * 函数作用：
 *   一次性初始化全部扩展串口。
 */
void bsp_usart_all_init(void)
{
    bsp_usart0_init();
    bsp_usart1_init();
    bsp_usart2_init();
    bsp_usart5_init();
}
