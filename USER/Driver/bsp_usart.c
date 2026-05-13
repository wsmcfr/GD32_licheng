#include "bsp_usart.h"

/* 各串口 DMA 接收缓冲区定义。 */
uint8_t usart0_rxbuffer[BSP_USART0_RX_BUFFER_SIZE];
uint8_t usart1_rxbuffer[BSP_USART1_RX_BUFFER_SIZE];
uint8_t usart2_rxbuffer[BSP_USART2_RX_BUFFER_SIZE];
uint8_t usart5_rxbuffer[BSP_USART5_RX_BUFFER_SIZE];

/*
 * 函数作用：
 *   将 RS485 方向控制脚 PE8 配置为可真实驱动外部 485_CS 网络的 GPIO 推挽输出。
 * 主要流程：
 *   1. 确保 GPIOE 时钟已开启。
 *   2. 将 PE8 从可能的模拟输入/高阻状态恢复为普通输出。
 *   3. 配置为推挽输出，避免 DE/RE# 悬空后被环境噪声耦合成正弦波。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
static void prv_rs485_direction_gpio_init(void)
{
    rcu_periph_clock_enable(RS485_DIR_CLK_PORT);
    gpio_mode_set(RS485_DIR_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, RS485_DIR_PIN);
    gpio_output_options_set(RS485_DIR_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, RS485_DIR_PIN);
}

/*
 * 函数作用：
 *   将 RS485 收发器切换到接收状态(PE8 低电平)。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   方向脚已在 bsp_usart1_init() 中初始化好，正常运行过程无需重复调用
 *   prv_rs485_direction_gpio_init()，避免频繁读-改-写 GPIO 寄存器带来
 *   不必要的瞬时抖动。深度睡眠唤醒后 bsp_deepsleep_reinit_after_wakeup()
 *   会重新调用 bsp_usart_init() 完成完整初始化。
 */
void bsp_rs485_direction_receive(void)
{
    gpio_bit_write(RS485_DIR_PORT, RS485_DIR_PIN, RS485_DIR_RX_LEVEL);
}

/*
 * 函数作用：
 *   将 RS485 收发器切换到发送状态(PE8 高电平)。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void bsp_rs485_direction_transmit(void)
{
    gpio_bit_write(RS485_DIR_PORT, RS485_DIR_PIN, RS485_DIR_TX_LEVEL);
}

/*
 * 函数作用：
 *   初始化 USART0 及其 DMA 空闲中断接收链路。
 * 主要流程：
 *   1. 配置 DMA 为串口到内存方向。
 *   2. 配置 GPIO 复用功能。
 *   3. 配置串口波特率和收发能力。
 *   4. 打开 IDLE 中断用于帧接收完成判定。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
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
    usart_baudrate_set(USART0, DEBUG_USART_BAUDRATE);
    usart_receive_config(USART0, USART_RECEIVE_ENABLE);
    usart_transmit_config(USART0, USART_TRANSMIT_ENABLE);
    usart_dma_receive_config(USART0, USART_RECEIVE_DMA_ENABLE);
    usart_enable(USART0);

    /* USART0 使用 IDLE 中断作为变长帧结束标志，DMA 只负责搬运原始字节。 */
    nvic_irq_enable(USART0_IRQn, 0U, 0U);
    usart_interrupt_enable(USART0, USART_INT_IDLE);
}

/*
 * 函数作用：
 *   初始化当前工程默认需要的串口资源。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   USART0 作为默认调试串口，USART1 作为 RS485 转发串口，两者在这里一起初始化。
 */
void bsp_usart_init(void)
{
    bsp_usart0_init();
    bsp_usart1_init();
}

/*
 * 函数作用：
 *   初始化 USART1 及 RS485 方向控制脚。
 * 主要流程：
 *   1. 打开 DMA、GPIOD、PE8 方向控制脚和 USART1 外设时钟。
 *   2. 配置 PD5/PD6 为 USART1 的 TX/RX 复用功能。
 *   3. 配置 PE8 为 RS485 收发器方向控制输出，并默认进入接收态。
 *   4. 配置 USART1 为默认 115200-8N1 收发模式。
 *   5. 打开 USART1 IDLE 中断，用于 RS485 接收帧完成判定。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
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

    prv_rs485_direction_gpio_init();
    bsp_rs485_direction_receive();

    usart_deinit(USART1);
    usart_baudrate_set(USART1, 115200U);
    usart_receive_config(USART1, USART_RECEIVE_ENABLE);
    usart_transmit_config(USART1, USART_TRANSMIT_ENABLE);
    usart_dma_receive_config(USART1, USART_RECEIVE_DMA_ENABLE);
    usart_enable(USART1);

    /* USART1/RS485 同样使用 IDLE 中断移交整帧，避免在中断里解析业务协议。 */
    nvic_irq_enable(USART1_IRQn, 1U, 0U);
    usart_interrupt_enable(USART1, USART_INT_IDLE);
}

/*
 * 函数作用：
 *   初始化 USART2 及其 DMA 接收链路。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
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
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
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
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void bsp_usart_all_init(void)
{
    bsp_usart0_init();
    bsp_usart1_init();
    bsp_usart2_init();
    bsp_usart5_init();
}
