# BootLoader 与 App 实际升级运行流程详解

## 1. 先说结论

这套方案里，职责分工是固定的：

| 角色 | 负责什么 | 不负责什么 |
|---|---|---|
| 上位机工具 | 把 `Project.bin` 拆成 START / DATA / END 帧，并按 ACK 节流发送 | 不直接改 MCU Flash |
| App | 接收分包、写下载缓存区、写参数区、软件复位 | 不把新固件搬到正式 App 区 |
| BootLoader | 上电后读取参数区、把下载缓存区新固件搬到正式 App 区、CRC 校验、跳转新 App | 不负责接收整包 OTA 数据 |

一句话：

> **App 负责“准备好升级现场”，BootLoader 负责“执行最终切换”。**

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

## 2.2 参数区为什么正好是 4KB

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

## 2.3 为什么正式 App 区比下载缓存区大很多

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
| 1 | 上位机 | 发送 START 帧 | 告诉 App：接下来要升级，固件多大、CRC 是多少、版本号是多少 |
| 2 | App | 校验 START，擦下载缓存区 | 为接收新固件腾出干净空间 |
| 3 | 上位机 | 逐帧发送 DATA | 每帧携带 `seq`、`offset`、`length`、`chunkCRC32` |
| 4 | App | 每收一帧就写到 `0x08067000 + offset` | 新固件逐步写入下载缓存区 |
| 5 | 上位机 | 发送 END 帧 | 告诉 App：固件发送结束 |
| 6 | App | 校验整包 CRC、回读下载区 CRC、写参数区 | 告诉 BootLoader“新固件已准备好” |
| 7 | App | 软件复位 | 控制权交还 BootLoader |
| 8 | BootLoader | 启动后读取参数区 | 看到 `updateFlag=0x5A`、`updateStatus=0x01` |
| 9 | BootLoader | 把 `0x08067000` 搬到 `0x0800D000` | 新固件变成正式 App |
| 10 | BootLoader | 回读正式 App 区计算 CRC | 确认搬运结果正确 |
| 11 | BootLoader | 清升级标志并复位 | 避免下次上电重复搬运 |
| 12 | BootLoader | 再次启动后跳转 `0x0800D000` | 新 App 正式运行 |

---

## 4. App 端到底做了什么

App 侧核心文件是 [USER/App/usart_app.c](D:/GD32/2026706296/USER/App/usart_app.c:1)。

### 4.1 START 阶段

入口函数：`prv_uart_ota_process_start()`  
位置：[usart_app.c:823](D:/GD32/2026706296/USER/App/usart_app.c:823)

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

### 4.2 DATA 阶段

入口函数：`prv_uart_ota_process_data()`  
位置：[usart_app.c:889](D:/GD32/2026706296/USER/App/usart_app.c:889)

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

### 4.3 END 阶段

入口函数：`prv_uart_ota_process_end()`  
位置：[usart_app.c:968](D:/GD32/2026706296/USER/App/usart_app.c:968)

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
| 8 | 返回成功 | 外层 `uart_task()` 会触发软件复位 |

关键点：

> App 到这里仍然**没有把新固件搬到 `0x0800D000`**。  
> 它只是确认下载缓存区里的新固件是可靠的，然后把升级条件写到参数区。

---

## 5. App 端写参数区到底写了什么

关键函数：`prv_uart_ota_write_boot_param()`  
位置：[usart_app.c:699](D:/GD32/2026706296/USER/App/usart_app.c:699)

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

App 在 [usart_app.c:1230](D:/GD32/2026706296/USER/App/usart_app.c:1230) 收到 `UART_OTA_RESULT_SUCCESS` 后，会调用 `prv_uart_ota_system_reset()`，见 [usart_app.c:1080](D:/GD32/2026706296/USER/App/usart_app.c:1080)。

原因有三个：

| 原因 | 说明 |
|---|---|
| 1 | App 自己没有把新固件搬到正式 App 区，必须让 BootLoader 上电后接手 |
| 2 | BootLoader 负责最终校验和正式切换，职责更单一、更安全 |
| 3 | 复位后能走最标准的启动路径，避免旧 App 的中断、DMA、串口现场污染新 App |

---

## 7. BootLoader 端到底做了什么

BootLoader 核心文件是 [BootLoader_Two_Stage/27_0_BootLoader/Function/Function.c](D:/GD32/2026706296/BootLoader_Two_Stage/27_0_BootLoader/Function/Function.c:137)。

### 7.1 读取参数区并决定是否搬运

入口函数：`UsrFunction()`  
位置：[Function.c:137](D:/GD32/2026706296/BootLoader_Two_Stage/27_0_BootLoader/Function/Function.c:137)

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
位置：[Function.c:398](D:/GD32/2026706296/BootLoader_Two_Stage/27_0_BootLoader/Function/Function.c:398)

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
位置：[Function.c:641](D:/GD32/2026706296/BootLoader_Two_Stage/27_0_BootLoader/Function/Function.c:641)

它会：

| 步骤 | 动作 |
|---|---|
| 1 | 读取 `0x0800D000` 向量表第 0 项和第 1 项 |
| 2 | 检查栈顶地址是否合法 |
| 3 | 检查 Reset_Handler 入口是否合法 |
| 4 | 调 `iap_load_app(0x0800D000)` |

### 8.3 `iap_load_app()` 的真正跳转动作

函数：`iap_load_app()`  
位置：[Function.c:578](D:/GD32/2026706296/BootLoader_Two_Stage/27_0_BootLoader/Function/Function.c:578)

它不是简单函数跳转，而是完整切换运行现场：

| 动作 | 目的 |
|---|---|
| 关中断 | 防止 BootLoader 中断打到 App |
| 关 SysTick | 防止节拍中断沿用旧现场 |
| 清 NVIC 使能和挂起位 | 给 App 一个干净中断环境 |
| `SCB->VTOR = 0x0800D000` | 改用 App 的向量表 |
| `__set_MSP(app_stack_addr)` | 切到 App 自己的栈 |
| 跳到 App Reset_Handler | 真正开始运行新 App |

---

## 9. 新 App 启动后为什么还要再做一次接管

App 侧还有两个关键文件：

| 文件 | 作用 |
|---|---|
| [USER/boot_app_config.c](D:/GD32/2026706296/USER/boot_app_config.c:1) | 接管 BootLoader 跳转后的中断/向量表现场 |
| [USER/App/scheduler.c](D:/GD32/2026706296/USER/App/scheduler.c:68) | 在系统初始化最前面调用接管函数 |

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

当前 App 的修复方式是在 `USER/main.c` 中声明 `__use_no_semihosting`，并提供 `_sys_open()`、`_sys_write()`、`_sys_read()`、`_sys_exit()`、`_ttywrch()`、`fputc()` 等 retarget 桩函数。这样 C 库启动阶段只打开固件内部的标准流，不再访问调试器主机文件系统。

| 验证项 | 正确结果 |
|---|---|
| Keil 编译 | `MDK/output/Project.build_log.htm` 显示 `0 Error(s)` |
| map 符号 | `MDK/Listings/Project.map` 中 `_sys_open/_sys_write/_sys_exit/_ttywrch` 来自 `main.o` |
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
| 1 | [USER/App/usart_app.c](D:/GD32/2026706296/USER/App/usart_app.c:823) | App 如何收 START/DATA/END |
| 2 | [USER/App/usart_app.c](D:/GD32/2026706296/USER/App/usart_app.c:699) | App 如何写参数区通知 BootLoader |
| 3 | [USER/App/usart_app.c](D:/GD32/2026706296/USER/App/usart_app.c:1194) | App 什么时候触发复位交接 |
| 4 | [BootLoader_Two_Stage/27_0_BootLoader/Function/Function.c](D:/GD32/2026706296/BootLoader_Two_Stage/27_0_BootLoader/Function/Function.c:137) | BootLoader 如何决定是否搬运 |
| 5 | [BootLoader_Two_Stage/27_0_BootLoader/Function/Function.c](D:/GD32/2026706296/BootLoader_Two_Stage/27_0_BootLoader/Function/Function.c:398) | BootLoader 如何真正搬运 |
| 6 | [BootLoader_Two_Stage/27_0_BootLoader/Function/Function.c](D:/GD32/2026706296/BootLoader_Two_Stage/27_0_BootLoader/Function/Function.c:578) | BootLoader 如何跳新 App |
| 7 | [USER/boot_app_config.c](D:/GD32/2026706296/USER/boot_app_config.c:1) | 新 App 如何接管现场 |

---

## 12. 一句话再总结

这套实际流程不是“App 直接升级自己”，而是：

> **App 负责接收并暂存新固件，BootLoader 负责正式搬运和切换运行。**

你只要牢牢记住这句，再看代码就不会混乱。
