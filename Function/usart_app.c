#include "usart_app.h"

/* IDLE 去抖共享变量：ISR 写，uart_task 读 */
__IO uint32_t g_idle_ms = 0;
__IO uint8_t  g_idle_pend = 0;

/* IDLE 去抖等待时间（毫秒）。*/
#define IDLE_DEBOUNCE   3

/*周期处理 USART1/RS485 收到的协议帧。*/
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
