#include "usart_app.h"

__IO uint8_t rx_flag = 0;
__IO uint16_t uart_dma_length = 0;
uint8_t uart_dma_buffer[UART_APP_DMA_BUFFER_SIZE] = {0};

/* IDLE 去抖共享变量：ISR 写，uart_task 读 */
__IO uint32_t g_usart_idle_tick = 0;
__IO uint8_t  g_usart_idle_pending = 0;

/*
 * IDLE 去抖等待时间（毫秒）。
 * USB 转串口芯片在 115200 下可能把一帧协议数据拆成多个 USB 包，
 * 帧间隙约 1ms 触发 USART IDLE；等 3ms 确保所有包都到齐。
 */
#define UART_IDLE_DEBOUNCE_MS   3

/*
 * 从 USART1/RS485 DMA 缓冲区取走一帧数据。
 * 关中断防止 ISR 在复制过程中写入竞争，output_size 必须 >=2（预留末尾 '\0'）。
 * 无新帧时返回 0。
 */
uint16_t uart_app_take_frame(uint8_t *output, uint16_t output_size)
{
    uint16_t valid_length = 0;

    if((NULL == output) || (output_size < 2)) {
        return 0;
    }

    __disable_irq();
    if(0 != rx_flag) {
        valid_length = uart_dma_length;
        if(valid_length >= output_size) {
            valid_length = (uint16_t)(output_size - 1);
        }

        if(valid_length > 0) {
            memcpy(output, uart_dma_buffer, valid_length);
        }
        output[valid_length] = '\0';

        uart_dma_length = 0;
        rx_flag = 0;
    }
    __enable_irq();

    return valid_length;
}

/*
 * 周期处理 USART1/RS485 收到的协议帧（IDLE 去抖模式）。
 * 先等 g_usart_idle_pending 置位，再确认距最后一次 IDLE 超过 3ms，
 * 然后关 DMA 取出数据、重置 DMA 后交给协议解析。
 * 3ms 去抖确保 USB 拆包全部到齐，避免部分帧 CRC 校验失败。
 */
void uart_task(void)
{
    uint32_t rx_len;
    uint16_t copy_len;
    uint8_t  frame_buffer[UART_APP_DMA_BUFFER_SIZE];

    if(0 == g_usart_idle_pending) {
        return;
    }

    if((uint32_t)(timebase_get_ms32() - g_usart_idle_tick) < UART_IDLE_DEBOUNCE_MS) {
        return;
    }

    g_usart_idle_pending = 0;

    dma_channel_disable(USART1_RX_DMA_PERIPH, USART1_RX_DMA_CHANNEL);

    rx_len = sizeof(usart1_rxbuffer) -
             dma_transfer_number_get(USART1_RX_DMA_PERIPH, USART1_RX_DMA_CHANNEL);

    if((rx_len > 0) && (rx_len <= sizeof(usart1_rxbuffer))) {
        copy_len = (uint16_t)rx_len;
        if(copy_len >= (uint16_t)sizeof(frame_buffer)) {
            copy_len = (uint16_t)(sizeof(frame_buffer) - 1);
        }
        memcpy(frame_buffer, usart1_rxbuffer, copy_len);
        frame_buffer[copy_len] = '\0';
    } else {
        copy_len = 0;
    }

    dma_flag_clear(USART1_RX_DMA_PERIPH, USART1_RX_DMA_CHANNEL, DMA_FLAG_FTF);
    dma_transfer_number_config(USART1_RX_DMA_PERIPH, USART1_RX_DMA_CHANNEL,
                               sizeof(usart1_rxbuffer));
    dma_channel_enable(USART1_RX_DMA_PERIPH, USART1_RX_DMA_CHANNEL);

    if(copy_len > 0) {
        proto_rx(frame_buffer, copy_len);
    }
}
