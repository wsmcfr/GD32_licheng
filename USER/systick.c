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

/* timebase状态。 */
static volatile uint32_t g_timebase_ms_low;
static volatile uint8_t  g_timebase_ready;
static volatile uint8_t  g_dwt_ready;
static uint32_t g_cycles_per_us;  /* DWT换算 */

/* 初始化DWT计数器。 */
static void timebase_dwt_init(void)
{
    uint32_t cycles_per_us = SystemCoreClock / 1000000;
    if(SystemCoreClock % 1000000 != 0) cycles_per_us++;
    if(cycles_per_us == 0) cycles_per_us = 1;

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;

    g_cycles_per_us = cycles_per_us;
    g_dwt_ready = 1;
}

/* DWT忙等。 */
static void timebase_delay_cycles(uint64_t cycles)
{
    uint32_t start, chunk;

    if(!g_dwt_ready) timebase_dwt_init();

    while(cycles) {
        chunk = (cycles > (uint64_t)0x7FFFFFFF) ? 0x7FFFFFFF : (uint32_t)cycles;
        start = DWT->CYCCNT;
        while((uint32_t)(DWT->CYCCNT - start) < chunk) __NOP();
        cycles -= chunk;
    }
}

/* 配置1ms SysTick。 */
void systick_config(void)
{
    uint32_t systick_reload = SystemCoreClock / 1000;
    if(!systick_reload || systick_reload > (SysTick_LOAD_RELOAD_Msk + 1)) {
        while(1) {}
    }

    g_timebase_ready = 0;

    if(SysTick_Config(systick_reload)) {
        while(1) {}
    }

    NVIC_SetPriority(SysTick_IRQn, 0x00);
    timebase_dwt_init();
    g_timebase_ready = 1;
}

/* SysTick每1ms调用。 */
void systick_tick_inc(void)
{
    g_timebase_ms_low++;
}

// 读取32位毫秒tick。
uint32_t timebase_get_ms32(void)
{
    return g_timebase_ms_low;
}

/* 毫秒阻塞延时。 */
void delay_ms(uint32_t ms)
{
    uint32_t start_time;

    if(!ms) return;

    if(!g_timebase_ready || __get_IPSR() || __get_PRIMASK()) {
        if(!g_dwt_ready) timebase_dwt_init();
        timebase_delay_cycles((uint64_t)ms * (uint64_t)g_cycles_per_us * 1000ULL);
        return;
    }

    start_time = timebase_get_ms32();
    while((uint32_t)(timebase_get_ms32() - start_time) < ms) __NOP();
}

/* 微秒阻塞延时。 */
void delay_us(uint32_t us)
{
    if(!us) return;
    if(!g_dwt_ready) timebase_dwt_init();
    timebase_delay_cycles((uint64_t)us * (uint64_t)g_cycles_per_us);
}
