#include "usart_app.h"

__IO uint8_t rx_flag = 0U;
__IO uint16_t uart_dma_length = 0U;
uint8_t uart_dma_buffer[UART_APP_DMA_BUFFER_SIZE] = {0U};

/*
 * 函数作用：
 *   从 USART1/RS485 共享接收缓冲区取走一帧数据。
 * 主要流程：
 *   1. 短暂关闭中断，避免复制共享缓冲区时被 USART1 ISR 改写。
 *   2. 将有效帧复制到调用方输出缓冲区，并补充字符串结束符。
 *   3. 清除共享长度和完成标志，允许下一帧覆盖共享缓冲区。
 * 参数说明：
 *   output：调用方提供的输出缓冲区，必须非空。
 *   output_size：输出缓冲区容量，单位为字节；函数会预留 1 字节给 '\0'。
 * 返回值说明：
 *   大于 0：成功取出的有效字节数。
 *   0：当前没有新帧，或参数无效。
 */
uint16_t uart_app_take_frame(uint8_t *output, uint16_t output_size)
{
    uint16_t valid_length = 0U;

    if((NULL == output) || (output_size < 2U)) {
        return 0U;
    }

    __disable_irq();
    if(0U != rx_flag) {
        valid_length = uart_dma_length;
        if(valid_length >= output_size) {
            /*
             * 赛题协议是 ASCII 文本帧，预留结尾 '\0' 可以让后续解析器安全使用字符串扫描。
             */
            valid_length = (uint16_t)(output_size - 1U);
        }

        if(valid_length > 0U) {
            memcpy(output, uart_dma_buffer, valid_length);
        }
        output[valid_length] = '\0';

        uart_dma_length = 0U;
        rx_flag = 0U;
    }
    __enable_irq();

    return valid_length;
}

/*
 * 函数作用：
 *   周期性处理 USART1/RS485 上收到的赛题协议帧。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   当前任务已经接入 Protocol/cimc_protocol.c。USART1 ISR 只负责复制完整
 *   ASCII HEX 帧，本任务再解析 CRC、命令字并执行应答或复位等业务动作。
 */
void uart_task(void)
{
    uint8_t frame_buffer[UART_APP_DMA_BUFFER_SIZE];
    uint16_t frame_length;

    frame_length = uart_app_take_frame(frame_buffer, (uint16_t)sizeof(frame_buffer));
    if(0U == frame_length) {
        return;
    }

    /*
     * 协议解析必须放在任务上下文执行。
     * ISR 只负责移交完整 ASCII 帧，这里再做 CRC、命令分发和 RS485 应答，
     * 避免中断内执行格式化、Flash 写入或软件复位等重操作。
     */
    (void)cimc_protocol_process_ascii_frame(frame_buffer, frame_length);
}
