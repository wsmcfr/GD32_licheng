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

/*
 * 全局状态作用：
 *   记录本地 timebase 的毫秒计数、DWT 微秒换算参数和初始化状态。
 * 设计说明：
 *   1. g_timebase_ms_low 由 SysTick 中断每 1ms 推进一次，供高频路径快速读取。
 *   2. g_timebase_ms_high 只在低 32 位回绕时自增，用于拼接 64 位毫秒时间戳。
 *   3. g_timebase_ready 标记 SysTick timebase 是否已经完成初始化，避免早期误用。
 *   4. g_dwt_ready 标记 DWT 是否已启用，供 delay_us() 选择更快的微秒忙等路径。
 *   5. g_cycles_per_us 用于把微秒换算成 CPU cycle，必须在 SystemCoreClock 更新后重算。
 *   6. g_systick_reload 保存 1ms 节拍的 reload 计数，便于深睡恢复和调试检查。
 */
static volatile uint32_t g_timebase_ms_low;
static volatile uint32_t g_timebase_ms_high;
static volatile uint8_t g_timebase_ready;
static volatile uint8_t g_dwt_ready;
static uint32_t g_cycles_per_us;
static uint32_t g_systick_reload;

/*
 * 函数作用：
 *   进入短临界区，并返回进入前的 PRIMASK 状态，供后续恢复使用。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   进入临界区前的 PRIMASK 原始值，0 表示原本允许中断，非 0 表示原本已关中断。
 */
static uint32_t timebase_enter_critical(void)
{
    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();

    return primask;
}

/*
 * 函数作用：
 *   恢复进入临界区之前的 PRIMASK 状态。
 * 参数说明：
 *   primask：timebase_enter_critical() 返回的原始 PRIMASK 值。
 * 返回值说明：
 *   无返回值。
 */
static void timebase_exit_critical(uint32_t primask)
{
    __set_PRIMASK(primask);
}

/*
 * 函数作用：
 *   在当前系统主频下初始化 DWT 周期计数器，并计算微秒换算系数。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   DWT 只用于微秒忙等和兼容时间戳换算，不参与业务调度逻辑。
 */
static void timebase_dwt_init(void)
{
    uint32_t cycles_per_us;

    /*
     * DWT 延时以“至少等待指定时长”为目标。
     * 当 SystemCoreClock 不能被 1MHz 整除时，向上取整可以避免微秒延时偏短。
     */
    cycles_per_us = SystemCoreClock / 1000000U;
    if (0U != (SystemCoreClock % 1000000U)) {
        cycles_per_us++;
    }
    if (0U == cycles_per_us) {
        /*
         * 如果主频低于 1MHz，则至少按 1 cycle / us 处理，避免后续微秒延时除零。
         * 该工程实际工作频率远高于此值，这里主要是为了边界健壮性。
         */
        cycles_per_us = 1U;
    }

    /* 打开调试与跟踪单元，确保 Cortex-M4 的 DWT 周期计数器可用。 */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;

    /*
     * 清零后再启用 CYCCNT，避免唤醒或重新配置后沿用旧的周期计数残值。
     * 当前工程只使用 DWT 做忙等，不依赖它保留跨配置连续性。
     */
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    g_cycles_per_us = cycles_per_us;
    g_dwt_ready = 1U;
}

/*
 * 函数作用：
 *   使用 DWT 周期计数器执行精确的忙等延时。
 * 参数说明：
 *   cycles：需要等待的 CPU cycle 数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   该辅助函数只在 SysTick 不可用、全局中断关闭或处于 ISR 时作为降级路径使用。
 */
static void timebase_delay_cycles(uint64_t cycles)
{
    uint32_t start;
    uint32_t chunk;

    if (0U == g_dwt_ready) {
        timebase_dwt_init();
    }

    while (0ULL != cycles) {
        chunk = (cycles > (uint64_t)0x7FFFFFFFUL) ? 0x7FFFFFFFUL : (uint32_t)cycles;
        start = DWT->CYCCNT;
        while ((uint32_t)(DWT->CYCCNT - start) < chunk) {
            __NOP();
        }
        cycles -= chunk;
    }
}

/*
 * 函数作用：
 *   计算并配置 1ms 周期的 SysTick，同时完成本地 timebase 与 DWT 的初始化。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值；若 SysTick 配置失败则进入无限循环停机。
 */
void systick_config(void)
{
    uint32_t systick_reload;

    /* 当前工程默认使用 1ms 节拍作为系统时间基准。 */
    systick_reload = SystemCoreClock / 1000U;
    if ((0U == systick_reload) || (systick_reload > (SysTick_LOAD_RELOAD_Msk + 1U))) {
        /*
         * SysTick 是系统时间基准，如果 1ms reload 计算结果非法，后续所有延时和调度都会失真。
         * 这里采用 fail-stop，避免带着错误节拍继续运行。
         */
        while (1) {
        }
    }

    g_timebase_ready = 0U;
    g_systick_reload = systick_reload;

    if (SysTick_Config(systick_reload)) {
        /*
         * SysTick_Config() 返回非 0 说明 reload 配置失败或超过了 SysTick 24 位限制。
         * 继续运行没有可靠的时间基准，因此必须停机。
         */
        while (1) {
        }
    }

    /*
     * 让 SysTick 保持最高优先级，减少阻塞延时和调度时间抖动。
     * 该工程的 SysTick 主要承担节拍推进，因此优先级不应被普通外设抢占。
     */
    NVIC_SetPriority(SysTick_IRQn, 0x00U);

    /* SysTick 已经恢复后，再初始化 DWT 作为微秒级忙等和兼容时间戳的辅助路径。 */
    timebase_dwt_init();

    g_timebase_ready = 1U;
}

/*
 * 函数作用：
 *   推进本地毫秒时间基准，由 SysTick 中断周期调用。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   该函数只做最小化状态推进，避免在 1ms 中断路径里引入额外抖动。
 */
void systick_tick_inc(void)
{
    g_timebase_ms_low++;
    if (0U == g_timebase_ms_low) {
        /*
         * 低 32 位回绕时才推进高 32 位，保持 64 位毫秒时间戳连续递增。
         * 这种拆分方式比每次都做 64 位自增更轻量，也更适合 ISR。
         */
        g_timebase_ms_high++;
    }
}

/*
 * 函数作用：
 *   返回当前 32 位毫秒 tick，供高频路径快速读取。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   当前毫秒 tick 的低 32 位值。
 */
uint32_t timebase_get_ms32(void)
{
    return g_timebase_ms_low;
}

/*
 * 函数作用：
 *   返回从 timebase 初始化开始到当前的 64 位毫秒时间戳。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   当前累计毫秒数。
 */
int64_t get_system_ms(void)
{
    uint32_t ms_low;
    uint32_t ms_high;
    uint64_t timestamp;

    do {
        ms_high = g_timebase_ms_high;
        ms_low = g_timebase_ms_low;
    } while ((ms_high != g_timebase_ms_high) || (ms_low != g_timebase_ms_low));

    timestamp = ((uint64_t)ms_high << 32U) | (uint64_t)ms_low;
    return (int64_t)timestamp;
}

/*
 * 函数作用：
 *   返回从 timebase 初始化开始到当前的微秒时间戳。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   当前累计微秒数。
 * 说明：
 *   该接口使用毫秒 tick 作为主时间基准，再用 SysTick 当前递减值估算当前毫秒内
 *   已经过的微秒数。若 timebase 尚未就绪，则退化为毫秒值换算。
 */
int64_t get_system_us(void)
{
    uint32_t ms_low;
    uint32_t ms_high;
    uint32_t systick_value;
    uint32_t systick_reload;
    uint32_t cycles_per_us;
    uint32_t systick_pending;
    uint32_t elapsed_cycles;
    uint32_t partial_us;
    uint64_t timestamp_ms;

    if ((0U == g_timebase_ready) || (0U == g_cycles_per_us) || (0U == g_systick_reload)) {
        return get_system_ms() * 1000LL;
    }

    do {
        ms_high = g_timebase_ms_high;
        ms_low = g_timebase_ms_low;
        systick_value = SysTick->VAL;
        systick_reload = g_systick_reload;
        cycles_per_us = g_cycles_per_us;
        systick_pending = SCB->ICSR & SCB_ICSR_PENDSTSET_Msk;
    } while ((ms_high != g_timebase_ms_high) || (ms_low != g_timebase_ms_low));

    timestamp_ms = ((uint64_t)ms_high << 32U) | (uint64_t)ms_low;
    if (0U != systick_pending) {
        /*
         * 如果 SysTick 已经溢出但中断尚未执行，毫秒 tick 仍停在旧值，
         * 而 SysTick->VAL 已经进入下一毫秒周期。这里主动补偿 1ms，
         * 避免 get_system_us() 在关中断或高优先级 ISR 中出现短暂回退。
         */
        timestamp_ms++;
    }

    elapsed_cycles = (systick_value < systick_reload) ? (systick_reload - systick_value) : 0U;
    partial_us = elapsed_cycles / cycles_per_us;
    if (partial_us > 999U) {
        /*
         * SysTick 当前值和毫秒 tick 在边界附近可能存在极小相位差。
         * 这里把单毫秒内插值钳制到 0..999us，避免返回跨毫秒的重复时间。
         */
        partial_us = 999U;
    }

    return (int64_t)((timestamp_ms * 1000ULL) + (uint64_t)partial_us);
}

/*
 * 函数作用：
 *   执行毫秒级阻塞延时。
 * 参数说明：
 *   ms：需要延时的毫秒数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   正常运行时优先使用 SysTick 毫秒节拍差值；如果 timebase 尚未就绪、全局中断关闭，
 *   或者当前处于 ISR，则退化为 DWT 忙等，避免 SysTick 不推进时死等。
 */
void delay_ms(uint32_t ms)
{
    uint32_t start_time;

    if (0U == ms) {
        return;
    }

    if ((0U == g_timebase_ready) || (0U != __get_IPSR()) || (0U != __get_PRIMASK())) {
        /*
         * 当 SysTick 无法推进时，必须改走 DWT 降级路径。
         * 这种路径的 CPU 占用更高，但能保证在关中断阶段不出现永远等待。
         */
        if (0U == g_dwt_ready) {
            timebase_dwt_init();
        }
        timebase_delay_cycles((uint64_t)ms * (uint64_t)g_cycles_per_us * 1000ULL);
        return;
    }

    start_time = timebase_get_ms32();
    while ((uint32_t)(timebase_get_ms32() - start_time) < ms) {
        __NOP();
    }
}

/*
 * 函数作用：
 *   执行微秒级阻塞延时。
 * 参数说明：
 *   us：需要延时的微秒数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   该接口优先依赖 DWT 周期计数器，避免空循环在不同优化级别和主频下产生明显误差。
 */
void delay_us(uint32_t us)
{
    if (0U == us) {
        return;
    }

    if (0U == g_dwt_ready) {
        timebase_dwt_init();
    }
    timebase_delay_cycles((uint64_t)us * (uint64_t)g_cycles_per_us);
}

/*
 * 函数作用：
 *   兼容原厂 1ms 阻塞延时接口，内部复用本地毫秒延时实现。
 * 参数说明：
 *   count：需要延时的毫秒数。
 * 返回值说明：
 *   无返回值。
 */
void delay_1ms(uint32_t count)
{
    delay_ms(count);
}

/*
 * 函数作用：
 *   兼容旧 SysTick 延时框架的中断回调入口。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   当前工程已经改为由 systick_tick_inc() 维护 timebase，因此该函数保留为空实现，
 *   仅用于兼容旧代码和减少迁移时的符号缺失风险。
 */
void delay_decrement(void)
{
    /* 旧框架的递减计数已被本地 timebase 接管，保留空实现仅用于兼容。 */
}

/*
 * 函数作用：
 *   在进入深度睡眠或重新配置系统时钟前，统一停用并清理本地 timebase。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   该接口负责收口 SysTick 中断、清理 pending 状态，并标记 timebase 不可用，
 *   以避免唤醒恢复前误用旧的节拍状态。
 */
void timebase_prepare_reconfiguration(void)
{
    uint32_t primask;

    primask = timebase_enter_critical();
    g_timebase_ready = 0U;
    /*
     * DWT 的换算系数与 SystemCoreClock 绑定。时钟重配或深睡恢复前先标记失效，
     * 后续 timebase_update_after_clock_change() 会按新的主频重新初始化。
     */
    g_dwt_ready = 0U;
    g_cycles_per_us = 0U;

    /* 深睡或时钟重配前必须彻底停止 SysTick，避免旧节拍在低功耗阶段继续推进。 */
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL = 0U;

    /* 清掉已经挂起的 SysTick，避免恢复时先吃到一个陈旧中断。 */
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;

    timebase_exit_critical(primask);
}

/*
 * 函数作用：
 *   在系统时钟变化或深度睡眠唤醒后，重新建立本地 timebase。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   该接口复用 systick_config() 的完整恢复逻辑，语义上更贴近“恢复 timebase”而不是
 *   旧外部库的实现细节。
 */
void timebase_update_after_clock_change(void)
{
    systick_config();
}
