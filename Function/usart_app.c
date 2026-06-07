#include "usart_app.h"

__IO uint8_t rx_flag = 0U;
__IO uint16_t uart_dma_length = 0U;
uint8_t uart_dma_buffer[UART_APP_DMA_BUFFER_SIZE] = {0U};

/* IDLE 去抖共享变量：ISR 写，uart_task 读 */
__IO uint32_t g_usart_idle_tick = 0U;
__IO uint8_t  g_usart_idle_pending = 0U;

/*
 * 宏作用：
 *   IDLE 去抖等待时间（毫秒）。
 *   USB 转串口芯片在 115200 下可能把一帧协议数据拆成多个 USB 包，
 *   帧间隙约 1ms 会触发 USART IDLE 中断。等 3ms 确保所有 USB 包都到齐。
 */
#define UART_IDLE_DEBOUNCE_MS   3U

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
 *   周期性处理 USART1/RS485 上收到的赛题协议帧（IDLE 去抖模式）。
 * 主要流程：
 *   1. 检查 ISR 是否标记了 IDLE 事件。
 *   2. 等待最后一次 IDLE 后至少 UART_IDLE_DEBOUNCE_MS 毫秒，
 *      确保 USB 转串口芯片在 115200 下拆分成多个 USB 包的数据全部到齐。
 *   3. 关 DMA，取出完整帧数据，重置 DMA 后交给协议解析。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void uart_task(void)
{
    uint32_t rx_len;
    uint16_t copy_len;
    uint8_t  frame_buffer[UART_APP_DMA_BUFFER_SIZE];

    /* 没有 IDLE 事件则直接返回 */
    if(0U == g_usart_idle_pending) {
        return;
    }

    /* 距最后一次 IDLE 不足去抖时间，等待 USB 后续分包到齐 */
    if((uint32_t)(timebase_get_ms32() - g_usart_idle_tick) < UART_IDLE_DEBOUNCE_MS) {
        return;
    }

    /* 去抖完成，取出 DMA 累积的全部数据 */
    g_usart_idle_pending = 0U;

    dma_channel_disable(USART1_RX_DMA_PERIPH, USART1_RX_DMA_CHANNEL);

    rx_len = sizeof(usart1_rxbuffer) -
             dma_transfer_number_get(USART1_RX_DMA_PERIPH, USART1_RX_DMA_CHANNEL);

    if((rx_len > 0U) && (rx_len <= sizeof(usart1_rxbuffer))) {
        copy_len = (uint16_t)rx_len;
        if(copy_len >= (uint16_t)sizeof(frame_buffer)) {
            copy_len = (uint16_t)(sizeof(frame_buffer) - 1U);
        }
        memcpy(frame_buffer, usart1_rxbuffer, copy_len);
        frame_buffer[copy_len] = '\0';
    } else {
        copy_len = 0U;
    }

    /* 重置 DMA，准备接收下一帧 */
    dma_flag_clear(USART1_RX_DMA_PERIPH, USART1_RX_DMA_CHANNEL, DMA_FLAG_FTF);
    dma_transfer_number_config(USART1_RX_DMA_PERIPH, USART1_RX_DMA_CHANNEL,
                               sizeof(usart1_rxbuffer));
    dma_channel_enable(USART1_RX_DMA_PERIPH, USART1_RX_DMA_CHANNEL);

    if(copy_len > 0U) {
        (void)cimc_protocol_process_ascii_frame(frame_buffer, copy_len);
    }
}
