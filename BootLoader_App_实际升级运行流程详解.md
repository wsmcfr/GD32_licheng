# Bootloader 与 App 实际升级运行流程详解

## 1. 核心结论

当前正式版采用赛题升级流程：`0x0501 -> 0x0502 -> bin -> 0x0503`。

| 阶段 | 负责方 | 说明 |
|---|---|---|
| `0x0501` | App | App 收到升级请求后回复 OK，只写 Bootloader 等待标志并复位 |
| `0x0502` | Bootloader | Bootloader 进入等待窗口后接收赛题 `.bin` 裸数据 |
| bin 文件 | Bootloader | 前 4 字节必须是 `5AA5C33C`，魔术字不写入 App 下载区 |
| `0x0503` | Bootloader | Bootloader 回复 OK 后执行备份、搬运、CRC 校验和回滚 |

旧的 `Project_ota.bin` 头部包、App 侧裸流 OTA、`OTA485: ready` 提示和 `115200` 升级口径已经废弃。当前默认通信波特率必须是赛题要求的 `19200 8N1`。

## 2. Flash 分区

| 区域 | 地址 | 大小 | 作用 | 谁写入 |
|---|---:|---:|---|---|
| Bootloader 区 | `0x08000000 ~ 0x0800FFFF` | `64KB` | 复位后先运行 | 烧录器 |
| 参数区 | `0x08010000 ~ 0x08010FFF` | `4KB` | 保存升级状态、大小、CRC | App / Bootloader |
| App 运行区 | `0x08011000 ~ 0x08030FFF` | `128KB` | 正式运行 App | Bootloader |
| App 备份区 | `0x08031000 ~ 0x08050FFF` | `128KB` | 升级前备份旧 App | Bootloader |
| App 缓存区 | `0x08051000 ~ 0x08070FFF` | `128KB` | 暂存新 App payload | Bootloader |
| 未使用区 | `0x08071000 ~ 0x0807FFFF` | `60KB` | 预留 | 无 |

## 3. App 端边界

| 文件 | 当前职责 |
|---|---|
| `Protocol/cimc_protocol.c/.h` | 解析赛题 ASCII HEX 帧，处理 `0x0501` |
| `Driver/BOOTLOADER/bootloader_port.c/.h` | 写参数区等待标志，不写固件大小和 CRC |
| `Function/usart_app.c/.h` | 从 USART1/RS485 取完整 ASCII 帧并交给协议层 |
| `User/gd32f4xx_it.c` | USART1 IDLE 中断只做 DMA 帧移交，不解析协议 |

App 收到 `0x0501` 后写入：

| 字段 | 值 |
|---|---|
| `magicWord` | `0xC0DEF47A` |
| `updateFlag` | `0x5A` |
| `updateStatus` | `0x02` |

`updateStatus=0x02` 的含义是：下一次复位进入 Bootloader 后，等待赛题 `0x0502/0x0503`，而不是直接搬运。

## 4. Bootloader 端边界

| 文件 | 当前职责 |
|---|---|
| `Function/Function.c` | 判断普通启动、等待升级、执行搬运、跳转 App |
| `Protocol/boot_cimc_protocol.c/.h` | 处理 `0x0502/0x0503`，接收 bin 并计算 payload CRC32 |
| `Driver/USART/bsp_usart.c/.h` | 初始化 USART1/RS485 19200，发送应答时切换 PE8 方向 |
| `Driver/ROM/rom.c` | 内部 Flash 擦除、写入、读取 |

Bootloader 收到 `0x0502` 后会擦除 `0x08051000 ~ 0x08070FFF`，接收 `.bin` 裸数据，校验前 4 字节魔术字 `5AA5C33C`。魔术字只用于识别文件，不会写到 App 下载区，避免覆盖 App 向量表。

## 5. 一次成功升级链路

| 顺序 | 执行方 | 动作 | 结果 |
|---:|---|---|---|
| 1 | 上位机 | 发送 `0x0501` 到 App | App 回复 OK |
| 2 | App | 写 `updateStatus=0x02` 并复位 | Bootloader 获得升级请求 |
| 3 | Bootloader | 检测等待标志，进入 10s 升级窗口 | 等待 `0x0502` |
| 4 | 上位机 | 发送 `0x0502` | Bootloader 准备接收 bin |
| 5 | 上位机 | 发送带 `5AA5C33C` 的赛题 bin | Bootloader 写 payload 到下载区 |
| 6 | Bootloader | 接收完成并校验通过 | 回复 `0x0502` OK |
| 7 | 上位机 | 发送 `0x0503` | Bootloader 回复 OK 并开始搬运 |
| 8 | Bootloader | 备份旧 App 到 `0x08031000` | 具备回滚条件 |
| 9 | Bootloader | 从 `0x08051000` 搬运到 `0x08011000` | 新 App 进入运行区 |
| 10 | Bootloader | 对运行区重新计算 CRC32 | 成功则清标志并复位 |

## 6. 验证命令

App 工程：

```powershell
& 'E:\Keil_v5\UV4\UV4.exe' -b 'project\2026706296.uvprojx' -j0
Select-String -Path 'project\output\Project.build_log.htm' -Pattern 'Program Size|Error\(s\)|Warning\(s\)'
```

Bootloader 工程：

```powershell
& 'E:\Keil_v5\UV4\UV4.exe' -b 'project\2026706296.uvprojx' -j0
Select-String -Path 'project\Objects\2026706296.build_log.htm' -Pattern 'Program Size|Error\(s\)|Warning\(s\)'
```

两个工程都应为 `0 Error(s), 0 Warning(s)`。
