/*!
    \file    systick.h
    \brief   the header file of systick

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

#ifndef SYS_TICK_H
#define SYS_TICK_H

#include <stdint.h>

/*
 * 函数作用：
 *   配置 SysTick 为 1ms 系统节拍，并初始化本地 timebase 与 DWT 计数器。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值；若底层 SysTick 配置失败则进入无限循环停机。
 * 说明：
 *   该函数是工程当前唯一的 timebase 初始化入口，后续唤醒恢复也复用它。
 */
void systick_config(void);

/*
 * 函数作用：
 *   推进本地毫秒时间基准，由 SysTick 中断周期调用。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   该函数必须保持极轻量，只能做 tick 累加和回绕处理。
 */
void systick_tick_inc(void);

/*
 * 函数作用：
 *   返回当前 32 位毫秒 tick，供调度器这类高频路径快速读取。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   当前毫秒 tick 的低 32 位值。
 */
uint32_t timebase_get_ms32(void);

/*
 * 函数作用：
 *   返回从 timebase 初始化开始到当前的 64 位毫秒时间戳。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   当前累计毫秒数；若在初始化之前调用，返回值为当前已累计状态。
 */
int64_t get_system_ms(void);

/*
 * 函数作用：
 *   返回从 timebase 初始化开始到当前的微秒时间戳。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   当前累计微秒数；由毫秒 tick 和 SysTick 当前计数估算得到。
 */
int64_t get_system_us(void);

/*
 * 函数作用：
 *   执行毫秒级阻塞延时。
 * 参数说明：
 *   ms：需要延时的毫秒数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   正常情况下使用 32 位毫秒 tick 差值等待；若 timebase 尚未就绪或中断被关闭，
 *   会退化为基于 DWT 的短延时循环，避免 SysTick 无法推进时死等。
 */
void delay_ms(uint32_t ms);

/*
 * 函数作用：
 *   执行微秒级阻塞延时。
 * 参数说明：
 *   us：需要延时的微秒数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   该接口以 DWT 周期计数器为核心，优先保证微秒时序稳定。
 */
void delay_us(uint32_t us);

/*
 * 函数作用：
 *   兼容原厂 1ms 阻塞延时接口，内部复用本地毫秒延时实现。
 * 参数说明：
 *   count：需要延时的毫秒数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   该接口保留用于兼容旧代码，不再依赖独立的递减计数器。
 */
void delay_1ms(uint32_t count);

/*
 * 函数作用：
 *   兼容旧 SysTick 延时框架的中断回调入口，当前工程不再依赖它递减计数。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   保留该声明是为了减少旧代码迁移成本，但当前实现不会再驱动核心延时逻辑。
 */
void delay_decrement(void);

/*
 * 函数作用：
 *   在进入深度睡眠或重新配置系统时钟前，统一停用并清理本地 timebase。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   该接口负责把 SysTick 停止、pending 清理和内部状态收口，避免唤醒后吃到旧节拍。
 */
void timebase_prepare_reconfiguration(void);

/*
 * 函数作用：
 *   在系统时钟变化或深度睡眠唤醒后，重新建立本地 timebase。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   当前实现直接复用 systick_config() 的恢复逻辑，但保留独立接口是为了让低功耗流程
 *   的语义清晰，不再泄露旧外部 timebase 的命名。
 */
void timebase_update_after_clock_change(void);

#endif /* SYS_TICK_H */
