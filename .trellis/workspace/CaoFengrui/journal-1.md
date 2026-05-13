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

(Add summary)

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

- [OK] (Add test results)

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

(Add summary)

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

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete
