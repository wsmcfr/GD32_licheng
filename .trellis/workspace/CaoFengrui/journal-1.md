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

完成 App 侧 OTA 通道迁移：升级数据改走 RS485/USART1，USART0 继续负责日志与调试命令，旧 USART2 OTA 链路和 RS485 原样回显任务已从 App 工程中移除。同步更新驱动、中断、调度、低功耗恢复、PC 工具、工程文档和 Trellis 规范，并完成提交与远端推送确认。

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

- [OK] `python -m unittest tools.test_uart_ota_packet`：11 项测试通过
- [OK] 核心范围旧符号扫描：`USART2/usart2/rs485_task/rs485_app` 等关键字无命中
- [OK] `git diff --check`：无空白错误，仅 Git LF/CRLF 提示
- [OK] Keil `uVision.com` rebuild：`0 Error(s), 26 Warning(s)`，warning 为既有 `perf_counter.h` GNU extension 提示
- [OK] GitHub 远端确认：`origin/fix-wkup-deepsleep` 指向 `cafbc342e31dfeccc0190f170003ea415dacc87a`

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 14: 补记按键与深睡唤醒实板验证结果

**Date**: 2026-05-15
**Task**: 补记按键与深睡唤醒实板验证结果
**Branch**: `fix-wkup-deepsleep`

### Summary

Completed and pushed OTA/storage hardening for the header-bin OTA branch. The session focused on making user-visible OTA retries safer, propagating low-level Flash and ADC failures through firmware layers, and syncing the resulting contracts into repository docs and Trellis specs.

### Main Changes

| 项目 | 内容 |
|---|---|
| 实板验证 | 用户确认板上按键功能与深睡/唤醒链路已完成实测，结果通过 |
| 验证范围 | `KEY1~KEY6` 普通按键动作正常；`KEY2` 进入深睡正常；`KEYW(PA0)` 唤醒链路正常 |
| 结果结论 | 上一轮 `39b5ffd` 中的按键简化与引脚重分配改动已通过真实硬件验证，不再只是静态检查结论 |
| 工程说明 | 额外确认 `RTE/` 目录是 Keil/uVision 的 Run-Time Environment 自动生成配置目录，当前工程主要用于 `CMSIS_device_header` 与 `perf_counter` 宏配置痕迹，不属于业务逻辑目录 |

**关联提交**:
- `39b5ffd refactor(key): 简化按键扫描并重分配引脚`
- `cfbcea6 docs(trellis): 记录按键简化与引脚重分配会话`

**补充说明**:
- 本次记录用于补齐“实板已验证通过”的证据链
- 本次未修改业务代码，仅补充会话知识与验证结论


### Git Commits

| Hash | Message |
|------|---------|
| `39b5ffd` | (see git log) |
| `cfbcea6` | (see git log) |

### Testing

- [OK] `python -m unittest tools.test_header_bin_ota_static`
- [OK] `python tools/test_static_optimizations.py`
- [OK] `gcc -std=c99 -Wall -Wextra -Werror tools/pack_ota_image.c ...`
- [OK] `git diff --check`
- [OK] Keil build: `0 Error(s), 0 Warning(s)`
- [OK] OTA artifact check: `Project_ota.bin` is 64 bytes larger than `Project.bin`

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 15: LittleFS 壳层与 100KB OTA 分区收尾

**Date**: 2026-05-15
**Task**: LittleFS 壳层与 100KB OTA 分区收尾
**Branch**: `fix-wkup-deepsleep`

### Summary

Completed the firmware optimization pass requested for OLED, RS485/USART1 OTA receive flow, ADC/DMA setup, LED state ownership, low-power LED blanking, and PT100 debug logging. DAC behavior was intentionally left unchanged per user request.

### Main Changes

| Feature | Description |
|---------|-------------|
| OTA 分区调整 | 将下载缓存区从 64KB 扩到 100KB，并同步更新 App / BootLoader / MDK 工程参数与文档。 |
| USART0 LittleFS 壳层 | 完成 `help/pwd/ls/cd/cat/write/mkdir/touch/rm/stat/df` 命令集，USART0 不再转发到 RS485。 |
| 目录大小语义修正 | 修复最初 `ls` 目录固定显示 `0` 的问题，最终统一为目录树下全部文件内容的递归总字节数。 |
| 递归删除 | 新增 `rm <path>`，文件直接删除，目录递归删除，根目录 `/` 明确拒绝。 |
| 规范沉淀 | 在 `.trellis/spec/backend/database-guidelines.md` 补充 LittleFS UART shell 的目录大小语义、递归 helper 分层约束、验证矩阵和测试点。 |

**关键 Bug 总结**:
- `ls` 目录显示为 `0`：根因是 UART 壳层把目录大小硬编码成 `0`，而不是定义清楚目录列语义。
- 改成子项数量后与文件“大小”语义不一致：最终统一改成目录递归总字节数。
- 输入命令即卡死并触发 LittleFS `ASSERT: block != ((lfs_block_t) - 1)`：根因是递归目录大小统计错误回调了高层 `get_path_info`，形成递归套娃；最终拆分为 raw path-info helper 与 enriched size helper。

**验证**:
- Keil App 工程重新构建通过。
- 实板启动日志正常，LittleFS 自检通过。
- 串口命令链路完成 `ls/stat/rm` 语义调整并通过人工回归。


### Git Commits

| Hash | Message |
|------|---------|
| `d122c77` | (see git log) |

### Testing

- [OK] `python tools\test_static_optimizations.py`
- [OK] `python -m unittest tools.test_header_bin_ota_static` - 8 tests OK
- [OK] `git diff --check` - no whitespace errors, only CRLF conversion warnings
- [OK] Keil batch build for `project\2026706296.uvprojx` - `0 Error(s), 0 Warning(s)`
- [OK] Legacy-pattern scan found no old OLED void signatures, 10000ms OLED wait, or app/power direct LED toggle/off usage

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 16: LittleFS write 追加写参数与会话收尾

**Date**: 2026-05-15
**Task**: LittleFS write 追加写参数与会话收尾
**Branch**: `fix-wkup-deepsleep`

### Summary

重新读取 CIMC 初赛题目后，新增正式开发顺序与自动测评对应说明文档；本地已提交文档变更，并记录 GitHub 推送因 HTTPS 连接超时/重置暂未完成。

### Main Changes

| 模块 | 变更 |
|------|------|
| LittleFS shell | `write` 命令升级为 `write [-a] <file> <text>`，默认覆盖写，`-a` 追加写 |
| 存储接口 | 新增 `lfs_storage_append_file()`，并把覆盖写/追加写收敛到统一内部写入流程 |
| 校验逻辑 | `write -a` 读回后校验最终长度和文件尾内容，避免误判追加成功 |
| 文档与规范 | 同步更新 `工程文档.md`、`BootLoader_APP_接入说明.md` 和 `.trellis/spec/backend/database-guidelines.md` |

**验证情况**:
- 用户已实机验证 `write -a` 行为正确
- Keil 构建命令 `E:\Keil_v5\UV4\UV4.exe -b D:\GD32\2026706296\MDK\2026706296.uvprojx -j0` 已通过

**关键提交**:
- `b043da4` `feat(storage): 支持 LittleFS write 追加写参数`


### Git Commits

| Hash | Message |
|------|---------|
| `b043da4` | (see git log) |

### Testing

- [OK] `python tools\test_static_optimizations.py`
- [OK] `git diff --check`

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 17: RTC串口命令与USART1 RS485原样回显

**Date**: 2026-05-15
**Task**: RTC串口命令与USART1 RS485原样回显
**Branch**: `fix-wkup-deepsleep`

### Summary

(Add summary)

### Main Changes

| 模块 | 变更内容 |
|------|----------|
| RTC 串口命令 | 为 `USART0` 调试壳层新增 `gettime` 与 `settime yyyy-mm-dd hh:mm:ss`，支持完整年月日时分秒读取与设置，并补充格式/范围校验与成功回读回包 |
| RTC 驱动接口 | 在 `bsp_rtc.c/.h` 新增十进制日期时间结构、BCD 转换、闰年/日期校验、星期自动计算，以及 `bsp_rtc_get_datetime()` / `bsp_rtc_set_datetime()` 接口 |
| RS485 回显 | 新增 `rs485_app.c/.h`，将 `USART1 IDLE + DMA` 接收到的一帧数据从 ISR 移交到任务层，并通过 `USART1` 原样回显 |
| 中断与调度 | `USART1_IRQHandler()` 从“丢弃帧”改为“限长复制并置位标志”，`scheduler.c` 新增 `rs485_task()` 调度入口 |
| 工程与文档 | 更新 `MDK/2026706296.uvprojx` 纳入 `rs485_app.c`，同步更新 `工程文档.md`、`BootLoader_APP_接入说明.md` 与 `.trellis/spec/` 中的长期约束 |

**验证结果**:
- 人工测试已完成：`gettime/settime` 能正常使用
- 人工测试已完成：外部串口工具向 `RS485/USART1` 发一帧数据后，板子可通过 `USART1` 原样回显
- 代码已提交并推送到 `origin/fix-wkup-deepsleep`


### Git Commits

| Hash | Message |
|------|---------|
| `aab2084` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 18: 迁移 App OTA 通道到 RS485/USART1

**Date**: 2026-05-16
**Task**: 迁移 App OTA 通道到 RS485/USART1
**Branch**: `fix-wkup-deepsleep`

### Summary

(Add summary)

### Main Changes

| 项目 | 内容 |
|------|------|
| OTA 通道 | 将 App 侧 OTA 从 USART2 迁移到 RS485/USART1，USART0 保持日志和调试命令输出。 |
| 串口驱动 | UART_OTA_USART 改为 RS485_USART，USART1 DMA 缓冲扩展到 1024 字节，删除 USART2 OTA 初始化、DMA 宏和中断声明。 |
| 中断与任务 | USART1_IRQHandler 接管 OTA DMA 帧移交，删除旧 rs485_task/rs485_app 回显逻辑，调度表只保留 uart_ota_task。 |
| RS485 半双工 | OTA ACK 和 OTA485 启动探测串发送前切换 TX，发送完成后恢复 RX。 |
| 低功耗 | 深睡前后不再处理 USART2，唤醒后通过 bsp_usart_init 恢复 USART0/USART1 并重置 OTA 运行态。 |
| 工具与文档 | PC OTA 工具 channel 输出改为 RS485/USART1，同步更新工程文档、接入说明、脚本文档和 .trellis/spec OTA 规范。 |
| 验证 | Python 单测 11 项通过；旧 USART2/rs485_task 关键字核心范围无命中；git diff --check 无空白错误；Keil rebuild 0 Error(s), 26 Warning(s)，warning 为既有 perf_counter GNU extension 提示。 |
| GitHub | 提交 cafbc34 已推送到 origin/fix-wkup-deepsleep，用户确认远端 refs/heads/fix-wkup-deepsleep 指向 cafbc342e31dfeccc0190f170003ea415dacc87a。 |

**关键文件**:
- `USER/Driver/bsp_usart.h`
- `USER/Driver/bsp_usart.c`
- `USER/gd32f4xx_it.c`
- `USER/App/uart_ota_app.c`
- `USER/App/scheduler.c`
- `USER/Driver/bsp_power.c`
- `tools/make_uart_ota_packet.py`
- `tools/test_uart_ota_packet.py`
- `.trellis/spec/backend/embedded-ota-guidelines.md`
- `BootLoader_APP_接入说明.md`
- `UART_OTA_Python脚本详解.md`
- `工程文档.md`


### Git Commits

| Hash | Message |
|------|---------|
| `cafbc34` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 19: BootLoader 与 UART OTA 学习文档补充

**Date**: 2026-05-19
**Task**: BootLoader 与 UART OTA 学习文档补充
**Branch**: `fix-wkup-deepsleep`

### Summary

围绕 GD32 BootLoader 两阶段升级和 App 侧 UART/RS485 OTA 流程进行学习讲解，并同步补充文档。内容包括 Flash/RAM 运行关系、参数区 4KB 与 packed 结构体、App 接收 START/DATA/END 帧的分发和解析、DATA 分包写入下载缓存区、END 阶段整包校验，以及 bootloader_port_write_upgrade_info 如何写入升级参数区通知 BootLoader 搬运新 App。

### Main Changes

(Add details)

### Git Commits

| Hash | Message |
|------|---------|
| `bfb2592` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 20: 迁移外部 SPI Flash 文件系统到 SMARTFS

**Date**: 2026-05-22
**Task**: 迁移外部 SPI Flash 文件系统到 SMARTFS
**Branch**: `fix-wkup-deepsleep`

### Summary

(Add summary)

### Main Changes

| 项目 | 内容 |
|------|------|
| 文件系统迁移 | 将外部 GD25Q16 SPI Flash 文件系统从 LittleFS 替换为裸机 SMARTFS 风格最小端口。 |
| Flash 布局 | 取消末尾 4KB 裸 Flash 测试保留区，整片 2MB Flash 全部归 SMARTFS 使用。 |
| 工程配置 | MDK 工程切换为编译 `smartfs_port.c`，不再编译 `lfs.c`、`lfs_port.c`、`lfs_util.c`。 |
| 应用接入 | `scheduler` 启动自检和 `usart_app` 文件壳层统一改为 `smart_storage_*` 接口与 `SMARTFS:` 日志。 |
| 关键策略 | SMARTFS 端口使用静态镜像、静态 sector 缓冲和双副本元数据，不依赖堆；写入先提交新链与新元数据，再释放旧链。 |
| 启动行为 | 首次遇到旧 LittleFS 数据或空片时输出 `SMARTFS: metadata invalid, format whole flash` 并格式化整片外部 Flash。 |
| 验证 | Keil 构建 0 Error / 0 Warning；OTA 包大小 47040 字节，CRC 为 `0x31295A9F`；用户 OTA 实测自检通过。 |
| 文档同步 | 已同步 `.trellis/spec/backend/` 与 `工程文档.md` 中的 Flash 布局、SMARTFS 约束、OTA 示例和验证方式。 |

**提交结果**：
- `8eed037 feat(storage): replace LittleFS with SMARTFS`
- 已推送到 `origin/fix-wkup-deepsleep`


### Git Commits

| Hash | Message |
|------|---------|
| `8eed037` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 21: 重构工程目录并移除 perf_counter

**Date**: 2026-05-24
**Task**: 重构工程目录并移除 perf_counter
**Branch**: `template-style-layout`

### Summary

按示例工程风格重构目录，保留原 scheduler 调度框架，移除 perf_counter，更新 Keil 工程、OTA 默认路径、工程文档和 .trellis/spec；Keil 编译无错误，git diff --cached --check 已通过。

### Main Changes

| 项目 | 内容 |
|------|------|
| 目录重构 | 按示例工程命名风格整理 App 工程根目录，形成 `User/`、`Function/`、`HardWare/`、`Library/`、`CMSIS/`、`Startup/`、`HeaderFiles/`、`project/` 分层。 |
| 逻辑层归位 | 将原 `USER/App/` 下的业务任务迁移到 `Function/`，保留 `scheduler.c` 作为统一调度中心，继续使用原工程的调度器框架。 |
| 驱动层归位 | 将板级驱动和外设组件迁移到 `HardWare/`，按 `LED`、`KEY`、`USART`、`RTC`、`POWER`、`BOOTLOADER`、`GD25QXX`、`SDIO`、`STORAGE`、`OLED` 等模块分目录管理。 |
| 库层归位 | 将 GD32 标准外设库迁移到 `Library/GD32F4xx_standard_peripheral/`，将 FatFs 迁移到 `Library/Third_Party/fat_fs/`，将 CMSIS 和启动文件独立到 `CMSIS/` 与 `Startup/`。 |
| Keil 工程 | 将 MDK 工程迁移到 `project/2026706296.uvprojx`，同步 include path、源文件路径、RTE 路径和输出目录，输出目录统一为 `project/output/`。 |
| perf_counter 移除 | 删除 `PACK/perf_counter-2.5.4` 和旧用途说明，不再依赖第三方 perf_counter；时间基准由现有 `systick`、调度器和延时接口等效承载。 |
| systick 等效方案 | 保留 1ms 系统节拍、`delay_1ms()`、`delay_ms()`、`delay_us()`、`delay_decrement()`、`system_millis()` 等接口，确保 App 调度、超时判断、低功耗唤醒流程继续可用。 |
| FatFs 路径修正 | 修正 FatFs `option/*.c` 迁移后的头文件包含路径，避免新目录层级下出现 `../ff.h` 失效问题。 |
| OTA 工具同步 | 将 UART OTA 打包脚本默认输入/输出路径同步为 `project/output/Project.bin` 与 `project/output/Project.uota`，避免继续引用旧 `MDK/output`。 |
| 文档同步 | 同步更新 `工程文档.md`、BootLoader/OTA 文档、Flash 分区说明、UART OTA 脚本文档以及 `.trellis/spec/` 中的目录结构和维护规则。 |
| 清理策略 | 编译产物继续由 `.gitignore` 排除，`project/output/`、`*.axf`、`*.hex`、`*.bin` 等不进入仓库。 |
| GitHub 分支 | 新建并推送 `template-style-layout` 分支，主重构提交为 `e1adf9a refactor: restructure firmware project layout`。 |

### Git Commits

| Hash | Message |
|------|---------|
| `e1adf9a` | `refactor: restructure firmware project layout` |

### Testing

- [OK] 用户已在 Keil 中重新编译，结果为 0 Error。
- [OK] `git diff --cached --check` 通过，没有空白格式错误。
- [OK] `project/output/Project.axf`、`Project.hex`、`Project.bin` 已生成，说明迁移后的工程路径可参与构建。
- [OK] OTA stream-info 曾验证 `project/output/Project.bin` 可用于生成升级流，`Project.bin` 大小为 46504 字节，CRC 为 `0x43A1A307`。
- [OK] `python -m unittest tools.test_uart_ota_packet` 通过 11 个测试，OTA 打包脚本路径调整未破坏单元测试。

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 22: 记录低功耗运行时优化提交

**Date**: 2026-05-24
**Task**: 记录低功耗运行时优化提交
**Branch**: `feature/lowpower-runtime-optimizations`

### Summary

(Add summary)

### Main Changes

| 项目 | 内容 |
|------|------|
| 分支 | `feature/lowpower-runtime-optimizations` |
| 代码提交 | `9f042fb optimize low power runtime paths` |
| 推送状态 | 已推送到 `origin/feature/lowpower-runtime-optimizations` |
| PR 链接 | `https://github.com/wsmcfr/GD32_licheng/pull/new/feature/lowpower-runtime-optimizations` |

| 工作项 | 结果 |
|--------|------|
| 低功耗入口可靠性 | 为 SPI Flash WIP、SPI/DMA 等等待路径增加超时返回，避免休眠准备阶段因外设异常无限卡死。 |
| 唤醒后调度基线 | 新增 `scheduler_reset_runtime()`，唤醒恢复完成后统一重置各周期任务 `last_run`，避免 OLED/UART/RTC/ADC 集中到期。 |
| SysTick 与睡眠时间 | 明确运行时 tick 与 RTC 墙上时间的边界，并补充跨睡眠计时相关说明。 |
| OLED 刷新开销 | 缩小 `oled_printf()` 栈缓冲，增加行缓存/脏行刷新，减少 I2C/OLED 重复写入。 |
| RTC 初始化可靠性 | 为 LXTAL 启动加入显式超时处理，失败时可记录并回退。 |
| 唤醒恢复分层 | 将 SD/FatFs 等非关键恢复改为按状态/按需处理，避免后续扩展拖慢唤醒主路径。 |
| GD30AD3344 修正 | 修正 PGA 量程映射分支，并为 ADC/SPI 等等待路径补充超时。 |
| 调度任务恢复 | 将被临时放入 `oled_task()` 的 `led_task()`、`adc_task()`、`rtc_task()` 恢复为 `scheduler_task[]` 独立调度。 |

| 验证 | 结果 |
|------|------|
| `python tools/test_static_optimizations.py` | 通过 |
| `git diff --check` | 通过，仅 CRLF 提示 |
| Keil 构建 | 通过，`0 Error(s), 0 Warning(s)`，Program Size: Code=43024 RO-data=4812 RW-data=380 ZI-data=37372 |

**后续注意**:
- `led_task` 当前仍为 1ms，但实际只同步 `ucLed[6]` 且已有去重，后续可单独改为 20ms 或 50ms，并同步工程文档。
- `btn_task` 的 5ms 周期与 `BTN_TASK_PERIOD_MS` 去抖累计绑定，若改周期必须同步宏和文档。


### Git Commits

| Hash | Message |
|------|---------|
| `9f042fb` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 23: Remove migrated BootLoader copy

**Date**: 2026-05-24
**Task**: Remove migrated BootLoader copy
**Branch**: `feature/lowpower-runtime-optimizations`

### Summary

删除已迁移到外部目录的旧 BootLoader 副本，整理主 App Keil 工程分组，并同步更新 BootLoader/OTA 文档中的当前路径说明。

### Main Changes

| 项目 | 记录 |
|---|---|
| BootLoader 迁移清理 | 删除主仓库内旧的 `BootLoader_Two_Stage` 目录，确认实际 BootLoader 已迁移到 `D:\GD32\2026706296_bootloader`。 |
| Keil 工程整理 | 将主 App 工程中原 `Peripherals` 分组的 GD32 标准外设库源文件并入 `Library` 分组，删除 IDE 中多余的 `Peripherals` 分组。 |
| 文档同步 | 更新 `BootLoader_APP_接入说明.md`、`BootLoader_App_实际升级运行流程详解.md`、`工程文档.md`、`Flash分区原理与OTA容量限制说明.md` 和 `.trellis/spec/backend/embedded-ota-guidelines.md`，避免继续指向已删除的 `BootLoader_Two_Stage\27_0_BootLoader` 路径。 |
| 验证 | `rg` 检查旧本地 BootLoader 源码路径和 `<GroupName>Peripherals</GroupName>` 无残留；Keil 重建 `project\2026706296.uvprojx` 通过，日志为 `0 Error(s), 0 Warning(s)`，程序大小 `Code=43024 RO-data=4812 RW-data=380 ZI-data=37372`。 |
| 推送 | 提交 `51da429 refactor: remove migrated bootloader copy` 已推送到 `origin/feature/lowpower-runtime-optimizations`。 |


### Git Commits

| Hash | Message |
|------|---------|
| `51da429` | (see git log) |

### Testing

- [OK] Keil 重建 `project\2026706296.uvprojx` 通过，构建日志显示 `0 Error(s), 0 Warning(s)`。
- [OK] `rg` 检查旧本地 BootLoader 源码路径和 `<GroupName>Peripherals</GroupName>` 无残留。

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 24: Optimize LED changed-bit refresh

**Date**: 2026-05-24
**Task**: Optimize LED changed-bit refresh
**Branch**: `feature/lowpower-runtime-optimizations`

### Summary

本次完成 LED 周期刷新路径优化：`led_disp()` 首次运行时同步 6 路 LED，后续只对状态发生变化的 LED 调用控制宏，减少 1ms 周期任务中的重复 GPIO 写入。同步更新工程文档和 Trellis 状态管理规范，并确认代码提交已推送到 GitHub。

### Main Changes

| 项目 | 内容 |
|------|------|
| 代码变更 | 将 `Function/led_app.c::led_disp()` 从任意变化时全量刷新 6 路 LED，优化为首次全量刷新、后续只刷新 `changed_mask` 中发生变化的 LED。 |
| 接口保持 | `led_task()` 和全局 `ucLed[6]` 对外行为不变；私有 `led_disp()` 参数改为 `const uint8_t *`，并新增 `LED_APP_COUNT` / `LED_APP_VALID_MASK`。 |
| 边界处理 | 新增 `led_cache_valid`，避免使用伪历史状态导致首次调用不能把应用层初始 LED 状态同步到硬件。 |
| 文档同步 | 更新 `工程文档.md` 的 LED 控制逻辑说明，并更新 `.trellis/spec/frontend/state-management.md` 中的缓存状态示例。 |
| 验证 | 运行 Keil 命令行构建，`Project.axf - 0 Error(s), 0 Warning(s)`；运行 `git diff --check`，无空白错误，仅有 LF/CRLF 提示。 |
| 提交 | `3b23b49 optimize led display change refresh` 已推送到 `origin/feature/lowpower-runtime-optimizations`。 |
| 保留事项 | `project/2026706296.uvprojx` 在本会话开始前已有未提交目标名改动，本次未提交也未回退。 |


### Git Commits

| Hash | Message |
|------|---------|
| `3b23b49` | (see git log) |

### Testing

- [OK] Keil 命令行构建通过，`Project.axf - 0 Error(s), 0 Warning(s)`。
- [OK] `git diff --check` 通过，未发现空白格式错误。
- [OK] 逻辑模拟覆盖首次全量刷新、状态不变不刷新、单个 LED 变化只刷新对应变化位。

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 25: GD30AD3344 PT100 app

**Date**: 2026-05-24
**Task**: GD30AD3344 PT100 app
**Branch**: `feature/lowpower-runtime-optimizations`

### Summary

(Add summary)

### Main Changes

| 项目 | 内容 |
|------|------|
| 实现 | 新增 `Function/gd30ad3344_pt100_app.c/h`，封装 GD30AD3344 读取 PT100 调理板输出后的电阻和温度换算。 |
| 采样参数 | 默认 `AIN0~GND`、`GD30AD3344_PGA_4V096`、`1mA` 激励电流、`R5=6.49kΩ` 对应前端增益 `16.41`。 |
| 调度集成 | 在 `scheduler_task[]` 中以 `200ms` 周期注册 `gd30ad3344_pt100_task()`，并在 GD30AD3344 初始化后调用 `gd30ad3344_pt100_app_init()`。 |
| 接口 | 提供 `pt100_measurement_t` 缓存结果结构体、`gd30ad3344_pt100_get_latest()` 读取最近采样。 |
| 文档 | 同步 `工程文档.md` 和 `.trellis/spec/frontend/directory-structure.md`，说明启动流程、Function 目录和 PT100 换算参数。 |
| 验证 | 运行 `python tools/test_static_optimizations.py`、`git diff --check`，均通过；本机未找到 `UV4/UV5/armclang`，未进行 Keil/ArmClang 编译。 |


### Git Commits

| Hash | Message |
|------|---------|
| `9774f9a` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 26: OLED I2C refresh path optimization

**Date**: 2026-05-25
**Task**: OLED I2C refresh path optimization
**Branch**: `feature/lowpower-runtime-optimizations`

### Summary

Optimized the SSD1306 OLED refresh path after hardware testing confirmed display output is normal. The work reduced I2C transaction count for command positioning, string rendering, and app-layer status updates while preserving the legacy 8-pixel text grid.

### Main Changes

| 内容 | 说明 |
|------|------|
| OLED 命令批量发送 | 新增 `OLED_Write_cmd_buf()`，`OLED_Write_cmd()` 保留为兼容封装，连续 SSD1306 命令可按 DMA 缓冲区批量发送。 |
| OLED 定位事务压缩 | `OLED_Set_Position()` 改为通过 `oled_set_position_buf()` 一次发送页地址、高列地址和低列地址，定位从 3 次 I2C 命令事务降为 1 次。 |
| 字符串批量渲染 | `OLED_ShowStr()` 不再逐字符调用 `OLED_ShowChar()`；6x8 字符按 8 像素步进拼入行缓冲，8x16 字符按上下页批量写入。 |
| 应用层差异段刷新 | `oled_printf()` 新增 `oled_printf_diff_start()` 和 `oled_printf_diff_end()`，只刷新变化字符段，降低 `uwTick`、ADC 等状态行的 I2C/DMA 写入量。 |
| 文档与规则同步 | 更新 `工程文档.md`、`.trellis/spec/backend/quality-guidelines.md` 和 `tools/test_static_optimizations.py`，固化 OLED 批量传输与差异刷新契约。 |
| 验证 | 已通过 `python tools/test_static_optimizations.py`、`python -m py_compile tools/test_static_optimizations.py`、`git diff --check`、Keil rebuild；构建日志显示 `0 Error(s), 0 Warning(s)`。 |

**Updated Files**:
- `HardWare/OLED/oled.c`
- `HardWare/OLED/oled.h`
- `HardWare/OLED/bsp_oled.c`
- `HardWare/OLED/bsp_oled.h`
- `Function/oled_app.c`
- `tools/test_static_optimizations.py`
- `工程文档.md`
- `.trellis/spec/backend/quality-guidelines.md`


### Git Commits

| Hash | Message |
|------|---------|
| `49125a2` | (see git log) |

### Testing

- [OK] Human hardware check: OLED display output is normal.
- [OK] `python tools/test_static_optimizations.py`
- [OK] `python -m py_compile tools/test_static_optimizations.py`
- [OK] `git diff --check`
- [OK] Keil rebuild of target `2026706296`; `project/output/Project.build_log.htm` reports `0 Error(s), 0 Warning(s)`.

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 27: Three-level low power modes

**Date**: 2026-05-26
**Task**: Three-level low power modes
**Branch**: `feature/lowpower-runtime-optimizations`

### Summary

Added KEY1 Sleep, KEY2 Deep-sleep, and KEY3 Standby runtime low-power modes. This session did not include hardware board validation.

### Main Changes

| Item | Description |
|------|-------------|
| Low-power mapping | KEY1 now enters Sleep, KEY2 enters Deep-sleep, and KEY3 enters Standby; KEYW/PA0 remains the wake source. |
| Sleep mode | Added `bsp_enter_sleep()`, which masks USART0/USART1/SDIO runtime IRQs, stops SysTick, enters PMU Sleep, then restores timebase, runtime IRQs, button baseline, and scheduler baseline after KEYW/EXTI0 wake. |
| Deep-sleep mode | Kept the existing Deep-sleep resource shutdown and wake recovery path, and extracted RTC sleep-time compensation into a shared helper. |
| Standby mode | Added `bsp_enter_standby()`, which waits for KEYW to be held low, shuts down board resources, enables PMU WKUP on PA0, and enters Standby; releasing KEYW wakes by reset. |
| Startup marker | Startup now detects `PMU_FLAG_STANDBY`, prints `BOOT: wake from standby`, then clears PMU standby/wakeup flags. |
| Documentation | Updated `工程文档.md`, `.trellis/spec/backend/quality-guidelines.md`, and `tools/test_static_optimizations.py` with the new low-power contracts. |

**Updated Files**:
- `HardWare/POWER/bsp_power.c`
- `HardWare/POWER/bsp_power.h`
- `Function/btn_app.c`
- `Function/scheduler.c`
- `tools/test_static_optimizations.py`
- `工程文档.md`
- `.trellis/spec/backend/quality-guidelines.md`

### Git Commits

| Hash | Message |
|------|---------|
| `b132d93` | feat: add three-level low power modes |

### Testing

- [OK] `python tools/test_static_optimizations.py`
- [OK] `git diff --check`
- [OK] Keil/uVision rebuild target `2026706296`; log reports `0 Error(s), 0 Warning(s)`.
- [!] Hardware board validation was not performed in this session.

### Status

[OK] **Completed**

### Next Steps

- Perform board validation for KEY1 Sleep, KEY2 Deep-sleep, KEY3 Standby, and KEYW wake behavior.


## Session 28: Remove SD/FatFs and refine Standby confirmation

**Date**: 2026-05-31
**Task**: Remove SD/FatFs and refine Standby confirmation
**Branch**: `feature/remove-sd-fatfs`

### Summary

Removed the obsolete SD card/FatFs stack and refined the deepest low-power workflow. The final Standby behavior is KEY3 preparing the board, KEY4 press-release confirming entry, and KEYW/PA0 remaining wake-only. This session also captured the resulting low-power contract in Trellis specs for future maintenance.

### Main Changes

| Item | Summary |
|------|---------|
| SD/FatFs removal | Removed SD card, SDIO, FatFs sources, startup hooks, interrupt handler, power-path references, aggregate includes, Keil source entries, and related user docs/spec text. |
| PT100 context | Continued from the commercial PT100 calibration work already committed before this session; no extra PT100 code changes in this record. |
| Standby interaction fix | Changed KEY3 to enter Standby prepare without toggling LED3; Standby now blanks OLED/LED immediately, waits for KEY4 press-release confirmation, and keeps KEYW/PA0 as wake source only. |
| Code-spec update | Captured the KEY3/KEY4/KEYW Standby contract in `.trellis/spec/backend/quality-guidelines.md` with signatures, ordering contracts, validation matrix, good/bad cases, and wrong/correct examples. |
| Static regression | Extended `tools/test_static_optimizations.py` to reject KEYW-as-entry regressions, require KEY4 confirmation, verify OLED/LED blanking order, and ensure KEY3 no longer toggles LED3. |
| Documentation | Updated `工程文档.md` to match the SD/FatFs removal and the new Standby operation flow. |

**Updated Files**:
- `.trellis/spec/backend/database-guidelines.md`
- `.trellis/spec/backend/directory-structure.md`
- `.trellis/spec/backend/error-handling.md`
- `.trellis/spec/backend/index.md`
- `.trellis/spec/backend/logging-guidelines.md`
- `.trellis/spec/backend/quality-guidelines.md`
- `.trellis/spec/frontend/component-guidelines.md`
- `.trellis/spec/frontend/directory-structure.md`
- `.trellis/spec/frontend/type-safety.md`
- `Function/btn_app.c`
- `Function/scheduler.c`
- `HardWare/POWER/bsp_power.c`
- `HeaderFiles/system_all.h`
- `USER/gd32f4xx_it.c`
- `USER/gd32f4xx_libopt.h`
- `project/2026706296.uvprojx`
- `tools/test_static_optimizations.py`
- `工程文档.md`
- deleted `Function/sd_app.c`, `Function/sd_app.h`, `HardWare/SDIO/*`, and `Library/Third_Party/fat_fs/*`

### Git Commits

| Hash | Message |
|------|---------|
| `42b660d` | refactor: remove sd card and fatfs support |
| `90ee58c` | fix: confirm standby entry with key4 release |

### Testing

- [OK] `python tools/test_static_optimizations.py`
- [OK] `python -m unittest discover -s tools -p "test_*.py"` (`Ran 11 tests ... OK`; argparse negative-case message is expected)
- [OK] `git diff --check`
- [OK] Keil build target `2026706296`; `project/build_codex.log` reports `0 Error(s), 0 Warning(s)`.
- [OK] Branch pushed to GitHub: `feature/remove-sd-fatfs`.
- [!] Hardware board validation of the final KEY3 -> KEY4 confirmation -> KEYW wake sequence was not performed in this session.

### Status

[OK] **Completed**

### Next Steps

- Flash the App and verify: KEY3 blanks OLED/LED, KEY4 press-release confirms Standby entry, KEYW/PA0 wakes and boot log prints `BOOT: wake from standby`.
- If Standby immediately wakes after KEY4 confirmation, verify PA0/KEYW PMU WKUP active-level behavior against the board hardware.


## Session 29: 添加 YModem OTA 升级与规则

**Date**: 2026-05-31
**Task**: 添加 YModem OTA 升级与规则
**Branch**: `feature/ymodem-ota`

### Summary

(Add summary)

### Main Changes

| 项目 | 内容 |
|---|---|
| 本次目标 | 增加不依赖 Python 引导的 OTA 升级方式，让用户可用串口工具直接以 YModem 发送 `Project.bin`。 |
| 功能实现 | 新增 App 侧 YModem OTA 接收模块，支持纸飞机调试助手等串口工具通过 `文件 -> 发送文件 -> YModem` 发送 bin 文件。 |
| BootLoader 影响 | 第一版方案保持 BootLoader 不变，仍复用下载缓存区 `0x08067000` 和参数区 `0x0800C000` 完成 App 到 BootLoader 的升级交接。 |
| 兼容性 | 保留旧 Python `START/DATA/END` 协议，新增 YModem 只是补充入口，不破坏原有升级脚本。 |
| 工程接入 | 将 `Function/uart_ota_ymodem.c/.h` 接入 UART OTA 轮询逻辑，并加入 Keil 工程文件。 |
| 缓冲区规则 | 将 USART1 RX 缓冲区调整为可容纳 1K YModem 包的数据规模，避免 1024 数据包加协议头尾后溢出。 |
| 文档同步 | 更新 `工程文档.md`、`BootLoader_APP_接入说明.md`、`BootLoader_App_实际升级运行流程详解.md` 和 `.trellis/spec/backend/embedded-ota-guidelines.md`，写明新升级流程和长期维护规则。 |
| 测试规则 | 新增 `tools/test_ymodem_ota_static.py`，静态校验 YModem 接入契约、缓冲区大小、Keil 工程配置和文档同步。 |
| 验证结果 | 已运行 `python -m tools.test_ymodem_ota_static`、`python -m unittest tools.test_uart_ota_packet`、`python tools/test_static_optimizations.py`、`git diff --check`，均通过；Keil 构建日志确认 `0 Error(s), 0 Warning(s)`。 |
| 分支上传 | 已在新分支 `feature/ymodem-ota` 提交并推送功能提交 `b27b9e2 feat: add ymodem ota receiver`。 |


### Git Commits

| Hash | Message |
|------|---------|
| `b27b9e2` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 30: 方案B头部BIN OTA升级流程

**Date**: 2026-05-31
**Task**: 方案B头部BIN OTA升级流程
**Branch**: `feature/header-bin-ota`

### Summary

本次会话完成并记录方案 B 的头部 BIN OTA 流程：App 分支改为生成并接收 `Project_ota.bin`，现场只需要原始/直接发送一个 bin 文件；BootLoader 继续只负责读取参数区和搬运下载区 payload。后续又把 App 与独立 BootLoader 的参数区魔术字统一改为 `0xC0DEF47A`，并明确旧 BootLoader 需要重新烧录。

### Main Changes

| 项目 | 本次记录 |
|---|---|
| OTA 方案 | 切换到方案 B：Keil 构建后生成 `Project_ota.bin = 64 字节 OTA 头 + Project.bin payload`，现场只需通过 RS485/USART1 原始/直接发送该 bin 文件。 |
| 旧升级路径清理 | 删除旧辅助发送工具、旧 YModem/旧包发送入口和对应旧测试，避免后续误用 Python 引导、握手、C 端发送等额外手段。 |
| App 接收链路 | App 侧解析 `Project_ota.bin` 头部，先完整接收 payload 到 RAM，CRC/向量表校验通过后写入 `0x08067000` 下载缓存区，再写 BootLoader 参数区并复位。 |
| BootLoader 分工 | BootLoader 不解析 `Project_ota.bin` 头部，只读取 `0x0800C000` 参数区，把 `0x08067000` 的 payload 搬运到 `0x0800D000` 并做 CRC 校验。 |
| 共享协议更新 | App 与 `D:\GD32\2026706296_bootloader` 中 BootLoader 的参数区魔术字统一改为 `0xC0DEF47A`，旧值 `0x5AA5C33C` 不再用于当前分支。 |
| 文档同步 | 更新 `OTA头部BIN升级说明.md`、`BootLoader_APP_接入说明.md`、`工程文档.md`、BootLoader 流程说明和 `.trellis/spec/backend/embedded-ota-guidelines.md`。 |
| 验证 | `gcc -std=c99 -Wall -Wextra -Werror tools\pack_ota_image.c -o tools\pack_ota_image.exe` 通过；`python -m unittest tools.test_header_bin_ota_static` 6 tests OK；App Keil 构建 0 Error/0 Warning；BootLoader Keil 构建 0 Error/0 Warning。 |
| GitHub | App 仓库分支 `feature/header-bin-ota` 已推送，最新提交 `bb2dd52`；BootLoader 目录不是 Git 仓库，魔术字改动保存在本地 BootLoader 工程。 |

**后续注意**：板子如果已经烧录旧 BootLoader，必须重新烧录当前本地 `D:\GD32\2026706296_bootloader` 生成的新 BootLoader；否则新 App 写入 `0xC0DEF47A` 后，旧 BootLoader 仍按 `0x5AA5C33C` 判断参数区，会放弃升级搬运。


### Git Commits

| Hash | Message |
|------|---------|
| `330d3e5` | (see git log) |
| `bb2dd52` | (see git log) |

### Testing

- [OK] `gcc -std=c99 -Wall -Wextra -Werror tools\pack_ota_image.c -o tools\pack_ota_image.exe`
- [OK] `python -m unittest tools.test_header_bin_ota_static`，6 tests OK
- [OK] App Keil 构建，0 Error(s)，0 Warning(s)
- [OK] BootLoader Keil 构建，0 Error(s)，0 Warning(s)
- [OK] 旧魔术字 `0x5AA5C33C` 只剩在静态测试的反向断言里

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 31: OTA and storage error hardening

**Date**: 2026-05-31
**Task**: OTA and storage error hardening
**Branch**: `feature/header-bin-ota`

### Summary

(Add summary)

### Main Changes

| 类别 | 记录 |
|---|---|
| OTA ready 时序 | 将 `uart_ota_emit_startup_probe()` 后移到 `scheduler_init()` 后，ready 表示启动自检和调度器已就绪。 |
| OTA 错误重试 | 增加错误态 magic 前缀缓存，支持误发文件后不复位直接重发合法 `Project_ota.bin`，包括 magic 被 DMA 分块拆开的情况。 |
| GD25QXX 写擦错误 | `spi_flash_write_enable/sector_erase/bulk_erase/page_write/buffer_write` 改为状态返回，WREN、命令字节、页写和 WIP 超时都会上抛。 |
| SMARTFS/LittleFS | 检查底层 Flash 写擦返回码，将失败映射为 `SMART_STORAGE_ERR_IO` / `LFS_ERR_IO`，避免错误元数据提交。 |
| GD30AD3344/PT100 | `GD30AD3344_AD_Read()` 改为 `0/-1` + 输出参数，PT100 采样失败时清 `sample_ready/range_valid` 并打印错误。 |
| 文档规范 | 同步 `工程文档.md`、BootLoader/OTA 说明和 `.trellis/spec` 中 OTA、存储、错误处理、状态管理合同。 |
| 验证 | `python -m unittest tools.test_header_bin_ota_static`、`python tools/test_static_optimizations.py`、`gcc -std=c99 -Wall -Wextra -Werror tools/pack_ota_image.c ...`、`git diff --check` 均通过；Keil 构建 `0 Error(s), 0 Warning(s)`；`Project_ota.bin` 比 `Project.bin` 大 64 字节。 |
| GitHub | 已推送到 `origin/feature/header-bin-ota`，提交 `27c1725 fix: harden ota and storage error handling`。 |


### Git Commits

| Hash | Message |
|------|---------|
| `27c1725` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 32: 优化 OLED 串口 ADC LED 路径

**Date**: 2026-05-31
**Task**: 优化 OLED 串口 ADC LED 路径
**Branch**: `feature/header-bin-ota`

### Summary

(Add summary)

### Main Changes

| 项目 | 内容 |
|------|------|
| 优化范围 | OLED、RS485/USART1 OTA、ADC/DMA、LED/按键/低功耗、PT100 日志节流 |
| OLED | 将 I2C 忙等待从 10000ms 收敛到 20ms；底层写命令/数据/字符串接口返回成功状态；`oled_printf()` 仅在实际写屏成功后更新行缓存 |
| LED | 新增 `led_app_set/toggle/all_off/blank_for_sleep/reset_cache` 统一状态源；按键和低功耗流程不再直接操作 LED 宏；睡前强制关断 6 路 LED 并复位缓存 |
| USART/OTA | `uart_ota_task()` 增加单次调度排空上限，按队列深度尽量处理连续 DMA 槽位，降低大包传输时队列积压风险 |
| ADC/DMA | `adc_value` 改为 `__IO uint16_t`；多个 DMA 配置结构体补初始化，避免局部结构体残留字段影响外设行为 |
| PT100 | 保持 200ms 采样节拍，将串口浮点调试日志节流到约 1s 一次，减小调试串口占用 |
| 文档同步 | 更新 `工程文档.md` 与 `.trellis/spec/` 中 OTA、日志、质量、状态管理约束 |
| 验证 | `python tools\\test_static_optimizations.py` 通过；`python -m unittest tools.test_header_bin_ota_static` 8 tests OK；`git diff --check` 通过，仅行尾 warning；Keil 构建 `0 Error(s), 0 Warning(s)`；旧模式扫描无命中 |

**提交**：`e705b9a perf: optimize oled uart adc led paths`


### Git Commits

| Hash | Message |
|------|---------|
| `e705b9a` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 33: OTA 三分区和 144KB 测试包

**Date**: 2026-06-02
**Task**: OTA 三分区和 144KB 测试包
**Branch**: `feature/header-bin-ota`

### Summary

(Add summary)

### Main Changes

| 项目 | 内容 |
|---|---|
| OTA 分区 | 将当前 App OTA 契约改为 App 运行区、App 备份区、App 缓存区各 152KB，最后 4KB 预留页。 |
| 地址 | 运行区 `0x0800D000~0x08032FFF`，备份区 `0x08033000~0x08058FFF`，缓存区 `0x08059000~0x0807EFFF`。 |
| 协议 | 上位机发送格式不变，仍然一次性原始/直接发送 `Project_ota.bin`，文件格式是 64 字节 OTA 头部 + 原始 App payload。 |
| App 侧 | 更新 `bootloader_port.h`、`boot_app_config.h`、Keil IROM 和 `pack_ota_image.c`，payload/App 上限统一为 `152KB`。 |
| BootLoader 侧 | `D:\GD32\2026706296_bootloader\Function\Function.c` 增加运行区备份和失败恢复逻辑：先备份旧 App，再搬运缓存区新 App，搬运失败时尽量恢复旧 App。 |
| 文档/spec | 更新 `.trellis/spec/backend/embedded-ota-guidelines.md`、工程文档、OTA 头部 BIN 说明、Flash 分区说明、BootLoader 接入/流程文档和官方例程差异说明。 |
| 测试 | `python -m unittest tools.test_header_bin_ota_static` 通过 9 项；`gcc -std=c99 -Wall -Wextra -Werror tools\pack_ota_image.c -o tools\pack_ota_image.exe` 通过。 |
| 构建 | App Keil Rebuild：`0 Error(s), 0 Warning(s)`；BootLoader Keil Rebuild：`0 Error(s), 0 Warning(s)`。 |
| 资源风险 | App map 显示 `RW_IRAM1 = 0x2FC68 / 0x30000`，主 SRAM 剩余约 920 字节，152KB RAM payload 缓冲已经接近上限。 |
| 144KB 测试包 | 生成 ignored 产物 `project/output/Project_144KB.bin` 和 `project/output/Project_ota_144KB.bin`；OTA 文件大小 `147520` 字节，payload `147456` 字节，头部 64 字节。 |
| GitHub | 主仓库提交并推送到 `origin/feature/header-bin-ota`：`8a4dacb feat: repartition ota app regions`。 |
| 注意 | `D:\GD32\2026706296_bootloader` 不是 Git 仓库，BootLoader 目录内的源码/文档改动已在本机完成并通过构建，但不随主仓库 push 到 GitHub。 |


### Git Commits

| Hash | Message |
|------|---------|
| `8a4dacb` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 34: OTA raw stream circular DMA

**Date**: 2026-06-03
**Task**: OTA raw stream circular DMA
**Branch**: `feature/streaming-raw-ota`

### Summary

(Add summary)

### Main Changes

| 项目 | 本次记录 |
|---|---|
| 分支 | `feature/streaming-raw-ota` |
| 提交 | `1b02724 feat(ota): stream raw image through circular dma` |
| 远端 | 已推送到 `origin/feature/streaming-raw-ota` |
| 核心改动 | 将 RS485/USART1 OTA 从整包 RAM 接收改为 ready 前预擦下载区、USART1 circular DMA 环形缓冲、任务层 512B 窗口流式写 Flash |
| 缓冲配置 | `BSP_USART1_RX_BUFFER_SIZE` 加倍到 `32KB`，`115200 8N1` 下约 `2.8s` 输入余量 |
| DMA 策略 | USART1 RX 使用 circular DMA，IDLE/HTF/FTF 中断只清标志并提示任务消费，不停 DMA、不重装 DMA、不在 ISR 复制数据 |
| 文档同步 | 更新 OTA 规格、接入说明、实际升级流程和工程文档；同步 `.trellis/spec/` 中 ISR-to-task / circular DMA 约束 |
| 验证 | `python -m unittest tools.test_header_bin_ota_static` 通过 10 项；`gcc -std=c99 -Wall -Wextra -Werror tools\pack_ota_image.c` 通过；`git diff --check` 无错误；Keil 构建 `0 Error(s), 0 Warning(s)`，`Project_ota.bin` 比 `Project.bin` 大 64 字节 |
| 注意 | 本地仍有未提交 `tools/pack_ota_image.exe`，是验证时重新编译出的构建产物，未纳入业务提交 |


### Git Commits

| Hash | Message |
|------|---------|
| `1b02724` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 35: PT100 two-point calibration and interpolation

**Date**: 2026-06-05
**Task**: PT100 two-point calibration and interpolation
**Branch**: `feature/streaming-raw-ota`

### Summary

Updated GD30AD3344 PT100 conversion to use firmware-Vout two-point resistance calibration and resistor-temperature table interpolation. Synced project docs and static regression checks; verified with python tools/test_static_optimizations.py and git diff --check.

### Main Changes

(Add details)

### Git Commits

| Hash | Message |
|------|---------|
| `dc810a2` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 36: Align Driver layout and OTA partitions

**Date**: 2026-06-05
**Task**: Align Driver layout and OTA partitions
**Branch**: `feature/streaming-raw-ota`

### Summary

(Add summary)

### Main Changes

| 项目 | 内容 |
|---|---|
| 用户目标 | 按截图要求把 `HardWare` 目录改为 `Driver`，新增 `Protocol` 协议层，并把 Flash 分区统一为 BootLoader 64KB、参数区 4KB、App/备份/缓存各 128KB。 |
| 主要实现 | 迁移 App 工程目录到 `Driver/`，新增 `Protocol/ota_image_protocol.h/.c`，将 OTA 头部解析、头 CRC、payload CRC 更新和头字段校验下沉到协议层。 |
| 分区同步 | App 起始地址改为 `0x08011000`，App 容量 `0x00020000`；参数区 `0x08010000`；备份区修正为 `0x08031000`；缓存区 `0x08051000`。 |
| 工程配置 | 更新 `project/2026706296.uvprojx` 的 IROM、Include Path、Keil Group 和 After Build 打包地址，生成 `Project.bin` 后继续生成 `Project_ota.bin`。 |
| 交叉工程 | 同步相邻 BootLoader 工程 `D:\GD32\2026706296_bootloader` 的 `Driver` 目录和新分区地址，BootLoader 构建日志确认通过。 |
| 文档/spec | 同步更新工程文档、OTA 文档、Flash 分区说明、官方例程说明中的当前仓库差异，以及 `.trellis/spec/` 中 Driver/Protocol/OTA 分区约定。 |
| 关键修复 | 修正旧备份区 `0x08033000` 误用风险；按 `0x08011000 + 128KB = 0x08031000` 设置备份区，避免覆盖 `0x08051000` 缓存区。 |
| 编译修复 | 修复 `HeaderFiles/system_all.h` 中 `SYSTEM_ALL_BASE_ONLY` 作用域不稳定导致 `bootloader_port_status_t` 在协议头中不可见的 include 环问题。 |
| 验证 | `python -m unittest tools.test_header_bin_ota_static` 11 项通过；`python tools/test_static_optimizations.py` 通过；`gcc -std=c99 -Wall -Wextra -Werror tools\pack_ota_image.c -o tools\pack_ota_image.exe` 通过。 |
| Keil 验证 | App 工程 Keil 构建日志为 `0 Error(s), 0 Warning(s)`，并生成 `Project_ota.bin`；BootLoader 工程 Keil 构建日志为 `0 Error(s), 0 Warning(s)`。 |
| GitHub | 提交 `5e90b38 feat(ota): align driver layout and ota partitions` 已推送到 `origin/feature/streaming-raw-ota`，本地 HEAD 与远端分支一致。 |


### Git Commits

| Hash | Message |
|------|---------|
| `5e90b38` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 37: CIMC formal contest firmware cleanup

**Date**: 2026-06-06
**Task**: CIMC formal contest firmware cleanup
**Branch**: `feature/streaming-raw-ota`

### Summary

(Add summary)

### Main Changes

| 项目 | 记录 |
|---|---|
| 目标 | 按 2026 CIMC 工业嵌入式初赛题裁剪正式 App 工程，并同步正式 USART1/RS485 协议、DAC 控制和 OTA 说明 |
| 资源裁剪 | 物理删除按键、低功耗按键演示、USART 旧 OTA、外部 GD25QXX/SMARTFS/littlefs 文件系统源码；正式版只保留 LED1/LED2、USART1/RS485、双行 OLED、内部 Flash 参数区 |
| 协议改造 | App 侧新增 `Protocol/cimc_protocol.*` 和 `Function/cimc_status.*`；USART1/RS485 默认 `19200 8N1`；DAC `0x0301` 直接控制 DAC0 OUT0，ADC 不再覆盖 DAC |
| OTA 口径 | 旧 `Project_ota.bin`/`pack_ota_image`/App 裸流 OTA 废弃；文档改为 App `0x0501` 复位进 Bootloader，Bootloader 处理 `0x0502/0x0503` 和 `5AA5C33C` 大赛 bin 魔术字 |
| 文档同步 | 更新 `CIMC赛题工程裁剪与新增说明.md`、`工程文档.md`、BootLoader/Flash/OTA 说明，以及 `.trellis/spec` 中正式版约束 |
| 验证 | App Keil 构建 `0 Error(s), 0 Warning(s)`；Bootloader Keil 构建 `0 Error(s), 0 Warning(s)`；`git diff --check` 无空白错误，仅 LF/CRLF 提示 |

**注意**: `D:\GD32\2026706296_bootloader` 当前不是 Git 仓库，本次 GitHub 推送只覆盖 `D:\GD32\2026706296` 仓库内容。


### Git Commits

| Hash | Message |
|------|---------|
| `f2cb083` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 38: Remove formal debug output

**Date**: 2026-06-06
**Task**: Remove formal debug output
**Branch**: `feature/streaming-raw-ota`

### Summary

Formal App debug output was removed instead of being stubbed behind a silent helper. The commit also deleted the old header-BIN OTA packer and its static test so the formal tree only carries the CIMC `0x0501/0x0502/0x0503` workflow.

### Main Changes

| 项目 | 内容 |
|---|---|
| 目标 | 按正式赛题要求删除 App 调试输出链和旧头部 OTA 工具残留 |
| 提交 | 757aa3f refactor(cimc): remove formal debug output |
| 代码裁剪 | 删除 app_debug_usart_putc、fputc、__io_putchar、my_printf、DEBUG_USART、CIMC_DEBUG_LOG_ENABLE、启动 BOOT 日志、PT100/GD30 调试日志 |
| 保留项 | 保留 ARMCLANG no-semihosting 所需 _sys_open/_sys_write/_sys_read/_sys_exit/_ttywrch 空桩，输出直接丢弃且不访问 USART1/RS485 |
| 旧工具删除 | 删除 tools/pack_ota_image.c、tools/pack_ota_image.exe、tools/test_header_bin_ota_static.py |
| 文档同步 | 更新正式无日志、旧头部 BIN OTA 删除、静态裁剪契约相关仓库文档和 .trellis/spec |
| 验证 | python tools/test_static_optimizations.py；App Keil build 0 Error(s), 0 Warning(s)；Bootloader Keil build 0 Error(s), 0 Warning(s)；Project.bin 24904 bytes；git diff --check 通过；旧串口/调试/旧 OTA 工程项检索无命中 |


### Git Commits

| Hash | Message |
|------|---------|
| `757aa3f` | (see git log) |

### Testing

- [OK] `python tools/test_static_optimizations.py`
- [OK] App Keil build: `project/output/Project.build_log.htm` reports `0 Error(s), 0 Warning(s)`
- [OK] Bootloader Keil build: `project/Objects/2026706296.build_log.htm` reports `0 Error(s), 0 Warning(s)`
- [OK] `project/output/Project.bin` exists and is 24904 bytes
- [OK] `git diff --check`
- [OK] Removed UART/debug/old OTA source and Keil-entry searches returned no formal-path matches

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 39: 记录 CIMC 开发顺序文档与推送状态

**Date**: 2026-06-06
**Task**: 记录 CIMC 开发顺序文档与推送状态
**Branch**: `feature/streaming-raw-ota`

### Summary

(Add summary)

### Main Changes

| 项目 | 记录 |
|------|------|
| 本次目标 | 重新读取 2026 年 CIMC 工业嵌入式系统开发初赛 PDF 后，补充正式开发顺序与自动测评对应说明，并按用户要求尝试上传 GitHub。 |
| 新增文档 | `CIMC赛题开发顺序与自动测评对应说明.md` |
| 关键结论 | 开发顺序不建议机械按自动测评 A~N 编写，应先完成协议底座、参数持久化、系统命令、RTC、采样/DAC/变比、阈值告警、自动上报、睡眠、异常帧，最后做 Bootloader；最终验收再按自动测评顺序逐项跑。 |
| 验证结果 | `python tools\test_static_optimizations.py` 通过；`git diff --check` 通过。 |
| GitHub 状态 | 文档提交已生成，本地分支 `feature/streaming-raw-ota` 相对远端 ahead；推送时 GitHub HTTPS 连接多次超时或重置，需网络恢复后继续推送。 |

**涉及提交**：
- `69d82c0 docs(cimc): add development order guide`

**后续建议**：
- 网络恢复后执行 `git push`，将文档提交和本会话记录提交一起上传到 `origin/feature/streaming-raw-ota`。


### Git Commits

| Hash | Message |
|------|---------|
| `69d82c0` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete
