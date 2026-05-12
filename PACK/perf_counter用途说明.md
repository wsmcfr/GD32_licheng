# perf_counter 在当前工程中的用途说明

## 1. 当前工程为什么要保留 perf_counter

当前工程是 GD32F470VET6 的 Keil MDK 工程，`perf_counter-2.5.4` 被接入为系统时间与阻塞延时辅助库。它不是普通示例文件，也不是只用于调试的无关目录；工程已经把它加入编译，并且应用初始化、低功耗唤醒恢复、OLED 等外设初始化流程都会间接依赖它。

| 证据位置 | 当前工程行为 | 说明 |
|---|---|---|
| `USER/system_all.h` | 包含 `perf_counter.h` | 工程公共头文件把 perf_counter 接入到全局公共依赖里。 |
| `MDK/2026706296.uvprojx` | 添加 `..\PACK\perf_counter-2.5.4` 到 IncludePath | Keil 编译时会从该目录查找 `perf_counter.h` 等头文件。 |
| `MDK/2026706296.uvprojx` | 编译 `perf_counter.c`、`perfc_port_default.c`、`systick_wrapper_ual.s` | 这 3 个源文件是当前工程实际参与构建的 perf_counter 实现。 |
| `USER/App/scheduler.c` | 调用 `init_cycle_counter(false)` | 系统初始化时启动 perf_counter 的周期计数服务。 |
| `USER/App/scheduler.c` | 调用 `delay_ms(200)` | 当前 `delay_ms` 宏来自 `perf_counter.h`，实际进入 `perfc_delay_ms()`。 |
| `USER/Driver/bsp_power.c` | 深度睡眠前调用 `before_cycle_counter_reconfiguration()` | 进入低功耗前通知 perf_counter 即将重配/停止 SysTick。 |
| `USER/Driver/bsp_power.c` | 唤醒后调用 `update_perf_counter()` | 系统时钟和 SysTick 恢复后，重新同步 perf_counter 的时间换算参数。 |
| `USER/gd32f4xx_it.c` | 实现 `SysTick_Handler()` | perf_counter 通过汇编 wrapper 在原 SysTick 中断前插入自己的计数更新逻辑，同时保留工程原有 `delay_decrement()`。 |

## 2. 当前工程用 perf_counter 干什么

| 用途 | 具体作用 | 当前工程中的意义 |
|---|---|---|
| 系统周期计数 | 通过 SysTick 当前计数值和溢出次数组合出连续递增的 tick 时间戳 | 给 `delay_ms()`、`delay_us()` 和性能测量宏提供统一时间基准。 |
| 毫秒/微秒阻塞延时 | `delay_ms(x)` 映射到 `perfc_delay_ms(x)`，`delay_us(x)` 映射到 `perfc_delay_us(x)` | OLED 初始化、上电等待、外设时序等待等位置需要阻塞延时。 |
| 兼容原 SysTick 中断 | `systick_wrapper_ual.s` 先执行 perf_counter 的 SysTick 插入函数，再跳回工程自己的 `SysTick_Handler()` | 不需要手动改 `gd32f4xx_it.c` 中的 SysTick 中断函数，也能让 perf_counter 维护内部时间基准。 |
| 低功耗唤醒后的时间校准 | 唤醒后重新配置系统时钟和 SysTick，再调用 `update_perf_counter()` | 深度睡眠会暂停/重配 SysTick，唤醒后必须更新 perf_counter 的换算参数，否则延时和时间戳可能不准。 |
| 代码耗时测量能力 | 提供 `get_system_ticks()`、`start_cycle_counter()`、`stop_cycle_counter()`、`__cycleof__()` 等接口 | 当前工程主要显式使用延时和初始化接口；这些接口保留后可用于后续性能分析。 |

## 3. 6 个必须保留文件的具体作用

| 文件 | 类型 | 主要职责 | 当前工程为什么需要 |
|---|---|---|---|
| `perf_counter.h` | 头文件 | 对外暴露 perf_counter 的 API、宏和数据结构；定义 `init_cycle_counter()`、`delay_ms()`、`delay_us()` 等兼容宏。 | `system_all.h` 已包含它；`scheduler.c` 使用的 `init_cycle_counter(false)` 和 `delay_ms(200)` 都依赖这里的声明/宏。 |
| `perf_counter.c` | C 源文件 | 实现核心计数逻辑，包括 `perfc_init()`、`update_perf_counter()`、`get_system_ticks()`、`perfc_delay_ms()`、`perfc_delay_us()` 等。 | Keil 工程已编译它；没有该文件会出现函数未定义，延时和时间戳功能无法链接。 |
| `perfc_common.h` | 公共头文件 | 提供编译器识别、通用宏、类型辅助、临界区/安全访问等基础定义。 | `perf_counter.h` 直接包含它；删除后 `perf_counter.h` 编译失败。 |
| `perfc_port_default.c` | 默认移植层源文件 | 直接操作 Cortex-M SysTick/SCB 寄存器，实现系统定时器初始化、读取、清除溢出、获取频率等底层接口。 | 当前工程使用默认 SysTick 移植方案，Keil 已编译该文件；`perf_counter.c` 的底层定时器操作依赖它。 |
| `perfc_port_default.h` | 默认移植层头文件 | 声明默认移植层接口，配置默认 SysTick port 的编译选项。 | `perf_counter.h` 在未指定自定义移植层时自动包含它；删除后头文件依赖断裂。 |
| `systick_wrapper_ual.s` | Keil/ARM 汇编源文件 | 通过 `$Sub$$SysTick_Handler` / `$Super$$SysTick_Handler` 包装 SysTick 中断：先调用 `perfc_port_insert_to_system_timer_insert_ovf_handler()`，再跳回工程原本的 `SysTick_Handler()`。 | 当前 Keil 工程已编译它，并定义了 `__PERF_COUNTER_CFG_USE_SYSTICK_WRAPPER__`；它让 perf_counter 不必手动侵入 `gd32f4xx_it.c`。 |

## 4. 当前工程的运行流程

### 4.1 上电初始化流程

| 步骤 | 代码位置 | 发生了什么 |
|---|---|---|
| 1 | `USER/App/scheduler.c` -> `system_init()` | 调用 `systick_config()`，把 SysTick 配置成 1ms 周期中断。 |
| 2 | `USER/App/scheduler.c` -> `system_init()` | 调用 `init_cycle_counter(false)`，宏展开后进入 `perfc_init(false)`，初始化 perf_counter。 |
| 3 | `PACK/perf_counter-2.5.4/perf_counter.c` | `perfc_init()` 调用底层 port 初始化 SysTick 计数参数，并调用 `update_perf_counter()` 计算 ms/us/tick 换算关系。 |
| 4 | `USER/App/scheduler.c` -> `system_init()` | 调用 `delay_ms(200)`，宏展开为 `perfc_delay_ms(200)`，基于 perf_counter 的 tick 计数阻塞等待。 |
| 5 | 后续外设初始化 | OLED 等组件继续使用 `delay_ms()` 完成硬件时序等待。 |

### 4.2 SysTick 中断流程

| 步骤 | 文件 | 作用 |
|---|---|---|
| 1 | `systick_wrapper_ual.s` | SysTick 中断进入后，wrapper 先接管入口。 |
| 2 | `systick_wrapper_ual.s` -> `perfc_port_insert_to_system_timer_insert_ovf_handler()` | 通知 perf_counter：SysTick 已经发生一次溢出/周期到达，需要累计内部时间基准。 |
| 3 | `systick_wrapper_ual.s` -> `$Super$$SysTick_Handler` | 跳回工程原本的 `SysTick_Handler()`。 |
| 4 | `USER/gd32f4xx_it.c` -> `SysTick_Handler()` | 工程自己的 SysTick 逻辑继续执行，目前主要调用 `delay_decrement()`。 |

### 4.3 深度睡眠与唤醒流程

| 步骤 | 代码位置 | 作用 |
|---|---|---|
| 1 | `USER/Driver/bsp_power.c` -> `bsp_enter_deepsleep()` | 调用 `before_cycle_counter_reconfiguration()`，通知 perf_counter 即将重配/停止系统定时器。 |
| 2 | `USER/Driver/bsp_power.c` -> `bsp_enter_deepsleep()` | 关闭 SysTick 中断，进入深度睡眠。 |
| 3 | `USER/Driver/bsp_power.c` -> `bsp_deepsleep_reinit_after_wakeup()` | 唤醒后重新执行 `SystemInit()`、`SystemCoreClockUpdate()` 和 `systick_config()`。 |
| 4 | `USER/Driver/bsp_power.c` -> `bsp_deepsleep_reinit_after_wakeup()` | 调用 `update_perf_counter()`，让 perf_counter 重新读取当前系统定时器频率并更新换算参数。 |

## 5. 为什么当前只保留这 6 个源码文件就够

当前工程使用的是“默认 SysTick 移植 + Keil wrapper”方案，不使用 PMU 移植、RTOS 补丁、协程扩展、官方示例、CMSIS-Pack 安装包或 benchmark。因此参与当前工程编译和头文件依赖的核心文件只有这 6 个。

| 已删除类别 | 当前不需要的原因 |
|---|---|
| `example/`、`CI/`、`benchmark/`、`documents/` | 示例、测试、跑分、文档图片，不参与工程编译。 |
| `os/` | 当前工程没有启用 FreeRTOS/RT-Thread/RTX5/ThreadX 的 perf_counter 补丁。 |
| `perfc_port_pmu.c/.h` | 当前工程未使用 PMU port，而是使用 default SysTick port。 |
| `perfc_task_coroutine.c`、`perfc_task_pt.h`、`__perfc_task_common.h` | 当前工程未定义协程/PT 扩展宏。 |
| `systick_wrapper_gcc.S`、`systick_wrapper_gnu.s` | 当前 Keil 工程使用 `systick_wrapper_ual.s`，不是 GCC/GNU wrapper。 |
| `cmsis-pack/`、`.pdsc`、`gen_pack.sh`、`SConscript`、`Doxyfile` | 打包、构建脚本或文档生成文件，当前工程直接引用源码，不依赖这些文件。 |

## 6. 后续维护注意事项

| 场景 | 需要注意什么 |
|---|---|
| 修改 SysTick 配置 | 修改前调用 `before_cycle_counter_reconfiguration()`，修改后调用 `update_perf_counter()`，否则 perf_counter 的时间换算可能不准。 |
| 修改系统主频 `SystemCoreClock` | 主频变化后必须重新配置 SysTick，并调用 `update_perf_counter()`。 |
| 修改 `SysTick_Handler()` | 不要删除工程自己的 `SysTick_Handler()`；Keil wrapper 需要 `$Super$$SysTick_Handler` 指向这个真实函数。 |
| 更换编译器为 GCC | 当前 `systick_wrapper_ual.s` 只适合 Keil/ARM 链接器方案，GCC 需要恢复对应的 `systick_wrapper_gcc.S` 或改成手动调用插入函数。 |
| 启用 RTOS | 可能需要恢复 `os/` 目录中的对应 RTOS patch，并重新检查 SysTick 所有权。 |
| 启用 PMU port | 需要恢复 `perfc_port_pmu.c/.h`，并修改工程宏和编译文件列表。 |
| 启用协程/PT 扩展 | 需要恢复 `perfc_task_coroutine.c`、`perfc_task_pt.h`、`__perfc_task_common.h`。 |

## 7. 一句话总结

当前工程保留的 6 个 perf_counter 源码文件，核心作用是：**基于 Cortex-M SysTick 提供连续系统时间戳、毫秒/微秒阻塞延时、低功耗唤醒后的计时校准，并通过 Keil 汇编 wrapper 自动接入现有 SysTick 中断。**
