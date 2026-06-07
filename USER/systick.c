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

/* timebase状态：ms低32位（ISR推进）、ms高32位（回绕时递增）、就绪标记、DWT标记 */
static volatile uint32_t g_timebase_ms_low;
static volatile uint32_t g_timebase_ms_high;
static volatile uint8_t  g_timebase_ready;
static volatile uint8_t  g_dwt_ready;
static uint32_t g_cycles_per_us;  /* 每微秒CPU周期数，DWT忙等换算用 */
static uint32_t g_systick_reload; /* 1ms节拍reload值，深睡恢复后复用 */

// 进入短临界区，返回进入前PRIMASK供恢复
static uint32_t timebase_enter_critical(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

// 恢复进入临界区前的PRIMASK
static void timebase_exit_critical(uint32_t primask)
{
    __set_PRIMASK(primask);
}

/* 初始化DWT周期计数器，计算微秒换算系数 */
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

/* DWT周期计数忙等，SysTick不可用或关中断时的降级路径 */
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

/* 配置1ms SysTick节拍，同步初始化DWT；reload非法时死循环停机 */
void systick_config(void)
{
    uint32_t systick_reload = SystemCoreClock / 1000;
    if(!systick_reload || systick_reload > (SysTick_LOAD_RELOAD_Msk + 1)) {
        while(1) {}
    }

    g_timebase_ready = 0;
    g_systick_reload = systick_reload;

    if(SysTick_Config(systick_reload)) {
        while(1) {}
    }

    NVIC_SetPriority(SysTick_IRQn, 0x00);
    timebase_dwt_init();
    g_timebase_ready = 1;
}

/* SysTick中断每1ms调用，低32位回绕时递增高32位 */
void systick_tick_inc(void)
{
    g_timebase_ms_low++;
    if(!g_timebase_ms_low) g_timebase_ms_high++;
}

// 快速读取当前32位毫秒tick
uint32_t timebase_get_ms32(void)
{
    return g_timebase_ms_low;
}

// 读取64位毫秒时间戳，循环采样防撕裂
int64_t get_system_ms(void)
{
    uint32_t ms_low, ms_high;
    do {
        ms_high = g_timebase_ms_high;
        ms_low  = g_timebase_ms_low;
    } while((ms_high != g_timebase_ms_high) || (ms_low != g_timebase_ms_low));

    return (int64_t)(((uint64_t)ms_high << 32) | (uint64_t)ms_low);
}

/*
 * 读取微秒时间戳。以毫秒tick为主基准，用SysTick当前递减值估算毫秒内已过微秒数。
 * timebase未就绪时退化为ms*1000。
 */
int64_t get_system_us(void)
{
    uint32_t ms_low, ms_high;
    uint32_t systick_value, systick_reload, cycles_per_us, systick_pending;
    uint32_t elapsed_cycles, partial_us;
    uint64_t timestamp_ms;

    if(!g_timebase_ready || !g_cycles_per_us || !g_systick_reload) {
        return get_system_ms() * 1000LL;
    }

    do {
        ms_high         = g_timebase_ms_high;
        ms_low          = g_timebase_ms_low;
        systick_value   = SysTick->VAL;
        systick_reload  = g_systick_reload;
        cycles_per_us   = g_cycles_per_us;
        systick_pending = SCB->ICSR & SCB_ICSR_PENDSTSET_Msk;
    } while((ms_high != g_timebase_ms_high) || (ms_low != g_timebase_ms_low));

    timestamp_ms = ((uint64_t)ms_high << 32) | (uint64_t)ms_low;
    /* SysTick已溢出但中断未执行时，主动补偿1ms避免时间戳回退 */
    if(systick_pending) timestamp_ms++;

    elapsed_cycles = (systick_value < systick_reload) ? (systick_reload - systick_value) : 0;
    partial_us = elapsed_cycles / cycles_per_us;
    if(partial_us > 999) partial_us = 999;

    return (int64_t)((timestamp_ms * 1000ULL) + (uint64_t)partial_us);
}

/* 毫秒阻塞延时；timebase未就绪、关中断或在ISR中退化为DWT忙等 */
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

/* 微秒阻塞延时，优先使用DWT周期计数 */
void delay_us(uint32_t us)
{
    if(!us) return;
    if(!g_dwt_ready) timebase_dwt_init();
    timebase_delay_cycles((uint64_t)us * (uint64_t)g_cycles_per_us);
}

// 兼容原厂1ms延时接口
void delay_1ms(uint32_t count)
{
    delay_ms(count);
}

// 旧框架递减回调，已被本地timebase接管，保留空实现兼容旧代码
void delay_decrement(void)
{
}

/* 深睡或时钟重配前停用SysTick和DWT，并清挂起位 */
void timebase_prepare_reconfiguration(void)
{
    uint32_t primask = timebase_enter_critical();

    g_timebase_ready = 0;
    g_dwt_ready      = 0;
    g_cycles_per_us  = 0;

    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;

    timebase_exit_critical(primask);
}

// 时钟重配或深睡唤醒后重建timebase
void timebase_update_after_clock_change(void)
{
    systick_config();
}

/*
 * 向timebase追加已知经过的毫秒数，用于深睡唤醒后用RTC秒差补偿时间戳。
 * 低32位回绕时推进高32位，保持get_system_ms()结果单调递增。
 */
void timebase_adjust_ms(uint32_t elapsed_ms)
{
    uint32_t primask, old_low, new_low;

    if(!elapsed_ms) return;

    primask = timebase_enter_critical();
    old_low = g_timebase_ms_low;
    new_low = old_low + elapsed_ms;
    g_timebase_ms_low = new_low;
    if(new_low < old_low) g_timebase_ms_high++;
    timebase_exit_critical(primask);
}
