#include "usart_app.h"

__IO uint8_t rx_flag = 0;
__IO uint16_t uart_dma_length = 0;
uint8_t uart_dma_buffer[UART_APP_DMA_BUFFER_SIZE] = {0};
__IO uint8_t rs485_rx_flag = 0;
__IO uint16_t rs485_dma_length = 0;
uint8_t rs485_dma_buffer[UART_APP_DMA_BUFFER_SIZE] = {0};
/*
 * 变量作用：
 *   任务层专用快照缓冲区。
 * 说明：
 *   先从 ISR 共享缓冲区复制，再执行阻塞发送，避免发送过程中共享缓冲区被下一次中断改写。
 */
static uint8_t uart_forward_buffer[UART_APP_DMA_BUFFER_SIZE] = {0};
static uint8_t rs485_forward_buffer[UART_APP_DMA_BUFFER_SIZE] = {0};

/*
 * 宏作用：
 *   定义 USART 轮询等待超时计数上限。
 * 说明：
 *   阻塞发送必须有退出条件，否则目标串口异常时会卡死周期任务和主循环。
 */
#define UART_APP_TX_WAIT_TIMEOUT       1000000UL

/*
 * 函数作用：
 *   等待指定 USART 标志变为 SET，并在异常情况下超时返回。
 * 主要流程：
 *   1. 循环读取目标 USART 的指定标志位。
 *   2. 标志置位则返回 1。
 *   3. 计数耗尽仍未置位则返回 0，避免串口异常时卡死主循环。
 * 参数说明：
 *   usart_periph：目标 USART 外设编号，例如 USART0 或 USART1。
 *   flag：需要等待变为 SET 的 USART 状态标志，例如 USART_FLAG_TBE 或 USART_FLAG_TC。
 * 返回值说明：
 *   1：表示目标标志在超时前变为 SET。
 *   0：表示等待超时，调用者应停止本次发送或返回已发送长度。
 */
static uint8_t prv_uart_wait_flag_set(uint32_t usart_periph, usart_flag_enum flag)
{
    uint32_t timeout = UART_APP_TX_WAIT_TIMEOUT;

    while(RESET == usart_flag_get(usart_periph, flag)){
        if(0U == timeout){
            return 0U;
        }
        /* 这里使用简单递减计数，避免在串口异常时永久停留在轮询循环。 */
        timeout--;
    }

    return 1U;
}

/*
 * 函数作用：
 *   通过阻塞轮询方式向指定串口发送一段原始字节流。
 * 主要流程：
 *   1. 检查数据指针和长度，空数据直接返回 0。
 *   2. 逐字节等待 TBE 后写入 USART_DATA。
 *   3. 发送完所有字节后等待 TC，确认最后 1 字节已经完全移出移位寄存器。
 * 参数说明：
 *   usart_periph：目标 USART 外设编号。
 *   data：待发送数据起始地址，必须指向至少 length 字节的有效缓冲区。
 *   length：待发送字节数，单位为字节。
 * 返回值说明：
 *   返回实际完成发送流程的字节数。
 *   若等待 TBE 超时，返回超时前已经写入 USART 的字节数。
 *   若等待最终 TC 超时，返回 length，表示数据已写入 USART 但最终完成标志未确认。
 * 说明：
 *   不使用 usart_flag_clear() 清除 TC，该函数在 GD32 库中直接写
 *   ~BIT(6) 到 USART_STAT0 寄存器，会污染保留位(bit10-31)导致外设异常。
 *   TC 的正确清除方式"读 STAT0 + 写 DATA"已由 for 循环中的 TBE 等待
 *   (读) + usart_data_transmit(写) 隐式完成。
 */
static uint16_t prv_uart_send_buffer(uint32_t usart_periph, const uint8_t *data, uint16_t length)
{
    uint16_t index;

    if((NULL == data) || (0U == length)){
        return 0U;
    }

    for(index = 0U; index < length; index++){
        if(0U == prv_uart_wait_flag_set(usart_periph, USART_FLAG_TBE)){
            return index;
        }
        /* TBE 置位后再写 DATA，保证不会覆盖仍在发送缓冲中的上一字节。 */
        usart_data_transmit(usart_periph, data[index]);
    }

    if(0U == prv_uart_wait_flag_set(usart_periph, USART_FLAG_TC)){
        return index;
    }

    return length;
}

/*
 * 函数作用：
 *   将一段原始字节流通过 RS485 总线发送出去。
 * 主要流程：
 *   1. 发送前拉高 PE8，让 MAX3485 进入发送状态。
 *   2. 复用阻塞串口发送函数，把数据发到 USART1。
 *   3. 等待最后一个字节完成后拉低 PE8，释放 RS485 总线回到接收状态。
 * 参数说明：
 *   data：待发送数据起始地址，必须指向至少 length 字节的有效缓冲区。
 *   length：待发送字节数，单位为字节。
 * 返回值说明：
 *   返回实际完成发送流程的字节数。
 *   0 表示参数无效或未能开始发送。
 */
static uint16_t prv_rs485_send_buffer(const uint8_t *data, uint16_t length)
{
    uint16_t sent_length;

    if((NULL == data) || (0U == length)){
        return 0U;
    }

    bsp_rs485_direction_transmit();
    sent_length = prv_uart_send_buffer(RS485_USART, data, length);
    /* 等待发送函数返回后再切回接收态，避免最后一字节还在移位时提前释放 DE。 */
    bsp_rs485_direction_receive();

    return sent_length;
}

/*
 * 函数作用：
 *   将中断上下文产生的一帧数据原子地转存到任务层私有缓冲区。
 * 主要流程：
 *   1. 关总中断，防止复制过程中 ISR 再次改写源缓冲区和长度标志。
 *   2. 按有效长度把共享缓冲区复制到任务层专用缓冲区。
 *   3. 清除共享长度和完成标志，然后恢复中断。
 * 说明：
 *   这里只在很短的时间内屏蔽中断，复制完成后再做阻塞串口发送，
 *   既保证数据一致性，也避免长时间阻塞中断响应。
 * 参数：
 *   ready_flag     表示该帧是否已经准备好。
 *   shared_length  表示 ISR 记录的有效长度。
 *   shared_buffer  表示 ISR 使用的共享缓冲区。
 *   task_buffer    表示任务层私有缓冲区。
 *   task_buf_size  表示任务层私有缓冲区大小。
 * 返回值说明：
 *   返回成功取出的有效字节数。
 *   0 表示当前没有可处理的新帧，或参数无效。
 */
static uint16_t prv_uart_take_frame(__IO uint8_t *ready_flag,
                                    __IO uint16_t *shared_length,
                                    uint8_t *shared_buffer,
                                    uint8_t *task_buffer,
                                    uint16_t task_buf_size)
{
    uint16_t valid_length = 0U;

    if((NULL == ready_flag) || (NULL == shared_length) ||
       (NULL == shared_buffer) || (NULL == task_buffer) ||
       (0U == task_buf_size)){
        return 0U;
    }

    __disable_irq();
    if(0U != *ready_flag){
        valid_length = *shared_length;
        /* 再做一次目标缓冲区长度钳位，避免 ISR 或未来调用点传入异常长度。 */
        if(valid_length > task_buf_size){
            valid_length = task_buf_size;
        }
        if(valid_length > 0U){
            memcpy(task_buffer, shared_buffer, valid_length);
        }
        /* 清标志必须和复制共享数据处于同一个临界区，避免任务层重复消费同一帧。 */
        *shared_length = 0U;
        *ready_flag = 0U;
    }
    __enable_irq();

    return valid_length;
}

/*
 * 函数作用：
 *   通过阻塞发送的方式向指定串口输出格式化字符串。
 * 主要流程：
 *   1. 使用 vsnprintf 将可变参数格式化到固定长度缓冲区。
 *   2. 根据格式化结果计算实际发送长度，超长时发送已截断的缓冲区内容。
 *   3. 通过 prv_uart_send_buffer() 发送到指定 USART。
 * 参数说明：
 *   usart_periph：目标 USART 外设编号。
 *   format：printf 风格格式字符串，后续可变参数必须与格式占位符匹配。
 * 返回值说明：
 *   正数或 0：vsnprintf 返回的完整格式化长度，可能大于实际发送长度。
 *   负数：格式化失败，函数不发送数据并原样返回该错误值。
 */
int my_printf(uint32_t usart_periph, const char *format, ...)
{
    char buffer[UART_APP_DMA_BUFFER_SIZE];
    va_list arg;
    int len;
    uint16_t send_length;

    va_start(arg, format);
    len = vsnprintf(buffer, sizeof(buffer), format, arg);
    va_end(arg);

    if(len <= 0){
        return len;
    }

    if(len >= (int)sizeof(buffer)){
        /* vsnprintf 已经截断缓冲区，发送时保留结尾 '\0' 位置不作为串口数据。 */
        send_length = (uint16_t)(sizeof(buffer) - 1U);
    }else{
        send_length = (uint16_t)len;
    }

    (void)prv_uart_send_buffer(usart_periph, (const uint8_t *)buffer, send_length);

    return len;
}

/*
 * 函数作用：
 *   周期性处理 USART0 与 RS485 的双向透明转发。
 * 主要流程：
 *   1. 若 RS485 收到一帧，则原样转发到默认调试串口 USART0。
 *   2. 若 USART0 收到一帧，则原样转发到 RS485 总线。
 * 说明：
 *   这里不再插入 "RS485 RX:" 等调试文本，也不对 USART0 做本地回显，
 *   避免上位机看到附加字符或误判为桥接链路异常。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void uart_task(void)
{
    uint16_t rx_length;
    uint16_t rs485_rx_length;

    /*
     * USART1/RS485 接收处理：
     *   中断层只负责把 DMA 数据搬到 rs485_dma_buffer 并置位标志；
     *   应用层在这里原样转发到 USART0，保持桥接数据透明。
     */
    rs485_rx_length = prv_uart_take_frame(&rs485_rx_flag,
                                          &rs485_dma_length,
                                          rs485_dma_buffer,
                                          rs485_forward_buffer,
                                          (uint16_t)sizeof(rs485_forward_buffer));
    if(rs485_rx_length > 0U){
        /* RS485 到调试串口保持原样转发，确保桥接链路对上位机透明。 */
        (void)prv_uart_send_buffer(DEBUG_USART, rs485_forward_buffer, rs485_rx_length);
    }

    /*
     * USART0 接收处理：
     *   上位机发来的数据被转发到 RS485 总线；发送函数内部负责方向脚切换和 TC 等待。
     */
    rx_length = prv_uart_take_frame(&rx_flag,
                                    &uart_dma_length,
                                    uart_dma_buffer,
                                    uart_forward_buffer,
                                    (uint16_t)sizeof(uart_forward_buffer));
    if(rx_length > 0U){
        (void)prv_rs485_send_buffer(uart_forward_buffer, rx_length);
    }
}
