# OTA 头部 BIN 升级说明

## 当前状态

本文档记录旧 OTA 方案的处理结论：旧的 `Project_ota.bin` 头部 BIN 升级已经废弃，正式赛题版不再使用。

| 旧内容 | 当前处理 |
|---|---|
| `Project_ota.bin` | 不再生成、不再发送 |
| 64 字节 OTA 头部 | 不再解析 |
| `tools/pack_ota_image.exe` | 已从 Keil After Build 移除 |
| App 侧 `uart_ota_app` 裸流接收 | 已从 Keil 编译项移除 |
| `Protocol/ota_image_protocol` | 已从 Keil 编译项移除 |

## 正式赛题升级流程

| 步骤 | 通信对象 | 内容 |
|---:|---|---|
| 1 | App | 上位机下发 `0x0501` 升级请求 |
| 2 | App | 回复 OK，写 `updateStatus=0x02`，软件复位 |
| 3 | Bootloader | 进入 10s 升级窗口，等待 `0x0502` |
| 4 | Bootloader | 收到 `0x0502` 后接收赛题 `.bin` 裸数据 |
| 5 | Bootloader | 校验 `.bin` 前 4 字节 `5AA5C33C`，将后续 payload 写入 `0x08051000` |
| 6 | Bootloader | 收到 `0x0503` 后回复 OK，执行备份、搬运、CRC 校验和回滚 |

通信口固定为 `USART1/RS485`，默认 `19200 8N1`。不要再按旧文档使用 `115200` 发送 `Project_ota.bin`。

## 构建产物

| 文件 | 当前用途 |
|---|---|
| `project/output/Project.axf` | Keil 调试用 |
| `project/output/Project.hex` | 烧录器按地址下载用 |
| `project/output/Project.bin` | 原始 App 镜像，可作为生成赛题 bin 的 payload 来源 |
| `project/output/Project_ota.bin` | 旧方案文件，正式版不再需要 |

正式版构建只需要确认 `Project.bin` 存在，不要求 `Project_ota.bin` 存在。
