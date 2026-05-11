/* Licence
* Company: MCUSTUDIO
* Auther: Ahypnis.
* Version: V0.10
* Time: 2025/06/05
* Note:
*/
#include "mcu_cmic_gd32f470vet6.h"

__IO uint16_t tx_count = 0;
__IO uint8_t rx_flag = 0;
uint8_t uart_dma_buffer[512] = {0};

int my_printf(uint32_t usart_periph, const char *format, ...)
{
    char buffer[512];
    va_list arg;
    int len;
    // Initialize variable argument list.
    va_start(arg, format);
    len = vsnprintf(buffer, sizeof(buffer), format, arg);
    va_end(arg);
    
    for(tx_count = 0; tx_count < len; tx_count++){
        usart_data_transmit(usart_periph, buffer[tx_count]);
        while(RESET == usart_flag_get(usart_periph, USART_FLAG_TBE));
    }
    
    return len;
}

void uart_task(void)
{
    if(!rx_flag) return;
    
    my_printf(DEBUG_USART, "%s", uart_dma_buffer);
    memset(uart_dma_buffer, 0, sizeof(uart_dma_buffer));
    rx_flag = 0;
}
