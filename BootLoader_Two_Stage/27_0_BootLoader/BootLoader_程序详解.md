# 27_0_BootLoader 程序详解

## 1. 先从哪里开始看

| 阅读顺序 | 文件 | 为什么先看它 |
|---|---|---|
| 1 | `Startup/startup_gd32f407_427.s` | 先知道芯片复位后第一步去了哪里，理解 `Reset_Handler -> SystemInit -> __main -> main` 这条启动链。 |
| 2 | `User/main.c` | C 层真正入口很短，能快速看到 BootLoader 只做两件事：初始化、执行主流程。 |
| 3 | `Function/Function.c` | 这是整个 BootLoader 的核心，升级判断、App 搬运、CRC 校验、跳转 App 全在这里。 |
| 4 | `HardWare/BootLoader/BootConfig.h` | 这里定义了参数区布局，决定 BootLoader 和 App 如何“通过 Flash 对话”。 |
| 5 | `HardWare/ROM/rom.c` | 这里封装了内部 Flash 的擦除、写入、读取，是搬运 App 的底层执行器。 |
| 6 | `HardWare/USART/usart.c` | 这里负责日志串口和接收中断，帮助你理解调试输出和串口扩展能力。 |
| 7 | `User/systick.c` / `User/gd32f4xx_it.c` | 这两个文件是延时和异常/滴答中断支撑层，用来补齐启动环境。 |

## 2. 这个 BootLoader 到底在干什么

| 角色 | 地址 | 作用 |
|---|---|---|
| BootLoader 自身 | `0x08000000 ~ 0x0800BFFF` | 芯片上电后先运行这里。Keil 工程里 IROM 配成了 `0x08000000`、大小 `0xC000`，也就是 48KB。 |
| 参数区 | `0x0800C000 ~ 0x0800CFFF` | BootLoader 和 App 共用的“状态区”，里面保存升级标志、App 大小、CRC、版本等信息。 |
| App 正式运行区 | `0x0800D000` 起 | BootLoader 成功后最终要跳转到这里。 |
| 下载缓存区 | `0x08073000` 起 | 旧 App 先把新固件下载到这里，BootLoader 下次启动时再搬运到正式 App 区。 |

一句话概括：

> 这个 BootLoader 不是“自己直接接收完整新固件”的类型，而是“两阶段升级”：
> 旧 App 负责接收并暂存新固件，BootLoader 负责上电后搬运、校验、切换到新 App。

## 3. 上电后的真实执行链路

| 阶段 | 代码位置 | 做了什么 | 为什么这样做 |
|---|---|---|---|
| 1 | `startup_gd32f407_427.s` 的 `Reset_Handler` | 调 `SystemInit`，再进 `__main` | 这是 Cortex-M 工程标准启动流程，先建好时钟/运行库环境。 |
| 2 | `User/main.c::main()` | 调 `System_Init()` | 先把 SysTick 和串口准备好，后续 `delay_1ms()`、`printf()` 才能用。 |
| 3 | `User/main.c::main()` | 调 `UsrFunction()` | BootLoader 的业务主流程从这里开始。 |
| 4 | `Function.c::UsrFunction()` | 从 `0x0800C000` 读完整 4KB 参数区到 RAM | 先把共享状态读出来，后面才能判断“要不要升级”。 |
| 5 | `Function.c::UsrFunction()` | 检查 `magicWord`、`updateFlag`、`updateStatus` | 这是 BootLoader 判断“参数区是否有效”和“当前是否需要搬运新 App”的关键。 |
| 6A | `updateFlag/updateStatus` 表示需要升级 | 调 `Download_Transport()` | 把下载区的固件搬到正式 App 区，并做 CRC 校验。 |
| 6B | 不需要升级 | 调 `jump_to_app()` | 直接去运行当前 App。 |
| 7 | `Function.c::iap_load_app()` | 设置 `VTOR`、`MSP`，再跳 `Reset_Handler` | 这才是正确切换到 App 的标准做法。 |

## 4. 这套“两阶段升级”是怎么实现的

### 4.1 第 1 阶段：旧 App 下载新固件

这个阶段 **不在这个 BootLoader 工程里完成**，而是在旧 App 里完成。

旧 App 的典型动作是：

| 步骤 | 动作 |
|---|---|
| 1 | 通过串口 / OTA / 上位机，把新固件接收到 RAM 或边接收边写入下载缓存区。 |
| 2 | 把新固件写到 `0x08073000` 开始的下载缓存区。 |
| 3 | 计算新固件的 `appSize` 和 `appCRC32`。 |
| 4 | 把 `magicWord=0x5AA5C33C`、`updateFlag=0x5A`、`updateStatus=0x01` 等字段写入参数区。 |
| 5 | 软件复位。 |

### 4.2 第 2 阶段：BootLoader 上电搬运

复位后进入这个 BootLoader：

| 步骤 | 代码 | 动作 |
|---|---|---|
| 1 | `UsrFunction()` | 先读参数区。 |
| 2 | `UsrFunction()` | 如果看到 `magicWord` 正确，且 `updateFlag == 0x5A`、`updateStatus == 0x01`，就认为“旧 App 已经把新固件准备好了”。 |
| 3 | `Download_Transport()` | 擦除正式 App 区。 |
| 4 | `Download_Transport()` | 分块从 `0x08073000` 读新固件，再写到 `0x0800D000`。 |
| 5 | `Download_Transport()` | 从正式 App 区重新读回数据做 CRC32，确认写入成功。 |
| 6 | `UsrFunction()` | 成功则清除升级标志，更新计数，再软件复位。 |
| 7 | 复位后再次进入 BootLoader | 这次标志已经清除，于是直接 `jump_to_app()`，运行新 App。 |

## 5. 为什么要这样设计

| 设计点 | 为什么这样做 |
|---|---|
| 不直接在 BootLoader 里收完整固件 | BootLoader 应尽量小、尽量稳定，逻辑越少越不容易把自己升级坏。 |
| 新固件先写下载区，再由 BootLoader 搬运 | 这样即使下载过程中断电，正式 App 区也不会被写到一半。 |
| 用 `magicWord` 判断参数区是否有效 | 防止参数区没初始化、全是 `0xFF` 时被误当成有效升级信息。 |
| 用 `updateFlag + updateStatus` 双字段判断 | 比只看一个标志更稳，能区分“准备升级中”和“已经完成”。 |
| 搬运后重新读正式 App 区做 CRC | 不能只相信下载区，因为真正运行的是正式 App 区。 |
| 跳转前检查 `MSP` 和 `Reset_Handler` | 防止跳到空 Flash、坏镜像、错误地址。 |
| 跳转前关中断、停 SysTick、清 NVIC 挂起位 | 避免 BootLoader 的中断现场污染 App。 |
| 先复位再进新 App，而不是搬运完直接跳 | 复位后外设环境更干净，App 启动路径更接近真实上电。 |

## 6. `Function.c` 应该怎么读

### 6.1 先看宏定义

| 宏 | 含义 |
|---|---|
| `CONFIG_SIZE` | 参数区总大小，4KB。 |
| `BOOT_PARAM_MAGIC` | 参数区有效魔术字。 |
| `BOOT_APP_START_ADDR` | 正式 App 固定起始地址。 |
| `APP_DOWNLOAD_ADDR` | 下载缓存区地址。 |
| `APP_DOWNLOAD_MAX_SIZE` | 下载缓存区最大可容纳的新固件大小。 |
| `FLASH_PAGE_SIZE` | Flash 页大小，当前按 4KB 擦除。 |
| `BOOT_COPY_CHUNK_SIZE` | 搬运时每次处理 1KB，避免占用太多 RAM。 |

### 6.2 再看 `Parameter_t`

`Parameter_t` 不是单个主参数结构，而是把整个 4KB 参数区在 RAM 里拼成一个总结构：

| 成员 | 含义 |
|---|---|
| `BootParam` | 主参数区，BootLoader 运行时真正关心的大部分字段都在这里。 |
| `BootParam_Reserved` | 预留参数区，目前未参与主流程。 |
| `UpdateLog` | 升级日志区。 |
| `UserConfig` | 用户配置区。 |
| `CalibData` | 校准区。 |

### 6.3 `UsrFunction()` 是总调度

可以按下面的顺序读：

| 代码动作 | 你要重点理解什么 |
|---|---|
| `Analysis_ConfigForAddr()` | 为什么一上来先把参数区读到 RAM。 |
| `memcpy(&my_param_sum, config_buf, sizeof(Parameter_t))` | 为什么要把裸字节解释成结构体。 |
| 读取 `appStackAddr / appEntryAddr` | 为什么 App 的真实入口不写死，而是从向量表第 0、1 项取。 |
| `magicWord` 判断 | 为什么先判“参数区是否可信”。 |
| `updateFlag/updateStatus` 判断 | 为什么这就是升级开关。 |
| `Download_Transport()` | 为什么搬运逻辑单独封成函数。 |
| 成功后清标志、更新计数并复位 | 为什么不直接跳新 App。 |
| `jump_to_app()` | 为什么正常路径最终只是“检查后跳转”。 |

## 7. `Download_Transport()` 该怎么理解

它是整个升级流程里最关键的函数。

### 7.1 它做了四件事

| 步骤 | 动作 | 原因 |
|---|---|---|
| 1 | 检查下载区地址、App 大小、App 目标地址 | 防止写错区域，把 BootLoader 或参数区擦掉。 |
| 2 | 按 `appSize` 计算需要擦多少页 | App 变大后不能再假设“固定擦 3 页”这种写法。 |
| 3 | 从下载区分块读到 `app_copy_buf`，再写入正式 App 区 | 避免一次性把整个 App 放到 RAM。 |
| 4 | 从正式 App 区重新读回数据做 CRC32 | 证明“搬运结果”正确，而不仅仅是下载区正确。 |

### 7.2 为什么分块搬运

因为 RAM 是有限的。  
如果每次都申请一个和固件一样大的缓冲区，BootLoader 会很臃肿，也更容易出错。  
现在这份代码每次只搬 1KB，所以更稳、更容易扩展。

## 8. `jump_to_app()` / `iap_load_app()` 为什么这样写

这是 Cortex-M BootLoader 最容易让初学者疑惑的地方。

### 8.1 不能只写一个函数指针直接跳

因为 App 启动不仅仅需要“PC 跳到入口函数”，还需要：

| 必做动作 | 原因 |
|---|---|
| 设置 `VTOR` | 不然中断还会去找 BootLoader 的向量表。 |
| 设置 `MSP` | 不然 App 的栈还是 BootLoader 的栈。 |
| 关中断、关 SysTick | 避免跳转瞬间还响应 BootLoader 的中断。 |
| 清 NVIC 挂起状态 | 避免 App 一启动就收到 BootLoader 遗留中断。 |

### 8.2 这两个函数的分工

| 函数 | 分工 |
|---|---|
| `jump_to_app()` | 站在“业务层”角度，决定当前 App 镜像是否值得跳。 |
| `iap_load_app()` | 站在“CPU 切换现场”角度，真正完成 VTOR/MSP/入口切换。 |

## 9. 这个 BootLoader 当前“具体怎么操作”

### 9.1 正常启动

| 条件 | 结果 |
|---|---|
| 参数区没有升级标志 | BootLoader 直接检查 `0x0800D000` 的 App 向量表是否合法，合法就跳。 |
| App 镜像非法 | BootLoader 打印错误并停在死循环。当前没有串口菜单或回退逻辑。 |

### 9.2 执行一次升级

| 操作顺序 | 说明 |
|---|---|
| 1 | 由旧 App 接收新固件。 |
| 2 | 旧 App 把新固件写入 `0x08073000` 下载区。 |
| 3 | 旧 App 把升级参数写入 `0x0800C000` 参数区。 |
| 4 | 旧 App 触发软件复位。 |
| 5 | BootLoader 检测到升级标志，开始搬运。 |
| 6 | 搬运成功后清标志、更新计数，再复位。 |
| 7 | BootLoader 再次启动，直接跳到新 App。 |

## 10. 你阅读时最容易忽略的几个点

| 容易忽略的点 | 实际含义 |
|---|---|
| `bootloader_config_init()` 没被主流程调用 | 它是“默认参数生成器”，不是“每次上电都执行”的逻辑。 |
| `usart.c` 有接收中断，但主流程没用它升级 | 说明当前升级触发核心不在 BootLoader 串口命令，而在参数区标志。 |
| `BootParam.appEntryAddr` 在启动时又被刷新了一次 | 这是为了始终以当前 App 向量表内容为准，而不是盲信参数区旧值。 |
| `magicWord` 错误时仍然尝试跳 App | 说明参数区无效不等于 App 一定无效，BootLoader 仍会给当前 App 一个启动机会。 |
| 搬运成功后先复位再跳 | 这是为了让新 App 在更干净的上电环境中启动。 |

## 11. 你接下来最推荐的阅读方式

| 轮次 | 做法 |
|---|---|
| 第 1 遍 | 只看 `main.c` 和 `Function.c`，先把流程串起来，不要纠结每个寄存器细节。 |
| 第 2 遍 | 对照 `BootConfig.h` 看每个关键参数字段到底从哪里来、给谁用。 |
| 第 3 遍 | 再看 `rom.c` 和 `iap_load_app()`，理解“怎么擦 Flash”和“怎么正确跳转”。 |
| 第 4 遍 | 最后再看 `usart.c`、`systick.c`、`startup_gd32f407_427.s`，补齐外围支撑层。 |

## 12. 一句话总结

这个 BootLoader 的核心思想不是“自己下载固件”，而是：

> **用参数区做状态同步，用下载区做固件暂存，用 BootLoader 做最终搬运与安全切换。**

只要你抓住这三点，再看代码就不会乱。
