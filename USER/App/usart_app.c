#include "usart_app.h"

__IO uint8_t rx_flag = 0;
__IO uint16_t uart_dma_length = 0;
uint8_t uart_dma_buffer[UART_APP_DMA_BUFFER_SIZE] = {0};
__IO uint8_t rs485_rx_flag = 0;
__IO uint16_t rs485_dma_length = 0;
uint8_t rs485_dma_buffer[UART_APP_RS485_BUFFER_SIZE] = {0};

/*
 * 变量作用：
 *   任务层专用快照缓冲区。
 * 说明：
 *   先从 ISR 共享缓冲区复制，再执行阻塞发送，避免发送过程中共享缓冲区被下一次中断改写。
 */
static uint8_t uart_forward_buffer[UART_APP_DMA_BUFFER_SIZE] = {0};
static uint8_t rs485_forward_buffer[UART_APP_RS485_BUFFER_SIZE] = {0};

/*
 * 宏作用：
 *   定义 my_printf() 的格式化缓冲区长度。
 * 说明：
 *   日志格式化不需要跟随 OTA 缓冲扩展，因此单独限制单行日志长度。
 */
#define UART_APP_PRINTF_BUFFER_SIZE    512U

/*
 * 函数作用：
 *   将中断上下文产生的一帧数据原子地转存到任务层私有缓冲区。
 * 主要流程：
 *   1. 关总中断，避免复制过程中 ISR 再次改写共享缓冲区。
 *   2. 按有效长度将共享缓冲区复制到任务层私有缓冲区。
 *   3. 清除共享长度和完成标志，然后恢复中断。
 * 参数说明：
 *   ready_flag：表示对应共享缓冲区当前是否已经有一帧待处理数据。
 *   shared_length：表示共享缓冲区当前有效长度。
 *   shared_buffer：ISR 使用的共享缓冲区。
 *   task_buffer：任务层私有缓冲区。
 *   task_buf_size：任务层私有缓冲区大小，单位为字节。
 * 返回值说明：
 *   返回成功取出的有效字节数；0 表示当前没有新帧或参数无效。
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
        if(valid_length > task_buf_size){
            valid_length = task_buf_size;
        }
        if(valid_length > 0U){
            memcpy(task_buffer, shared_buffer, valid_length);
        }
        /* 复制与清标志放在同一个临界区，避免任务层重复消费同一帧。 */
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
 *   1. 用 vsnprintf 将可变参数格式化到固定长度缓冲区。
 *   2. 根据格式化结果计算实际发送长度。
 *   3. 通过 Driver 层通用发送接口发送到目标串口。
 * 参数说明：
 *   usart_periph：目标 USART 外设编号。
 *   format：printf 风格格式字符串，后续可变参数必须与格式占位符匹配。
 * 返回值说明：
 *   正数或 0：vsnprintf 返回的完整格式化长度，可能大于实际发送长度。
 *   负数：格式化失败，函数不发送数据并原样返回该错误值。
 */
int my_printf(uint32_t usart_periph, const char *format, ...)
{
    char buffer[UART_APP_PRINTF_BUFFER_SIZE];
    va_list arg;
    int length;
    uint16_t send_length;

    va_start(arg, format);
    length = vsnprintf(buffer, sizeof(buffer), format, arg);
    va_end(arg);

    if(length <= 0){
        return length;
    }

    if(length >= (int)sizeof(buffer)){
        /* vsnprintf 已截断输出，发送时不把结尾 '\0' 当作串口数据发送。 */
        send_length = (uint16_t)(sizeof(buffer) - 1U);
    }else{
        send_length = (uint16_t)length;
    }

    (void)bsp_usart_send_buffer(usart_periph, (const uint8_t *)buffer, send_length);

    return length;
}

/*
 * 函数作用：
 *   通过 RS485 总线发送一段原始字节流。
 * 主要流程：
 *   1. 发送前先切换收发器到发送态。
 *   2. 使用 Driver 层串口发送接口把数据发到 USART1。
 *   3. 等待最后一字节发送完成后切回接收态。
 * 参数说明：
 *   data：待发送数据起始地址。
 *   length：待发送字节数，单位为字节。
 * 返回值说明：
 *   返回实际完成发送流程的字节数；0 表示参数无效或未能开始发送。
 */
static uint16_t prv_rs485_send_buffer(const uint8_t *data, uint16_t length)
{
    uint16_t sent_length;

    if((NULL == data) || (0U == length)){
        return 0U;
    }

    bsp_rs485_direction_transmit();
    sent_length = bsp_usart_send_buffer(RS485_USART, data, length);
    /* 等发送函数返回后再切回接收态，避免最后一字节还在移位时提前释放 DE。 */
    bsp_rs485_direction_receive();

    return sent_length;
}

/*
 * 函数作用：
 *   周期性处理 USART0 与 RS485 的双向透明转发。
 * 主要流程：
 *   1. 若 RS485 收到一帧，则原样转发到 USART0 调试串口。
 *   2. 若 USART0 收到一帧，则原样转发到 RS485 总线。
 * 说明：
 *   当前 OTA 已完全迁移到 USART2，本函数只处理普通串口透传，不再尝试解析 OTA 协议。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void uart_task(void)
{
    uint16_t rs485_rx_length;
    uint16_t rx_length;

    rs485_rx_length = prv_uart_take_frame(&rs485_rx_flag,
                                          &rs485_dma_length,
                                          rs485_dma_buffer,
                                          rs485_forward_buffer,
                                          (uint16_t)sizeof(rs485_forward_buffer));
    if(rs485_rx_length > 0U){
        /* RS485 到调试终端保持原样转发，确保桥接链路对上位机透明。 */
        (void)bsp_usart_send_buffer(DEBUG_USART, rs485_forward_buffer, rs485_rx_length);
    }

    rx_length = prv_uart_take_frame(&rx_flag,
                                    &uart_dma_length,
                                    uart_dma_buffer,
                                    uart_forward_buffer,
                                    (uint16_t)sizeof(uart_forward_buffer));
    if(rx_length > 0U){
        /* USART0 普通数据继续原样转发到 RS485，总线方向由发送函数内部切换。 */
        (void)prv_rs485_send_buffer(uart_forward_buffer, rx_length);
    }
}
