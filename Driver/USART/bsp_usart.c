#include "bsp_usart.h"
#include "usart_app.h"

/* USART1/RS485 DMA 接收缓冲区定义，IDLE 中断会按有效长度转交给应用层。 */
uint8_t usart1_rxbuffer[BSP_USART1_RX_BUFFER_SIZE];

/*
 * 函数作用：
 *   等待指定 USART 标志置位，并在异常情况下超时返回。
 * 参数说明：
 *   usart_periph：目标 USART 外设编号，正式版实际只使用 USART1/RS485。
 *   flag：需要等待置位的 USART 状态标志。
 * 返回值说明：
 *   1：表示在超时前等到目标标志置位。
 *   0：表示等待超时。
 */
static uint8_t prv_bsp_usart_wait_flag_set(uint32_t usart_periph, usart_flag_enum flag)
{
    uint32_t timeout = 1000000UL;

    while(RESET == usart_flag_get(usart_periph, flag)) {
        if(0U == timeout) {
            return 0U;
        }
        /* 简单递减计数作为超时条件，避免异常串口把主循环永久卡死。 */
        timeout--;
    }

    return 1U;
}

/*
 * 函数作用：
 *   将 RS485 方向控制脚 PE8 配置为可真实驱动外部 485_CS 网络的 GPIO 推挽输出。
 * 主要流程：
 *   1. 确保 GPIOE 时钟已开启。
 *   2. 将 PE8 配置为普通推挽输出。
 *   3. 初始化后默认回到接收态，避免上电占用 RS485 总线。
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
    bsp_rs485_direction_receive();
}

/*
 * 函数作用：
 *   将 RS485 收发器切换到接收状态。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void bsp_rs485_direction_receive(void)
{
    gpio_bit_write(RS485_DIR_PORT, RS485_DIR_PIN, RS485_DIR_RX_LEVEL);
}

/*
 * 函数作用：
 *   将 RS485 收发器切换到发送状态。
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
 *   初始化当前正式版唯一需要的串口资源。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void bsp_usart_init(void)
{
    bsp_usart1_init();
}

/*
 * 函数作用：
 *   初始化 USART1 及 RS485 方向控制脚。
 * 主要流程：
 *   1. 打开 DMA0、GPIOD、GPIOE 和 USART1 外设时钟。
 *   2. 配置 PD5/PD6 为 USART1 的 TX/RX 复用功能。
 *   3. 配置 PE8 为 RS485 收发器方向控制输出，并默认进入接收态。
 *   4. 配置 USART1 为赛题默认 19200-8N1 收发模式。
 *   5. 配置 USART1 RX DMA 为普通接收缓冲，IDLE 中断用于识别一帧 ASCII 协议报文结束。
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
    dma_single_data_para_struct_init(&dma_init_struct);
    dma_init_struct.direction = DMA_PERIPH_TO_MEMORY;
    dma_init_struct.memory0_addr = (uint32_t)usart1_rxbuffer;
    dma_init_struct.memory_inc = DMA_MEMORY_INCREASE_ENABLE;
    dma_init_struct.number = sizeof(usart1_rxbuffer);
    dma_init_struct.periph_addr = USART1_RDATA_ADDRESS;
    dma_init_struct.periph_inc = DMA_PERIPH_INCREASE_DISABLE;
    dma_init_struct.periph_memory_width = DMA_PERIPH_WIDTH_8BIT;
    dma_init_struct.priority = DMA_PRIORITY_HIGH;
    dma_single_data_mode_init(USART1_RX_DMA_PERIPH, USART1_RX_DMA_CHANNEL, &dma_init_struct);

    /*
     * 正式协议普通命令是 ASCII 十六进制帧，依赖 IDLE 判定单帧结束。
     * 旧 32KB circular DMA 仅服务自定义裸流 OTA，已从 APP 正式链路删除。
     */
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

    usart_deinit(USART1);
    usart_baudrate_set(USART1, CIMC_RS485_BAUDRATE);
    usart_receive_config(USART1, USART_RECEIVE_ENABLE);
    usart_transmit_config(USART1, USART_TRANSMIT_ENABLE);
    usart_dma_receive_config(USART1, USART_RECEIVE_DMA_ENABLE);
    usart_enable(USART1);

    /* USART1/RS485 只使用 IDLE 中断把完整 ASCII 帧交给任务层，ISR 不解析协议。 */
    nvic_irq_enable(USART1_IRQn, 1U, 0U);
    usart_interrupt_enable(USART1, USART_INT_IDLE);
}

/*
 * 函数作用：
 *   通过阻塞轮询方式向指定串口发送一段原始字节流。
 * 主要流程：
 *   1. 若目标是 RS485，先切换到发送态。
 *   2. 逐字节等待 TBE 后写入数据寄存器。
 *   3. 所有字节发送完后等待 TC，确保最后一位已经移出移位寄存器。
 *   4. RS485 发送结束后立即切回接收态，避免占用半双工总线。
 * 参数说明：
 *   usart_periph：目标 USART 外设编号，正式版应传入 RS485_USART。
 *   data：待发送数据起始地址。
 *   length：待发送字节数，单位为字节。
 * 返回值说明：
 *   返回实际完成发送流程的字节数；若等待标志超时，则返回超时前已发送长度。
 */
uint16_t bsp_usart_send_buffer(uint32_t usart_periph, const uint8_t *data, uint16_t length)
{
    uint16_t index;

    if((NULL == data) || (0U == length)) {
        return 0U;
    }

    if(RS485_USART == usart_periph) {
        /*
         * 先切到发送态再写 USART DATA，避免 MAX3485 仍在接收态时吞掉首字节。
         * 这里保留一个很短的建立时间，覆盖 GPIO 到收发器方向脚的传播延迟。
         */
        bsp_rs485_direction_transmit();
        delay_us(10U);
    }

    for(index = 0U; index < length; index++) {
        if(0U == prv_bsp_usart_wait_flag_set(usart_periph, USART_FLAG_TBE)) {
            if(RS485_USART == usart_periph) {
                bsp_rs485_direction_receive();
            }
            return index;
        }
        /* TBE 置位后再写 DATA，确保不会覆盖上一字节尚未装载完成的发送缓冲。 */
        usart_data_transmit(usart_periph, data[index]);
    }

    if(0U == prv_bsp_usart_wait_flag_set(usart_periph, USART_FLAG_TC)) {
        if(RS485_USART == usart_periph) {
            bsp_rs485_direction_receive();
        }
        return index;
    }

    if(RS485_USART == usart_periph) {
        bsp_rs485_direction_receive();
    }

    return length;
}

/*
 * 函数作用：
 *   原地切换 USART1 波特率，不重新初始化 GPIO 或 NVIC。
 * 主要流程：
 *   1. 等待 TC 确认当前 TX 完全结束，避免最后一帧末尾数据损坏。
 *   2. 先关 DMA RX 通道，再关 USART1，修改波特率寄存器，重开 USART1。
 *   3. 清零 RX 缓冲区（清除旧波特率下收到的任何乱码）并重置 DMA 传输计数。
 *   4. 重新使能 DMA RX 通道，恢复正常接收。
 * 参数说明：
 *   baudrate：新波特率数值；为 0 时直接返回，不做任何操作。
 * 返回值说明：
 *   无返回值。
 */
void bsp_usart_change_baudrate(uint32_t baudrate)
{
    if(0U == baudrate) {
        return;
    }

    /* 等待当前最后一帧完全移出 USART 移位寄存器，再改波特率。 */
    (void)prv_bsp_usart_wait_flag_set(USART1, USART_FLAG_TC);

    /* 关闭 IDLE 中断，防止切换过程中 ISR 标记虚假 IDLE 事件。 */
    usart_interrupt_disable(USART1, USART_INT_IDLE);

    /* 先停 DMA，避免波特率过渡期间 DMA 把乱码写入接收缓冲。 */
    dma_channel_disable(USART1_RX_DMA_PERIPH, USART1_RX_DMA_CHANNEL);

    usart_disable(USART1);
    usart_baudrate_set(USART1, baudrate);
    usart_enable(USART1);

    /* 清除 USART 可能残留的 IDLE / ORE 标志（读 STAT + 读 DATA）。 */
    (void)USART_STAT0(USART1);
    (void)USART_DATA(USART1);

    /*
     * 清零接收缓冲区，确保旧波特率下的残留字节不会被 usart_app 当作新帧解析。
     * 然后重置 DMA CNT 寄存器（需在通道关闭状态下写入），重新使能。
     */
    memset(usart1_rxbuffer, 0U, sizeof(usart1_rxbuffer));
    dma_transfer_number_config(USART1_RX_DMA_PERIPH, USART1_RX_DMA_CHANNEL,
                                sizeof(usart1_rxbuffer));
    dma_channel_enable(USART1_RX_DMA_PERIPH, USART1_RX_DMA_CHANNEL);

    /* 清除去抖状态，避免处理旧波特率的残留数据。 */
    g_usart_idle_pending = 0U;

    /* 重新使能 IDLE 中断。 */
    usart_interrupt_enable(USART1, USART_INT_IDLE);
}
