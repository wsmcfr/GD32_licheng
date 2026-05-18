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
| 下载缓存区 | `0x08067000` 起 | 旧 App 先把新固件下载到这里，BootLoader 下次启动时再搬运到正式 App 区。 |

### 2.1 程序是在 Flash 运行，还是在 RAM 运行

当前 Cortex-M / GD32 工程的默认方式是：**代码主要存放在内部 Flash，并由 CPU 直接从 Flash 取指执行**。  
这种方式常叫就地执行，也就是程序不需要先整包复制到 RAM 才能运行。

| 存储区域 | 典型地址 | 主要放什么 |
|---|---:|---|
| 内部 Flash | `0x08000000` 起 | BootLoader/App 代码、向量表、只读常量、已初始化变量的初始值。 |
| SRAM/RAM | `0x20000000` 起 | 全局变量、静态变量、栈、堆、DMA 缓冲区、OTA/CRC 临时缓冲区。 |

因此，`Download_Transport()` 中的分块搬运不是把程序搬到 RAM 里运行，而是：

```text
下载区 Flash -> RAM 中转缓冲 app_copy_buf[1024] -> 正式 App 区 Flash
```

其中 `app_copy_buf` 是 RAM 里的临时工作区；BootLoader 代码本身仍然在 BootLoader Flash 区执行，新 App 最终也在正式 App Flash 区执行。

### 2.2 RAM 在启动和运行时主要做什么

上电后启动文件和运行库会准备 RAM 中的运行环境，典型动作如下：

| 内容 | 启动时怎么处理 | 为什么需要 RAM |
|---|---|---|
| `.data` 已初始化全局/静态变量 | 初始值保存在 Flash，启动时复制到 RAM。 | 变量运行时会被修改，不能只放在只读 Flash。 |
| `.bss` 未初始化全局/静态变量 | 启动时在 RAM 清零。 | C 语言要求未初始化的静态存储期变量默认是 0。 |
| 栈 `stack` | MSP 指向 RAM 栈顶。 | 函数调用、局部变量、中断现场保存都依赖栈。 |
| 堆 `heap` | 预留一段 RAM 给动态分配。 | `malloc()` 这类动态申请释放只能在可读写内存中完成。 |
| DMA/通信缓冲 | 由代码定义在 RAM。 | USART、SPI、DMA 等外设需要直接读写缓冲区。 |

所以可以把内部 Flash 理解成“掉电不丢的程序存储区”，把 RAM 理解成“运行时工作台”。  
Flash 适合保存代码和固件，RAM 适合保存会变化、需要频繁读写的数据。

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
| 2 | 把新固件写到 `0x08067000` 开始的下载缓存区。 |
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
| 4 | `Download_Transport()` | 分块从 `0x08067000` 读新固件，再写到 `0x0800D000`。 |
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

### 6.3 为什么参数区结构体要写 `typedef struct __attribute__((packed))`

BootLoader 参数区本质上是一段固定地址的二进制数据，不是普通只在 RAM 里临时使用的结构体。  
因此结构体字段必须和 Flash 中的字节偏移严格一致，App 写入和 BootLoader 读取时才不会错位。

| 写法 | 含义 |
|---|---|
| `typedef` | 给结构体类型起一个别名，后续可以直接用 `BootParam_t`、`Parameter_t` 声明变量。 |
| `struct` | 定义一个结构体，把多个字段按顺序组织成一块内存布局。 |
| `Parameter_SUM` | 结构体标签名，也就是 `struct Parameter_SUM` 这种完整写法里的名字。 |
| `Parameter_t` | `typedef` 生成的类型别名，工程里通常直接使用这个名字。 |
| `__attribute__((packed))` | GCC/ArmClang 风格的编译器属性，要求结构体成员紧凑排列，不让编译器自动插入对齐填充字节。 |

不加 `packed` 时，编译器为了让 16 位、32 位变量按更高效的地址对齐，可能在成员之间插入看不见的填充字节。例如：

```c
typedef struct
{
    uint8_t  flag;
    uint32_t value;
} Test_t;
```

普通结构体可能被排成这样：

```text
offset 0: flag
offset 1~3: 编译器填充字节
offset 4~7: value
```

加上 `__attribute__((packed))` 后，结构体会紧凑排列：

```text
offset 0: flag
offset 1~4: value
```

这个 BootLoader 里必须关心字段偏移，因为 `BootParam_t` 注释里已经明确规划了固定位置：

| 字段 | 固定偏移 | BootLoader 用途 |
|---|---:|---|
| `magicWord` | `[0-3]` | 判断参数区是否有效。 |
| `updateFlag` | `[16]` | 判断是否存在待升级任务。 |
| `updateStatus` | `[18]` | 判断当前是否应进入搬运流程。 |
| `appSize` | `[32-35]` | 决定要擦写和搬运多少字节。 |
| `appCRC32` | `[36-39]` | 搬运后校验正式 App 区内容。 |

如果 App 侧和 BootLoader 侧因为结构体填充导致字段偏移不一致，就可能出现 App 明明写了 `updateFlag=0x5A`，BootLoader 却在错误位置读取，最终表现为“不升级”或读取到错误的 App 大小、CRC。

所以这里使用 `packed` 的核心原因是：

> 参数区是 App 与 BootLoader 共享的 Flash 二进制协议，字段偏移必须稳定，不能交给编译器自由插入填充字节。

需要注意的是，`packed` 也有代价：字段可能不按 2 字节或 4 字节对齐，访问效率可能下降，某些架构还可能不支持非对齐访问。当前工程使用它是因为参数区字段布局的确定性比普通结构体访问效率更重要。

### 6.4 `UsrFunction()` 是总调度

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

### 8.3 最后四句跳转代码分别是什么意思

`iap_load_app()` 前面会先校验 App 的 MSP 和 Reset_Handler，再关闭中断、关闭 SysTick、清理 NVIC。  
真正把执行权交给 App 的核心动作可以简化成下面四句：

```c
SCB->VTOR = 0x0800D000;
jump2app = (pFunction)entry_addr;
__set_MSP(stack_addr);
jump2app();
```

| 代码 | 作用 | 为什么必须这样做 |
|---|---|---|
| `SCB->VTOR = 0x0800D000;` | 把中断向量表基地址切到 App 区。 | MCU 复位后默认向量表在 BootLoader 起点 `0x08000000`。如果不切到 `0x0800D000`，App 运行后的 SysTick、USART、DMA、HardFault 等中断仍会按 BootLoader 向量表找入口。 |
| `jump2app = (pFunction)entry_addr;` | 把 App 向量表第 1 项读出的 Reset_Handler 地址转换成函数指针。 | `entry_addr` 是一个 32 位地址值，BootLoader 需要把它当成 `void (*)(void)` 类型的函数入口，后面才能用 `jump2app()` 跳过去执行。 |
| `__set_MSP(stack_addr);` | 把主栈指针 MSP 改成 App 向量表第 0 项记录的初始栈顶。 | App 不能继续使用 BootLoader 的栈。C 运行库初始化、函数调用、局部变量和中断入栈都依赖 App 自己的栈空间。 |
| `jump2app();` | 跳到 App 的 Reset_Handler。 | 这一步真正交出执行权，App 随后会执行自己的启动代码，完成 `.data` 初始化、`.bss` 清零，再进入 App 的 `main()`。 |

这里的逻辑是在用软件手动模拟 Cortex-M 复位启动 App 的动作：

```text
硬件复位正常做：
  MSP = *(向量表 + 0)
  PC  = *(向量表 + 4)

BootLoader 软件跳转手动做：
  SCB->VTOR = App 起始地址
  __set_MSP(*(App 起始地址 + 0))
  跳到 *(App 起始地址 + 4)
```

代码里先给 `jump2app` 赋值，再调用 `__set_MSP()`，是为了减少切换栈之后继续依赖 BootLoader 当前函数局部变量的风险。MSP 一旦改成 App 的栈，后续就应该尽快跳进 App，不要再在 BootLoader 当前调用栈里做复杂逻辑。

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
| 2 | 旧 App 把新固件写入 `0x08067000` 下载区。 |
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
