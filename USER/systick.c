/*!
    \file    systick.c
    \brief   the systick configuration file

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

#include "gd32f4xx.h"
#include "systick.h"

volatile static uint32_t delay;

/*
 * 函数作用：
 *   配置 SysTick 为 1ms 周期中断，并设置最高优先级。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值；若 SysTick_Config 配置失败则进入无限循环停机。
 */
void systick_config(void)
{
    /* SystemCoreClock / 1000 生成 1ms 节拍，作为 delay 和调度时间基准。 */
    if(SysTick_Config(SystemCoreClock / 1000U)) {
        /* SysTick 是系统时间基准，配置失败后继续运行没有确定时序，因此停机。 */
        while(1) {
        }
    }
    /* 让 SysTick 保持高优先级，减少阻塞延时和周期调度的时间抖动。 */
    NVIC_SetPriority(SysTick_IRQn, 0x00U);
}

/*
 * 函数作用：
 *   基于 SysTick 递减计数实现毫秒级阻塞延时。
 * 参数说明：
 *   count：需要延时的毫秒数，函数会阻塞直到该计数递减到 0。
 * 返回值说明：
 *   无返回值。
 */
void delay_1ms(uint32_t count)
{
    delay = count;

    /* delay 由 SysTick_Handler() 递减，这里只等待计数归零。 */
    while(0U != delay) {
    }
}

/*
 * 函数作用：
 *   在 SysTick 中断中递减阻塞延时计数。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void delay_decrement(void)
{
    if(0U != delay) {
        delay--;
    }
}
