# OTA 头部 BIN 升级说明

## 核心结论

当前分支的在线升级文件是 `project/output/Project_ota.bin`。
现场发送协议不因为三分区而改变：仍然从文件第 0 字节开始，把 `Project_ota.bin` 作为一个普通二进制文件原始/直接连续发送完。

| 文件 | 用途 | 是否现场发送 |
|------|------|--------------|
| `Project.bin` | Keil/fromelf 生成的原始 App payload | 否 |
| `Project_ota.bin` | 64 字节 OTA 头部 + `Project.bin` payload | 是 |
| `Project.hex` | 调试器或烧录器使用 | 否 |

## 文件格式

`Project_ota.bin` 的前 64 字节是 OTA 头部，后面紧跟原始 App。

| 偏移 | 字段 | 说明 |
|------|------|------|
| `0x00` | `magic = 0x474F5441` | 判断是否为当前 OTA 文件 |
| `0x04` | `header_size = 64` | 固定头部长度 |
| `0x08` | `image_size` | App payload 字节数 |
| `0x0C` | `load_addr = 0x08011000` | App 正式写入地址 |
| `0x10` | `version` | 写入 BootLoader 参数区的版本号 |
| `0x14` | `image_crc32` | App payload CRC32 |
| `0x18` | `flags = 0` | 预留 |
| `0x1C` | `header_crc32` | 头部 CRC32，计算时本字段置 0 |
| `0x20` | `stack_addr` | App 向量表第 0 项 |
| `0x24` | `entry_addr` | App 向量表第 1 项 |

## Keil 生成流程

Keil After Build 当前执行两步：

```powershell
E:\Keil_v5\ARM\ARMCLANG\bin\fromelf.exe --bin --output=.\output\Project.bin .\output\Project.axf
..\tools\pack_ota_image.exe .\output\Project.bin .\output\Project_ota.bin 0x00000001 0x08011000
```

也可以手动重新打包：

```powershell
tools\pack_ota_image.exe project\output\Project.bin project\output\Project_ota.bin 0x00000001 0x08011000
```

## 现场发送步骤

| 步骤 | 操作 | 预期现象 |
|------|------|----------|
| 1 | 编译 Keil 工程 | `Project.bin` 和 `Project_ota.bin` 都生成 |
| 2 | 打开 RS485/USART1 串口，`115200 8N1` | 启动自检和调度器初始化完成后看到 `OTA485: ready, send Project_ota.bin raw` |
| 3 | 使用串口工具的原始/直接发送文件功能 | 选择 `project/output/Project_ota.bin` |
| 4 | 等待发送完成 | USART0 日志出现 `OTA: header ok`、`OTA: payload ok` |
| 5 | 等待 App 自动复位 | BootLoader 日志出现 `app crc32 check pass` 和 `app update success` |

串口工具必须使用原始/直接发送文件模式；现场只选择 `Project_ota.bin`。

若误发 `Project.bin`、旧格式流或损坏文件，`USART0` 会出现 `OTA: bad header status=...` 或 `OTA: payload failed status=...`。修正后可直接从文件开头重新发送合法 `Project_ota.bin`；看到 `OTA: resync after error code=...` 表示 App 已从错误态重新同步，不必为了普通发错文件而复位。

## 限制

| 限制项 | 当前值 |
|--------|--------|
| App payload 最大值 | `128KB` |
| App 起始地址 | `0x08011000` |
| App 运行区 | `0x08011000 ~ 0x08030FFF`，`128KB` |
| App 备份区 | `0x08031000 ~ 0x08050FFF`，`128KB` |
| App 缓存区 | `0x08051000 ~ 0x08070FFF`，`128KB` |
| 参数区 | `0x08010000 ~ 0x08010FFF` |
| 串口波特率 | `115200` |

当前 App 在发出 `OTA485: ready` 前会预擦完整 App 缓存区。收到 `Project_ota.bin` 后，任务层从 USART1 circular DMA 环形缓冲取 512B 窗口，边更新 CRC32 边把 payload 写入已擦好的 `0x08051000` 缓存区，不再申请 128KB 整包 RAM。
