/*!
    \file    gd32f4xx_it.c
    \brief   interrupt service routines

    \version 2024-12-20, V3.3.1, firmware for GD32F4xx
*/

/*
    Copyright (c) 2024, GigaDevice Semiconductor Inc.

    Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice, this
       list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice,
       this list of conditions and the following disclaimer in the documentation
       and/or other materials provided with the distribution.
    3. Neither the name of the copyright holder nor the names of its contributors
       may be used to endorse or promote products derived from this software without
       specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
OF SUCH DAMAGE.
*/

#include "gd32f4xx_it.h"

/*
 * 函数作用：
 *   处理 NMI 不可屏蔽中断异常。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值；发生该异常后进入无限循环，便于调试器停机定位。
 */
void NMI_Handler(void)
{
    /* NMI 属于不可恢复异常，停在现场便于查看堆栈和寄存器。 */
    while(1) {
    }
}

/*
 * 函数作用：
 *   处理 HardFault 硬 fault 异常。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值；发生该异常后进入无限循环，保留故障现场。
 */
void HardFault_Handler(void)
{
    /* HardFault 后继续运行风险不可控，直接停机保留故障现场。 */
    while(1) {
    }
}

/*
 * 函数作用：
 *   处理 MemManage 内存管理异常。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值；发生该异常后进入无限循环，保留故障现场。
 */
void MemManage_Handler(void)
{
    /* 内存访问异常通常表示栈、指针或 MPU 配置错误，需要停机排查。 */
    while(1) {
    }
}

/*
 * 函数作用：
 *   处理 BusFault 总线访问异常。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值；发生该异常后进入无限循环，保留故障现场。
 */
void BusFault_Handler(void)
{
    /* 总线异常可能来自非法外设地址或未就绪总线访问，停机便于定位访问源。 */
    while(1) {
    }
}

/*
 * 函数作用：
 *   处理 UsageFault 指令使用异常。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值；发生该异常后进入无限循环，保留故障现场。
 */
void UsageFault_Handler(void)
{
    /* 指令使用异常通常需要结合异常寄存器分析，直接停机避免覆盖现场。 */
    while(1) {
    }
}

/*
 * 函数作用：
 *   处理 SVC 异常入口。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值；当前工程未使用 SVC 服务，异常后停机。
 */
void SVC_Handler(void)
{
    /* 当前裸机工程没有 SVC 调度需求，进入该入口说明流程异常。 */
    while(1) {
    }
}

/*
 * 函数作用：
 *   处理 DebugMon 调试监控异常。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值；当前工程未使用 DebugMon，异常后停机。
 */
void DebugMon_Handler(void)
{
    /* 当前工程未配置 DebugMon 业务处理，停机保留异常现场。 */
    while(1) {
    }
}

/*
 * 函数作用：
 *   处理 PendSV 异常入口。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值；当前工程未使用 RTOS 上下文切换，异常后停机。
 */
void PendSV_Handler(void)
{
    /* 裸机调度器不使用 PendSV，进入该入口说明异常触发来源需要排查。 */
    while(1) {
    }
}

/*
 * 函数作用：
 *   处理 USART0 IDLE 中断，将 DMA 接收到的一帧调试串口数据移交给应用层。
 * 主要流程：
 *   1. 判断并清除 IDLE 中断标志。
 *   2. 暂停 DMA，按剩余传输计数计算本帧有效长度。
 *   3. 做长度边界检查后追加到 uart_dma_buffer，并置位 rx_flag。
 *   4. 重新装载 DMA 计数，准备下一帧接收。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void USART0_IRQHandler(void)
{
    uint32_t rx_len;
    uint32_t copy_len;

    if(RESET != usart_interrupt_flag_get(USART0, USART_INT_FLAG_IDLE)){
        /* 清除 IDLE 标志：先读状态再读数据寄存器是 GD32 USART 空闲中断的清除流程。 */
        usart_data_receive(USART0);
        dma_channel_disable(USART0_RX_DMA_PERIPH, USART0_RX_DMA_CHANNEL);
        
        /* 根据 DMA 剩余传输数计算本次 IDLE 前收到的字节数。 */
        rx_len = sizeof(usart0_rxbuffer) - dma_transfer_number_get(USART0_RX_DMA_PERIPH, USART0_RX_DMA_CHANNEL);
        if((rx_len > 0U) && (rx_len <= sizeof(usart0_rxbuffer))){
            copy_len = rx_len;
            /*
             * 大文件通过 USB 转串口发送时可能被拆成多个 IDLE 小块。
             * 这里把每个小块追加到 App 缓冲区，直到任务层识别出完整 OTA 包；
             * 如果缓冲区满，只保留已经收到的数据并等待任务层按错误处理。
             */
            if((uint32_t)uart_dma_length + copy_len > sizeof(uart_dma_buffer)){
                copy_len = sizeof(uart_dma_buffer) - (uint32_t)uart_dma_length;
            }
            if(copy_len > 0U){
                memcpy(&uart_dma_buffer[uart_dma_length], usart0_rxbuffer, copy_len);
                uart_dma_length = (uint16_t)((uint32_t)uart_dma_length + copy_len);
                rx_flag = 1;
            }
        }

        /*
         * 应用层严格依赖 uart_dma_length 取有效数据，这里不再整块清空缓冲区，
         * 以缩短中断执行时间，避免高频串口流量下的额外开销。
         */
        /* 重新装载 DMA 计数并打开通道，准备接收下一帧。 */
        dma_flag_clear(USART0_RX_DMA_PERIPH, USART0_RX_DMA_CHANNEL, DMA_FLAG_FTF);
        dma_transfer_number_config(USART0_RX_DMA_PERIPH, USART0_RX_DMA_CHANNEL, sizeof(usart0_rxbuffer));
        dma_channel_enable(USART0_RX_DMA_PERIPH, USART0_RX_DMA_CHANNEL);
    }
}

/*
 * 函数作用：
 *   处理 USART1/RS485 IDLE 中断，将 DMA 接收到的一帧 RS485 数据移交给应用层。
 * 主要流程：
 *   1. 判断并清除 USART1 IDLE 中断标志。
 *   2. 暂停 DMA，计算 RS485 本帧有效长度。
 *   3. 做长度边界检查后复制到 rs485_dma_buffer，并置位 rs485_rx_flag。
 *   4. 重新装载 DMA 计数，准备下一帧 RS485 接收。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void USART1_IRQHandler(void)
{
    uint32_t rx_len;
    uint32_t copy_len;

    if(RESET != usart_interrupt_flag_get(USART1, USART_INT_FLAG_IDLE)){
        /* 清除 IDLE 标志：读数据寄存器用于结束本次空闲中断状态。 */
        usart_data_receive(USART1);
        dma_channel_disable(USART1_RX_DMA_PERIPH, USART1_RX_DMA_CHANNEL);

        /*
         * 根据 DMA 剩余传输数计算 RS485 本帧长度。
         * 中断内只做边界检查、内存拷贝和置标志，具体桥接转发交给 uart_task()。
         */
        rx_len = sizeof(usart1_rxbuffer) - dma_transfer_number_get(USART1_RX_DMA_PERIPH, USART1_RX_DMA_CHANNEL);
        if((rx_len > 0U) && (rx_len <= sizeof(usart1_rxbuffer))){
            copy_len = rx_len;
            if(copy_len >= sizeof(rs485_dma_buffer)){
                copy_len = sizeof(rs485_dma_buffer) - 1U;
            }
            memcpy(rs485_dma_buffer, usart1_rxbuffer, copy_len);
            rs485_dma_length = (uint16_t)copy_len;
            rs485_rx_flag = 1U;
        }

        /*
         * 应用层严格依赖 rs485_dma_length 取有效数据，这里不再整块清空缓冲区，
         * 以缩短 RS485 中断占用时间，避免影响下一帧 DMA 重装。
         */
        /* 重新装载 DMA 计数并打开通道，准备接收下一帧 RS485 数据。 */
        dma_flag_clear(USART1_RX_DMA_PERIPH, USART1_RX_DMA_CHANNEL, DMA_FLAG_FTF);
        dma_transfer_number_config(USART1_RX_DMA_PERIPH, USART1_RX_DMA_CHANNEL, sizeof(usart1_rxbuffer));
        dma_channel_enable(USART1_RX_DMA_PERIPH, USART1_RX_DMA_CHANNEL);
    }
}

/*
 * 函数作用：
 *   处理 EXTI0 外部中断，用于唤醒按键触发后的中断标志清理。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void EXTI0_IRQHandler(void)
{
    if(RESET != exti_interrupt_flag_get(EXTI_0)) {
        /* 唤醒后只清除中断标志，具体外设恢复由深度睡眠返回路径处理。 */
        exti_interrupt_flag_clear(EXTI_0);
    }
}

/*
 * 函数作用：
 *   处理 SDIO 中断，并转交给 SD 卡组件完成底层状态推进。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void SDIO_IRQHandler(void)
{
    sd_interrupts_process();
}

/*
 * 函数作用：
 *   处理 SysTick 中断，递减阻塞延时计数。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void SysTick_Handler(void)
{
    delay_decrement();
}
