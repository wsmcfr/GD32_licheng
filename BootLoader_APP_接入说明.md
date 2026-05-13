# 当前工程作为 BootLoader App 的接入说明

## 1. 当前目标

| 项目 | 说明 |
|---|---|
| 当前工程角色 | App 工程，不再从 `0x08000000` 独立启动 |
| 参考 BootLoader | `D:\GD32\04 例程模板\04 例程模板\27_BootLoader_Two_Stage` |
| App 链接地址 | `0x0800D000` |
| App 最大运行区 | `0x00063000` |
| 当前已完成内容 | 当前工程已经按 App 地址链接，并在启动时重定位中断向量表；复制到本工程内的 BootLoader 已按当前 App 优化搬运逻辑；App 侧已拆分为 `USART0` 日志/普通透传 + `USART2` 专用分包 OTA；BootLoader 交接层已独立为 `bootloader_port.c/.h`；App/上位机工具默认升级波特率已统一为 `460800`，发送工具会打印分帧 ACK 进度 |
| 仍需注意内容 | 首次导入 OTA 功能时仍需先用 SWD/烧录器把带 OTA 接收逻辑的 App 写到 `0x0800D000`；后续升级使用 `tools/make_uart_ota_packet.py --mode send --port COMx --baudrate 460800`，不要直接发送 `Project.bin` |

## 2. Flash 分区关系

| 分区 | 起始地址 | 大小 | 用途 |
|---|---:|---:|---|
| BootLoader 区 | `0x08000000` | `48KB` | MCU 复位后先运行 BootLoader |
| 参数区 | `0x0800C000` | `4KB` | 保存升级标志、App 地址、App 大小、CRC 等参数 |
| App 区 | `0x0800D000` | `0x63000` | 当前工程的链接和运行地址 |
| 下载缓存区 | `0x08073000` | `52KB` | App 接收 DATA 分包后，先把新固件按偏移写到这里 |

### 2.1 为什么这样分区

| 设计点 | 当前工程的做法 | 原因 |
|---|---|---|
| 复位入口 | BootLoader 固定放 `0x08000000` 开始 | Cortex-M 复位后必须先从这里取向量表 |
| 参数区 | 单独留 `0x0800C000 ~ 0x0800CFFF` 一整页 | 当前按 `4KB` 页擦写，参数区独占一页最安全 |
| 正式 App 区 | 放在参数区后面，当前链接为 `0x0800D000 / 0x63000` | 这是设备真正长期运行的程序空间 |
| 下载缓存区 | 放在 Flash 尾部 `0x08073000 ~ 0x0807FFFF` | 升级时先写缓存区，避免先破坏旧 App |

地址可以这样快速记：

| 计算 | 结果 |
|---|---:|
| `0x08000000 + 48KB` | `0x0800C000` |
| `0x0800C000 + 4KB` | `0x0800D000` |
| `0x08080000 - 52KB` | `0x08073000` |

要特别区分两个概念：

| 问题 | 当前答案 |
|---|---|
| 当前工程“最多能运行多大的 App” | 正式 App 区上限是 `0x63000 = 396KB` |
| 当前这套串口 OTA“最多能升级多大的 App” | 下载缓存区上限是 `52KB` |

所以 App 区和下载缓存区大小不一致，不是算错了，而是因为：

> 正式 App 区是“长期运行空间”，  
> 下载缓存区只是“升级过程中的临时仓库”。

### 2.2 参数区为什么是 4KB

| 原因 | 说明 |
|---|---|
| Flash 擦除按 `4KB` 页进行 | 参数区独占 1 页，读写最简单，也最不容易误伤邻接区域 |
| 当前实现按整页回写 | App/BootLoader 都会把整个参数页读到 RAM，修改后整页擦除、整页写回 |
| 参数区不只是升级标志 | 还包含备份参数、日志、用户配置、校准数据和预留扩展区 |

### 2.3 当前限制

| 项目 | 当前限制 |
|---|---|
| 在线 OTA 固件大小 | `Project.bin` 必须 `<= 52KB` |
| 超过 `52KB` 后怎么办 | 需要重做下载缓存规划，或改成外部 Flash / SD 卡升级 |

## 3. 当前工程已经做了什么

| 文件 | 改动 | 作用 |
|---|---|---|
| `USER/boot_app_config.h` | 定义 `BOOT_APP_START_ADDRESS = 0x0800D000` | 统一保存 App 起始地址，避免代码和 Keil 配置不一致 |
| `USER/boot_app_config.c` | 设置 `SCB->VTOR = 0x0800D000` | 让 SysTick、USART、DMA 等中断从 App 自己的向量表取入口 |
| `USER/boot_app_config.c` | 新增 `boot_app_handoff_init()` | 兼容 BootLoader 跳转前关闭全局中断的现场，避免 App 中断不工作 |
| `USER/App/scheduler.c` | `system_init()` 开头调用 `boot_app_handoff_init()` | 在 SysTick 和外设中断初始化前完成 App 向量表接管 |
| `MDK/2026706296.uvprojx` | IROM 改为 `0x0800D000 / 0x63000` | 让链接器把当前工程放到 App 区 |
| `MDK/output/Project.sct` | `LR_IROM1 0x0800D000 0x00063000` | 与 Keil IROM 配置保持一致 |
| `MDK/2026706296.uvprojx` | 打开 HEX 输出，并在构建后生成 `Project.bin` | `Project.bin` 用于 BootLoader 搬运写入 App 区，`Project.hex` 用于调试/烧录工具 |
| `USER/App/usart_app.c` | 瘦身为 `USART0 <-> RS485` 普通透明转发和日志输出 | `USART0` 不再解析 OTA 帧，避免普通通信和升级流量共口互扰 |
| `USER/App/uart_ota_app.c` | 新增 `USART2` START/DATA/END 分包 OTA、CRC 校验、下载缓存区写入和参数区写入 | App 每收到一个 OTA DATA 帧就写 `0x08073000`，最后写 `0x0800C000` 并复位交给 BootLoader 搬运 |
| `USER/Driver/bootloader_port.c/.h` | 新增 BootLoader 交接层封装 | 统一管理共享地址、CRC32、向量表校验、下载区擦写、参数区回写和软件复位 |
| `tools/make_uart_ota_packet.py` | 根据 `Project.bin` 生成旧 `.uota` 包，或通过 `--mode send` 按 ACK 分包发送 | 推荐使用 `--mode send --port COMx --baudrate 460800`，避免 App 侧占用 52KB 级 RAM 缓冲，并可观察发送进度 |

## 4. 官方 BootLoader 的实际工作流程

| 阶段 | 位置 | 关键行为 |
|---|---|---|
| 复位启动 | BootLoader 从 `0x08000000` 运行 | 初始化 SysTick 和串口 |
| 读取参数区 | `BOOT_CONFIG_ADDR = 0x0800C000` | 读取 `magicWord`、`updateStatus`、`updateFlag`、`appSize`、`appCRC32` |
| 判断是否升级 | `updateStatus == 0x01` 且 `updateFlag == 0x5A` | 进入固件搬运流程 |
| 搬运新固件 | 从 `0x08073000` 读，写入 `0x0800D000` | 把下载缓存区的新 App 复制到 App 区 |
| CRC 校验 | 对搬运数据计算 CRC32 | 与参数区 `appCRC32` 比较 |
| 更新参数 | 清除升级标志并记录升级次数 | 成功后复位 |
| 跳转 App | `iap_load_app(0x0800D000)` | 设置 MSP、VTOR，然后跳转到 App 复位入口 |

## 5. “下载”这件事要分清两种场景

| 场景 | 当前状态 | 说明 |
|---|---|---|
| 首次烧录 BootLoader + App | 当前工程已具备 App 镜像条件 | 先烧 BootLoader 到 `0x08000000`，再把当前 App 写到 `0x0800D000` |
| 运行中升级 App | 当前工程已支持 USART2 ACK 分包升级 | App 通过 `USART2` 接收 START/DATA/END 帧，边收边写 `0x08073000`，最后写参数区并复位交给 BootLoader 搬运 |

也就是说，官方 Two Stage 例程里，BootLoader 本身不是直接串口收文件的程序；串口接收升级包的逻辑属于 App 侧。本工程现在由 `USER/App/uart_ota_app.c` 负责 `USART2` 分包 OTA 接收，由 `USER/App/usart_app.c` 负责 `USART0` 普通透传。

## 6. 当前工程输出文件怎么用

| 输出文件 | 用途 |
|---|---|
| `MDK/output/Project.axf` | Keil 调试用 ELF/AXF 文件 |
| `MDK/output/Project.hex` | 带地址信息，可用于烧录工具直接写到 `0x0800D000` |
| `MDK/output/Project.bin` | 纯二进制 App 镜像，用于分包 OTA 发送，不要直接通过串口助手发送 |
| `MDK/output/Project.uota` | 旧完整包格式，仍可生成用于离线检查；低 RAM 流式 OTA 不再推荐直接发送该文件 |

如果通过当前 App 侧 `USART2` 在线升级，应该使用 `tools/make_uart_ota_packet.py --mode send --port COMx --baudrate 460800` 发送分包流，不要直接发送 `Project.bin`、`Project.hex` 或旧完整包。

## 6.1 生成并发送串口升级包

| 步骤 | 操作 | 说明 |
|---|---|---|
| 1 | 在 Keil 中重新编译当前 App 工程 | 构建后生成 `MDK/output/Project.bin` |
| 2 | 打开 PowerShell 并进入工程根目录 | 路径为 `D:\GD32\2026706296` |
| 3 | 执行 `python tools\make_uart_ota_packet.py --mode stream-info --version 0x00000006 --chunk-size 512` | 先检查当前 `Project.bin` 的大小、CRC 和分包数量；后续每次升级都要递增 `--version` |
| 4 | 执行 `python tools\make_uart_ota_packet.py --mode send --port COM29 --baudrate 460800 --version 0x00000006 --chunk-size 512` | 上位机通过 `USART2` 所在串口每发一帧等待 App ACK；`COM29` 按实际串口号替换 |
| 5 | 观察上位机依次打印 `send stream ...`、`START acked ...`、多行 `DATA acked ...` 和 `END acked ...` | 表示 START、DATA、END 三类帧都已收到 App 成功 ACK |
| 6 | 等待 App 打印 `OTA: ready, reset to BootLoader` | App 已写入下载缓存区和参数区，并准备复位 |
| 7 | 观察 BootLoader 打印 `app crc32 check pass` 和 `app update success` | 表示 BootLoader 已完成搬运并清除升级标志 |

当前串口观察口径要分清：

| 串口 | 引脚 | 你应该看到什么 |
|---|---|---|
| `USART0` | `PA9/PA10` | `BOOT: start`、`OTA: rx ...`、`OTA: ready, reset to BootLoader` 等日志 |
| `USART2` | `PD8/PD9` | 上电一次性探测串 `OTA2: ready`，以及 OTA 过程中返回给上位机的二进制 ACK |

注意：`PD8/PD9` **看不到** `BOOT: start` 属于正常现象，因为启动日志固定走 `USART0`。

命令示例：

```powershell
cd D:\GD32\2026706296
python tools\make_uart_ota_packet.py --mode stream-info --version 0x00000006 --chunk-size 512
python tools\make_uart_ota_packet.py --mode send --port COM29 --baudrate 460800 --version 0x00000006 --chunk-size 512
```

发送时的典型输出如下：

```text
send stream MDK\output\Project.bin: firmware=33536 bytes, crc=0xC0B85342, version=0x00000006, chunk_size=512, chunks=66, channel=USART2, port=COM29, baudrate=460800
START acked: chunk=0/66, frames=1/68, bytes=0/33536 (0%)
DATA acked: chunk=1/66, frames=2/68, bytes=512/33536 (1%)
...
DATA acked: chunk=66/66, frames=67/68, bytes=33536/33536 (100%)
END acked: chunk=66/66, frames=68/68, bytes=33536/33536 (100%)
sent stream frames=68, channel=USART2, port=COM29, baudrate=460800
```

如果只是想离线检查旧完整包格式，可以单独执行 `python tools\make_uart_ota_packet.py --mode packet --version 0x00000006` 生成 `Project.uota`；当前低 RAM 在线升级不要直接发送这个文件。

如果上位机在 `START` 阶段超时，可按下面顺序排查：

| 顺序 | 检查项 | 期望结果 |
|---|---|---|
| 1 | 只连 `PD8/PD9` 打开串口终端后重新上电 | 能看到一次 `OTA2: ready`，证明 `USART2` TX 和串口号基本正确 |
| 2 | 同时观察 `PA9/PA10` 的调试口 | 发送 `--mode send` 时应出现 `OTA: rx irq=... len=... magic=... type=...`，证明 `USART2` RX 已进 App |
| 3 | 若 `USART0` 有 `OTA: rx ...`，但上位机仍收不到 ACK | 优先查 `PD8` 发射链路、USB 转串口方向、共地和串口占用 |
| 4 | 若两边都没有任何 OTA 痕迹 | 优先查当前板子是否已烧入带 `USART2` OTA 的新 App、以及线是否真的接到 `PD8/PD9` |

当前流式 OTA 帧格式：

| 帧 | 固定字段 | 说明 |
|---|---|---|
| START | `0xA55A5AA5, type=1, appVersion, firmwareSize, firmwareCRC32, headerCRC32` | App 校验后擦除 `0x08073000` 下载缓存区 |
| DATA | `0xA55A5AA5, type=2, seq, offset, length, chunkCRC32, chunk` | App 校验序号、偏移和 CRC 后立即写入 Flash |
| END | `0xA55A5AA5, type=3, firmwareSize, firmwareCRC32` | App 读回校验下载缓存区，写参数区并复位 |
| ACK | `0xA55A5AA5, type=0x80|原帧类型, status, value0, value1` | 上位机收到成功 ACK 后才发送下一帧 |

## 7. 已经对本工程内 BootLoader 副本做的修改

| 文件 | 原始限制 | 当前处理 | 作用 |
|---|---:|---|---|
| `27_0_BootLoader\Function\Function.c` | `CONFIG_APP_SIZE 1024*20`，一次性读整个 App | 改为 `1024B` 分块搬运 | 避免当前 App 超过 20KB 后造成数组越界或截断 |
| `27_0_BootLoader\Function\Function.c` | 固定只擦除 App 开头 `3 * 4KB` | 改为按 `appSize` 计算擦除页数 | 当前 App 超过 12KB 时也能完整擦除后再写入 |
| `27_0_BootLoader\Function\Function.c` | 跳转合法性检查只粗略判断 SRAM 高位 | 改为检查 MSP 位于 `0x20000000~0x20030000`，入口位于 App 区且为 Thumb 地址 | 避免空镜像、错地址或损坏镜像被误跳转 |
| `27_0_BootLoader\project\CIMC_GD32_BootLoader.uvprojx` | Keil 工程界面 IROM 仍显示 `0x08000000 / 0x080000` | 改为 `0x08000000 / 0x00C000` | 与 BootLoader 实际 48KB 分区保持一致 |
| 下载缓存区 | `0x08073000` 起 52KB | 保留 52KB 上限检查 | 当前 App 约 30KB，可放入缓存区；后续 App bin 必须小于 52KB，除非重做下载缓存规划 |

## 8. 当前 BootLoader 搬运逻辑

| 步骤 | 当前实现 |
|---|---|
| 大小检查 | `appSize` 必须大于 0，且不能超过下载缓存区 `52KB` 和 App 区上限 |
| 地址检查 | 目标 App 地址固定要求为 `0x0800D000` |
| 擦除 App 区 | `erase_pages = (appSize + 4095) / 4096`，按 4KB 页擦除 |
| 分块复制 | 每次搬运 `1024B`，从 `0x08073000` 写到 `0x0800D000` |
| 写后校验 | 从正式 App 区重新读出数据并分块计算 CRC32 |
| 成功后清标志 | 清除 `updateStatus` 和 `updateFlag`，更新计数后软件复位 |

## 8.1 当前 App 是否还需要加官方 App 里的 BootLoader 内容

| 官方 App 内容 | 当前工程是否必须 | 原因 |
|---|---|---|
| `SCB->VTOR = 0x0800D000` 和重新开中断 | 必须 | 当前工程已经通过 `boot_app_handoff_init()` 实现 |
| `BootConfig.h/c` 参数结构 | 不单独引入官方文件 | 当前 App 已改为在 `bootloader_port.c/.h` 内维护与 BootLoader 参数区兼容的交接结构，避免协议散落在业务代码里 |
| 串口接收新固件、写 `0x08073000` 缓存区 | 已实现 | `uart_ota_app.c` 通过 `USART2` 分包接收 DATA 帧后立即写下载缓存区，并用 CRC32 校验写入结果 |
| 软件复位让 BootLoader 搬运 | 已实现 | App 写入参数区后复位，BootLoader 根据 `updateStatus=0x01`、`updateFlag=0x5A` 搬运 |

## 9. 当前工程接入后的启动注意事项

| 注意点 | 原因 |
|---|---|
| App 必须先设置 `SCB->VTOR` | 否则中断仍可能跳到 BootLoader 的向量表 |
| App 必须重新打开全局中断 | 官方 BootLoader 跳转前会 `__disable_irq()`，普通函数跳转不会自动恢复 PRIMASK |
| 不要把 App 链接回 `0x08000000` | `0x08000000` 必须留给 BootLoader |
| SWD 调试时不要整片擦除 | 整片擦除会把 BootLoader 和参数区一起擦掉 |
| 在线升级必须使用分包发送工具 | App 需要 START/DATA/END 帧和 ACK 节流；直接发送 `.bin` 或旧完整包不会触发当前低 RAM OTA 流程 |

## 10. 当前运行中升级流程

| 步骤 | 当前实现 |
|---|---|
| 1 | `tools/make_uart_ota_packet.py --mode send --baudrate 460800` 根据 `Project.bin` 生成 START/DATA/END 帧，并在每个 ACK 后打印进度 |
| 2 | App 的 `USART2` IDLE + DMA 每次接收一个 OTA 小帧，处理完成后返回 ACK；`USART0` 仅保留日志和普通透传 |
| 3 | App 校验 START 中的固件长度、固件 CRC32，并在首个 DATA 中校验 App 向量表 |
| 4 | App 每收到一个 DATA 帧就写入下载缓存区 `0x08073000`，最后 END 阶段读回计算 CRC32 |
| 5 | App END 校验通过后写入参数区 `0x0800C000`：`magicWord/updateStatus/updateFlag/appSize/appCRC32/appVersion` |
| 6 | App 软件复位，BootLoader 搬运缓存区固件到 `0x0800D000` |
| 7 | BootLoader 搬运后重新计算 CRC32，成功后清除 `updateStatus/updateFlag` 并跳转新 App |

限制说明：当前实现仍使用内部 Flash 下载缓存区，单个 `Project.bin` 必须不超过 `52KB`。如果后续 App 超过该大小，需要重新规划下载缓存区，或改成外部 Flash/SD 卡分包升级。
