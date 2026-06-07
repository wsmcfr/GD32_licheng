#include "usart_app.h"

/* IDLE 去抖共享变量：ISR 写，uart_task 读 */
__IO uint32_t g_idle_ms = 0;
__IO uint8_t  g_idle_pend = 0;

/*
 * IDLE 去抖等待时间（毫秒）。
 * USB 转串口芯片在 115200 下可能把一帧协议数据拆成多个 USB 包，
 * 帧间隙约 1ms 触发 USART IDLE；等 3ms 确保所有包都到齐。
 */
#define IDLE_DEBOUNCE   3

/*
 * 周期处理 USART1/RS485 收到的协议帧（IDLE 去抖模式）。
 * 先等 g_idle_pend 置位，再确认距最后一次 IDLE 超过 3ms，
 * 然后关 DMA 取出数据、重置 DMA 后交给协议解析。
 * 3ms 去抖确保 USB 拆包全部到齐，避免部分帧 CRC 校验失败。
 */
void uart_task(void)
{
    uint32_t rlen;
    uint16_t clen;
    uint8_t  fbuf[UART_APP_DMA_BUFFER_SIZE];

    if(0 == g_idle_pend) 
	{
        return;
    }

    if((uint32_t)(timebase_get_ms32() - g_idle_ms) < IDLE_DEBOUNCE) 
	{
        return;
    }

    g_idle_pend = 0;

    dma_channel_disable(USART1_RX_DMA_PERIPH, USART1_RX_DMA_CHANNEL);

    rlen = sizeof(usart1_rxbuffer) -
           dma_transfer_number_get(USART1_RX_DMA_PERIPH, USART1_RX_DMA_CHANNEL);

    if((rlen > 0) && (rlen <= sizeof(usart1_rxbuffer))) 
	{
        clen = (uint16_t)rlen;
        if(clen >= (uint16_t)sizeof(fbuf)) 
		{
            clen = (uint16_t)(sizeof(fbuf) - 1);
        }
        memcpy(fbuf, usart1_rxbuffer, clen);
        fbuf[clen] = '\0';
    } 
	else 
	{
        clen = 0;
    }

    dma_flag_clear(USART1_RX_DMA_PERIPH, USART1_RX_DMA_CHANNEL, DMA_FLAG_FTF);
    dma_transfer_number_config(USART1_RX_DMA_PERIPH, USART1_RX_DMA_CHANNEL,
                               sizeof(usart1_rxbuffer));
    dma_channel_enable(USART1_RX_DMA_PERIPH, USART1_RX_DMA_CHANNEL);

    if(clen > 0) 
	{
        proto_rx(fbuf, clen);
    }
}
