# 当前工程作为 BootLoader App 的接入说明

## 1. 当前目标

| 项目 | 当前说明 |
|---|---|
| 当前工程角色 | CIMC 赛题 App 工程，由独立 Bootloader 跳转运行 |
| App 链接地址 | `0x08011000` |
| App 最大运行区 | `0x00020000`，也就是 `128KB` |
| 正式通信口 | `USART1 + RS485` |
| 默认串口参数 | `19200 8N1` |
| 升级触发方式 | App 收到赛题 `0x0501` 后回复 OK，写 Bootloader 等待标志并软件复位 |
| 升级文件 | 上位机在 Bootloader 等待窗口内按赛题发送带 `5AA5C33C` 魔术字的 `.bin` |

当前正式版已经废弃旧的 `Project_ota.bin` 头部包 OTA。App 不再接收完整升级文件，也不再输出 `OTA485: ready`。升级文件接收由 `D:\GD32\2026706296_bootloader` 中的 Bootloader 通过 `0x0502/0x0503` 完成。

## 2. Flash 分区关系

| 分区 | 起始地址 | 大小 | 用途 |
|---|---:|---:|---|
| Bootloader 区 | `0x08000000` | `64KB` | MCU 复位后先运行 Bootloader |
| 参数区 | `0x08010000` | `4KB` | 保存升级标志、App 大小、CRC、入口地址等参数 |
| App 运行区 | `0x08011000` | `128KB` | 当前工程的链接和运行地址 |
| App 备份区 | `0x08031000` | `128KB` | Bootloader 升级前备份旧 App，搬运失败时恢复 |
| App 缓存区 | `0x08051000` | `128KB` | Bootloader 接收 `0x0502` 后写入新 App payload |
| 未使用区 | `0x08071000` | `60KB` | 当前不参与升级 |

## 3. 当前工程职责

| 文件 | 当前作用 |
|---|---|
| `User/boot_app_config.h/.c` | App 启动时设置 `SCB->VTOR = 0x08011000` 并恢复 Bootloader 跳转现场 |
| `project/2026706296.uvprojx` | IROM 为 `0x08011000 / 0x020000`，After Build 只生成 `Project.bin` |
| `Driver/USART/bsp_usart.c/.h` | 只初始化 USART1/RS485，默认 `19200 8N1`，PE8 控制 RS485 方向 |
| `Function/usart_app.c/.h` | 在任务上下文消费 USART1 IDLE 帧并交给 `cimc_protocol` |
| `Protocol/cimc_protocol.c/.h` | 解析赛题 ASCII HEX 帧、CRC-16-Modbus，并处理 `0x0301/0x0302/0x0303/0x0501` |
| `Driver/BOOTLOADER/bootloader_port.c/.h` | App 收到 `0x0501` 后写 `updateStatus=0x02` 并复位 |

## 4. 升级流程

| 步骤 | 执行方 | 行为 |
|---:|---|---|
| 1 | 上位机 | 通过 USART1/RS485 向 App 下发 `0x0501` 升级请求帧 |
| 2 | App | 校验帧，回复 OK，应答内容为 `FF` |
| 3 | App | 写参数区：`magicWord=0xC0DEF47A`、`updateFlag=0x5A`、`updateStatus=0x02` |
| 4 | App | 延时约 20ms 后软件复位 |
| 5 | Bootloader | 普通上电静默等待；看到 `updateStatus=0x02` 时进入 10s 升级窗口 |
| 6 | 上位机 | 下发 `0x0502`，随后发送赛题 `.bin` 裸数据 |
| 7 | Bootloader | 校验 `.bin` 前 4 字节魔术字 `5AA5C33C`，只把魔术字后的 App payload 写入 `0x08051000` |
| 8 | 上位机 | 下发 `0x0503` |
| 9 | Bootloader | 先回复 OK，再备份旧 App、搬运新 App、CRC 校验、失败回滚、成功复位 |

## 5. 旧功能状态

| 旧内容 | 当前状态 |
|---|---|
| `Project_ota.bin` 64 字节头部包 | 已废弃，不再生成，不再发送 |
| `Function/uart_ota_app.c/.h` | 已从源码树和 Keil 编译项移除 |
| `Protocol/ota_image_protocol.c/.h` | 已从源码树和 Keil 编译项移除 |
| USART0 调试 Shell | 已从正式业务删除，`printf/my_printf` 默认不输出 |
| SMARTFS/littlefs/GD25QXX 文件系统 | 已从源码树和正式 Keil 编译链路移除 |
| 按键和低功耗按键演示 | 已从源码树和正式 Keil 编译链路移除 |

## 6. 构建与检查

```powershell
& 'E:\Keil_v5\UV4\UV4.exe' -b 'project\2026706296.uvprojx' -j0
Select-String -Path 'project\output\Project.build_log.htm' -Pattern 'Program Size|Error\(s\)|Warning\(s\)'
Test-Path 'project\output\Project.bin'
```

预期：构建日志为 `0 Error(s), 0 Warning(s)`，`Project.bin` 存在。正式赛题升级不要检查或发送 `Project_ota.bin`。
