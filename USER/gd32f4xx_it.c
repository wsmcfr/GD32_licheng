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
#include "system_all.h"

// NMI 不可屏蔽中断——不可恢复，停机保留现场
void NMI_Handler(void)
{
    while(1) {
    }
}

// HardFault——继续运行风险不可控，停机保留故障现场
void HardFault_Handler(void)
{
    while(1) {
    }
}

// MemManage——通常是栈溢出或 MPU 配置错误，停机排查
void MemManage_Handler(void)
{
    while(1) {
    }
}

// BusFault——非法外设地址或未就绪总线访问，停机定位访问源
void BusFault_Handler(void)
{
    while(1) {
    }
}

// UsageFault——结合 UFSR 分析，停机避免覆盖现场
void UsageFault_Handler(void)
{
    while(1) {
    }
}

// SVC——裸机工程未使用 SVC 服务，进入即流程异常
void SVC_Handler(void)
{
    while(1) {
    }
}

// DebugMon——未配置 DebugMon 业务，停机保留异常现场
void DebugMon_Handler(void)
{
    while(1) {
    }
}

// PendSV——裸机调度器不使用 PendSV，进入即流程异常
void PendSV_Handler(void)
{
    while(1) {
    }
}

/*
 * USART1/RS485 IDLE 中断：仅记录时间戳，不处理 DMA 数据。
 * 清 IDLE 标志后记录 ms tick，置 pending 标志，DMA 保持运行继续累积。
 * uart_task 在去抖超时后统一取出 DMA 数据解析协议帧。
 *
 * 背景：115200 下 USB 转串口芯片帧间隙约 1ms，会把一帧协议数据拆成多个 USB 包
 * 分别触发 IDLE；加 3ms 去抖确保所有分包到齐后再统一处理。
 */
void USART1_IRQHandler(void)
{
    if(RESET != usart_interrupt_flag_get(USART1, USART_INT_FLAG_IDLE)) {
        /* 清除 IDLE 标志：先读 STAT 再读 DATA 是 GD32F4xx 的标准清除序列。 */
        usart_data_receive(USART1);

        g_usart_idle_tick = timebase_get_ms32();
        g_usart_idle_pending = 1U;
    }
}

// SysTick 中断，推进本地 1ms timebase
void SysTick_Handler(void)
{
    systick_tick_inc();
}

/*
 * RTC 自动唤醒定时器中断（EXTI_22），深度睡眠 10s 后触发。
 * 只需清 EXTI 挂起标志，WFI 即可自动返回；RTC 标志和定时器由
 * cimc_power_sleep_10s() 在 WFI 返回后统一清理。
 */
void RTC_WKUP_IRQHandler(void)
{
    exti_flag_clear(EXTI_22);
}
