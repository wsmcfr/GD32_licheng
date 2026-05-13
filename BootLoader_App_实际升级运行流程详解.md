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
| 正式 App 区 | `0x0800D000 ~ 0x0806FFFF` | 当前真正运行的 App | BootLoader |
| 下载缓存区 | `0x08073000 ~ 0x0807FFFF` | 暂存“下一版新固件” | App |

最关键的区别：

| 区域 | 含义 |
|---|---|
| `0x0800D000` | 当前要运行的正式 App |
| `0x08073000` | 新固件的临时缓存区，还不能直接运行 |

---

## 3. 整体时序

### 3.1 一次成功升级的完整链路

| 顺序 | 执行方 | 动作 | 结果 |
|---|---|---|---|
| 1 | 上位机 | 发送 START 帧 | 告诉 App：接下来要升级，固件多大、CRC 是多少、版本号是多少 |
| 2 | App | 校验 START，擦下载缓存区 | 为接收新固件腾出干净空间 |
| 3 | 上位机 | 逐帧发送 DATA | 每帧携带 `seq`、`offset`、`length`、`chunkCRC32` |
| 4 | App | 每收一帧就写到 `0x08073000 + offset` | 新固件逐步写入下载缓存区 |
| 5 | 上位机 | 发送 END 帧 | 告诉 App：固件发送结束 |
| 6 | App | 校验整包 CRC、回读下载区 CRC、写参数区 | 告诉 BootLoader“新固件已准备好” |
| 7 | App | 软件复位 | 控制权交还 BootLoader |
| 8 | BootLoader | 启动后读取参数区 | 看到 `updateFlag=0x5A`、`updateStatus=0x01` |
| 9 | BootLoader | 把 `0x08073000` 搬到 `0x0800D000` | 新固件变成正式 App |
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
| 4 | 检查固件大小是否超过 `52KB` | 下载缓存区上限就是 `52KB` |
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
| 5 | 立即写 `0x08073000 + offset` | 不占用大 RAM，支持低内存 OTA |
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
| 4 | 从 `0x08073000` 分块读 | 从下载缓存区取新固件 |
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

---

## 10. 最容易混淆的几个问题

| 问题 | 正确答案 |
|---|---|
| App 有没有把新固件写到 `0x0800D000`？ | 没有。App 只写 `0x08073000` 下载缓存区。 |
| 谁真正负责搬运到正式 App 区？ | BootLoader 的 `Download_Transport()`。 |
| App 写参数区的意义是什么？ | 通知 BootLoader“下载区新固件已准备好，你下次启动来搬运”。 |
| 为什么升级成功后 App 要复位？ | 因为后续搬运和正式切换是 BootLoader 的职责。 |
| 为什么 BootLoader 搬运成功后还要再复位一次？ | 让新 App 在更干净、更标准的启动环境中运行。 |
| BootLoader 跳过去以后，App 为什么还要重新设置 VTOR 和开中断？ | 因为普通函数跳转不会自动恢复 PRIMASK，也不能假设所有现场都已经完全属于 App。 |

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
