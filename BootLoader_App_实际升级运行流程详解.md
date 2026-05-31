# BootLoader 与 App 实际升级运行流程详解

## 1. 先说结论

当前分支采用“头部 BIN 裸流升级”方案。现场只发送 Keil 构建后生成的 `project/output/Project_ota.bin`，App 负责接收和校验，BootLoader 只负责复位后的最终搬运。

| 角色 | 负责什么 | 不负责什么 |
|---|---|---|
| Keil After Build | 先用 `fromelf` 生成 `Project.bin`，再用 `tools/pack_ota_image.exe` 生成 `Project_ota.bin` | 不直接烧录 MCU Flash |
| 上位机串口工具 | 通过 RS485/USART1 原始/直接发送 `Project_ota.bin` | 不拆包、不握手、不改 MCU Flash |
| App | 接收 `Project_ota.bin` 裸流、解析 64 字节头部、校验 payload、写下载缓存区和参数区、软件复位 | 不把新固件搬到正式 App 区 |
| BootLoader | 上电后读取参数区、把下载缓存区新固件搬到正式 App 区、CRC 校验、跳转新 App | 不负责接收串口 OTA 数据 |

一句话：

> **App 负责“准备好升级现场”，BootLoader 负责“执行最终切换”。**

当前分支不使用额外脚本引导发送，不使用串口协议握手，也不要求上位机发送文本命令。操作者只需要在串口工具里选择原始/直接发送文件，并选择 `Project_ota.bin`。

---

## 2. Flash 分区关系

| 区域 | 地址 | 大小 | 作用 | 谁会写 |
|---|---:|---:|---|---|
| BootLoader 区 | `0x08000000 ~ 0x0800BFFF` | `48KB` | MCU 复位后先运行这里 | 烧录器 / BootLoader 工程 |
| 参数区 | `0x0800C000 ~ 0x0800CFFF` | `4KB` | 保存升级标志、大小、CRC、版本、App 地址 | App / BootLoader |
| 正式 App 区 | `0x0800D000 ~ 0x08066FFF` | `360KB` | 当前真正运行的 App | BootLoader / 烧录器 |
| 下载缓存区 | `0x08067000 ~ 0x0807FFFF` | `100KB` | 暂存“下一版新固件”payload | App |

| 问题 | 当前答案 |
|---|---|
| 这块板子最多能运行多大的 App | 按当前链接地址，正式 App 区上限是 `360KB` |
| 当前串口 OTA 最多能升级多大的 App | `Project_ota.bin` 里的 payload，也就是原始 `Project.bin`，必须 `<= 100KB` |

当前 OTA 的瓶颈不是正式 App 区，而是内部 Flash 下载缓存区只有 `100KB`。如果后续 App payload 超过 `100KB`，需要重新规划下载缓存区或改用外部 Flash/其他 OTA 架构。

---

## 3. `Project_ota.bin` 文件格式

`Project_ota.bin` 由 64 字节 OTA 头部加原始 App payload 组成：

```text
Project_ota.bin
├─ OTA 头部，固定 64 字节
└─ Project.bin payload，必须从 App 向量表开始
```

所有多字节字段均为小端 `uint32_t`。

| 偏移 | 字段 | 当前要求 |
|---:|---|---|
| `0x00` | `magic` | `0x474F5441` |
| `0x04` | `header_size` | `64` |
| `0x08` | `image_size` | App payload 字节数，范围 `1..100KB` |
| `0x0C` | `load_addr` | `0x0800D000` |
| `0x10` | `version` | 写入 BootLoader 参数区的版本号 |
| `0x14` | `image_crc32` | 对 App payload 计算的 CRC32 |
| `0x18` | `flags` | 当前固定为 `0` |
| `0x1C` | `header_crc32` | 对 64 字节头部计算 CRC32，计算前本字段置 `0` |
| `0x20` | `stack_addr` | payload 第 0 个字，必须是 SRAM 地址 |
| `0x24` | `entry_addr` | payload 第 1 个字，必须是 App 区 Thumb 入口地址 |
| `0x28..0x3F` | `reserved` | 当前固定为 `0` |

Keil After Build 当前执行：

```powershell
E:\Keil_v5\ARM\ARMCLANG\bin\fromelf.exe --bin --output=.\output\Project.bin .\output\Project.axf
..\tools\pack_ota_image.exe .\output\Project.bin .\output\Project_ota.bin 0x00000001 0x0800D000
```

也可以在仓库根目录手动重新打包：

```powershell
tools\pack_ota_image.exe project\output\Project.bin project\output\Project_ota.bin 0x00000001 0x0800D000
```

---

## 4. 一次成功升级的完整链路

| 顺序 | 执行方 | 动作 | 结果 |
|---:|---|---|---|
| 1 | Keil | 构建 App，并生成 `Project.bin` 和 `Project_ota.bin` | OTA 文件准备完成 |
| 2 | 上位机串口工具 | 以原始/直接发送方式发送 `Project_ota.bin` | 字节流进入 RS485/USART1 |
| 3 | USART1 + DMA | 持续接收裸流，IDLE 或 DMA 满缓冲时移交数据片段 | ISR 只复制数据并置标志 |
| 4 | App `uart_ota_task()` | 解析 64 字节头部，校验 magic、大小、地址、头部 CRC 和向量表 | 头部合法后继续接收 payload |
| 5 | App `uart_ota_task()` | 把 payload 收到 RAM 缓冲，并计算 CRC32 | 避免边接收边擦写内部 Flash 导致丢字节 |
| 6 | App | payload CRC 和向量表复核通过后，写入 `0x08067000` 下载缓存区 | 新固件 payload 暂存在 Flash |
| 7 | App | 回读下载缓存区并重新计算 CRC32 | 确认 Flash 写入正确 |
| 8 | App | 写 `0x0800C000` 参数区，置 `updateFlag=0x5A`、`updateStatus=0x01` | 告诉 BootLoader 新固件已准备好 |
| 9 | App | 软件复位 | 控制权交还 BootLoader |
| 10 | BootLoader | 启动后读取参数区 | 识别到升级任务 |
| 11 | BootLoader | 把 `0x08067000` 搬到 `0x0800D000` | 新固件变成正式 App |
| 12 | BootLoader | 对正式 App 区重新计算 CRC32 | 与参数区 `appCRC32` 比较 |
| 13 | BootLoader | 清升级标志并复位 | 避免下次上电重复搬运 |
| 14 | BootLoader | 再次启动后跳转 `0x0800D000` | 新 App 正式运行 |

---

## 5. App 端实现边界

App 侧 OTA 主要由以下文件完成：

| 文件 | 作用 |
|---|---|
| [Function/uart_ota_app.c](D:/GD32/2026706296/Function/uart_ota_app.c:1) | RS485/USART1 OTA 总入口，负责头部解析、裸流接收、CRC 校验、下载区写入和参数区提交 |
| [Function/uart_ota_app.h](D:/GD32/2026706296/Function/uart_ota_app.h:1) | OTA 接收缓冲、共享标志和任务接口声明 |
| [HardWare/BOOTLOADER/bootloader_port.c](D:/GD32/2026706296/HardWare/BOOTLOADER/bootloader_port.c:1) | 下载缓存区擦写、CRC32、参数区回写和软件复位封装 |
| [User/gd32f4xx_it.c](D:/GD32/2026706296/User/gd32f4xx_it.c:1) | USART1 IDLE 和 DMA 满缓冲中断，把 DMA 数据片段移交给 OTA 任务 |
| [HardWare/USART/bsp_usart.c](D:/GD32/2026706296/HardWare/USART/bsp_usart.c:1) | USART1/RS485、DMA、IDLE 中断和 DMA 满缓冲中断初始化 |
| [tools/pack_ota_image.c](D:/GD32/2026706296/tools/pack_ota_image.c:1) | PC 侧打包工具，把 `Project.bin` 转换为 `Project_ota.bin` |

当前 App 的关键约束：

| 约束 | 说明 |
|---|---|
| ISR 不解析协议 | 中断里只做 DMA 数据复制、长度记录、标志置位和 DMA 重新装载 |
| 任务层解析裸流 | `uart_ota_task()` 周期性消费 DMA 片段，先拼头部，再收 payload |
| payload 先完整进 RAM | 内部 Flash 擦写可能阻塞取指，先完整接收可降低串口丢字节风险 |
| 校验失败不擦写参数 | magic、头部 CRC、payload CRC、向量表、回读 CRC 任一失败，都不会设置 BootLoader 升级标志 |
| 成功后自动复位 | 参数区写入成功后，App 延时给日志和 RS485 发送完成留时间，再软件复位 |

---

## 6. BootLoader 端实现边界

BootLoader 不关心 `Project_ota.bin` 的 64 字节头部，也不接收串口数据。它只读取参数区中的升级任务字段。

| 参数区字段 | App 成功提交时写入 |
|---|---|
| `magicWord` | `0x5AA5C33C` |
| `updateFlag` | `0x5A` |
| `updateStatus` | `0x01` |
| `appSize` | 头部中的 `image_size` |
| `appCRC32` | 头部中的 `image_crc32` |
| `appVersion` | 头部中的 `version` |
| `appStartAddr` | `0x0800D000` |

BootLoader 搬运时必须满足：

| 检查项 | 当前要求 |
|---|---|
| 固件大小 | `appSize > 0` 且不超过下载缓存区和 App 区上限 |
| 搬运源地址 | 固定 `0x08067000` |
| 搬运目标地址 | 固定 `0x0800D000` |
| 擦除策略 | 按 `appSize` 动态计算 App 区擦除页数 |
| 搬运策略 | 以 1024 字节分块复制 |
| 最终校验 | 从正式 App 区重新读出并计算 CRC32，与参数区 `appCRC32` 比较 |

---

## 7. 现场升级步骤

| 步骤 | 操作 | 预期现象 |
|---:|---|---|
| 1 | 打开 `project/2026706296.uvprojx`，重新编译 App | 构建日志为 `0 Error(s)` |
| 2 | 检查输出目录 | `project/output/Project.bin` 和 `project/output/Project_ota.bin` 都存在 |
| 3 | 打开 RS485/USART1 对应串口 | 波特率 `460800`，8N1 |
| 4 | 复位或重新上电板子 | RS485 口看到一次 `OTA485: ready, send Project_ota.bin raw` |
| 5 | 在串口工具中选择原始/直接发送文件 | 选择 `project/output/Project_ota.bin` |
| 6 | 等待发送完成 | USART0 日志出现 `OTA: header ok` 和 `OTA: payload ok` |
| 7 | 等待 App 自动复位 | USART0 日志出现 `OTA: ready, reset to BootLoader` |
| 8 | 观察 BootLoader 日志 | 出现 `app crc32 check pass` 和 `app update success` |

---

## 8. 常见问题定位

| 现象 | 优先判断 | 检查方法 |
|---|---|---|
| RS485 口上电没有 `OTA485: ready` | App 没跑到当前分支，或 RS485 接线/串口号不对 | 先看 USART0 启动日志，再查 RS485 A/B、共地和 COM 口 |
| USART0 出现 `OTA: header error` | 发送的不是 `Project_ota.bin` 或文件被截断/污染 | 重新确认串口工具选择的是原始/直接发送，并选择 `project/output/Project_ota.bin` |
| `OTA: payload crc error` | 传输中丢字节或串口工具发送设置不对 | 降低发送速率、关闭附加换行/文本转义，确认是二进制原始发送 |
| 下载区回读 CRC 失败 | 内部 Flash 写入失败或下载缓存区地址不一致 | 检查 `0x08067000` 分区和 BootLoader/App 常量是否一致 |
| BootLoader 搬运后 CRC 失败 | 搬运过程或正式 App 区擦写异常 | 看 BootLoader 日志中的大小、CRC 和擦除页数 |
| BootLoader `jump app` 后脱机卡住 | App 早期 C 库可能进入 semihosting | 检查 `project/Listings/Project.map` 中 `_sys_open/_sys_write/_sys_exit/_ttywrch` 是否来自 `main.o` |

---

## 9. 验证命令

提交 OTA 改动前至少运行：

```powershell
gcc -std=c99 -Wall -Wextra -Werror tools\pack_ota_image.c -o tools\pack_ota_image.exe
python -m unittest tools.test_header_bin_ota_static
& 'E:\Keil_v5\UV4\UV4.exe' -b 'project\2026706296.uvprojx' -j0
Select-String -Path 'project\output\Project.build_log.htm' -Pattern 'Program Size|Error\(s\)|Warning\(s\)'
Test-Path 'project\output\Project.bin'
Test-Path 'project\output\Project_ota.bin'
Select-String -Path 'project\Listings\Project.map' -Pattern '__use_no_semihosting|_sys_open|_sys_write|_sys_exit|_ttywrch'
```

`Project_ota.bin` 必须比 `Project.bin` 大 64 字节，且 payload 大小不能超过 `100KB`。
