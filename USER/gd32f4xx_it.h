/*!
    \file    gd32f4xx_it.h
    \brief   the header file of the ISR

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

#ifndef GD32F4XX_IT_H
#define GD32F4XX_IT_H

#include "system_all.h"

/*
 * 函数作用：
 *   处理 NMI 不可屏蔽中断异常。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void NMI_Handler(void);

/*
 * 函数作用：
 *   处理 HardFault 硬 fault 异常。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void HardFault_Handler(void);

/*
 * 函数作用：
 *   处理 MemManage 内存管理异常。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void MemManage_Handler(void);

/*
 * 函数作用：
 *   处理 BusFault 总线访问异常。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void BusFault_Handler(void);

/*
 * 函数作用：
 *   处理 UsageFault 指令使用异常。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void UsageFault_Handler(void);

/*
 * 函数作用：
 *   处理 SVC 异常入口。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void SVC_Handler(void);

/*
 * 函数作用：
 *   处理 DebugMon 调试监控异常。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void DebugMon_Handler(void);

/*
 * 函数作用：
 *   处理 PendSV 异常入口。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void PendSV_Handler(void);

/*
 * 函数作用：
 *   处理 USART0 IDLE 中断并移交 DMA 接收帧。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void USART0_IRQHandler(void);

/*
 * 函数作用：
 *   处理 USART1/RS485 IDLE 中断，并移交 DMA 接收帧给 OTA 应用层。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void USART1_IRQHandler(void);

/*
 * 函数作用：
 *   处理 EXTI0 外部中断，主要用于唤醒按键中断标志清理。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void EXTI0_IRQHandler(void);

/*
 * 函数作用：
 *   处理 SysTick 中断并维护阻塞延时计数。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void SysTick_Handler(void);

#endif /* GD32F4XX_IT_H */
