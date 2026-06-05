# 当前工程作为 BootLoader App 的接入说明

## 1. 当前目标

| 项目 | 说明 |
|---|---|
| 当前工程角色 | App 工程，不再从 `0x08000000` 独立启动 |
| App 链接地址 | `0x08011000` |
| App 最大运行区 | `0x00020000`，也就是 `128KB` |
| 在线升级文件 | `project/output/Project_ota.bin` |
| 当前升级方式 | RS485/USART1 原始/直接发送 `Project_ota.bin` |
| 当前接收约束 | 仅接收 `Project_ota.bin` 原始字节流，不需要额外引导命令或文件传输握手，必须能从文件第 0 字节一次性连续发送完 |

首次导入 OTA 功能时，仍需先用 SWD/烧录器把带当前 OTA 接收逻辑的 App 写到 `0x08011000`。板子里如果还是旧 BootLoader，也必须同步重刷 `D:\GD32\2026706296_bootloader` 中的 BootLoader，否则仍可能受旧容量、旧跳转地址或旧搬运逻辑限制。

## 2. Flash 分区关系

| 分区 | 起始地址 | 大小 | 用途 |
|---|---:|---:|---|
| BootLoader 区 | `0x08000000` | `64KB` | MCU 复位后先运行 BootLoader |
| 参数区 | `0x08010000` | `4KB` | 保存升级标志、App 大小、CRC、版本等参数 |
| App 运行区 | `0x08011000` | `128KB` | 当前工程的链接和运行地址 |
| App 备份区 | `0x08031000` | `128KB` | BootLoader 升级前备份旧 App，搬运失败时恢复 |
| App 缓存区 | `0x08051000` | `128KB` | App 在 ready 前预擦，随后边接收 payload 边把新固件流式写到这里 |
| 未使用区 | `0x08071000` | `60KB` | 当前不参与 OTA，避免越过题目规定的固件暂存区域 |

| 问题 | 当前答案 |
|---|---|
| 当前工程“最多能运行多大的 App” | 正式 App 运行区上限是 `0x00020000 = 128KB` |
| 当前这套串口 OTA“最多能升级多大的 App” | `Project_ota.bin` 内的 App payload 必须 `<= 128KB` |

## 3. 当前工程已经做了什么

| 文件 | 作用 |
|---|---|
| `User/boot_app_config.h/.c` | 定义 App 起始地址并在启动时设置 `SCB->VTOR = 0x08011000` |
| `Function/scheduler.c` | `system_init()` 开头调用 `boot_app_handoff_init()` |
| `project/2026706296.uvprojx` | IROM 为 `0x08011000 / 0x020000`，构建后生成 `Project.bin` 和 `Project_ota.bin` |
| `User/main.c` | 提供 `__use_no_semihosting` 和 `_sys_*` retarget，避免脱机运行卡在 `BKPT 0xAB` |
| `Function/usart_app.c` | `USART0` 仅作为日志和 LittleFS 调试命令口 |
| `Function/uart_ota_app.c/.h` | ready 前预擦下载区，消费 32KB DMA 环形缓冲，流式写下载区，校验 payload CRC 和向量表，写参数区 |
| `Protocol/ota_image_protocol.c/.h` | 解析 `Project_ota.bin` 头部，校验头部 CRC、目标地址、payload 大小和向量表元数据 |
| `Driver/USART/bsp_usart.c/.h` | USART1/RS485 使用 32KB circular DMA，并通过 IDLE + DMA 半满/满中断提示任务层消费连续裸流 |
| `Driver/BOOTLOADER/bootloader_port.c/.h` | 封装下载区擦写、参数区回写和软件复位 |
| `tools/pack_ota_image.c` | 把 `Project.bin` 打包为带 64 字节头部的 `Project_ota.bin` |

## 4. 输出文件怎么用

| 输出文件 | 用途 |
|---|---|
| `project/output/Project.axf` | Keil 调试用 AXF |
| `project/output/Project.hex` | 带地址信息，可用于烧录工具写到 App 区 |
| `project/output/Project.bin` | 原始 App payload，不直接现场发送 |
| `project/output/Project_ota.bin` | 当前 OTA 文件，现场通过 RS485/USART1 原始/直接发送 |

Keil After Build 当前执行：

```powershell
E:\Keil_v5\ARM\ARMCLANG\bin\fromelf.exe --bin --output=.\output\Project.bin .\output\Project.axf
..\tools\pack_ota_image.exe .\output\Project.bin .\output\Project_ota.bin 0x00000001 0x08011000
```

## 5. 现场升级步骤

| 步骤 | 操作 | 预期现象 |
|---|---|---|
| 1 | 在 Keil 中重新编译当前 App 工程 | 构建后生成 `Project.bin` 和 `Project_ota.bin` |
| 2 | 打开 RS485/USART1 对应串口 | 波特率 `115200`，8N1 |
| 3 | 复位或重新上电板子 | USART0 先看到 `OTA: pre-erase ok`，RS485 口随后看到一次 `OTA485: ready, send Project_ota.bin raw` |
| 4 | 在串口工具中选择原始/直接发送文件 | 选择 `project/output/Project_ota.bin` |
| 5 | 观察 `USART0` 日志 | 出现 `OTA: header ok`、`OTA: payload ok`、`OTA: ready, reset to BootLoader` |
| 6 | 观察 BootLoader 日志 | 出现 `app crc32 check pass` 和 `app update success` |

串口工具必须使用原始/直接发送文件模式；现场只选择 `Project_ota.bin`，不要选择 `Project.bin` 或 `Project.hex`。

如果误发了 `Project.bin`、旧格式流或损坏文件，App 会打印 `OTA: bad header status=...` 或 `OTA: payload failed status=...` 并进入错误态。若错误发生在 payload 写入 Flash 之前，修正文件后可以不复位，直接从文件开头重新原始发送合法 `Project_ota.bin`；`USART0` 出现 `OTA: resync after error code=...` 后会重新解析新文件头。若已经开始写 payload 后才失败，下载区不再保持全擦除状态，必须复位或重新上电，等待下一次 `OTA: pre-erase ok` 和 `OTA485: ready` 后再发送。

## 6. OTA 文件头部格式

| 偏移 | 字段 | 说明 |
|---|---|---|
| `0x00` | `magic = 0x474F5441` | 判断是否为当前 OTA 文件 |
| `0x04` | `header_size = 64` | 固定头部长度 |
| `0x08` | `image_size` | App payload 字节数 |
| `0x0C` | `load_addr = 0x08011000` | App 正式写入地址 |
| `0x10` | `version` | 写入 BootLoader 参数区的版本号 |
| `0x14` | `image_crc32` | App payload CRC32 |
| `0x18` | `flags = 0` | 预留 |
| `0x1C` | `header_crc32` | 头部 CRC32，计算时本字段置 0 |
| `0x20` | `stack_addr` | App 向量表第 0 项 MSP |
| `0x24` | `entry_addr` | App 向量表第 1 项 Reset_Handler |

## 7. BootLoader 的实际工作流程

| 阶段 | 位置 | 关键行为 |
|---|---|---|
| 复位启动 | BootLoader 从 `0x08000000` 运行 | 初始化 SysTick 和串口 |
| 读取参数区 | `0x08010000` | 读取 `magicWord/updateStatus/updateFlag/appSize/appCRC32` |
| 判断是否升级 | `updateStatus == 0x01` 且 `updateFlag == 0x5A` | 进入固件搬运流程 |
| 备份旧固件 | 从 `0x08011000` 读，写入 `0x08031000` | 升级前保存当前 App，给失败恢复留后路 |
| 搬运新固件 | 从 `0x08051000` 读，写入 `0x08011000` | 把缓存区的新 App 复制到 App 运行区 |
| CRC 校验 | 对正式 App 区重新计算 CRC32 | 与参数区 `appCRC32` 比较 |
| 失败恢复 | 搬运失败时从 `0x08031000` 恢复到 `0x08011000` | 尽量回到旧 App，避免坏镜像反复启动 |
| 更新参数 | 清除升级标志并记录升级次数 | 成功后复位 |
| 跳转 App | `iap_load_app(0x08011000)` | 设置 MSP、VTOR，然后跳转到 App 复位入口 |

## 8. 当前 BootLoader 搬运逻辑

| 步骤 | 当前实现 |
|---|---|
| 大小检查 | `appSize` 必须大于 0，且不能超过缓存区 `128KB` 和 App 运行区上限 |
| 地址检查 | 目标 App 地址固定要求为 `0x08011000` |
| 擦除 App 区 | `erase_pages = (appSize + 4095) / 4096`，按 4KB 页擦除 |
| 备份复制 | 每次搬运 `1024B`，从 `0x08011000` 备份完整 `128KB` 到 `0x08031000` |
| 分块复制 | 每次搬运 `1024B`，从 `0x08051000` 写到 `0x08011000` |
| 写后校验 | 从正式 App 区重新读出数据并分块计算 CRC32 |
| 成功后清标志 | 清除 `updateStatus` 和 `updateFlag`，更新计数后软件复位 |

## 9. 启动注意事项

| 注意点 | 原因 |
|---|---|
| App 必须先设置 `SCB->VTOR` | 否则中断仍可能跳到 BootLoader 的向量表 |
| App 必须重新打开全局中断 | 官方 BootLoader 跳转前会 `__disable_irq()` |
| App 必须禁用 semihosting | AC6 默认 C 库可能通过 `BKPT 0xAB` 请求调试器服务 |
| 不要把 App 链接回 `0x08000000` | `0x08000000` 必须留给 BootLoader |
| SWD 调试时不要整片擦除 | 整片擦除会把 BootLoader 和参数区一起擦掉 |

## 10. `jump app` 后脱机卡死的快速判断

| 现象 | 优先判断 | 验证方法 |
|---|---|---|
| 串口最后一行是 `BootLoader : jump app vtor:0x08011000 ...` | BootLoader 已经完成跳转前检查 | 看日志中 MSP 和 entry 是否是有效 App 向量表值 |
| Keil 调试点继续几次后 App 能运行，脱机复位不运行 | App 早期 C 库进入 semihosting | 调试器反汇编若停在 `BKPT 0xAB`，调用栈出现 `_sys_open/freopen/__rt_lib_init`，就是该问题 |
| 修复后仍不放心 | 查 map 文件符号来源 | `project/Listings/Project.map` 中 `_sys_open/_sys_write/_sys_exit/_ttywrch` 应来自 `main.o`，并出现 `__use_no_semihosting` |
