#include "bsp_usart.h"
#include "usart_app.h"

/* USART1/RS485 DMA 接收缓冲区，IDLE 中断按有效长度转交应用层 */
uint8_t usart1_rxbuffer[BSP_USART1_RX_BUFFER_SIZE];

static void rs485_direction_receive(void);

/* 等待 USART 标志置位，递减计数超时返回 0，避免异常串口卡死主循环 */
static uint8_t wait_flag(uint32_t usart_periph, usart_flag_enum flag)
{
    uint32_t timeout = 1000000UL;

    while(RESET == usart_flag_get(usart_periph, flag)) 
	{
        if(0 == timeout)
		{
            return 0;
        }
        timeout--;
    }

    return 1;
}

/*
 * 配置 RS485 方向控制脚 PE8 为推挽输出，初始化后默认进入接收态，
 * 避免上电后占用 RS485 总线。
 */
static void rs485_init_gpio(void)
{
    rcu_periph_clock_enable(RS485_DIR_CLK_PORT);
    gpio_mode_set(RS485_DIR_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, RS485_DIR_PIN);
    gpio_output_options_set(RS485_DIR_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, RS485_DIR_PIN);
    rs485_direction_receive();
}

// RS485 切换到接收状态
static void rs485_direction_receive(void)
{
    gpio_bit_write(RS485_DIR_PORT, RS485_DIR_PIN, RS485_DIR_RX_LEVEL);
}

// RS485 切换到发送状态
static void rs485_direction_transmit(void)
{
    gpio_bit_write(RS485_DIR_PORT, RS485_DIR_PIN, RS485_DIR_TX_LEVEL);
}

// 初始化当前正式版唯一需要的串口资源（USART1/RS485）
void bsp_usart_init(void)
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

    dma_circulation_disable(USART1_RX_DMA_PERIPH, USART1_RX_DMA_CHANNEL);
    dma_channel_subperipheral_select(USART1_RX_DMA_PERIPH,USART1_RX_DMA_CHANNEL,USART1_RX_DMA_SUBPERI);
    dma_channel_enable(USART1_RX_DMA_PERIPH, USART1_RX_DMA_CHANNEL);

    gpio_af_set(USART1_TX_PORT, USART1_AF, USART1_TX_PIN | USART1_RX_PIN);
    gpio_mode_set(USART1_TX_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, USART1_TX_PIN | USART1_RX_PIN);
    gpio_output_options_set(USART1_TX_PORT,GPIO_OTYPE_PP,GPIO_OSPEED_50MHZ,USART1_TX_PIN | USART1_RX_PIN);

    rs485_init_gpio();

    usart_deinit(USART1);
    usart_baudrate_set(USART1, RS485_BAUD);
    usart_receive_config(USART1, USART_RECEIVE_ENABLE);
    usart_transmit_config(USART1, USART_TRANSMIT_ENABLE);
    usart_dma_receive_config(USART1, USART_RECEIVE_DMA_ENABLE);
    usart_enable(USART1);

    nvic_irq_enable(USART1_IRQn, 1, 0);
    usart_interrupt_enable(USART1, USART_INT_IDLE);
}

/*
 * 阻塞轮询向指定串口发送原始字节流。
 * RS485 发送前先切换方向脚并等 10µs 建立时间，全部字节移出后切回接收态。
 * 返回实际完成的字节数；等待标志超时时提前返回。
 */
uint16_t bsp_usart_send_buffer(uint32_t usart_periph, const uint8_t *data, uint16_t length)
{
    uint16_t i;

    if(!data || (0 == length)) 
	{
        return 0;
    }

    if(RS485_USART == usart_periph) 
	{
        rs485_direction_transmit();
        delay_us(10);
    }

    for(i = 0; i < length; i++) 
	{
        if(0 == wait_flag(usart_periph, USART_FLAG_TBE)) 
		{
            if(RS485_USART == usart_periph) 
			{
                rs485_direction_receive();
            }
            return i;
        }
        usart_data_transmit(usart_periph, data[i]);
    }

    if(0 == wait_flag(usart_periph, USART_FLAG_TC)) 
	{
        if(RS485_USART == usart_periph) 
		{
            rs485_direction_receive();
        }
        return i;
    }

    if(RS485_USART == usart_periph) 
	{
        rs485_direction_receive();
    }

    return length;
}

/*
 * 原地切换 USART1 波特率，不重新初始化 GPIO 或 NVIC。
 * 等 TC 确认当前 TX 结束 → 关 DMA → 关 USART → 改波特率 → 开 USART →
 * 清零接收缓冲（清除旧波特率乱码）→ 重置 DMA CNT → 重新使能 DMA 和 IDLE 中断。
 */
void bsp_usart_change_baudrate(uint32_t baudrate)
{
    if(0 == baudrate) 
	{
        return;
    }

    wait_flag(USART1, USART_FLAG_TC);

    /* 关闭 IDLE 中断，防止切换过程中 ISR 标记虚假 IDLE 事件 */
    usart_interrupt_disable(USART1, USART_INT_IDLE);

    dma_channel_disable(USART1_RX_DMA_PERIPH, USART1_RX_DMA_CHANNEL);

    usart_disable(USART1);
    usart_baudrate_set(USART1, baudrate);
    usart_enable(USART1);

    /* 清除 USART 可能残留的 IDLE / ORE 标志（读 STAT + 读 DATA）*/
    USART_STAT0(USART1);
    USART_DATA(USART1);

    memset(usart1_rxbuffer, 0, sizeof(usart1_rxbuffer));
    dma_transfer_number_config(USART1_RX_DMA_PERIPH, USART1_RX_DMA_CHANNEL,sizeof(usart1_rxbuffer));
    dma_channel_enable(USART1_RX_DMA_PERIPH, USART1_RX_DMA_CHANNEL);

    /* 清除去抖状态，避免处理旧波特率的残留数据 */
    g_idle_pend = 0;

    usart_interrupt_enable(USART1, USART_INT_IDLE);
}
