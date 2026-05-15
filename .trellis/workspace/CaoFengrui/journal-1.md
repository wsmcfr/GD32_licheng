# Journal - CaoFengrui (Part 1)

> AI development session journal
> Started: 2026-05-11

---



## Session 1: Bootstrap firmware guidelines

**Date**: 2026-05-11
**Task**: Bootstrap firmware guidelines
**Branch**: `main`

### Summary

本次会话完成了工程注释规范化收尾：按 `AGENTS.md` 中文注释模板补齐自有源码的函数作用、参数说明、返回值说明，并提交推送到 GitHub。

### Main Changes

| 项目 | 内容 |
|------|------|
| 任务 | Bootstrap Guidelines |
| 结果 | 补全 `.trellis/spec/backend/`、`.trellis/spec/frontend/` 与索引导航，使 Trellis 模板适配当前 GD32 裸机固件工程 |
| 关键调整 | 将 backend/frontend 语义映射为“底层驱动/中断/存储层”和“App 任务/OLED/按键交互层” |
| 规范产出 | 新增并落地目录结构、存储、错误处理、日志、质量、组件、状态、类型与 hook/调度模式文档 |
| 版本管理 | 调整 `.gitignore`，允许 `.trellis/spec/**` 被纳入版本库 |
| 提交 | `91e058b docs(trellis): bootstrap firmware development guidelines` |

**Updated Areas**:
- `.trellis/spec/backend/*.md`
- `.trellis/spec/frontend/*.md`
- `.trellis/spec/guides/*.md`
- `.gitignore`

**Verification**:
- `task.py validate .trellis/tasks/00-bootstrap-guidelines` 通过
- `.trellis/spec/` 模板残留已清理
- 任务已归档，工作区记录时为干净状态


### Git Commits

| Hash | Message |
|------|---------|
| `91e058b` | (see git log) |

### Testing

- [OK] 旧式注释扫描：目标范围内无 `@brief/@param/@retval`、`//` 等残留。
- [OK] `git diff --check`：未发现空白格式错误。
- [INFO] 按用户要求未由 Codex 执行编译，后续由用户本地编译并反馈错误。

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 2: RS485排障与注释规范记录

**Date**: 2026-05-12
**Task**: RS485排障与注释规范记录
**Branch**: `main`

### Summary

记录本次 RS485 排障根因、桥接代码收敛情况，以及新的函数注释规范；业务代码注释整理留到下次会话。

### Main Changes

| 项目 | 记录 |
|------|------|
| RS485根因 | 本次排障确认 RS485 方向控制脚之前误写为 `PA1`，实物真实连接脚位是 `PE8`，修正后收发恢复正常。 |
| 关键经验 | `RS485 -> MCU` 能接收并不能证明 `DE/RE#` 方向脚配置正确；后续排障必须同时做断电导通测试和上电示波验证。 |
| Spec沉淀 | 已在 `.trellis/spec/backend/quality-guidelines.md` 记录“RS485 方向控制脚必须按实物板卡核对”的规则和正反案例。 |
| 代码状态 | 已将 `USER/Driver/bsp_usart.*`、`USER/App/usart_app.*`、`USER/gd32f4xx_it.*` 调整为正式桥接思路，删除临时测试/调试路径，保留稳定的 RS485 双向透明转发主链路。 |
| 并发优化 | `uart_task()` 已改为先从 ISR 共享缓冲区取原子快照，再做阻塞发送，避免主循环发送时被中断改写导致半帧或乱帧。 |
| 提示词更新 | 已把函数头注释模板、参数说明、返回值说明、以及函数内必要注释规则写入 `AGENTS.md`。 |
| 下次待办 | 当前业务代码的注释风格统一整理留到下一次会话处理，本次只记录规范，不继续扩展注释修改。 |


### Git Commits

(No commits - planning session)

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 3: 规范化工程中文注释

**Date**: 2026-05-12
**Task**: 规范化工程中文注释
**Branch**: `main`

### Summary

完成深睡前外设收拢、芯片级低功耗配置和 GitHub 推送。该轮重点验证低功耗链路不会破坏 `WK_UP` 唤醒恢复，且构建产物可正常生成。

### Main Changes

| 项目 | 内容 |
|------|------|
| 主要工作 | 按 AGENTS.md 注释规范系统整理工程中文注释，覆盖 App、Driver、中断、SysTick、main、LittleFS 移植层和 OLED 公共 API。 |
| 质量检查 | 执行旧式注释扫描，确认目标范围内无 @brief/@param/@retval、// 等残留；执行 git diff --check，无空白错误。 |
| 构建说明 | 按用户要求未由 Codex 执行编译；用户会自行编译并反馈错误。 |
| GitHub 上传 | 已提交并推送到 origin/main。 |
| 剩余本地项 | 未跟踪文件 西门子原理图.pdf 未纳入提交。 |

**提交记录**:
- `4d6ebe4ad9bc9432dbc434251177af8cd79a0331` docs: 按规范完善工程中文注释

**主要文件范围**:
- `USER/App/*`
- `USER/Driver/*`
- `USER/gd32f4xx_it.c/.h`
- `USER/main.c`
- `USER/systick.c/.h`
- `USER/Component/gd25qxx/lfs_port.c/.h`
- `USER/Component/oled/oled.h`


### Git Commits

| Hash | Message |
|------|---------|
| `4d6ebe4ad9bc9432dbc434251177af8cd79a0331` | (see git log) |

### Testing

- [OK] Keil 命令行构建 `MDK/2026706296.uvprojx` 通过并生成 `Project.axf/bin/hex`。
- [OK] 用户硬件实测睡眠电流约从 `0.116A` 降到 `0.115A`。
- [OK] 用户硬件实测 `WK_UP` 可在约 0.6s 内恢复显示。

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 4: BootLoader two-stage app integration

**Date**: 2026-05-12
**Task**: BootLoader two-stage app integration
**Branch**: `main`

### Summary

将当前工程接入两阶段BootLoader并精简依赖；BootLoader硬件上板验证仍未完成。

### Main Changes

| 项目 | 本次记录 |
|---|---|
| 工程目标 | 将当前 `D:\GD32\2026706296` 工程整理为可被两阶段 BootLoader 跳转运行的 App，并把官方 `27_BootLoader_Two_Stage` 例程副本放入当前仓库。 |
| App 链接规划 | 当前 App 已改为从 `0x0800D000` 链接运行，Flash 使用范围为 `0x0800D000 ~ 0x08070000`，保留 `0x08000000 ~ 0x0800BFFF` 给 BootLoader，`0x0800C000` 给参数区，`0x08073000` 起作为下载缓存区。 |
| App 启动接管 | 新增 `USER/boot_app_config.c/.h`，在 `system_init()` 开头调用 `boot_app_handoff_init()`，完成 `SCB->VTOR` 切换、清理挂起中断、重新开启全局中断，解决 BootLoader 跳转后 App 中断入口和 PRIMASK 状态问题。 |
| Keil 工程配置 | `MDK/2026706296.uvprojx` 的 IROM 改为 `0x0800D000 / 0x63000`，启用 HEX 输出，并恢复直接 `fromelf.exe --bin --output=.\output\Project.bin .\output\Project.axf` 生成 BIN；已记录 `nStopA1X=0` 经验，避免 After Build 辅助命令误导致主目标失败。 |
| BootLoader 副本 | 新增 `BootLoader_Two_Stage/`，修改其中 BootLoader 工程：App 起始地址统一为 `0x0800D000`，下载缓存区为 `0x08073000`，按 `appSize` 擦除页，按 1024B 分块搬运，搬运后从 App 区重新计算 CRC，并增强 MSP/Reset_Handler 合法性检查。 |
| 依赖精简 | `PACK/perf_counter-2.5.4` 已精简为当前工程必须文件；`Driver/CMSIS_6.2.0` 删除，必要 CMSIS Core 文件复制/合并到 `Driver/CMSIS`，当前保留 `core_cm4.h`、`cmsis_armclang.h`、`cmsis_compiler.h`、`cmsis_version.h`、`m-profile/armv7m_mpu.h`、`m-profile/cmsis_armclang_m.h` 等。 |
| 文档沉淀 | 新增/更新 `BootLoader_APP_接入说明.md`、`BootLoader_Two_Stage_官方例程详解.md`、`PACK/perf_counter用途说明.md`、`工程文档.md`，并在 `.trellis/spec/backend/quality-guidelines.md` 记录 Keil After Build 的 `nStopA1X=0` 排障经验。 |
| 已验证 | App 工程 Keil 构建曾显示 `0 Error(s), 0 Warning(s)`，`Project.bin` 已能生成；BootLoader 副本通过 Keil UV4 批处理构建，日志显示 `0 Error(s), 0 Warning(s)`。 |
| 尚未验证 | **BootLoader 尚未进行硬件上板验证**：还没有实际烧录 BootLoader 到 `0x08000000`、App 到 `0x0800D000`，也没有在板子上验证复位启动、跳转 App、OLED/串口/SysTick/DMA/任务运行是否正常。 |
| 后续重点 | 上板时先分别烧录 BootLoader 和 App，确认 BootLoader 能跳转到当前 App；再观察串口日志/OLED/任务调度/中断是否正常；运行中升级功能还未移植，后续要新增 App 侧接收固件、写下载缓存区、写参数区和软件复位流程。 |

本次 Git 提交：`58275bc feat(bootloader): 接入两阶段BootLoader并精简依赖`。

注意：当前仍未提交的 `MDK/2026706296.uvguix.caofengrui` 属于 Keil 个人界面状态文件，`GD32F470 Development Kit V2.0 原理图.pdf` 也未纳入本次提交。


### Git Commits

| Hash | Message |
|------|---------|
| `58275bc` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 5: Fix WK_UP deep-sleep wake recovery

**Date**: 2026-05-13
**Task**: Fix WK_UP deep-sleep wake recovery
**Branch**: `fix-wkup-deepsleep`

### Summary

Fixed the WK_UP deep-sleep wake recovery path for the BootLoader-relocated GD32F470 App.
The session also captured the root cause and OTA operator procedure in executable
project specs so future low-power or OTA work has concrete guardrails.

### Main Changes

| Item | Details |
|------|---------|
| Work | Fixed deep-sleep WK_UP wake recovery for the BootLoader-relocated App and recorded executable specs. |
| Root Cause | The App runs at 0x0800D000, but wake recovery called SystemInit(), which can restore SCB->VTOR to 0x08000000. Interrupts after wake could dispatch through the BootLoader vector table, making the board look like it could not return from WK_UP wake. |
| Firmware Changes | USER/Driver/bsp_key.c now clears EXTI0/NVIC pending state and uses EXTI_TRIG_FALLING for the pulled-up WK_UP button. USER/Driver/bsp_power.c now restores the App vector table after SystemInit() inside a short interrupt-disabled section and clears EXTI0 pending before WFI. |
| Spec Updates | .trellis/spec/backend/quality-guidelines.md documents the deep-sleep wakeup VTOR/EXTI/PMU contract. .trellis/spec/backend/embedded-ota-guidelines.md records the UART OTA operator procedure and COM29 send command. |
| Verification | python -m unittest tools.test_uart_ota_packet: OK. Keil build: 0 Error(s), 1 existing perf_counter.h warning. Project.bin: 33536 bytes. stream-info: crc=0xC0B85342, version=0x00000005, chunks=66. |
| Git | Commit 8e352ce on fix-wkup-deepsleep, pushed to origin/fix-wkup-deepsleep. |
| Hardware Follow-up | Send with python tools\make_uart_ota_packet.py --mode send --port COM29 --version 0x00000005 --chunk-size 512, then verify KEY2 enters deep sleep and WK_UP wakes with UART/OLED/tasks restored. |
| Notes | Unrelated local changes remain in MDK user option files, the schematic PDF, and tmp/. They were not included in the fix commit. |


### Git Commits

| Hash | Message |
|------|---------|
| `8e352ce` | (see git log) |

### Testing

- [OK] `python -m unittest tools.test_uart_ota_packet`
- [OK] Keil command-line build: `0 Error(s), 1 Warning(s)`
- [OK] `python tools\make_uart_ota_packet.py --mode stream-info --version 0x00000005 --chunk-size 512`
- [OK] Confirmed `MDK/output/Project.bin` exists and is `33536` bytes

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 6: Raise UART OTA baudrate and sync workflow docs

**Date**: 2026-05-13
**Task**: Raise UART OTA baudrate and sync workflow docs
**Branch**: `fix-wkup-deepsleep`

### Summary

Raised UART OTA to 460800, added ACK progress output, and synced repository docs plus maintenance rules.

### Main Changes

| Item | Details |
|------|---------|
| Work | Raised the UART OTA path to 460800 baud across the App, BootLoader copy, and PC-side tool, added frame-by-frame ACK progress output, and synchronized the operator docs and maintenance rules. |
| Firmware Defaults | `USER/Driver/bsp_usart.*`, `BootLoader_Two_Stage/27_0_BootLoader/HardWare/USART/*`, and `BootLoader_Two_Stage/27_1_App/HardWare/USART/*` now use `460800` as the default USART0 OTA baudrate. |
| Tooling | `tools/make_uart_ota_packet.py` now prints `send stream ...`, `START acked ...`, repeated `DATA acked ...`, and `END acked ...` so OTA progress and the failing frame position are visible during transfer. |
| Hardware Validation | The operator flow was validated with `python tools\\make_uart_ota_packet.py --mode send --port COM29 --baudrate 460800 --version 0x00000006 --chunk-size 512`, which completed with `sent stream frames=68`. |
| Local Board Adjustment | The user's manual BootLoader LED remap from `GPIOA 4~7` to `GPIOD 10~13` was preserved while syncing the BootLoader-side serial defaults. |
| Repository Docs | `BootLoader_APP_接入说明.md`, `工程文档.md`, and `BootLoader_Two_Stage_官方例程详解.md` were updated to reflect the 460800 workflow, ACK progress output, version increment expectations, and the current COM29 send command. |
| Prompt + Spec Rules | `AGENTS.md` now requires code/config/protocol changes to update the matching docs, and `.trellis/spec/backend/quality-guidelines.md` now records the same requirement as a durable review rule. |
| Scope Control | Unrelated local files in `MDK/*.uvoptx`, the schematic PDF, and `tmp/` remained outside the commits. |

### Git Commits

| Hash | Message |
|------|---------|
| `8ac0be5` | feat(ota-tool): 增加串口发送进度输出 |
| `7f186d6` | feat(uart): 提升默认升级串口波特率并同步bootloader |
| `b1c9f07` | docs(ota): 同步升级流程文档与文档维护规则 |

### Testing

- [OK] `python -m unittest tools.test_uart_ota_packet`
- [OK] App Keil build: `0 Error(s), 1 existing warning`
- [OK] BootLoader Keil build: `0 Error(s), 0 Warning(s)`
- [OK] Official `27_1_App` copy build: `0 Error(s), 0 Warning(s)`
- [OK] `python tools\\make_uart_ota_packet.py --mode stream-info --version 0x00000006 --chunk-size 512`
- [OK] `python tools\\make_uart_ota_packet.py --mode send --port COM29 --baudrate 460800 --version 0x00000006 --chunk-size 512`

### Status

[OK] **Completed**

### Next Steps

- Keep incrementing `--version` for each OTA attempt.
- If transfer speed is still not enough, evaluate whether the target UART clock and host adapter remain stable at a higher baudrate before changing defaults again.


## Session 7: 梳理BootLoader与App两阶段升级实际流程并补充注释

**Date**: 2026-05-13
**Task**: 梳理BootLoader与App两阶段升级实际流程并补充注释
**Branch**: `fix-wkup-deepsleep`

### Summary

完成 GD25QXX LittleFS 安全接入、OTA 下载缓存区重新规划、BootLoader 跳转诊断增强，以及 AC6 semihosting 脱机卡死修复。该轮同时把 `BKPT 0xAB` 根因、map 符号检查和不可优先修改厂家系统文件的规则沉淀到 `.trellis/spec`。

### Main Changes

| 模块 | 内容 |
|---|---|
| BootLoader 工程讲解 | 新增 `BootLoader_Two_Stage/27_0_BootLoader/BootLoader_程序详解.md`，按启动、参数区、下载缓存区、正式 App 区、搬运和跳转顺序解释 BootLoader 两阶段升级方案。 |
| App/BootLoader 实际流程文档 | 新增 `BootLoader_App_实际升级运行流程详解.md`，梳理上位机、App、下载缓存区、参数区、BootLoader 和新 App 之间的真实升级时序与职责边界。 |
| BootLoader 注释补强 | 为 `27_0_BootLoader` 下的 `main.c`、`Function.c/.h`、`BootConfig.c/.h`、`rom.c/.h`、`usart.c/.h`、`HeaderFiles.h` 补充中文注释，重点说明参数区判断、下载区搬运、CRC 校验和 App 跳转原因。 |
| App 侧注释补强 | 为 `USER/App/usart_app.c`、`USER/boot_app_config.c/.h`、`USER/App/scheduler.c` 补充与 BootLoader 交接相关的详细中文注释，明确 App 只负责写下载缓存区与参数区，真正搬运由 BootLoader 完成。 |
| 会话产出 | 代码提交 `cf27b9d docs(boot): 补充BootLoader与App升级流程讲解并完善注释`。 |

**验证说明**：
- 已执行 `git diff --check` 对本次相关改动做文本级校验，无 diff 格式错误。
- 本次改动以文档和注释补充为主，未额外执行固件编译。


### Git Commits

| Hash | Message |
|------|---------|
| `cf27b9d` | (see git log) |

### Testing

- [OK] Keil 命令行构建 `MDK/2026706296.uvprojx` 通过：`0 Error(s), 1 Warning(s)`。
- [OK] `MDK/Listings/Project.map` 中 `__use_no_semihosting` 存在，`_sys_open/_sys_write/_sys_exit/_ttywrch` 解析到 `main.o`。
- [OK] 用户硬件验证脱机复位后可从 `BootLoader : jump app ...` 继续进入 App，输出 `BOOT: handoff start` 和 LittleFS 自检 PASS。

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 8: 整理Flash分区原理与OTA容量限制说明

**Date**: 2026-05-13
**Task**: 整理Flash分区原理与OTA容量限制说明
**Branch**: `fix-wkup-deepsleep`

### Summary

新增独立文档解释当前工程的Flash分区来源、4KB参数区原因以及396KB运行区与52KB OTA上限的关系，并同步修正文档中的旧分区表述。

### Main Changes

| 模块 | 内容 |
|---|---|
| 独立分区文档 | 新增 `Flash分区原理与OTA容量限制说明.md`，专门说明 BootLoader 区、参数区、正式 App 区、预留间隙和下载缓存区的地址来源、页数、容量关系。 |
| 地址计算说明 | 在文档中明确给出 `0x08000000 + 48KB = 0x0800C000`、`0x0800C000 + 4KB = 0x0800D000`、`0x08080000 - 52KB = 0x08073000` 等关键推导过程。 |
| 参数区原理 | 详细说明参数区为什么必须独占 `4KB`：当前工程按 `4KB` 页擦写，参数区需要支持整页读改写，并承载升级字段、备份区、日志区、配置区和校准区。 |
| OTA 容量边界 | 明确区分“正式 App 运行空间 `396KB`”和“当前内部 Flash OTA 可升级上限 `52KB`”，解释为什么二者不是同一个问题。 |
| 现有文档同步 | 更新 `BootLoader_APP_接入说明.md`、`BootLoader_App_实际升级运行流程详解.md` 和 `BootLoader_Two_Stage/地址_规划表.txt`，修正旧的 `76KB App 区` 表述并与当前工程真实常量对齐。 |
| 作用 | 让后续查看者不需要先读完整升级流程，也能单独理解 Flash 分区设计、当前 OTA 上限以及以后扩展的方向。 |

**验证说明**：
- 已核对新文档中的地址、页数、容量与当前工程常量一致：`0x0800D000`、`0x63000`、`0x08073000`、`52KB`、`4KB`。
- 本次改动为文档整理与说明增强，未修改固件逻辑，也未额外执行编译。


### Git Commits

| Hash | Message |
|------|---------|
| `44e9ec2` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 9: 完成 USART2 OTA 通道迁移并通过硬件升级验证

**Date**: 2026-05-13
**Task**: 完成 USART2 OTA 通道迁移并通过硬件升级验证
**Branch**: `fix-wkup-deepsleep`

### Summary

将 OTA 从 USART0 拆分到 USART2，新增 uart_ota_app 与 bootloader_port 模块，保留 USART0 作为日志和普通透传；同步更新 Keil 工程、Python 发送工具、仓库文档与 .trellis/spec，并增加 USART2 启动探测与 OTA 收包诊断日志。硬件实测已完成 START/DATA/END、BootLoader 搬运、CRC 校验与新 App 重启闭环验证。

### Main Changes

(Add details)

### Git Commits

| Hash | Message |
|------|---------|
| `577cb98` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 10: 修复低功耗唤醒与VBAT RTC续时

**Date**: 2026-05-14
**Task**: 修复低功耗唤醒与VBAT RTC续时
**Branch**: `fix-wkup-deepsleep`

### Summary

修复深睡唤醒后EXTI0残留，并让RTC在VBAT备份域有效时保持断电续时；用户已完成硬件验证并推送到远端。

### Main Changes

| 模块 | 变更 |
|------|------|
| 低功耗唤醒 | 深睡唤醒完成后新增 `bsp_wkup_key_exti_deinit()`，关闭 `EXTI0_IRQn` / `EXTI_0` 并清理挂起位，避免低功耗专用唤醒中断残留到正常运行态。 |
| RTC / VBAT | `bsp_rtc_init()` 改为区分“首次建表”和“备份域恢复”两条路径；当 `RTC_BKP0 == BKP_VALUE` 且 `VBAT` 备份域有效时，只同步并读取现有 RTC 时间，不再重写默认时间。 |
| 文档同步 | 更新 `工程文档.md` 与 `.trellis/spec/backend/quality-guidelines.md`，明确 `VBAT` 纽扣电池存在时 RTC 断电续时，以及 `EXTI0` 仅在深睡窗口内有效的约束。 |

**验证结果**:
- 用户已完成硬件实测并确认功能正常。
- 当前修复提交为 `34f0431 fix(power): preserve vbat rtc state and close wake exti`，已推送到 `origin/fix-wkup-deepsleep`。


### Git Commits

| Hash | Message |
|------|---------|
| `34f0431` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 11: 低功耗深睡优化与GitHub推送

**Date**: 2026-05-14
**Task**: 低功耗深睡优化与GitHub推送
**Branch**: `fix-wkup-deepsleep`

### Summary

完成深睡前外设收拢、芯片级低功耗配置和 GitHub 推送。该轮重点验证低功耗链路不会破坏 `WK_UP` 唤醒恢复，且构建产物可正常生成。

### Main Changes

| 项目 | 说明 |
|---|---|
| 深睡优化 | 修复 `OLED_Display_Off()` 关屏命令，补充 `GD25QXX` deep power-down / release，补充 `GD30AD3344` 芯片级低功耗配置。 |
| MCU 侧收拢 | 在 `bsp_power.c` 中新增统一深睡关钟函数，进入深睡前关闭 USART / SPI / I2C / DMA / ADC / DAC / TIMER / GPIOB-E 时钟门控，并完整停止 `SysTick`，启用 `PMU_LOWDRIVER_ENABLE`。 |
| 唤醒恢复 | 调整 Flash 唤醒时序，确保在 SPI0/GPIO/DMA 恢复后再发送 release 指令；保留 `WK_UP` 唤醒链路并完成外设重建。 |
| 文档同步 | 更新 `工程文档.md` 与 `PACK/perf_counter用途说明.md`，同步深睡流程、芯片级待机与 SysTick 停止说明。 |
| 验证结果 | 使用 `E:\Keil_v5\UV4\UV4.exe` 构建 `MDK/2026706296.uvprojx`，生成 `Project.axf/bin/hex`；用户实测睡眠电流由 `0.116A` 降到 `0.115A`，`WK_UP` 可在约 0.6s 内恢复显示。 |
| 版本控制 | 已提交 `0562e89 feat(power): 优化深睡外设收拢与芯片级待机`，并推送到 `origin/fix-wkup-deepsleep`。 |

**涉及文件**：
- `USER/Driver/bsp_power.c`
- `USER/Component/oled/oled.c`
- `USER/Component/gd25qxx/gd25qxx.c`
- `USER/Component/gd25qxx/gd25qxx.h`
- `USER/Component/gd30ad3344/gd30ad3344.c`
- `USER/Component/gd30ad3344/gd30ad3344.h`
- `工程文档.md`
- `PACK/perf_counter用途说明.md`


### Git Commits

| Hash | Message |
|------|---------|
| `0562e89` | (see git log) |

### Testing

- [OK] Keil 命令行构建 `MDK/2026706296.uvprojx` 通过并生成 `Project.axf/bin/hex`。
- [OK] 用户硬件实测睡眠电流约从 `0.116A` 降到 `0.115A`。
- [OK] 用户硬件实测 `WK_UP` 可在约 0.6s 内恢复显示。

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 12: LittleFS接入与BootLoader脱机跳转修复

**Date**: 2026-05-15
**Task**: LittleFS接入与BootLoader脱机跳转修复
**Branch**: `fix-wkup-deepsleep`

### Summary

完成 GD25QXX LittleFS 安全接入、OTA 下载缓存区重新规划、BootLoader 跳转诊断增强，以及 AC6 semihosting 脱机卡死修复。该轮同时把 `BKPT 0xAB` 根因、map 符号检查和不可优先修改厂家系统文件的规则沉淀到 `.trellis/spec`。

### Main Changes

| 项目 | 说明 |
|---|---|
| LittleFS 接入 | 为 GD25QXX 增加 LittleFS 静态缓存移植层，保留末尾 4KB 裸 Flash 测试区，避免 raw test 覆盖文件系统超级块。 |
| 原始 Flash 测试 | `test_spi_flash()` 改为 `SPI_FLASH_RAW_TEST_ENABLE` 控制的可选测试，默认跳过，降低误擦写风险。 |
| SPI Flash DMA | 明确 GD25QXX 驱动使用 `DMA1 CH2/CH3`，补充 DMA 初始化、释放和文档约束。 |
| OTA 缓存区 | 重新规划内部 Flash：BootLoader `0x08000000`，参数区 `0x0800C000`，App `0x0800D000..0x0806FFFF`，下载缓存 `0x08070000..0x0807FFFF`。 |
| BootLoader 跳转 | 增加跳转诊断日志和跳转前现场清理，确认参数区空不是卡死根因，BootLoader 已正确读到 App 向量表并跳转。 |
| AC6 semihosting 修复 | 在 `USER/main.c` 增加 `__use_no_semihosting` 与 `_sys_open/_sys_write/_sys_exit/_ttywrch/fputc` retarget，修复脱机停在 `BKPT 0xAB` 的问题。 |
| 经验沉淀 | 更新 `.trellis/spec`，新增 `BootLoader Handoff Standalone Hang` 场景，要求下次先查 map 符号和 `BKPT 0xAB`，不能先改 BootLoader 地址或厂家系统文件。 |
| 文档同步 | 更新工程文档、BootLoader 接入说明、实际升级流程、Flash 分区说明和 OTA 容量限制说明。 |

### 验证结果

- Keil 命令行构建 `MDK/2026706296.uvprojx` 通过：`0 Error(s), 1 Warning(s)`。
- `MDK/Listings/Project.map` 中 `__use_no_semihosting` 存在，`_sys_open/_sys_write/_sys_exit/_ttywrch` 解析到 `main.o`。
- 用户硬件验证脱机复位后可从 `BootLoader : jump app ...` 继续进入 App，输出 `BOOT: handoff start`、LittleFS 自检 PASS、raw Flash/SD 测试默认跳过。


### Git Commits

| Hash | Message |
|------|---------|
| `e407a08` | (see git log) |

### Testing

- [OK] Keil 命令行构建 `MDK/2026706296.uvprojx` 通过：`0 Error(s), 1 Warning(s)`。
- [OK] `MDK/Listings/Project.map` 中 `__use_no_semihosting` 存在，`_sys_open/_sys_write/_sys_exit/_ttywrch` 解析到 `main.o`。
- [OK] 用户硬件验证脱机复位后可从 `BootLoader : jump app ...` 继续进入 App，输出 `BOOT: handoff start` 和 LittleFS 自检 PASS。

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 13: 简化按键处理并重分配普通按键引脚

**Date**: 2026-05-15
**Task**: 简化按键处理并重分配普通按键引脚
**Branch**: `fix-wkup-deepsleep`

### Summary

(Add summary)

### Main Changes

| 项目 | 内容 |
|---|---|
| 代码改动 | 将 `USER/App/btn_app.c` 从 `ebtn` 事件库重构为 5ms 轮询 + 20ms 去抖 + 按下沿分发 |
| 引脚调整 | 普通按键重新映射为 `KEY1=PB1`、`KEY2=PC5`、`KEY3=PC4`、`KEY4=PA7`、`KEY5=PA6`、`KEY6=PA5`，`KEYW` 保持 `PA0` |
| 低功耗适配 | 深睡前按新脚位收拢 GPIO，唤醒后调用 `app_btn_init()` 重置按键状态，保持 `KEYW/EXTI0` 唤醒链路 |
| 工程清理 | 删除 `USER/Component/ebtn` 目录，移除 `system_all.h` 和 `MDK/2026706296.uvprojx` 中的 `ebtn` 依赖 |
| 文档同步 | 更新 `工程文档.md` 以及 `.trellis/spec/frontend/` 下与按键方案相关的说明 |

**提交信息**:
- `39b5ffd refactor(key): 简化按键扫描并重分配引脚`

**验证记录**:
- 已执行仓库内静态核查，确认 `USER`、`工程文档.md`、`.trellis/spec/frontend`、`MDK/2026706296.uvprojx` 中无残留 `ebtn` 依赖
- 已核查旧普通按键脚位 `PE15/PE6/PE11/PE4/PE7/PB0` 在目标范围内无残留
- 本次未在当前环境执行 MDK 编译、烧录和实板按键验证


### Git Commits

| Hash | Message |
|------|---------|
| `39b5ffd` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete
