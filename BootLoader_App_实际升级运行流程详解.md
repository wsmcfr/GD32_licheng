# BootLoader 与 App 实际升级运行流程详解

## 1. 先说结论

这套方案里，职责分工是固定的：

| 角色 | 负责什么 | 不负责什么 |
|---|---|---|
| 上位机工具 | 可用 Python 把 `Project.bin` 拆成 START / DATA / END 帧，也可用纸飞机调试助手先发 `YMODEM` 命令再通过 YModem 发送 `Project.bin` | 不直接改 MCU Flash |
| App | 接收 START/DATA/END 或 YModem 分包、写下载缓存区、写参数区、软件复位 | 不把新固件搬到正式 App 区 |
| BootLoader | 上电后读取参数区、把下载缓存区新固件搬到正式 App 区、CRC 校验、跳转新 App | 不负责接收整包 OTA 数据 |

一句话：

> **App 负责“准备好升级现场”，BootLoader 负责“执行最终切换”。**

第一版 YModem 接入只修改 App 侧接收流程，**BootLoader 端不需要修改**。BootLoader 仍然只看参数区里的 `updateFlag/updateStatus/appSize/appCRC32`，并从 `0x08067000` 下载缓存区搬运到 `0x0800D000` 正式 App 区。

---

## 2. Flash 分区关系

| 区域 | 地址 | 作用 | 谁会写 |
|---|---:|---|---|
| BootLoader 区 | `0x08000000 ~ 0x0800BFFF` | MCU 复位后先运行这里 | 烧录器 / BootLoader 工程 |
| 参数区 | `0x0800C000 ~ 0x0800CFFF` | 保存升级标志、大小、CRC、版本、App 地址 | App / BootLoader |
| 正式 App 区 | `0x0800D000 ~ 0x08066FFF` | 当前真正运行的 App | BootLoader |
| 下载缓存区 | `0x08067000 ~ 0x0807FFFF` | 暂存“下一版新固件” | App |

最关键的区别：

| 区域 | 含义 |
|---|---|
| `0x0800D000` | 当前要运行的正式 App |
| `0x08067000` | 新固件的临时缓存区，还不能直接运行 |

## 2.1 这些地址为什么这样分

这些地址不是芯片“唯一固定写法”，而是当前工程为了同时满足下面 4 个条件后，按 `4KB` 页对齐算出来的：

| 约束 | 说明 |
|---|---|
| MCU 复位入口固定 | Cortex-M 复位后一定先从 `0x08000000` 取向量表，所以 BootLoader 必须放在最前面 |
| 内部 Flash 按 `4KB` 页擦除 | 当前 App 和 BootLoader 都按 `4096` 字节页擦写，分区必须按页对齐 |
| 两阶段升级要保住旧 App | 升级时 App 只能先写“下载缓存区”，不能直接改正式 App 区 |
| App、参数区、缓存区要彼此隔离 | 这样擦除一个区域时，不会误伤另一个区域 |

如果把当前工程当成一片 `512KB`、共 `128` 页、每页 `4KB` 的内部 Flash 来看，整套分区正好可以拆成下面这样：

| 区域 | 起始地址 | 页数 | 大小 | 说明 |
|---|---:|---:|---:|---|
| BootLoader 区 | `0x08000000` | `12` 页 | `48KB` | 上电先运行 |
| 参数区 | `0x0800C000` | `1` 页 | `4KB` | 升级标志和共享参数 |
| 正式 App 区 | `0x0800D000` | `90` 页 | `360KB` (`0x5A000`) | 当前工程真正运行的 App |
| 下载缓存区 | `0x08067000` | `25` 页 | `100KB` | OTA 临时缓存 |

换算过程如下：

| 计算 | 结果 | 含义 |
|---|---:|---|
| `0x08000000 + 48KB` | `0x0800C000` | BootLoader 用完前 12 页后，下一页开始就是参数区 |
| `0x0800C000 + 4KB` | `0x0800D000` | 参数区独占 1 页后，下一页开始就是正式 App 区 |
| `0x0800D000 + 0x5A000` | `0x08067000` | 正式 App 区按当前链接配置结束在这里 |
| `0x08080000 - 100KB` | `0x08067000` | 从整片 Flash 末尾倒推 100KB，得到下载缓存区起点 |

可以把它理解成下面这张图：

```text
0x08000000
├─ BootLoader 区      48KB   = 12 页
0x0800C000
├─ 参数区             4KB    = 1 页
0x0800D000
├─ 正式 App 区        360KB  = 90 页
0x08067000
├─ 下载缓存区         100KB  = 25 页
0x08080000
```

要特别注意：

> 当前工程真正生效的正式 App 区大小是 `0x5A000 = 360KB`。
> 如果看到早期示意里写“76KB App 区”，那属于旧表述，不能作为当前工程实际配置使用。

## 2.2 Flash 和 RAM 在这套升级流程里分别干什么

当前工程默认是 **代码存放在内部 Flash，CPU 直接从 Flash 取指运行**，并不是上电后把整个程序复制到 RAM 再运行。

| 区域 | 地址范围示例 | 主要职责 |
|---|---:|---|
| 内部 Flash | `0x08000000` 起 | 保存 BootLoader、App、向量表、只读常量和固件镜像。 |
| SRAM/RAM | `0x20000000` 起 | 保存运行时变量、栈、堆、DMA 缓冲、OTA 分包缓冲和 CRC 中转缓冲。 |

在 OTA 搬运过程中，数据流是：

```text
下载缓存区 Flash 0x08067000
  -> RAM 中转缓冲 app_copy_buf[1024]
  -> 正式 App 区 Flash 0x0800D000
```

这里的 `app_copy_buf` 只是 1KB 临时缓冲。它的作用是减少 RAM 占用、简化 Flash 读写流程，不表示 App 会被搬到 RAM 里运行。  
BootLoader 搬运完成后，CPU 最终仍然跳到 `0x0800D000` 的 App Flash 区执行。

启动时 RAM 还会承担这些工作：

| RAM 用途 | 说明 |
|---|---|
| `.data` 段 | 已初始化全局变量的初始值从 Flash 复制到 RAM，之后变量在 RAM 中读写。 |
| `.bss` 段 | 未初始化全局变量和静态变量启动时在 RAM 清零。 |
| 栈 | 函数调用、局部变量和中断现场保存都依赖栈。 |
| 堆 | 动态内存分配使用。 |
| 外设缓冲 | USART/DMA/SPI 等外设通常直接读写 RAM 缓冲区。 |

一句话理解：

> Flash 像“掉电不丢的程序仓库”，RAM 像“运行时工作台”。代码通常在 Flash 里直接运行，RAM 主要放运行中会变化的数据。

## 2.3 参数区为什么正好是 4KB

很多人第一次看会误以为“升级标志就几个字节，为什么要浪费 4KB”。  
实际原因不是标志位本身大，而是 Flash 擦写规则决定参数区最好独占一整页。

| 原因 | 详细说明 |
|---|---|
| 擦除粒度就是 `4KB` | 代码里对参数区和 App 区都按 `4096` 字节页擦除，所以参数区天然应该按 1 页单独保留 |
| 参数区必须和 App 区隔离 | 如果参数区和 App 混在同一页里，升级擦 App 时就可能把参数一起擦掉 |
| 当前实现按整页读写 | BootLoader 会先把 `0x0800C000` 开始的 `4KB` 整块读到 RAM，修改后再整页擦除、整页写回 |
| 参数区不只放升级标志 | 当前规划里还包含主参数、备份参数、升级日志、用户配置、校准数据和预留扩展区 |

按当前文档规划，这 `4KB` 的内部结构是：

| 子区域 | 大小 | 用途 |
|---|---:|---|
| 主参数区 | `256B` | `magicWord`、`updateFlag`、`appSize`、`appCRC32` 等关键升级字段 |
| 备份参数区 | `256B` | 主参数的冗余备份 |
| 升级日志区 | `1024B` | 记录最近若干次升级结果 |
| 用户配置区 | `512B` | 串口等用户配置 |
| 校准数据区 | `512B` | 保留给校准数据 |
| 预留扩展区 | `1536B` | 给后续扩展留余量 |

所以“参数区是 4KB”真正的含义是：

> 这不是单纯为了放几个升级标志，  
> 而是为了做成一个可整页擦写、可长期扩展、不会误伤 App 的共享参数页。

### 2.3.1 为什么参数区结构体要用 `__attribute__((packed))`

参数区不是普通 RAM 变量，而是 App 和 BootLoader 共享的一段固定 Flash 二进制布局。  
代码里的 `typedef struct __attribute__((packed))` 可以拆开理解：

| 写法 | 含义 |
|---|---|
| `typedef` | 给结构体类型起别名，后续可以直接使用 `BootParam_t`、`Parameter_t`。 |
| `struct` | 定义结构体，把多个字段按顺序放进同一块内存布局。 |
| 结构体标签名 | 例如 `Parameter_SUM`，可通过 `struct Parameter_SUM` 使用。 |
| 类型别名 | 例如 `Parameter_t`，工程里更常用这种简短写法。 |
| `__attribute__((packed))` | 要求编译器按字节紧凑排列字段，不自动插入对齐填充字节。 |

为什么要紧凑排列？因为参数区字段有固定偏移：

| 字段 | 固定偏移 | 用途 |
|---|---:|---|
| `magicWord` | `[0-3]` | 判断参数区是否有效 |
| `updateFlag` | `[16]` | 判断是否有升级任务 |
| `updateStatus` | `[18]` | 判断是否进入搬运流程 |
| `appSize` | `[32-35]` | 记录新固件大小 |
| `appCRC32` | `[36-39]` | 记录新固件 CRC32 |

如果不加 `packed`，编译器可能为了对齐访问在字段之间插入隐藏填充字节。这样 App 侧写入的字段偏移和 BootLoader 侧读取的字段偏移可能不一致，升级标志、固件大小或 CRC 就会被读错。

因此这里使用 `packed` 的核心目的不是节省几个字节，而是保证：

> `0x0800C000` 参数区里的每个字段偏移固定，App 写什么位置，BootLoader 就按同一位置读。

## 2.4 为什么正式 App 区比下载缓存区大很多

这两个区域名字看起来都和 App 有关，但职责完全不同：

| 区域 | 本质角色 | 当前大小 |
|---|---|---:|
| 正式 App 区 | 设备最终运行程序的长期空间 | `360KB` |
| 下载缓存区 | 升级过程中的临时中转仓库 | `100KB` |

它们不一样大，是因为设计目标本来就不一样：

| 设计点 | 为什么这样做 |
|---|---|
| 正式 App 区要尽量大 | 这里决定“设备最多能运行多大的程序”，所以给了 `0x5A000` 的运行空间 |
| 下载缓存区只需满足当前 OTA 方案 | 当前是内部 Flash 两阶段升级，只给下载缓存区留了 `100KB` 作为临时仓库 |
| 缓存区放在 Flash 尾部 | 便于和正式 App 区隔开，升级时不先破坏旧 App |
| App 先写缓存区、BootLoader 再搬运 | 即使下载中途断电，`0x0800D000` 里的旧 App 仍然还在 |

这会带来一个很重要、但很容易忽略的现实限制：

| 问题 | 当前答案 |
|---|---|
| 这块板子最多能运行多大的 App？ | 按当前链接地址，正式运行区上限是 `360KB` |
| 当前这套串口 OTA 最多能升级多大的 App？ | 只能升级 `<= 100KB` 的 `Project.bin` |

也就是说：

> “能运行多大 App”和“当前 OTA 能升级多大 App”不是同一个问题。  
> 现在的瓶颈不在正式 App 区，而在下载缓存区只有 `100KB`。

如果以后 App 变成 `60KB`、`80KB`、`100KB`，会出现下面这种情况：

| 场景 | 是否可行 | 原因 |
|---|---|---|
| 用烧录器/SWD 直接写到正式 App 区运行 | 可以 | 正式 App 区本身足够大 |
| 继续走当前这套内部 Flash 串口 OTA | 只有 `Project.bin <= 100KB` 时可以 | 下载缓存区只有 `100KB`，超过后 START 阶段就会因超长被拒绝 |

如果将来确实要在线升级更大的 App，就必须重做下载缓存方案，例如：

| 方向 | 说明 |
|---|---|
| 重新切分内部 Flash | 压缩正式 App 区或其它保留区，给下载缓存区让更多空间 |
| 改用外部 Flash / SD 卡 | 把临时固件放到片外存储，再由 BootLoader 搬运 |
| 设计新的升级架构 | 例如双分区 A/B、分段下载校验等，但实现复杂度会更高 |

---

## 3. 整体时序

### 3.1 一次成功升级的完整链路

| 顺序 | 执行方 | 动作 | 结果 |
|---|---|---|---|
| 1 | 上位机 | 发送 START 帧，或用纸飞机调试助手 YModem 发送 `Project.bin` | 告诉 App：接下来要升级，并提供固件大小等信息 |
| 2 | App | 校验 START 或 YModem 首包，擦下载缓存区 | 为接收新固件腾出干净空间 |
| 3 | 上位机 | 逐帧发送 DATA，或由 YModem 发送 128B/1K 数据块 | 持续传输 `Project.bin` 正文 |
| 4 | App | 每收一帧就写到 `0x08067000 + offset` | 新固件逐步写入下载缓存区 |
| 5 | 上位机 | 发送 END 帧，或由 YModem 发送 EOT/结束空包 | 告诉 App：固件发送结束 |
| 6 | App | 校验整包 CRC、回读下载区 CRC、写参数区 | 告诉 BootLoader“新固件已准备好” |
| 7 | App | 软件复位 | 控制权交还 BootLoader |
| 8 | BootLoader | 启动后读取参数区 | 看到 `updateFlag=0x5A`、`updateStatus=0x01` |
| 9 | BootLoader | 把 `0x08067000` 搬到 `0x0800D000` | 新固件变成正式 App |
| 10 | BootLoader | 回读正式 App 区计算 CRC | 确认搬运结果正确 |
| 11 | BootLoader | 清升级标志并复位 | 避免下次上电重复搬运 |
| 12 | BootLoader | 再次启动后跳转 `0x0800D000` | 新 App 正式运行 |

---

## 4. App 端到底做了什么

App 侧 OTA 已拆成两个层次：

| 文件 | 作用 |
|---|---|
| [Function/uart_ota_app.c](D:/GD32/2026706296/Function/uart_ota_app.c:1) | 负责 RS485/USART1 OTA 总入口、旧 START/DATA/END 分包协议、ACK、会话状态和复位交接。 |
| [Function/uart_ota_ymodem.c](D:/GD32/2026706296/Function/uart_ota_ymodem.c:1) | 负责纸飞机调试助手等串口工具的 YModem 接收、CRC16 校验、数据块写入和参数区准备。 |
| [HardWare/BOOTLOADER/bootloader_port.c](D:/GD32/2026706296/HardWare/BOOTLOADER/bootloader_port.c:1) | 负责下载缓存区擦写、固件向量表校验、CRC32、参数区回写和软件复位。 |
| [User/gd32f4xx_it.c](D:/GD32/2026706296/User/gd32f4xx_it.c:223) | 负责 USART1 IDLE 中断，把 DMA 收到的一帧原始数据移交给 OTA 任务。 |

### 4.1 App 接收上位机的完整链路

当前 App 侧支持两种升级入口。第一种是旧 Python START/DATA/END 分包流，按“发送一帧、等待 ACK、再发下一帧”的方式节流；第二种是串口工具的 YModem，推荐现场先在纸飞机调试助手里向 RS485 口发送 `YMODEM` 文本命令，再用 `文件 -> 发送文件 -> YModem` 选择 `project/output/Project.bin`。

```text
上位机 make_uart_ota_packet.py
  -> RS485/USART1 发送 START / DATA / END
  -> USART1 硬件接收
  -> DMA 自动搬运到 usart1_rxbuffer
  -> USART1 IDLE 中断判定一帧结束
  -> 复制到 uart_ota_dma_buffer 并置 uart_ota_rx_flag
  -> uart_ota_task() 周期取帧
  -> 复制到 g_uart_ota_frame_buffer 私有快照
  -> 解析 magic + frame_type
  -> START 擦下载区 / DATA 写下载区 / END 写参数区并复位
```

YModem 路径是：

```text
纸飞机调试助手
  -> RS485 发送输入框发送一行 YMODEM
  -> App 打开约 30 秒 YModem 请求窗口
  -> App 在请求窗口内发送 'C' 请求 CRC 模式
  -> 文件 -> 发送文件 -> YModem
  -> 选择 project/output/Project.bin
  -> YModem 首包携带文件名和文件大小
  -> YModem 数据包携带 128B 或 1024B 固件数据 + CRC16
  -> App 边收边写 0x08067000，结束后回读下载区计算 CRC32
  -> 写参数区 updateFlag/updateStatus/appSize/appCRC32
  -> uart_ota_task() 延时并软件复位
```

| 阶段 | 关键代码 | 说明 |
|---|---|---|
| 初始化 USART1/RS485 | [bsp_usart.c:172](D:/GD32/2026706296/HardWare/USART/bsp_usart.c:172) | 配置 USART1、DMA 接收、RS485 方向脚、IDLE 中断。 |
| 中断接收一帧 | [gd32f4xx_it.c:223](D:/GD32/2026706296/User/gd32f4xx_it.c:223) | USART1 IDLE 后暂停 DMA，计算本帧长度，复制到 OTA 共享缓冲。 |
| 任务取帧 | [uart_ota_app.c:251](D:/GD32/2026706296/Function/uart_ota_app.c:251) | 在临界区复制共享缓冲到任务私有缓冲，并清接收标志。 |
| 协议分发 | [uart_ota_app.c:652](D:/GD32/2026706296/Function/uart_ota_app.c:652) | 先检查 `magic=0xA55A5AA5`，再按帧类型分发到 START/DATA/END。 |
| 周期任务 | [uart_ota_app.c:776](D:/GD32/2026706296/Function/uart_ota_app.c:776) | 每 5ms 左右处理一次 OTA 帧，成功 END 后延时并软件复位。 |

USART1 中断只做“搬运和置标志”，不会在中断里擦 Flash、写 Flash 或解析协议。这样做是为了避免 ISR 执行时间过长，影响后续串口接收。

### 4.2 共享变量和协议常量怎么理解

OTA 接收入口前半部分是中断和任务之间的共享状态：

| 变量 | 谁写 | 谁读 | 用途 |
|---|---|---|---|
| `uart_ota_rx_flag` | USART1 IDLE 中断 | `uart_ota_task()` | 标记是否已有一帧新数据可处理。 |
| `uart_ota_dma_length` | USART1 IDLE 中断 | `uart_ota_task()` | 本帧有效字节数。 |
| `uart_ota_dma_buffer` | USART1 IDLE 中断 | `uart_ota_task()` | 中断移交给 OTA 任务的原始帧数据。 |
| `uart_ota_irq_count` | USART1 IDLE 中断 | 日志诊断 | 统计收到多少次 USART1 IDLE 帧。 |
| `uart_ota_overwrite_count` | USART1 IDLE 中断 | 日志诊断 | 统计任务还没消费上一帧时又收到新帧的次数。 |
| `uart_ota_last_irq_length` | USART1 IDLE 中断 | 日志诊断 | 最近一次中断计算出的原始接收长度。 |

这些带 `__IO` 的变量可能被中断异步修改，等价于告诉编译器“每次都要真实读写内存，不能缓存旧值”。

协议常量必须与 [tools/make_uart_ota_packet.py](D:/GD32/2026706296/tools/make_uart_ota_packet.py:32) 保持一致：

| 宏 | 值 | 含义 |
|---|---:|---|
| `UART_OTA_STREAM_MAGIC` | `0xA55A5AA5` | OTA 流式协议魔术字，用来识别这是一帧 OTA 数据。 |
| `UART_OTA_FRAME_START` | `1` | START 帧，表示一次升级会话开始。 |
| `UART_OTA_FRAME_DATA` | `2` | DATA 帧，携带固件分包数据。 |
| `UART_OTA_FRAME_END` | `3` | END 帧，表示固件发送结束。 |
| `UART_OTA_FRAME_ACK_BASE` | `0x80` | ACK 类型基值，ACK 类型为 `0x80 | 原帧类型`。 |
| `UART_OTA_START_FRAME_SIZE` | `24` | START 帧固定长度。 |
| `UART_OTA_DATA_HEADER_SIZE` | `24` | DATA 帧头长度，后面追加 chunk 数据。 |
| `UART_OTA_END_FRAME_SIZE` | `16` | END 帧固定长度。 |
| `UART_OTA_ACK_FRAME_SIZE` | `20` | App 回给上位机的 ACK 固定长度。 |
| `UART_OTA_STREAM_CHUNK_SIZE` | `512` | 单个 DATA 帧最多携带 512 字节固件数据。 |

YModem 相关常量如下：

| 常量 | 值 | 含义 |
|---|---:|---|
| `UART_OTA_YMODEM_SOH` | `0x01` | 128 字节数据块帧头。 |
| `UART_OTA_YMODEM_STX` | `0x02` | 1024 字节数据块帧头。 |
| `UART_OTA_YMODEM_EOT` | `0x04` | 文件正文结束标志。 |
| `UART_OTA_YMODEM_CRC_REQ` | `'C'` / `0x43` | App 收到 `YMODEM` 启动命令后才会在有限窗口内发送，要求上位机用 CRC 模式发送。 |
| `BSP_USART1_RX_BUFFER_SIZE` | `1152` | 必须容纳 YModem 1K 完整帧：`1 + 1 + 1 + 1024 + 2 = 1029` 字节。 |

纸飞机调试助手操作方式：

| 步骤 | 操作 |
|---|---|
| 1 | 打开连接到 `RS485/USART1` 的串口，波特率 `460800`，8N1。 |
| 2 | 重新上电或复位板子，确认 RS485 口能看到一次 `OTA485: ready`；没有升级命令时不会连续刷 C。 |
| 3 | 在 RS485 发送输入框发送一行 `YMODEM`，触发 App 发送 `C` 等待文件。 |
| 4 | 选择 `文件 -> 发送文件 -> YModem`。 |
| 5 | 选择 `project/output/Project.bin`，不要选 `.hex` 或旧 `.uota`。 |
| 6 | 等待发送完成，随后观察 USART0 日志里的 `YMODEM: ready ...` 和 BootLoader 搬运日志。 |

### 4.2.1 协议分发入口：为什么按 `frame_type` 进入三个函数

USART1 IDLE 中断交给 App 的只是一段原始字节，`uart_ota_task()` 并不知道这段字节到底是开始帧、数据帧还是结束帧。真正的协议识别发生在 `prv_uart_ota_try_process_packet()` 里：

```c
frame_type = prv_uart_ota_read_u32_le(&packet[4]);
if(UART_OTA_FRAME_START == frame_type){
    return prv_uart_ota_process_start(packet, packet_length);
}
if(UART_OTA_FRAME_DATA == frame_type){
    return prv_uart_ota_process_data(packet, packet_length);
}
if(UART_OTA_FRAME_END == frame_type){
    return prv_uart_ota_process_end(packet, packet_length);
}
```

这段代码的含义可以按下面的表理解：

| 字节偏移 | 读取内容 | 说明 |
|---:|---|---|
| `packet[0..3]` | `magic` | 必须等于 `0xA55A5AA5`，用于确认这是一帧 OTA 数据。 |
| `packet[4..7]` | `frame_type` | 决定这一帧交给 START、DATA 还是 END 处理函数。 |
| `packet[8..]` | 具体帧参数 | 不同帧类型从这里开始有不同含义。 |

分发关系如下：

| `frame_type` 值 | 宏 | 进入函数 | 当前帧职责 |
|---:|---|---|---|
| `1` | `UART_OTA_FRAME_START` | `prv_uart_ota_process_start()` | 建立一次 OTA 会话，校验固件大小和整包 CRC 元信息，擦除下载缓存区。 |
| `2` | `UART_OTA_FRAME_DATA` | `prv_uart_ota_process_data()` | 接收一小段固件数据，校验单包 CRC，按偏移写入下载缓存区。 |
| `3` | `UART_OTA_FRAME_END` | `prv_uart_ota_process_end()` | 确认所有 DATA 已收完，校验整包 CRC，写入升级参数区并准备复位。 |

这三个函数不是互相独立随便调用的，它们必须按下面顺序工作：

```text
START 成功
  -> g_uart_ota_session.state = UART_OTA_SESSION_RECEIVING
  -> 清空接收进度，保存 firmware_size / firmware_crc32 / app_version
  -> 回 START ACK

DATA 第 0 包成功
  -> 校验 App 向量表
  -> 写入 0x08067000 + 0
  -> received_size 增加，next_seq 加 1
  -> 回 DATA ACK

DATA 第 1 包、第 2 包、... 成功
  -> 按 seq 和 offset 连续写入下载缓存区
  -> 持续更新 running_crc 和 received_size
  -> 每包都回 DATA ACK

END 成功
  -> 确认 received_size 等于 firmware_size
  -> 确认 running_crc 等于 START 中声明的 firmware_crc32
  -> 回读下载缓存区再算 CRC
  -> 写参数区 updateFlag/updateStatus/appSize/appCRC32
  -> 回 END ACK
  -> uart_ota_task() 延时 50ms 后复位
```

一句话理解：

> `frame_type` 是 OTA 协议的“路由字段”。App 先用 `magic` 判断是不是 OTA 包，再用 `frame_type` 判断这包应该执行“开始升级”“写一段固件”还是“结束并提交升级”。

### 4.3 START 阶段

入口函数：`prv_uart_ota_process_start()`  
位置：[uart_ota_app.c:418](D:/GD32/2026706296/Function/uart_ota_app.c:418)

它做的事：

| 步骤 | 动作 | 为什么这样做 |
|---|---|---|
| 1 | 校验 START 帧长度 | 防止协议格式错乱 |
| 2 | 读取 `appVersion`、`firmwareSize`、`firmwareCRC32` | 保存本次升级元数据 |
| 3 | 校验 START 头部 CRC | 防止包头被破坏 |
| 4 | 检查固件大小是否超过 `100KB` | 下载缓存区上限就是 `100KB` |
| 5 | 擦除下载缓存区 | 准备接收新固件 |
| 6 | 初始化 OTA 会话状态 | 后续 DATA 要按顺序写入 |
| 7 | 回 ACK | 告诉上位机可以发下一帧 |

关键点：

> START 阶段只动下载缓存区，不动正式 App 区。  
> 所以升级过程中断电，不会把当前正在运行的 App 先破坏掉。

#### 4.3.1 START 帧字段和 App 解析关系

START 帧固定 24 字节，用来描述“这次要升级的固件整体信息”。它不携带固件正文。

| 偏移 | 长度 | 字段 | App 侧读取方式 | 作用 |
|---:|---:|---|---|---|
| `0` | 4 | `magic` | 外层分发函数读取 | 识别 OTA 协议帧。 |
| `4` | 4 | `frame_type` | 外层分发函数读取 | 值为 `1`，进入 START 处理函数。 |
| `8` | 4 | `app_version` | `prv_uart_ota_read_u32_le(&frame[8])` | 本次新固件版本号。 |
| `12` | 4 | `firmware_size` | `prv_uart_ota_read_u32_le(&frame[12])` | 完整 `Project.bin` 字节数。 |
| `16` | 4 | `firmware_crc32` | `prv_uart_ota_read_u32_le(&frame[16])` | 完整固件 CRC32，END 阶段还会再次使用。 |
| `20` | 4 | `header_crc32` | `prv_uart_ota_read_u32_le(&frame[20])` | 对前 20 字节计算的 CRC32，用来检查 START 头是否损坏。 |

START 成功以后，App 会初始化 `g_uart_ota_session`：

| 会话字段 | START 阶段写入的值 | 后续用途 |
|---|---|---|
| `state` | `UART_OTA_SESSION_RECEIVING` | 允许后续 DATA/END 进入处理流程。 |
| `app_version` | START 帧里的版本号 | END 成功后写入参数区。 |
| `firmware_size` | START 帧里的固件大小 | DATA 阶段防越界，END 阶段确认是否收满。 |
| `firmware_crc32` | START 帧里的整包 CRC | END 阶段做整包校验。 |
| `received_size` | 复位为 `0` | DATA 阶段累计已接收字节数。 |
| `next_seq` | 复位为 `0` | DATA 阶段要求第一包必须是 `seq=0`。 |
| `running_crc` | 复位为 CRC 初始值 | DATA 阶段边收边累计整包 CRC。 |

START ACK 的含义：

| ACK 字段 | 成功时的值 | 含义 |
|---|---|---|
| ACK type | `0x80 | UART_OTA_FRAME_START = 0x81` | 这是对 START 的应答。 |
| `status` | `0` | START 处理成功。 |
| `value0` | `UART_OTA_STREAM_CHUNK_SIZE = 512` | 告诉上位机 DATA 单包最大 512 字节。 |
| `value1` | `firmware_size` | 告诉上位机 App 认可的固件总大小。 |

### 4.4 DATA 阶段

入口函数：`prv_uart_ota_process_data()`  
位置：[uart_ota_app.c:489](D:/GD32/2026706296/Function/uart_ota_app.c:489)

它做的事：

| 步骤 | 动作 | 为什么这样做 |
|---|---|---|
| 1 | 校验帧长度 | 防止 chunk 被截断或粘包 |
| 2 | 校验 `seq`、`offset`、`length` | 防止乱序、重发、越界 |
| 3 | 校验当前 chunk 的 CRC32 | 防止单帧传输出错 |
| 4 | 首帧时校验向量表 | 提前拦截“这不是可启动 App”的坏文件 |
| 5 | 立即写 `0x08067000 + offset` | 不占用大 RAM，支持低内存 OTA |
| 6 | 更新累计 CRC 和接收进度 | 为 END 阶段做整包确认 |
| 7 | 回 ACK | 告诉上位机下一帧可以继续发 |

关键点：

> App 收到 DATA 后不是先拼在 RAM 里，而是**边收边写下载缓存区**。  
> 这样 App 不需要一个 30KB、40KB、50KB 的大缓冲区。

#### 4.4.1 DATA 帧字段和 App 解析关系

DATA 帧由 `24 字节头 + 当前 chunk 固件数据` 组成。上位机把 `Project.bin` 按 512 字节一包拆开，每一包都封装成一个 DATA 帧。

| 偏移 | 长度 | 字段 | App 侧读取方式 | 作用 |
|---:|---:|---|---|---|
| `0` | 4 | `magic` | 外层分发函数读取 | 识别 OTA 协议帧。 |
| `4` | 4 | `frame_type` | 外层分发函数读取 | 值为 `2`，进入 DATA 处理函数。 |
| `8` | 4 | `seq` | `prv_uart_ota_read_u32_le(&frame[8])` | 当前 DATA 包序号，从 `0` 开始递增。 |
| `12` | 4 | `offset` | `prv_uart_ota_read_u32_le(&frame[12])` | 当前 chunk 在完整固件中的偏移。 |
| `16` | 4 | `chunk_length` | `prv_uart_ota_read_u32_le(&frame[16])` | 当前 chunk 的字节数，最大 512。 |
| `20` | 4 | `chunk_crc32` | `prv_uart_ota_read_u32_le(&frame[20])` | 当前 chunk 自己的 CRC32。 |
| `24` | N | `chunk` | `chunk = &frame[24]` | 真正要写入下载缓存区的固件数据。 |

例如固件大小是 `1300` 字节，上位机会拆成：

| DATA 包 | `seq` | `offset` | `chunk_length` | 数据范围 | 写入地址 |
|---:|---:|---:|---:|---|---|
| 第 0 包 | `0` | `0` | `512` | `Project.bin[0..511]` | `0x08067000 + 0` |
| 第 1 包 | `1` | `512` | `512` | `Project.bin[512..1023]` | `0x08067000 + 512` |
| 第 2 包 | `2` | `1024` | `276` | `Project.bin[1024..1299]` | `0x08067000 + 1024` |

DATA 阶段最核心的三类检查如下：

| 检查类别 | 代码条件 | 解决的问题 |
|---|---|---|
| 会话状态检查 | `state == UART_OTA_SESSION_RECEIVING` | 防止没收到 START 就直接写 DATA。 |
| 长度检查 | `chunk_length > 0`、`chunk_length <= 512`、`frame_length == 24 + chunk_length` | 防止空包、超大包、短包或粘包。 |
| 连续性检查 | `seq == next_seq`、`offset == received_size`、`offset + chunk_length <= firmware_size` | 防止乱序、丢包、重复包和越界写。 |
| 单包 CRC 检查 | `bootloader_port_crc32_calc(chunk, chunk_length) == chunk_crc32` | 防止当前这一包在链路上传坏。 |
| 首包向量表检查 | 只在 `vector_checked == 0` 时执行 | 防止把一个不是合法 App 镜像的 bin 写进下载区。 |

首包要额外校验向量表，是因为 App 镜像开头就是 Cortex-M 向量表：

| bin 偏移 | 内容 | 为什么要检查 |
|---:|---|---|
| `0x00` | 初始 MSP 栈顶地址 | 必须落在 SRAM 范围，否则 App 启动后栈就错了。 |
| `0x04` | Reset_Handler 入口地址 | 必须落在正式 App Flash 区，并且最低位为 1，表示 Thumb 状态。 |

DATA 成功写入后会更新会话进度：

| 字段 | 更新方式 | 作用 |
|---|---|---|
| `running_crc` | 把当前 chunk 加入累计 CRC | END 阶段确认整包数据是否正确。 |
| `received_size` | `received_size += chunk_length` | 记录已经成功写入下载区的字节数。 |
| `next_seq` | `next_seq++` | 要求下一包必须是下一个序号。 |
| `vector_checked` | 首包成功后置 `1` | 后续包不再重复检查向量表。 |

DATA ACK 的含义：

| ACK 字段 | 成功时的值 | 含义 |
|---|---|---|
| ACK type | `0x80 | UART_OTA_FRAME_DATA = 0x82` | 这是对 DATA 的应答。 |
| `status` | `0` | 当前 DATA 包处理成功。 |
| `value0` | `seq` | 告诉上位机哪一包已经成功。 |
| `value1` | `received_size` | 告诉上位机当前 App 已成功接收多少字节。 |

### 4.5 END 阶段

入口函数：`prv_uart_ota_process_end()`  
位置：[uart_ota_app.c:580](D:/GD32/2026706296/Function/uart_ota_app.c:580)

它做的事：

| 步骤 | 动作 | 为什么这样做 |
|---|---|---|
| 1 | 检查 END 帧长度 | 保证协议完整 |
| 2 | 检查 END 帧里的 size/CRC 与 START 是否一致 | 防止前后不一致 |
| 3 | 检查累计接收大小是否等于固件总大小 | 防止少收 |
| 4 | 用会话累计值确认整包 CRC | 证明接收逻辑正确 |
| 5 | 从下载缓存区回读整段再算 CRC | 证明写进 Flash 的内容也正确 |
| 6 | 写参数区 | 通知 BootLoader“可以搬运了” |
| 7 | 回 END ACK | 告诉上位机 OTA 已准备完成 |
| 8 | 返回成功 | 外层 `uart_ota_task()` 会触发软件复位 |

关键点：

> App 到这里仍然**没有把新固件搬到 `0x0800D000`**。  
> 它只是确认下载缓存区里的新固件是可靠的，然后把升级条件写到参数区。

#### 4.5.1 END 帧字段和 App 解析关系

END 帧固定 16 字节，用来告诉 App：“上位机已经把固件数据发完了，请做最终确认。”

| 偏移 | 长度 | 字段 | App 侧读取方式 | 作用 |
|---:|---:|---|---|---|
| `0` | 4 | `magic` | 外层分发函数读取 | 识别 OTA 协议帧。 |
| `4` | 4 | `frame_type` | 外层分发函数读取 | 值为 `3`，进入 END 处理函数。 |
| `8` | 4 | `firmware_size` | `prv_uart_ota_read_u32_le(&frame[8])` | 上位机再次声明固件总大小。 |
| `12` | 4 | `firmware_crc32` | `prv_uart_ota_read_u32_le(&frame[12])` | 上位机再次声明整包 CRC32。 |

END 阶段会做两层 CRC 校验：

| 校验层级 | 数据来源 | 目的 |
|---|---|---|
| 接收过程累计 CRC | `g_uart_ota_session.running_crc` | 确认所有 DATA chunk 按顺序拼起来后，内容等于 START 声明的整包 CRC。 |
| 下载区回读 CRC | `bootloader_port_calc_download_crc32(firmware_size)` | 确认已经写入内部 Flash 下载缓存区的内容也正确，不只是 RAM 中收到过正确数据。 |

END 成功后写参数区的意义如下：

| 参数区字段 | 写入值 | BootLoader 后续怎么用 |
|---|---|---|
| `updateFlag` | `0x5A` | 表示有升级任务。 |
| `updateMode` | `0x01` | 表示当前是 App 下载完成后交给 BootLoader 搬运的模式。 |
| `updateStatus` | `0x01` | 表示新固件已经在下载缓存区准备好。 |
| `appSize` | `firmware_size` | BootLoader 搬运和 CRC 计算时使用。 |
| `appCRC32` | `firmware_crc32` | BootLoader 搬运后回读正式 App 区时使用。 |
| `appVersion` | `app_version` | 记录本次升级版本。 |

END ACK 的含义：

| ACK 字段 | 成功时的值 | 含义 |
|---|---|---|
| ACK type | `0x80 | UART_OTA_FRAME_END = 0x83` | 这是对 END 的应答。 |
| `status` | `0` | 整个 OTA 接收和参数区写入成功。 |
| `value0` | `firmware_size` | 告诉上位机最终确认的固件大小。 |
| `value1` | `firmware_crc32` | 告诉上位机最终确认的整包 CRC。 |

`prv_uart_ota_process_end()` 返回 `UART_OTA_RESULT_SUCCESS` 后，外层 `uart_ota_task()` 会延时 `50ms` 再调用 `bootloader_port_request_upgrade_reset()`。这个短延时是为了让 END ACK 和日志先发出去，再复位进入 BootLoader。

### 4.6 OTA 结果枚举怎么理解

`uart_ota_result_t` 是 OTA 帧处理函数的返回结果，告诉 `uart_ota_task()` 当前这一帧处理到什么状态了。

| 枚举值 | 含义 | 典型场景 |
|---|---|---|
| `UART_OTA_RESULT_NOT_PACKET` | 这帧不是 OTA 包。 | `magic` 不是 `0xA55A5AA5`，USART1 当前作为 OTA 专用口会直接忽略。 |
| `UART_OTA_RESULT_SUCCESS` | 整个 OTA 接收成功。 | END 帧处理完成，下载区 CRC 正确，参数区写入成功，准备复位交给 BootLoader。 |
| `UART_OTA_RESULT_BAD_LENGTH` | 帧长度、序号、偏移或边界不符合协议。 | START 不是 24 字节、END 不是 16 字节、DATA 长度不对、`seq/offset` 不连续。 |
| `UART_OTA_RESULT_BAD_VECTOR` | 固件向量表非法。 | 首个 DATA 分包里的 MSP 或 Reset_Handler 不像合法 App。 |
| `UART_OTA_RESULT_FLASH_ERROR` | Flash 操作失败。 | 擦下载区失败、写下载区失败、写参数区失败。 |
| `UART_OTA_RESULT_VERIFY_ERROR` | CRC 校验失败。 | START 头 CRC、DATA 分包 CRC、整包 CRC 或下载区回读 CRC 不一致。 |
| `UART_OTA_RESULT_WAIT_MORE` | 当前数据像 OTA 前缀，但还不完整。 | 收到的字节太短，只能等待下一轮。 |
| `UART_OTA_RESULT_FRAME_CONSUMED` | 当前帧已经成功消费，但完整 OTA 还没结束。 | START 成功或某个 DATA 成功，继续等下一帧。 |

最重要的区别是：

| 返回值 | 下一步 |
|---|---|
| `UART_OTA_RESULT_FRAME_CONSUMED` | 当前帧成功，继续等后续 DATA 或 END。 |
| `UART_OTA_RESULT_SUCCESS` | END 也成功，完整升级包已准备好，延时 50ms 后软件复位。 |

---

## 5. App 端写参数区到底写了什么

关键函数：`bootloader_port_write_upgrade_info()`  
位置：[bootloader_port.c:461](D:/GD32/2026706296/HardWare/BOOTLOADER/bootloader_port.c:461)

它先把整个 `0x0800C000` 开始的 4KB 参数区读到 RAM，然后只改升级相关字段，再整块回写。

### 5.1 为什么要先整块读出来

因为参数区不只有升级字段：

| 区域 | 内容 |
|---|---|
| 主参数区 | 升级标志、版本、CRC、地址 |
| 预留参数区 | 兼容布局 |
| 用户配置区 | 串口等参数 |
| 校准区 | 工厂校准数据 |

如果 App 只写前面几十个字节、却把剩余部分擦掉，校准和配置就丢了。  
所以必须走“**整块读 -> 改字段 -> 擦页 -> 整块写回**”。

### 5.2 关键字段

| 字段 | App 写入值 | BootLoader 如何使用 |
|---|---|---|
| `magicWord` | `0x5AA5C33C` | 判断参数区有效 |
| `updateFlag` | `0x5A` | 判断存在待升级任务 |
| `updateStatus` | `0x01` | 判断现在应该搬运 |
| `appSize` | 新固件大小 | BootLoader 决定擦多少页、搬多少字节 |
| `appCRC32` | 新固件 CRC | BootLoader 搬运后对正式 App 区做比较 |
| `appVersion` | 新版本号 | 记录版本信息 |
| `appStartAddr` | `0x0800D000` | 正式 App 目标地址 |
| `appStackAddr` | 取自当前 App 区向量表第 0 项 | 供 BootLoader 记录/跳转参考 |
| `appEntryAddr` | 取自当前 App 区向量表第 1 项 | 供 BootLoader 记录/跳转参考 |

注意一个常见误解：

> `appStartAddr = 0x0800D000` 不表示 App 已经把新固件写到这里。  
> 它只是告诉 BootLoader：**搬运完成后，你要把新固件放到这里运行。**

---

## 6. App 为什么要复位，而不是自己直接跳新 App

App 在 [uart_ota_app.c:818](D:/GD32/2026706296/Function/uart_ota_app.c:818) 收到 `UART_OTA_RESULT_SUCCESS` 后，会延时 50ms，再调用 [bootloader_port_request_upgrade_reset](D:/GD32/2026706296/HardWare/BOOTLOADER/bootloader_port.c:535) 触发软件复位。

原因有三个：

| 原因 | 说明 |
|---|---|
| 1 | App 自己没有把新固件搬到正式 App 区，必须让 BootLoader 上电后接手 |
| 2 | BootLoader 负责最终校验和正式切换，职责更单一、更安全 |
| 3 | 复位后能走最标准的启动路径，避免旧 App 的中断、DMA、串口现场污染新 App |

---

## 7. BootLoader 端到底做了什么

BootLoader 核心文件已经迁移到 [D:\GD32\2026706296_bootloader\Function\Function.c](D:/GD32/2026706296_bootloader/Function/Function.c:120)。

### 7.1 读取参数区并决定是否搬运

入口函数：`UsrFunction()`  
位置：[Function.c:120](D:/GD32/2026706296_bootloader/Function/Function.c:120)

它会做：

| 步骤 | 动作 |
|---|---|
| 1 | 读取 `0x0800C000` 整个参数区 |
| 2 | 检查 `magicWord` |
| 3 | 检查 `updateFlag == 0x5A` |
| 4 | 检查 `updateStatus == 0x01` |

如果三个条件满足，就说明：

> App 已经把新固件放进下载缓存区了，BootLoader 现在该搬运了。

### 7.2 真正的搬运函数

函数：`Download_Transport()`  
位置：[Function.c:376](D:/GD32/2026706296_bootloader/Function/Function.c:376)

它做的事：

| 步骤 | 动作 | 为什么这样做 |
|---|---|---|
| 1 | 检查 `appSize` 和 `appStartAddr` | 防止越界覆盖 BootLoader / 参数区 |
| 2 | 按 `appSize` 计算要擦除多少页 | App 大小可变，不能写死擦 3 页 |
| 3 | 擦除 `0x0800D000` 正式 App 区 | 为新固件腾空间 |
| 4 | 从 `0x08067000` 分块读 | 从下载缓存区取新固件 |
| 5 | 分块写到 `0x0800D000` | 变成正式 App 镜像 |
| 6 | 从 `0x0800D000` 回读再算 CRC | 证明搬运结果正确 |

这一步才是“真正搬运”。

---

## 8. BootLoader 怎么运行新 App

### 8.1 搬运成功后先清标志再复位

BootLoader 成功后会把：

| 字段 | 处理 |
|---|---|
| `updateFlag` | 清 0 |
| `updateStatus` | 清 0 |
| `updateCount` | 加 1 |

然后软件复位。

这样下次启动时，BootLoader 就知道：

> 升级已经完成，不要再重复搬运同一份缓存区固件。

### 8.2 第二次启动时跳转新 App

函数：`jump_to_app()`  
位置：[Function.c:635](D:/GD32/2026706296_bootloader/Function/Function.c:635)

它会：

| 步骤 | 动作 |
|---|---|
| 1 | 读取 `0x0800D000` 向量表第 0 项和第 1 项 |
| 2 | 检查栈顶地址是否合法 |
| 3 | 检查 Reset_Handler 入口是否合法 |
| 4 | 调 `iap_load_app(0x0800D000)` |

### 8.3 `iap_load_app()` 的真正跳转动作

函数：`iap_load_app()`  
位置：[Function.c:554](D:/GD32/2026706296_bootloader/Function/Function.c:554)

它不是简单函数跳转，而是完整切换运行现场：

| 动作 | 目的 |
|---|---|
| 关中断 | 防止 BootLoader 中断打到 App |
| 关 SysTick | 防止节拍中断沿用旧现场 |
| 清 NVIC 使能和挂起位 | 给 App 一个干净中断环境 |
| `SCB->VTOR = 0x0800D000` | 改用 App 的向量表 |
| `__set_MSP(app_stack_addr)` | 切到 App 自己的栈 |
| 跳到 App Reset_Handler | 真正开始运行新 App |

把最后几句代码展开看，就是：

```c
SCB->VTOR = 0x0800D000;
jump2app = (pFunction)entry_addr;
__set_MSP(stack_addr);
jump2app();
```

| 代码 | 含义 | 如果漏掉会怎样 |
|---|---|---|
| `SCB->VTOR = 0x0800D000;` | 告诉 Cortex-M 内核：后续异常和外设中断都从 App 向量表取入口。 | App 的 SysTick、USART、DMA、HardFault 等中断可能仍跳到 BootLoader 的中断函数。 |
| `jump2app = (pFunction)entry_addr;` | 把 App 向量表第 1 项，也就是 Reset_Handler 地址，转换成可调用的函数指针。 | 只有一个裸地址值，不能直接按 C 函数方式调用。 |
| `__set_MSP(stack_addr);` | 把 CPU 主栈指针切换成 App 向量表第 0 项记录的初始栈顶。 | App 会沿用 BootLoader 的栈，函数调用和中断入栈都可能破坏现场。 |
| `jump2app();` | 真正跳进 App 的 Reset_Handler。 | 不执行这句就只是准备好了现场，执行权还没有交给 App。 |

可以把这理解成 BootLoader 手动模拟一次“从 App 向量表启动”：

```text
App 向量表第 0 项 -> 初始 MSP -> __set_MSP(stack_addr)
App 向量表第 1 项 -> Reset_Handler -> jump2app()
App 向量表基址    -> 中断入口表 -> SCB->VTOR
```

---

## 9. 新 App 启动后为什么还要再做一次接管

App 侧还有两个关键文件：

| 文件 | 作用 |
|---|---|
| [User/boot_app_config.c](D:/GD32/2026706296/User/boot_app_config.c:1) | 接管 BootLoader 跳转后的中断/向量表现场 |
| [Function/scheduler.c](D:/GD32/2026706296/Function/scheduler.c:68) | 在系统初始化最前面调用接管函数 |

`boot_app_handoff_init()` 会：

| 动作 | 原因 |
|---|---|
| 再次设置 `SCB->VTOR = 0x0800D000` | 兼容直接调试 App、异常重进 App 等场景 |
| 清 NVIC 挂起位 | 防止旧现场残留中断 |
| 清 `PENDST` | 防止 SysTick 挂起残留 |
| `__enable_irq()` | BootLoader 跳转前会关总中断，App 必须自己打开 |

所以完整理解应该是：

> BootLoader 负责把执行流切到新 App，  
> 新 App 自己再把“中断和向量表主权”完全接回去。

### 9.1 `jump app` 后脱机卡死但调试能继续的原因

如果日志已经打印：

```text
BootLoader : jump app vtor:0x0800d000 msp:0x20005818 entry:0x0800d379
```

这说明 BootLoader 已经读到了合法的 App 向量表，并准备跳转到 App 的 Reset_Handler。此时脱机无后续 App 日志，但 Keil 调试模式点继续几次后可以运行，常见原因不是 BootLoader 没跳，而是 App 在 C 库初始化阶段进入了 ARM semihosting。

| 证据 | 含义 |
|---|---|
| 反汇编停在 `BKPT 0xAB` | 这是 Arm C 库 semihosting 请求调试器服务的断点指令 |
| 调用栈出现 `_sys_open -> freopen -> __rt_lib_init` | App 尚未进入 `main()`，C 库正在初始化标准流 |
| 脱机运行卡死，调试器连接后点继续可跑 | 调试器接管或吞掉了 semihosting 断点，所以现象与脱机不同 |

当前 App 的修复方式是在 `User/main.c` 中声明 `__use_no_semihosting`，并提供 `_sys_open()`、`_sys_write()`、`_sys_read()`、`_sys_exit()`、`_ttywrch()`、`fputc()` 等 retarget 桩函数。这样 C 库启动阶段只打开固件内部的标准流，不再访问调试器主机文件系统。

| 验证项 | 正确结果 |
|---|---|
| Keil 编译 | `project/output/Project.build_log.htm` 显示 `0 Error(s)` |
| map 符号 | `project/Listings/Project.map` 中 `_sys_open/_sys_write/_sys_exit/_ttywrch` 来自 `main.o` |
| 脱机复位 | `jump app` 后继续打印 `BOOT: handoff start`、`BOOT: start` 和后续外设初始化日志 |

---

## 10. 最容易混淆的几个问题

| 问题 | 正确答案 |
|---|---|
| App 有没有把新固件写到 `0x0800D000`？ | 没有。App 只写 `0x08067000` 下载缓存区。 |
| 谁真正负责搬运到正式 App 区？ | BootLoader 的 `Download_Transport()`。 |
| App 写参数区的意义是什么？ | 通知 BootLoader“下载区新固件已准备好，你下次启动来搬运”。 |
| 为什么升级成功后 App 要复位？ | 因为后续搬运和正式切换是 BootLoader 的职责。 |
| 为什么 BootLoader 搬运成功后还要再复位一次？ | 让新 App 在更干净、更标准的启动环境中运行。 |
| BootLoader 跳过去以后，App 为什么还要重新设置 VTOR 和开中断？ | 因为普通函数跳转不会自动恢复 PRIMASK，也不能假设所有现场都已经完全属于 App。 |
| `jump app` 后脱机没日志，是不是一定要改 BootLoader 或系统文件？ | 不一定。先查 App 是否进入 AC6 semihosting；`BKPT 0xAB` 才是这次脱机卡死的直接证据。 |

---

## 11. 你现在最推荐看的代码顺序

| 顺序 | 文件 | 看什么 |
|---|---|---|
| 1 | [HardWare/USART/bsp_usart.c](D:/GD32/2026706296/HardWare/USART/bsp_usart.c:172) | USART1/RS485 和 DMA 接收链路如何初始化 |
| 2 | [User/gd32f4xx_it.c](D:/GD32/2026706296/User/gd32f4xx_it.c:223) | USART1 IDLE 中断如何把一帧数据移交给 OTA 任务 |
| 3 | [Function/uart_ota_app.c](D:/GD32/2026706296/Function/uart_ota_app.c:776) | `uart_ota_task()` 如何取帧、解析协议、处理结果 |
| 4 | [Function/uart_ota_app.c](D:/GD32/2026706296/Function/uart_ota_app.c:418) | App 如何处理 START/DATA/END |
| 5 | [HardWare/BOOTLOADER/bootloader_port.c](D:/GD32/2026706296/HardWare/BOOTLOADER/bootloader_port.c:461) | App 如何写参数区通知 BootLoader |
| 6 | [D:\GD32\2026706296_bootloader\Function\Function.c](D:/GD32/2026706296_bootloader/Function/Function.c:120) | BootLoader 如何决定是否搬运 |
| 7 | [D:\GD32\2026706296_bootloader\Function\Function.c](D:/GD32/2026706296_bootloader/Function/Function.c:376) | BootLoader 如何真正搬运 |
| 8 | [D:\GD32\2026706296_bootloader\Function\Function.c](D:/GD32/2026706296_bootloader/Function/Function.c:554) | BootLoader 如何跳新 App |
| 9 | [User/boot_app_config.c](D:/GD32/2026706296/User/boot_app_config.c:1) | 新 App 如何接管现场 |

---

## 12. 一句话再总结

这套实际流程不是“App 直接升级自己”，而是：

> **App 负责接收并暂存新固件，BootLoader 负责正式搬运和切换运行。**

你只要牢牢记住这句，再看代码就不会混乱。
