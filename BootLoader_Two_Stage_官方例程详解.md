# GD32F470 BootLoader_Two_Stage 官方例程详解

## 1. 文档目的

本文用于说明官方例程 `27_BootLoader_Two_Stage` 的整体设计、Flash 地址规划、两阶段升级流程、关键源码逻辑、升级文件格式以及移植时必须注意的问题。

例程路径：

```text
D:\GD32\04 例程模板\04 例程模板\27_BootLoader_Two_Stage
```

目标平台：

| 项目 | 内容 |
|---|---|
| MCU | GD32F470VET6 |
| 开发板 | CIMC GD32F470 Development Kit V2.0 |
| 工程环境 | Keil MDK 5.25 |
| 固件库 | GD32F4xx Standard Peripheral Library |
| 核心功能 | 通过串口接收新 App 镜像，借助 BootLoader 完成 App 区更新 |

## 2. 一句话理解这个例程

这个例程不是“BootLoader 直接接收串口文件并升级”，而是一个两阶段升级方案：

| 阶段 | 执行者 | 主要工作 |
|---|---|---|
| 第一阶段 | 当前 App | 串口接收升级包，校验升级包头，把真正 App 镜像写入下载缓存区 `0x08073000`，再把升级标志、固件大小、CRC 等信息写入参数区 `0x0800C000`，最后软件复位 |
| 第二阶段 | BootLoader | 上电或复位后读取参数区，发现升级标志有效后，把下载缓存区里的新 App 镜像搬运到 App 区 `0x0800D000`，再次计算 CRC，成功后清除升级标志并复位，随后跳转到新 App |

因此，升级流程的核心是“App 负责接收，BootLoader 负责搬运和最终确认”。

## 3. 工程目录结构

根目录下主要有两个 Keil 子工程：

| 目录 | 作用 |
|---|---|
| `27_0_BootLoader` | BootLoader 工程，链接地址从 `0x08000000` 开始，用于启动判断、App 搬运、CRC 校验和跳转 |
| `27_1_App` | App 工程，链接地址从 `0x0800D000` 开始，用于正常业务运行、串口接收升级包、写下载缓存区和参数区 |
| `LED.bin` | 可升级成功的示例升级包，魔术值正确，版本号为 `0x00000000` |
| `LED10.bin` | 可升级成功的示例升级包，魔术值正确，版本号为 `0x00001000` |
| `LED111.bin` | 故意构造的错误包，魔术值不正确，App 会拒绝升级 |
| `地址_规划表.txt` | Flash 分区说明 |
| `Bootloader 参数区规划.md` | 参数区字段规划说明 |

关键源码位置：

| 模块 | 路径 | 重点 |
|---|---|---|
| BootLoader 主逻辑 | `27_0_BootLoader\Function\Function.c` | 读取参数、判断升级、搬运 App、CRC 校验、跳转 App |
| BootLoader 参数定义 | `27_0_BootLoader\HardWare\BootLoader\BootConfig.h` | `BootParam_t`、`UpdateLog_t`、`UserConfig_t`、`CalibData_t` |
| BootLoader Flash 操作 | `27_0_BootLoader\HardWare\ROM\rom.c` | 页擦除、字节写入、读取 Flash |
| App 主逻辑 | `27_1_App\Function\Function.c` | 串口升级包解析、写缓存区、写参数区、软件复位 |
| App 串口驱动 | `27_1_App\HardWare\USART\usart.c` | USART0 不定长接收，使用 IDLE 中断判定一包接收结束 |
| App 参数定义 | `27_1_App\HardWare\Config\BootConfig.h` | 与 BootLoader 侧保持相同的参数结构 |
| App Flash 操作 | `27_1_App\HardWare\ROM\rom.c` | 用于把接收数据写到下载缓存区和参数区 |

## 4. Flash 地址规划

官方地址规划如下：

| 区域 | 起始地址 | 规划大小 | 作用 |
|---|---:|---:|---|
| BootLoader 区 | `0x08000000` | 48KB | 存放 BootLoader，位于前三个 16KB 扇区 |
| 参数区 | `0x0800C000` | 4KB | 存放升级标志、App 大小、CRC、版本号、设备信息等参数 |
| App 区 | `0x0800D000` | 76KB | 存放实际用户程序 |
| 下载缓存区 | `0x08073000` | 52KB | App 接收升级包后，先把新镜像写到这里 |

Keil 实际链接配置如下：

| 工程 | Scatter 文件内容 | 实际含义 |
|---|---|---|
| BootLoader | `LR_IROM1 0x08000000 0x0000C000` | BootLoader 从 `0x08000000` 开始，最大 48KB |
| App | `LR_IROM1 0x0800D000 0x00063000` | App 从 `0x0800D000` 开始，最大约 396KB |

这里要特别注意：`地址_规划表.txt` 中写 App 区是 76KB，但 App 工程生成的 `CIMC_GD32_Template.sct` 实际给了 `0x00063000` 大小。文档规划、链接文件、BootLoader 搬运擦除范围之间并不完全一致，移植时必须统一。

## 5. 参数区规划

参数区从 `0x0800C000` 开始，总大小 4KB。

| 子区域 | 地址范围 | 大小 | 作用 |
|---|---:|---:|---|
| 主参数区 | `0x0800C000` - `0x0800C0FF` | 256B | 存放当前 BootLoader/App 升级控制信息 |
| 备份参数区 | `0x0800C100` - `0x0800C1FF` | 256B | 规划用于参数冗余备份 |
| 升级日志区 | `0x0800C200` - `0x0800C5FF` | 1024B | 规划用于记录升级日志 |
| 用户配置区 | `0x0800C600` - `0x0800C7FF` | 512B | 规划用于通信参数、功能开关等 |
| 校准数据区 | `0x0800C800` - `0x0800C9FF` | 512B | 规划用于出厂校准参数 |
| 预留扩展区 | `0x0800CA00` - `0x0800CFFF` | 1536B | 预留 |

当前代码里实际用得最关键的是 `BootParam_t` 的几个字段：

| 字段 | 典型值 | 当前用途 |
|---|---:|---|
| `magicWord` | `0x5AA5C33C` | 参数有效性标识。BootLoader 发现不匹配时，不执行升级，直接尝试跳 App |
| `updateFlag` | `0x5A` | App 写入，表示请求升级 |
| `updateStatus` | `0x01` | App 写入，表示正在升级 |
| `appSize` | 新 App 镜像字节数 | BootLoader 搬运时按这个长度读取缓存区和写 App 区 |
| `appCRC32` | 新 App 镜像 CRC32 | BootLoader 搬运后重新计算，用于确认搬运后的镜像正确 |
| `appVersion` | 升级包头中的版本号 | App 从升级包头解析后写入参数区 |
| `appStartAddr` | `0x0800D000` | App 区起始地址 |
| `appEntryAddr` | `*(0x0800D000 + 4)` | App Reset_Handler 地址 |
| `appStackAddr` | `*(0x0800D000 + 0)` | App 初始 MSP 栈顶地址 |
| `updateCount` | 自增 | BootLoader 搬运成功后加 1 |
| `resetCount` / `bootFailCount` | 自增 | BootLoader 搬运失败时增加 |

当前代码虽然定义了备份参数、升级日志、用户配置和校准数据结构，但真正的升级闭环主要依赖主参数区中的升级标志、App 大小、CRC 和地址字段。

## 6. 启动与跳转原理

Cortex-M4 从 Flash 启动时，默认从 `0x08000000` 读取中断向量表：

| 偏移 | 内容 |
|---:|---|
| `0x08000000 + 0` | 初始 MSP 栈顶地址 |
| `0x08000000 + 4` | Reset_Handler 入口地址 |

因为 BootLoader 工程链接在 `0x08000000`，所以 MCU 复位后先执行 BootLoader。

BootLoader 跳转 App 时，要做几件事：

| 步骤 | 代码位置 | 作用 |
|---|---|---|
| 检查 App 栈顶是否合法 | `iap_load_app()` | 判断 `*(uint32_t*)appxaddr` 是否像 SRAM 地址 |
| 关闭全局中断 | `__disable_irq()` | 防止 BootLoader 的中断在 App 中继续触发 |
| 关闭 SysTick | `SysTick->CTRL/LOAD/VAL = 0` | 清理 BootLoader 的周期中断来源 |
| 清除 NVIC 使能和挂起位 | `NVIC->ICER[]`、`NVIC->ICPR[]` | 避免残留外设中断进入 App |
| 设置向量表地址 | `SCB->VTOR = appxaddr` | 让异常和中断入口切换到 App 的向量表 |
| 设置 MSP | `__set_MSP(*(uint32_t*)appxaddr)` | 使用 App 自己的栈顶 |
| 跳转 Reset_Handler | `jump2app = *(appxaddr + 4); jump2app();` | 开始执行 App |

这也是 App 工程必须链接到 `0x0800D000` 的原因：App 镜像的前 8 字节必须分别是 App 自己的 MSP 和 Reset_Handler，而不是按 `0x08000000` 链接出来的地址。

App 自己初始化时也设置了：

```c
SCB->VTOR = 0x0800D000;
```

这是为了确保 App 运行后中断向量表指向 App 区，而不是默认的 BootLoader 区。

## 7. 正常启动流程

正常没有升级时，流程如下：

| 顺序 | 执行位置 | 行为 |
|---:|---|---|
| 1 | MCU 复位 | 从 `0x08000000` 进入 BootLoader |
| 2 | BootLoader `System_Init()` | 初始化 SysTick 和 USART0 |
| 3 | BootLoader `UsrFunction()` | 读取参数区 `0x0800C000` |
| 4 | BootLoader | 设置 `appStartAddr = 0x0800D000`，读取 App 栈顶和入口 |
| 5 | BootLoader | 如果参数魔术值不对，或者没有升级标志，直接跳转 App |
| 6 | BootLoader `jump_to_app()` | 清理中断、切换向量表、设置 MSP、跳 App |
| 7 | App `System_Init()` | 重新设置 `SCB->VTOR = 0x0800D000`，初始化 LED 和 USART0 |
| 8 | App `UsrFunction()` | 周期打印 `Hello World!`，LED 闪烁，并等待串口升级包 |

## 8. 串口升级完整流程

### 8.1 App 接收升级包

App 侧 USART0 配置：

| 项目 | 配置 |
|---|---|
| 串口 | USART0 |
| TX | PA9 |
| RX | PA10 |
| 波特率 | 115200 |
| 接收方式 | RBNE 字节接收 + IDLE 空闲中断判定一帧结束 |
| 接收缓存 | `usart0_tmp_buf[10 * 1024]` |

中断处理逻辑：

| 中断条件 | 处理 |
|---|---|
| RBNE 有新字节 | 如果缓存未满，就写入 `usart0_tmp_buf`，长度加 1；如果缓存满，就读取数据寄存器丢弃字节 |
| IDLE 空闲中断 | 认为一包接收结束，置位 `usart0_recv_flag`，并关闭 IDLE 和 RBNE 中断，避免重复触发 |

### 8.2 App 解析升级包头

升级包格式：

| 偏移 | 长度 | 含义 |
|---:|---:|---|
| 0 | 4B | 魔术值，示例文件中按大端字节序保存为 `5A A5 C3 3C` |
| 4 | 4B | 版本号，示例 `LED.bin` 为 `00 00 00 00`，`LED10.bin` 为 `00 00 10 00` |
| 8 | N 字节 | 真正要写入 App 区的固件镜像 |

App 代码收到数据后，先对前 8 字节做手工字节交换。交换完成后：

| 字段 | 判断或保存 |
|---|---|
| 魔术值 | 必须等于 `0x5AA5C33C`，否则打印错误并放弃升级 |
| 版本号 | 保存到 `my_param_sum.BootParam.appVersion` |
| 镜像长度 | 原始接收长度减 8，保存到 `appSize` |
| 镜像 CRC | 对偏移 8 后的镜像计算 CRC32，保存到 `appCRC32` |

### 8.3 App 写下载缓存区

App 通过 `internal_flash_erase()` 先擦除下载缓存区：

```text
DOWNLOAD_ADDR = 0x08073000
擦除页数 = 13 页
每页 = 4KB
理论擦除范围 = 52KB
```

随后把升级包头后面的真正 App 镜像写入 `0x08073000`。

注意：App 接收数组只有 `10 * 1024` 字节，而根目录 readme 写“升级数据内存最大 12KB”，地址规划又写下载缓存区 52KB。当前代码的真实串口单包接收上限受 `usart0_tmp_buf[10 * 1024]` 限制，不能简单认为支持 52KB 固件。

### 8.4 App 写参数区并复位

App 从 `0x0800C000` 读取原参数区，修改关键字段：

| 字段 | 写入值 |
|---|---|
| `magicWord` | `0x5AA5C33C` |
| `appStartAddr` | `0x0800D000` |
| `appEntryAddr` | 从 `0x0800D004` 读取 |
| `appStackAddr` | 从 `0x0800D000` 读取 |
| `appVersion` | 升级包头版本号 |
| `updateFlag` | `0x5A` |
| `updateStatus` | `0x01` |
| `appSize` | 新镜像大小 |
| `appCRC32` | 新镜像 CRC32 |

然后 App 擦除参数区第一页 `0x0800C000`，把 `Parameter_t` 写回参数区，最后调用 `NVIC_SystemReset()` 软件复位。

### 8.5 BootLoader 搬运新 App

复位后 MCU 又从 BootLoader 启动。BootLoader 读取参数区后，判断：

```text
magicWord == 0x5AA5C33C
updateStatus == 0x01
updateFlag == 0x5A
```

全部满足时进入升级搬运：

| 顺序 | 行为 |
|---:|---|
| 1 | 检查 `appSize`，如果为 0，直接失败 |
| 2 | 擦除 App 起始地址后 3 页，即 `0x0800D000` 开始的 12KB |
| 3 | 从下载缓存区 `0x08073000` 读取 `appSize` 字节到 RAM 缓冲 |
| 4 | 把 RAM 缓冲写入 App 区 `0x0800D000` |
| 5 | 对 RAM 缓冲计算 CRC32 |
| 6 | 与参数区中的 `appCRC32` 比较 |
| 7 | 成功则清除 `updateStatus` 和 `updateFlag`，并让 `updateCount++` |
| 8 | 失败则增加 `resetCount` 和 `bootFailCount` |
| 9 | 写回参数区并软件复位 |

注意：BootLoader 当前只擦除 App 区 3 页，也就是 12KB。如果未来要升级超过 12KB 的固件，必须按 `appSize` 动态计算擦除页数，否则后半段 Flash 没擦干净，写入可能失败或残留旧代码。

### 8.6 BootLoader 复位后跳转新 App

BootLoader 成功搬运后清除升级标志并复位。下一次启动时：

| 条件 | 行为 |
|---|---|
| `magicWord` 正确，但 `updateFlag/updateStatus` 已清除 | 不再搬运，直接跳 App |
| `magicWord` 不正确 | 也直接尝试跳 App |
| App 入口地址看起来不合法 | 打印 `zzz` 后死循环 |

## 9. CRC32 算法

BootLoader 和 App 中都有同名 `crc32_calc()`，算法一致：

| 项目 | 值 |
|---|---|
| 初值 | `0xFFFFFFFF` |
| 多项式 | `0xEDB88320` |
| 输入单位 | 字节 |
| 结果异或 | 最终返回 `crc ^ 0xFFFFFFFF` |

App 计算的是“升级包头 8 字节之后的镜像数据 CRC”；BootLoader 搬运后也对同一批镜像数据重新计算 CRC。只要两边结果一致，就认为从下载缓存区搬到 App 区的固件可信。

需要注意的是，BootLoader 当前比较的是“RAM 缓冲中的 CRC”，而不是重新从 App 区读回后计算 CRC。如果要更严谨，应在写入 App 区后再从 `0x0800D000` 读回计算一次。

## 10. 样例 bin 文件说明

三个示例文件长度都是 1784 字节，其中前 8 字节是升级头，后 1776 字节才是真正 App 镜像。

| 文件 | 前 4 字节 | 版本字段 | 结果 |
|---|---|---|---|
| `LED.bin` | `5A A5 C3 3C` | `00 00 00 00` | 魔术值正确，可升级 |
| `LED10.bin` | `5A A5 C3 3C` | `00 00 10 00` | 魔术值正确，可升级 |
| `LED111.bin` | `66 66 C3 3C` | `00 00 00 00` | 魔术值错误，App 会拒绝升级 |

因为 App 代码对前 8 字节做了手工字节交换，所以发送文件时应直接发送官方 `.bin` 文件，不需要再额外改变字节序。

## 11. 关键代码流程拆解

### 11.1 BootLoader `UsrFunction()`

BootLoader 的主逻辑集中在 `27_0_BootLoader\Function\Function.c`：

| 代码行为 | 作用 |
|---|---|
| `Analysis_ConfigForAddr()` | 从 `BOOT_CONFIG_ADDR` 读取 4KB 参数区到 `config_buf` |
| `memcpy(&my_param_sum, config_buf, sizeof(Parameter_t))` | 把 Flash 参数解析为结构体 |
| 强制设置 `appStartAddr = 0x800D000` | 当前例程固定 App 入口地址，不完全依赖参数区保存值 |
| 读取 `appStackAddr/appEntryAddr` | 从 App 向量表获取 MSP 和 Reset_Handler |
| 检查 `magicWord` | 参数区无效时不升级，直接跳 App |
| 检查 `updateStatus/updateFlag` | 决定是否从下载缓存区搬运新 App |
| `Download_Transport(APP_DOWNLOAD_ADDR)` | 执行擦除 App 区、读取缓存区、写 App 区、CRC 比较 |
| 写回参数区并复位 | 让下一次启动进入普通跳转流程 |

### 11.2 BootLoader `iap_load_app()`

`iap_load_app()` 是跳转 App 的核心函数：

| 关键动作 | 为什么需要 |
|---|---|
| 判断栈顶地址 | 防止跳到空 Flash 或错误镜像 |
| 关闭中断 | 防止 BootLoader 外设中断污染 App |
| 停止 SysTick | 避免 SysTick 在切换过程中进入错误向量表 |
| 清除 NVIC 使能和挂起 | 避免旧中断状态带入 App |
| 设置 `SCB->VTOR` | 切换中断向量表到 App |
| 设置 MSP | 使用 App 自己的栈 |
| 跳转到 `*(appxaddr + 4)` | 执行 App Reset_Handler |

### 11.3 App `UsrFunction()`

App 的升级入口也在 `27_1_App\Function\Function.c`：

| 代码行为 | 作用 |
|---|---|
| `bootloader_config_init()` | 初始化默认参数结构 |
| 检查 `usart0_recv_flag` | 判断 USART0 是否收到完整一包 |
| 调整前 8 字节字节序 | 把升级包头转换为 MCU 读取的 `uint32_t` |
| 检查魔术值 | 防止普通串口数据被误认为升级包 |
| 计算 CRC32 | 为 BootLoader 后续确认提供基准 |
| 擦写 `0x08073000` | 保存新 App 镜像 |
| 修改并写回 `0x0800C000` | 通知 BootLoader 下一次启动执行升级 |
| 软件复位 | 主动进入 BootLoader |

## 12. 这个例程的优点

| 优点 | 说明 |
|---|---|
| BootLoader 简洁 | BootLoader 不负责复杂串口收包，只负责判断、搬运、校验、跳转 |
| App 可扩展协议 | 串口协议、文件头、版本号处理都在 App，后续可扩展为 CAN、USB、SD 卡等 |
| 参数区解耦 | App 和 BootLoader 通过 Flash 参数区交接状态，复位后仍能继续判断 |
| 跳转流程比较完整 | 跳 App 前清理中断、SysTick、NVIC，并设置 VTOR 和 MSP |
| 有 CRC 保护 | 至少能发现接收数据或搬运过程中的明显损坏 |

## 13. 当前代码的重要问题和风险

| 风险点 | 现象 | 建议 |
|---|---|---|
| 容量配置不一致 | readme 写最大 12KB，App 缓冲是 10KB，下载缓存区规划是 52KB，App 链接区更大 | 统一升级包最大值，并按最大值设置串口接收、擦除页数、RAM 缓冲和文档 |
| BootLoader 只擦 12KB App 区 | `Download_Transport()` 固定擦 3 页 | 按 `appSize` 向上取整计算擦除页数 |
| BootLoader RAM 缓冲 20KB | `config_buf[CONFIG_APP_SIZE]`，其中 `CONFIG_APP_SIZE = 20KB` | 如果升级包超过 20KB 会越界或写不完整，应增加边界检查 |
| App 接收缓存 10KB | `usart0_tmp_buf[10 * 1024]` | 若需要 12KB 或更大升级包，应扩大缓存或改成分包协议 |
| 升级包头字节序处理很硬编码 | 手动交换前 8 字节 | 建议写成 `read_be32()` 这类函数，避免后续字段增多时出错 |
| 参数区 CRC 没真正使用 | `paramCRC32` 字段存在，但代码没校验整体参数 | 建议写参数时计算，读参数时验证主参数区和备份参数区 |
| 备份参数区没有真正冗余恢复 | 有结构和字段，但当前主流程没有完整恢复逻辑 | 建议实现主区损坏时从备份区恢复 |
| BootLoader 校验写入结果不够严谨 | 写 App 后计算的是 RAM 缓冲 CRC，不是从 App 区读回 CRC | 建议写完后从 Flash App 区读回计算 |
| 失败处理不完整 | 搬运失败后写失败计数并复位，下一次可能仍重复升级 | 建议失败后清除升级标志或引入最大失败次数和回滚策略 |
| 串口协议没有长度字段 | 当前靠 IDLE 中断判定一包结束 | 大文件或串口抖动时可靠性较弱，建议引入包长、分包序号、ACK/重传 |

## 14. 移植到自己项目时的检查清单

| 检查项 | 必须确认的内容 |
|---|---|
| App 链接地址 | Keil IROM1 起始地址必须是 `0x0800D000` 或你规划的新 App 起始地址 |
| BootLoader 链接地址 | 必须从 `0x08000000` 开始，否则 MCU 复位后不会先进入 BootLoader |
| `SCB->VTOR` | App 启动后必须设置成 App 起始地址 |
| 参数区地址 | App 和 BootLoader 中 `BOOT_CONFIG_ADDR/PARAM_ADDR` 必须一致 |
| 下载缓存区地址 | App 写入地址和 BootLoader 读取地址必须一致 |
| Flash 擦除粒度 | 当前按 4KB 页擦除，移植到不同 GD32/STM32 型号时要核对 Flash 页/扇区大小 |
| App 最大大小 | 串口缓存、BootLoader 搬运缓存、擦除页数、下载缓存区大小必须统一 |
| 升级包格式 | 发送给 App 的文件必须包含 8 字节升级头 |
| CRC 算法 | PC 打包工具、App、BootLoader 三方 CRC32 算法必须一致 |
| 中断清理 | BootLoader 跳 App 前必须关闭旧中断并切换向量表 |

## 15. 推荐的改进方向

如果要把这个例程改成更可靠的工程级 BootLoader，建议按下面顺序做：

| 优先级 | 改进项 | 原因 |
|---:|---|---|
| 1 | 统一所有地址和容量宏 | 避免 App、BootLoader、文档、Keil 链接脚本互相不一致 |
| 2 | 给升级包增加长度字段 | 当前只靠接收长度，不利于分包和错误恢复 |
| 3 | 改成分包写 Flash | 避免一次性占用大 RAM 缓冲 |
| 4 | BootLoader 写入后从 App 区读回 CRC | 确认 Flash 中最终内容正确 |
| 5 | 实现参数区主备恢复 | 防止写参数区掉电导致设备无法升级 |
| 6 | 增加失败回滚策略 | 防止坏包导致反复复位和反复搬运 |
| 7 | 增加版本比较策略 | 防止错误降级或重复升级 |
| 8 | 封装升级包头解析 | 去掉手工字节交换，提高可维护性 |

## 16. 最终总结

这个官方例程的核心价值是演示“App 接收 + BootLoader 搬运”的两阶段 IAP 思路：

| 关键点 | 结论 |
|---|---|
| 启动入口 | MCU 复位先进入 BootLoader，因为 BootLoader 位于 `0x08000000` |
| App 位置 | App 必须链接到 `0x0800D000`，并在运行时设置 `SCB->VTOR = 0x0800D000` |
| 升级触发 | App 串口收到魔术值正确的升级包后，把镜像写入 `0x08073000`，把升级标志写入 `0x0800C000`，然后复位 |
| 升级执行 | BootLoader 读取参数区，发现 `updateFlag=0x5A` 且 `updateStatus=0x01` 后，把缓存区镜像复制到 App 区 |
| 正确性检查 | App 和 BootLoader 使用同一套 CRC32 算法确认镜像数据 |
| 跳转关键 | 跳 App 前必须清中断、停 SysTick、切 VTOR、设 MSP，再跳 Reset_Handler |
| 当前短板 | 容量配置、擦除范围、参数 CRC、备份恢复和串口协议可靠性还不够工程化 |

如果只是学习 BootLoader 基本原理，这份代码已经足够清楚；如果要用于比赛或项目，建议重点改造“容量统一、分包协议、参数区容错、Flash 写后读回校验、失败回滚”这几部分。
