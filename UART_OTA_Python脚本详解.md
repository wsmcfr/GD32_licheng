# UART OTA Python 脚本详解

## 1. 文档目的

本文专门讲解这个上位机脚本：

| 文件 | 作用 |
|---|---|
| [tools/make_uart_ota_packet.py](D:/GD32/2026706296/tools/make_uart_ota_packet.py:1) | 生成或发送当前工程使用的 UART OTA 升级数据 |

它不是一个“普通串口发文件”脚本，而是一个**严格按当前 App 侧 OTA 协议生成帧、发送帧、等待 ACK、再继续发送**的脚本。

本文重点回答 4 个问题：

| 问题 | 说明 |
|---|---|
| 它到底干了什么 | 从 `Project.bin` 到实际串口发送，中间经过了哪些步骤 |
| 每个函数做什么 | 每个函数的作用、输入、输出、为什么存在 |
| 怎么使用 | 三种模式分别怎么用 |
| 为什么它能工作 | 为什么它能和 MCU App 侧配合完成升级 |

---

## 2. 先说一句总体结论

这个脚本之所以能上传新的 App 固件，不是因为它“会发串口”，而是因为它同时做对了下面几件事：

| 关键点 | 作用 |
|---|---|
| 协议字段完全对齐 App 侧 | App 能按约定正确解析每一帧 |
| 固件被拆成 START / DATA / END 三类帧 | 设备知道升级何时开始、每包写到哪里、何时结束 |
| 每个 DATA 分包都带 offset 和 CRC | App 能直接边收边写 Flash，并校验正确性 |
| 每发一帧都等待 ACK | 不会把 MCU 接收缓存或处理速度冲垮 |
| Python 侧和 App 侧使用同样的 CRC32 算法和小端字节序 | 双方对“同一份数据”的理解一致 |

当前工程的串口角色要先记住：

| 串口 | 当前职责 |
|---|---|
| `USART0` | 启动日志、调试输出、普通透传 |
| `RS485/USART1` | OTA 专用帧收发，ACK 返回前由 App 切换 RS485 发送方向 |

一句话概括：

> **这个脚本不是“把 bin 发出去”，而是“把 bin 变成 MCU 能安全接收和确认的协议流”。**

---

## 3. 它整体在做什么

脚本入口是 [main()](D:/GD32/2026706296/tools/make_uart_ota_packet.py:490)。

它支持 3 种模式：

| 模式 | 作用 | 适用场景 |
|---|---|---|
| `packet` | 生成旧格式 `.uota` 文件 | 兼容旧流程、离线检查 |
| `stream-info` | 打印分包传输前的关键元数据 | 发送前核对大小、CRC、版本、分包数 |
| `send` | 打开真实串口，按 START/DATA/END 协议发送 | 当前推荐的在线升级方式 |

### 3.1 三种模式分别对应什么命令

| 模式 | 命令 |
|---|---|
| `packet` | `python tools\make_uart_ota_packet.py --mode packet --version 0x00000006` |
| `stream-info` | `python tools\make_uart_ota_packet.py --mode stream-info --version 0x00000006 --chunk-size 512` |
| `send` | `python tools\make_uart_ota_packet.py --mode send --port COM29 --baudrate 460800 --version 0x00000006 --chunk-size 512` |

### 3.2 当前推荐模式

当前工程真正推荐的是：

```powershell
python tools\make_uart_ota_packet.py --mode send --port COM29 --baudrate 460800 --version 0x00000006 --chunk-size 512
```

原因是：

| 原因 | 说明 |
|---|---|
| App 侧已经实现流式 OTA | 它按 START/DATA/END 解析 |
| App 侧 RAM 不能长时间缓存整包固件 | 需要边收边写下载缓存区 |
| ACK 节流更稳 | 每帧成功才继续 |

---

## 4. 这个脚本依赖哪些输入

### 4.1 固件输入

默认输入文件是：

| 文件 | 默认路径 |
|---|---|
| 原始固件 bin | `MDK/output/Project.bin` |

这是 Keil 编译后得到的纯二进制固件。

### 4.2 关键命令行参数

定义位置在 [main()](D:/GD32/2026706296/tools/make_uart_ota_packet.py:499) 到 [507](D:/GD32/2026706296/tools/make_uart_ota_packet.py:507)。

| 参数 | 作用 | 默认值 |
|---|---|---|
| `--mode` | 选择工作模式 | `packet` |
| `--chunk-size` | 每个 DATA 分包的数据长度 | `512` |
| `--port` | 串口号 | 无 |
| `--baudrate` | 串口波特率 | `460800` |
| `--ack-timeout` | 每帧等待 ACK 的超时时间，秒 | `2.0` |
| `input_bin` | 输入 bin 路径 | `MDK/output/Project.bin` |
| `output_file` | 旧 `.uota` 输出路径 | `MDK/output/Project.uota` |
| `--version` | 本次升级版本号 | `0x00000001` |

---

## 5. 协议常量是怎么定义的

这些常量在脚本最前面定义，位置 [tools/make_uart_ota_packet.py:31](D:/GD32/2026706296/tools/make_uart_ota_packet.py:31) 到 [38](D:/GD32/2026706296/tools/make_uart_ota_packet.py:38)。

| 常量 | 值 | 作用 |
|---|---:|---|
| `UART_OTA_MAGIC` | `0x5AA5C33C` | 旧整包 `.uota` 模式的魔术字 |
| `UART_OTA_STREAM_MAGIC` | `0xA55A5AA5` | 新流式 OTA 的魔术字 |
| `UART_OTA_FRAME_START` | `1` | START 帧类型 |
| `UART_OTA_FRAME_DATA` | `2` | DATA 帧类型 |
| `UART_OTA_FRAME_END` | `3` | END 帧类型 |
| `UART_OTA_FRAME_ACK_BASE` | `0x80` | ACK 类型基值 |
| `UART_OTA_ACK_FRAME_SIZE` | `20` | ACK 帧固定长度 |
| `UART_OTA_DEFAULT_BAUDRATE` | `460800` | 默认串口波特率 |
| `UART_OTA_CHANNEL_NAME` | `RS485/USART1` | 当前脚本输出中标识的 OTA 专用通道名 |

### 5.1 为什么要有两套 magic

| magic | 用途 |
|---|---|
| `0x5AA5C33C` | 旧 `.uota` 文件头 |
| `0xA55A5AA5` | 当前流式 START/DATA/END/ACK 协议 |

这样设计的目的，是明确区分：

| 数据类型 | 设备如何理解 |
|---|---|
| 旧整包文件 | 旧逻辑 / 离线检查 |
| 当前流式协议帧 | App 当前 OTA 接收器真正识别的内容 |

---

## 6. 数据结构与抽象设计

### 6.1 `StreamProgress`

定义位置：[tools/make_uart_ota_packet.py:41](D:/GD32/2026706296/tools/make_uart_ota_packet.py:41)

这是一个只读数据类，用来描述当前已经被 App ACK 确认的发送进度。

| 字段 | 作用 |
|---|---|
| `stage` | 当前阶段：START / DATA / END |
| `frame_index` | 当前已成功确认第几帧 |
| `total_frames` | 总帧数 |
| `chunk_index` | 当前已成功的 DATA 分包数 |
| `total_chunks` | 总 DATA 分包数 |
| `bytes_sent` | 已被设备确认的固件字节数 |
| `total_bytes` | 固件总大小 |

它存在的意义是：

| 设计目的 | 说明 |
|---|---|
| 发送逻辑和进度显示解耦 | `send_stream()` 不直接写死打印格式 |
| 便于测试 | 单元测试直接检查进度对象内容 |
| 便于扩展 | 将来可以接 GUI、日志系统、进度条 |

### 6.2 `StreamTransport`

定义位置：[tools/make_uart_ota_packet.py:65](D:/GD32/2026706296/tools/make_uart_ota_packet.py:65)

这是一个 `Protocol`，不是实际串口类。

它只要求两件事：

| 方法 | 作用 |
|---|---|
| `write(data)` | 写出一段二进制数据 |
| `read(size)` | 读回最多 `size` 字节 |

为什么这样设计很重要？

| 好处 | 说明 |
|---|---|
| 不把发送主逻辑绑死在 `pyserial` 上 | 测试里可以用假的传输对象 |
| 复用性高 | 任何满足 `write/read` 的对象都能拿来跑 `send_stream()` |
| 结构更清晰 | “协议逻辑”和“真实串口设备”分层 |

---

## 7. 这个脚本的核心调用链

真正发送时，调用链如下：

| 顺序 | 函数 | 作用 |
|---|---|---|
| 1 | [main()](D:/GD32/2026706296/tools/make_uart_ota_packet.py:490) | 解析命令行 |
| 2 | [send_stream_over_serial()](D:/GD32/2026706296/tools/make_uart_ota_packet.py:441) | 打开真实串口 |
| 3 | [send_stream()](D:/GD32/2026706296/tools/make_uart_ota_packet.py:266) | 执行协议发送主流程 |
| 4 | [build_start_frame()](D:/GD32/2026706296/tools/make_uart_ota_packet.py:93) | 生成 START 帧 |
| 5 | [wait_for_ack()](D:/GD32/2026706296/tools/make_uart_ota_packet.py:234) | 等 START ACK |
| 6 | [iter_chunks()](D:/GD32/2026706296/tools/make_uart_ota_packet.py:217) | 把固件切成多个 chunk |
| 7 | [build_data_frame()](D:/GD32/2026706296/tools/make_uart_ota_packet.py:118) | 生成每个 DATA 帧 |
| 8 | [wait_for_ack()](D:/GD32/2026706296/tools/make_uart_ota_packet.py:234) | 等每个 DATA ACK |
| 9 | [build_end_frame()](D:/GD32/2026706296/tools/make_uart_ota_packet.py:145) | 生成 END 帧 |
| 10 | [wait_for_ack()](D:/GD32/2026706296/tools/make_uart_ota_packet.py:234) | 等 END ACK |

理解这条链路后，再看每个函数就很顺。

---

## 8. 每个函数详细说明

### 8.1 `crc32_bytes(data)`

位置：[tools/make_uart_ota_packet.py:81](D:/GD32/2026706296/tools/make_uart_ota_packet.py:81)

| 项目 | 说明 |
|---|---|
| 作用 | 计算一段字节的 CRC32 |
| 输入 | `bytes` |
| 输出 | `0~0xFFFFFFFF` 的无符号 CRC32 |

为什么需要它：

| 使用位置 | 作用 |
|---|---|
| START 帧 | 写入整包固件 CRC |
| DATA 帧 | 写入每个分包的 CRC |
| END 帧 | 再次携带整包固件 CRC |
| 旧 `.uota` | 写入完整包头 CRC 信息 |

为什么要 `& 0xFFFFFFFF`：

| 原因 | 说明 |
|---|---|
| Python 整数无限精度 | 需要收敛到 32 位无符号 |
| 和 MCU 一致 | App 侧按 `uint32_t` 比较 CRC |

---

### 8.2 `build_start_frame(app_version, firmware)`

位置：[tools/make_uart_ota_packet.py:93](D:/GD32/2026706296/tools/make_uart_ota_packet.py:93)

这个函数用来构造 START 帧。

生成内容如下：

| 字段 | 含义 |
|---|---|
| magic | 流式协议魔术字 |
| frame_type | START |
| app_version | 本次升级版本号 |
| firmware_size | 整个 bin 长度 |
| firmware_crc32 | 整个 bin 的 CRC32 |
| header_crc32 | 前 20 字节头部的 CRC32 |

为什么头部还要再算一次 CRC：

| 原因 | 说明 |
|---|---|
| 快速拒绝坏头 | App 不必进入升级状态再发现头部损坏 |
| 确保元数据可靠 | 版本号、长度、整包 CRC 这几个字段必须可信 |
| 降低误升级概率 | 避免随机数据被误识别成合法开始帧 |

---

### 8.3 `build_data_frame(seq, offset, chunk)`

位置：[tools/make_uart_ota_packet.py:118](D:/GD32/2026706296/tools/make_uart_ota_packet.py:118)

这个函数用来生成 DATA 帧。

DATA 帧格式是：

| 字段 | 含义 |
|---|---|
| magic | 流式协议魔术字 |
| frame_type | DATA |
| seq | 当前分包序号 |
| offset | 当前分包在整个固件中的偏移 |
| length | 当前分包长度 |
| chunk_crc32 | 当前分包数据 CRC |
| chunk | 分包原始内容 |

为什么 `seq` 和 `offset` 都要有：

| 字段 | 解决什么问题 |
|---|---|
| `seq` | 检查乱序、漏包、重发 |
| `offset` | 让 App 直接把数据写到下载缓存区正确位置 |

这就是为什么 App 端能够边收边写 Flash，而不是先缓存完整包。

---

### 8.4 `build_end_frame(firmware)`

位置：[tools/make_uart_ota_packet.py:145](D:/GD32/2026706296/tools/make_uart_ota_packet.py:145)

这个函数生成 END 帧。

END 帧内容：

| 字段 | 含义 |
|---|---|
| magic | 流式协议魔术字 |
| type | END |
| firmware_size | 整包长度 |
| firmware_crc32 | 整包 CRC |

为什么 END 阶段还要再带一次大小和 CRC：

| 原因 | 说明 |
|---|---|
| 和 START 做前后呼应 | 防止头尾元数据不一致 |
| 给 App 做最终确认 | App 会拿 START、运行中累计值、END 三方对照 |
| 防止中途状态错乱 | 即使某些帧异常，最终还能统一校验 |

---

### 8.5 `build_ack_frame(frame_type, status, value0, value1)`

位置：[tools/make_uart_ota_packet.py:163](D:/GD32/2026706296/tools/make_uart_ota_packet.py:163)

这个函数主要供测试使用，用于模拟 App 返回的 ACK。

ACK 帧格式：

| 字段 | 含义 |
|---|---|
| magic | 流式魔术字 |
| ack_type | `0x80 | 原帧类型` |
| status | `0` 成功，非 `0` 失败 |
| value0 | 附加值 |
| value1 | 附加值 |

为什么要单独保留这个函数：

| 原因 | 说明 |
|---|---|
| 单元测试需要 | 测试里要构造假 ACK |
| 自检方便 | ACK 格式有统一来源，不会手写错 |

---

### 8.6 `find_ack_frame(data, frame_type)`

位置：[tools/make_uart_ota_packet.py:185](D:/GD32/2026706296/tools/make_uart_ota_packet.py:185)

这个函数负责从串口回来的数据中找到对应的 ACK。

它之所以重要，是因为 App 串口可能同时返回：

| 数据 | 例子 |
|---|---|
| 文本日志 | `OTA: stream start ...` |
| 二进制 ACK | 固定 20 字节协议数据 |

所以这个函数不能假设“串口一读回来就是 ACK”。  
它必须：

| 步骤 | 动作 |
|---|---|
| 1 | 扫描流式魔术字 |
| 2 | 检查后面是否有完整 20 字节 |
| 3 | 解包并检查 ACK 类型是否匹配当前帧 |
| 4 | 如果不匹配继续向后找 |

这也是为什么它能跳过普通文本日志，只抓真正的 ACK。

---

### 8.7 `iter_chunks(firmware, chunk_size)`

位置：[tools/make_uart_ota_packet.py:217](D:/GD32/2026706296/tools/make_uart_ota_packet.py:217)

这个函数把完整固件切成多个 `(offset, chunk)`。

例如：

```text
firmware = 10 字节
chunk_size = 4
```

它会得到：

| offset | chunk |
|---:|---|
| 0 | `0123` |
| 4 | `4567` |
| 8 | `89` |

为什么返回 `(offset, chunk)` 而不是只返回 `chunk`：

因为 App 侧写下载缓存区时，真正需要的是：

```text
写入地址 = 下载缓存区基地址 + offset
```

所以 offset 是协议关键字段，不是附加信息。

---

### 8.8 `wait_for_ack(transport, frame_type, ack_timeout)`

位置：[tools/make_uart_ota_packet.py:234](D:/GD32/2026706296/tools/make_uart_ota_packet.py:234)

这个函数负责等待设备对当前帧的 ACK。

处理流程：

| 步骤 | 动作 |
|---|---|
| 1 | 设定超时时间 |
| 2 | 循环从串口 `read(64)` |
| 3 | 累积收到的数据 |
| 4 | 调 `find_ack_frame()` 找匹配 ACK |
| 5 | 找到后检查 `status` |
| 6 | `status != 0` 抛出异常 |
| 7 | 超时仍未收到也抛出异常 |

为什么设计成“抛异常”：

| 原因 | 说明 |
|---|---|
| 发送失败就不该继续 | OTA 某帧失败后继续发没有意义 |
| 主流程更清晰 | 成功路径不需要层层判断错误码 |
| CLI 体验更直观 | 用户能直接看到是哪种 ACK 错误或超时 |

---

### 8.9 `send_stream(...)`

位置：[tools/make_uart_ota_packet.py:266](D:/GD32/2026706296/tools/make_uart_ota_packet.py:266)

这是整个脚本最核心的函数。

它的职责是：**按流式协议把整份固件发完**。

主流程如下：

| 阶段 | 动作 |
|---|---|
| START | 生成 START 帧 -> 发送 -> 等 ACK |
| DATA | 固件切块 -> 每块生成 DATA 帧 -> 发送 -> 等 ACK |
| END | 生成 END 帧 -> 发送 -> 等 ACK |

为什么它能稳定工作：

| 设计 | 作用 |
|---|---|
| 每次只发一帧 | 降低设备处理压力 |
| 每帧必须等 ACK | 保证设备确实接收并处理成功 |
| DATA 帧有 offset 和 CRC | 设备可直接写 Flash 并校验 |
| 发送进度回调 | 发送时不会长时间无输出 |

#### 内部 `report()`

`send_stream()` 里还定义了一个内部小函数 `report()`，位置 [py:296](D:/GD32/2026706296/tools/make_uart_ota_packet.py:296)。

它的作用是把“当前 ACK 成功后的进度”封装成 `StreamProgress` 发给回调。

为什么这样写：

| 好处 | 说明 |
|---|---|
| 发送逻辑和显示逻辑解耦 | 主发送函数不关心如何打印 |
| 测试方便 | 可以直接收集进度对象做断言 |
| 扩展方便 | 后面可换成 GUI 或日志系统 |

---

### 8.10 `parse_u32(value)`

位置：[tools/make_uart_ota_packet.py:338](D:/GD32/2026706296/tools/make_uart_ota_packet.py:338)

这个函数负责把命令行里的版本号解析成 32 位无符号整数。

支持两种写法：

| 写法 | 示例 |
|---|---|
| 十进制 | `6` |
| 十六进制 | `0x00000006` |

为什么要单独写它：

| 原因 | 说明 |
|---|---|
| `argparse` 默认只会给字符串 | 需要统一解析 |
| 要限制在 `uint32` 范围 | 不能让版本号溢出 |

---

### 8.11 `parse_positive_u32(value)`

位置：[tools/make_uart_ota_packet.py:353](D:/GD32/2026706296/tools/make_uart_ota_packet.py:353)

它在 `parse_u32()` 的基础上进一步要求这个值必须大于 0。

主要用于：

| 参数 | 为什么必须大于 0 |
|---|---|
| `chunk-size` | 为 0 会导致切包逻辑无意义甚至死循环 |
| `baudrate` | 波特率不可能为 0 |

---

### 8.12 `build_packet(input_bin, output_file, app_version)`

位置：[tools/make_uart_ota_packet.py:368](D:/GD32/2026706296/tools/make_uart_ota_packet.py:368)

这个函数生成旧 `.uota` 格式。

它的格式是：

```text
16字节头 + 原始 firmware
```

其中 16 字节头是：

| 字段 | 含义 |
|---|---|
| magic | `0x5AA5C33C` |
| app_version | 版本号 |
| firmware_size | 固件长度 |
| firmware_crc32 | 固件 CRC |

为什么这个模式还保留：

| 原因 | 说明 |
|---|---|
| 兼容旧流程 | 历史上可能还在使用 |
| 离线检查方便 | 快速确认头部格式 |
| 不影响新协议 | 它只是一个辅助出口 |

但当前在线升级，不推荐直接用它发送。

---

### 8.13 `print_stream_info(input_bin, app_version, chunk_size)`

位置：[tools/make_uart_ota_packet.py:394](D:/GD32/2026706296/tools/make_uart_ota_packet.py:394)

这个函数不发送，只打印发送前需要核对的信息：

| 输出项 | 作用 |
|---|---|
| 固件大小 | 确认是否超过下载缓存区上限 |
| CRC32 | 后续和设备日志比对 |
| 版本号 | 确认这次测试版本号是否已递增 |
| chunk_size | 确认分包大小 |
| 分包数 | 估算总发送帧数 |

这一步很适合在真正发串口前做确认。

---

### 8.14 `print_stream_progress(progress)`

位置：[tools/make_uart_ota_packet.py:418](D:/GD32/2026706296/tools/make_uart_ota_packet.py:418)

它负责把进度对象打印成人能理解的一行文本，比如：

```text
DATA acked: chunk=1/66, frames=2/68, bytes=512/33536 (1%)
```

为什么打印的是“acked”而不是“sent”：

| 原因 | 说明 |
|---|---|
| 真实可靠的进度不是“已经写到 PC 串口缓冲” | 那不代表 MCU 真收到了 |
| 只有 ACK 才表示设备确认成功处理 | 才能算真正完成一帧 |

---

### 8.15 `send_stream_over_serial(...)`

位置：[tools/make_uart_ota_packet.py:441](D:/GD32/2026706296/tools/make_uart_ota_packet.py:441)

这个函数负责把“协议发送逻辑”绑定到真实串口上。

它做的事：

| 步骤 | 动作 |
|---|---|
| 1 | 动态导入 `pyserial` |
| 2 | 读取 `Project.bin` |
| 3 | 打印本次发送元数据 |
| 4 | 打开串口 |
| 5 | 调 `send_stream()` 完成实际发送 |

为什么 `pyserial` 不在文件开头导入：

| 原因 | 说明 |
|---|---|
| `packet` / `stream-info` 模式根本不需要串口 | 没必要强依赖 |
| 测试和离线模式可以在没装 `pyserial` 的机器上运行 | 更灵活 |
| 错误更清晰 | 只有 `send` 模式才提示安装串口依赖 |

---

### 8.16 `main(argv=None)`

位置：[tools/make_uart_ota_packet.py:490](D:/GD32/2026706296/tools/make_uart_ota_packet.py:490)

它是命令行总入口。

模式分发逻辑如下：

| 模式 | 调用 |
|---|---|
| `stream-info` | `print_stream_info()` |
| `send` | `send_stream_over_serial()` |
| `packet` | `build_packet()` |

为什么 `main()` 写得很短是好事：

| 原因 | 说明 |
|---|---|
| 入口清晰 | 用户一眼能看到三种模式 |
| 功能分层 | 协议逻辑不堆在 CLI 入口里 |
| 便于测试 | 各个能力可单独测试 |

---

## 9. 它为什么能和 App 侧完美对上

对应的 MCU 接收逻辑在：

| 文件 | 作用 |
|---|---|
| [USER/App/uart_ota_app.c](D:/GD32/2026706296/USER/App/uart_ota_app.c:1) | App 侧流式 OTA 解析、写下载缓存区、写参数区、复位 |
| [USER/Driver/bootloader_port.c](D:/GD32/2026706296/USER/Driver/bootloader_port.c:1) | BootLoader 交接层：CRC、向量表校验、下载区擦写、参数区写入、软件复位 |

### 9.1 关键协议项一一对应

| Python 脚本 | App 侧 |
|---|---|
| 流式 magic `0xA55A5AA5` | App 侧按同样魔术字判断是否为 OTA 帧 |
| `START=1, DATA=2, END=3` | App 侧按同样帧类型分发 |
| 小端 `struct.pack("<...")` | App 侧用小端读取 `uint32_t` |
| DATA 帧含 `seq/offset/length/chunk_crc32` | App 侧逐项校验 |
| `chunk_size=512` | App 侧允许的单帧数据上限也是 512 |
| END 帧再带整包 size/CRC | App 侧做最终整包校验 |

### 9.2 ACK 节流为什么是必须的

App 侧不是收完整包再处理，而是：

| 步骤 | 动作 |
|---|---|
| 1 | 收到一帧 |
| 2 | 校验 |
| 3 | 写 Flash |
| 4 | 更新 CRC / 进度 |
| 5 | 回 ACK |

所以 Python 侧必须“发一帧等一帧”，不能像文件直传那样一股脑推满串口。

---

## 10. 这个脚本为什么能做到低 RAM 在线升级

核心原因不是 Python 本身，而是**脚本协议设计和 App 侧处理方式是匹配的**。

| 设计 | 结果 |
|---|---|
| 把整包拆成小 DATA 分包 | App 每次只需处理小块数据 |
| 每包都带 offset | App 可直接写下载区，不用拼整包 RAM |
| 每包都带 chunk CRC | 每小块都能独立校验 |
| 每包都等待 ACK | App 有时间完成 Flash 写入 |
| END 阶段再做整包确认 | 确保最终可靠性 |

所以真正的低 RAM 原理是：

> **不是减少固件大小，而是把固件变成设备能逐块确认并落盘的小事务。**

---

## 11. 常用使用方式

### 11.1 发送前先看元数据

```powershell
python tools\make_uart_ota_packet.py --mode stream-info --version 0x00000006 --chunk-size 512
```

典型输出：

```text
stream MDK/output/Project.bin: firmware=33536 bytes, crc=0xC0B85342, version=0x00000006, chunk_size=512, chunks=66, channel=RS485/USART1
```

### 11.2 真正发送

```powershell
python tools\make_uart_ota_packet.py --mode send --port COM29 --baudrate 460800 --version 0x00000006 --chunk-size 512
```

典型输出：

```text
send stream MDK/output/Project.bin: firmware=33536 bytes, crc=0xC0B85342, version=0x00000006, chunk_size=512, chunks=66, channel=RS485/USART1, port=COM29, baudrate=460800
START acked: chunk=0/66, frames=1/68, bytes=0/33536 (0%)
DATA acked: chunk=1/66, frames=2/68, bytes=512/33536 (1%)
...
END acked: chunk=66/66, frames=68/68, bytes=33536/33536 (100%)
sent stream frames=68, channel=RS485/USART1, port=COM29, baudrate=460800
```

### 11.3 生成旧格式文件

```powershell
python tools\make_uart_ota_packet.py --mode packet --version 0x00000006
```

---

## 12. 典型排错思路

### 12.1 如果 `send` 模式一开始就失败

先检查：

| 检查项 | 说明 |
|---|---|
| `pyserial` 是否安装 | `python -m pip install pyserial` |
| 串口号是否正确 | 例如 `COM29`，且必须是接到 `RS485/USART1` 转换器的那一路 |
| 波特率是否为 `460800` | 必须和 App/BootLoader 约定一致 |

### 12.2 如果 START 超时

说明 App 没有对 START 回 ACK，常见原因：

| 原因 | 说明 |
|---|---|
| 端口错了 | 根本没发到目标板 |
| 波特率错了 | App 收到的是乱码 |
| App 不是当前带 OTA 的版本 | 没有 `START/DATA/END` 解析能力 |
| 串口被别的软件占用 | Python 发不进去或读不回来 |

当前工程新增了两条专门用于这类排障的现场证据：

| 观察口 | 正常现象 | 用途 |
|---|---|---|
| `RS485/USART1` | 上电一次性输出 `OTA485: ready` | 确认 OTA 专用口的 TX 线、方向控制、串口号和当前固件版本基本正确 |
| `USART0(PA9/PA10)` | 发送时输出 `OTA: rx irq=... len=... magic=... type=...` | 确认 `RS485/USART1` 的 RX/IDLE/DMA/任务层链路是否真的收到了首帧 |

要特别注意：

| 现象 | 结论 |
|---|---|
| RS485 端看不到 `BOOT: start` | 正常，因为 `BOOT: start` 固定走 `USART0`，不是 `RS485/USART1` |
| RS485 端能看到 `OTA485: ready`，但 Python 仍等不到 START ACK | 优先检查 RS485 方向控制、A/B 极性、USB-RS485 转换器 RX 方向、共地和串口占用 |
| `USART0` 能看到 `OTA: rx ...`，但上位机超时 | 说明设备已经收到 START，问题更偏向 ACK 发不回 PC |
| `USART0` 完全没有 `OTA: rx ...` | 说明 `RS485/USART1` 接收链路根本没进来，优先检查接线、A/B 极性、波特率和当前固件版本 |

### 12.3 如果 DATA 某一帧失败

常见原因：

| 原因 | 说明 |
|---|---|
| chunk 长度不匹配 App 约定 | 例如擅自改大 `chunk-size` |
| 固件首包向量表非法 | App 会拒绝第一个 DATA |
| 片内下载缓存区写失败 | App 会返回 Flash 错误 |
| 串口线路不稳定 | chunk CRC 校验不通过 |

### 12.4 如果 END 失败

说明前面虽然每帧都收到了，但最终整包确认没过，通常是：

| 原因 | 说明 |
|---|---|
| 某些 DATA 实际写入 Flash 时损坏 | 下载区回读 CRC 不一致 |
| 整包大小和 START 不一致 | 协议状态被破坏 |
| 固件本身不是当前 `Project.bin` | CRC 对不上 |

---

## 13. 单元测试为什么也很重要

测试文件在：

| 文件 | 作用 |
|---|---|
| [tools/test_uart_ota_packet.py](D:/GD32/2026706296/tools/test_uart_ota_packet.py:1) | 验证 Python 侧协议生成和发送逻辑 |

它主要验证：

| 测试点 | 说明 |
|---|---|
| START 帧字段和小端格式正确 | 确保版本、大小、CRC 不会打错 |
| DATA 帧序号、偏移、长度和 chunk CRC 正确 | 确保 App 能按协议解析 |
| chunk 切分正确 | 防止分包逻辑错误 |
| `chunk-size=0` 会报错 | 防止无效参数 |
| 默认波特率是 460800 | 确保命令行默认值正确 |
| ACK 解析能跳过文本日志 | 防止日志和二进制混流误判 |
| `send_stream()` 的发送顺序正确 | 确保 START -> DATA -> END 的顺序不乱 |
| 进度回调行为正确 | 确保长时间发送时有可读反馈 |

这说明：

> 这个脚本不是“写完看起来对”，而是“协议关键行为有测试兜底”。

---

## 14. 最推荐的阅读顺序

如果你要再回头读源码，建议按这个顺序：

| 顺序 | 函数 | 为什么先看它 |
|---|---|---|
| 1 | [main()](D:/GD32/2026706296/tools/make_uart_ota_packet.py:490) | 先知道有哪些模式 |
| 2 | [send_stream()](D:/GD32/2026706296/tools/make_uart_ota_packet.py:266) | 它是核心发送流程 |
| 3 | [build_start_frame()](D:/GD32/2026706296/tools/make_uart_ota_packet.py:93) | 理解协议从哪里开始 |
| 4 | [build_data_frame()](D:/GD32/2026706296/tools/make_uart_ota_packet.py:118) | 理解分包为什么能直接写 Flash |
| 5 | [wait_for_ack()](D:/GD32/2026706296/tools/make_uart_ota_packet.py:234) | 理解为什么发送可靠 |
| 6 | [find_ack_frame()](D:/GD32/2026706296/tools/make_uart_ota_packet.py:185) | 理解为什么文本日志和 ACK 混流也不怕 |
| 7 | [send_stream_over_serial()](D:/GD32/2026706296/tools/make_uart_ota_packet.py:441) | 最后看怎么接到真实串口上 |

---

## 15. 一句话总结

这个 Python 脚本之所以能上传新的 App 固件，是因为它不是简单“发文件”，而是：

> **把 `Project.bin` 按当前 MCU App 的协议要求，打包成可校验、可分包、可确认、可节流的 OTA 数据流。**

只要你抓住这句话，再看这个脚本的每个函数，就不会乱。
