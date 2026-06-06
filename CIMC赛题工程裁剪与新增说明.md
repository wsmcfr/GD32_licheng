# CIMC 赛题工程裁剪与新增说明

## 1. 目标口径

本文档按 2026 年 CIMC 工业嵌入式系统开发初赛赛题，对当前两个工程做裁剪和新增说明：

| 工程 | 路径 | 角色 |
|---|---|---|
| App 工程 | `D:\GD32\2026706296` | 实现采样、协议、参数、告警、自动上报、睡眠、进入升级请求 |
| Bootloader 工程 | `D:\GD32\2026706296_bootloader` | 实现启动等待、RS485 升级接收、固件校验、搬运、跳转 App |

精简原则如下：

| 原则 | 说明 |
|---|---|
| 只保留评分需要的外设 | USART1/RS485、OLED、2 个 LED、ADC、DAC、RTC、PT100 外部 ADC、内部 Flash 参数区 |
| 删除演示型功能 | USART0 文本 Shell、SMARTFS 文件系统、自定义 OTA 头部 BIN、按键低功耗演示、多余 LED 演示 |
| 删除多余串口 | 正式评测只使用 USART1/RS485；USART0 和 USART5 均从正式业务、初始化和编译链路删除 |
| 删除外部 Flash 文件系统 | 赛题只要求参数和告警持久化，不要求目录、文件、`ls/cat/write` 等文件系统能力 |
| 保留可复用底层 | ADC、DAC、RTC、OLED、PT100、内部 Flash 擦写、RS485 方向控制可继续复用 |

## 1.1 当前代码已执行的裁剪状态

| 类别 | 当前状态 |
|---|---|
| LED | Keil 工程仅编译 LED1/LED2 业务，LED1 1s 系统闪烁，LED2 跟随自动采集状态 |
| 串口 | App 和 Bootloader 正式通信均为 USART1/RS485，默认 19200；USART0/USART5 不再编译为业务串口 |
| 外部 Flash | SMARTFS/littlefs/GD25QXX 已从源码树和 Keil 工程移除，正式版当前只用内部 Flash 参数区 |
| 按键 | `btn_task`、KEY 驱动和 EXTI0 按键唤醒已从源码树和 Keil 工程移除 |
| OLED | App 只显示两行：队伍编号和 `AutoSample/IDLE` |
| DAC | `0x0301` 已接入 USART1/RS485 协议，内容 `0000~0FFF` 写 DAC0 OUT0 |
| OTA | App 旧自定义 OTA 已移除；App `0x0501` 回复 OK 后复位进 Bootloader；Bootloader 支持 `0x0502/0x0503` 和 bin 魔术字 `5AA5C33C` |
| Bootloader | 普通上电静默 5s 后跳 App；只有 App 写入 `updateStatus=0x02` 后才等待 10s 升级命令 |

## 2. App 工程当前应删除或停用的内容

### 2.1 串口相关

| 当前内容 | 文件 | 建议处理 | 原因 |
|---|---|---|---|
| USART0 SMARTFS 文本命令壳 | `Function/usart_app.c`、`Function/usart_app.h` | 已改写为 USART1/RS485 赛题协议入口 | 赛题不使用 `help/gettime/settime/ls/cat/write/mkdir/rm/df` 文本命令；评分协议必须走 USART1/RS485 的 ASCII 十六进制帧 |
| USART0 DMA 接收 | `Driver/USART/bsp_usart.c`、`User/gd32f4xx_it.c` | 正式版删除或用宏关闭 | USART0 不是评分通道，接收中断和 DMA 会增加复杂度 |
| USART0 调试输出 | `DEBUG_USART`、`my_printf()` 调用点 | 已删除；App 不再提供调试输出 API，C 库输出桩直接丢弃 | 避免非协议文本污染 USART1/RS485 评分链路 |
| USART5 | `Driver/USART/bsp_usart.*` 中 USART5 宏、缓冲区、初始化函数 | 删除 | 赛题没有 USART5 需求 |
| USART1 当前 OTA 裸流接收 | `Function/uart_ota_app.c`、`User/gd32f4xx_it.c` | 已从 App 编译项移除，USART1 改为比赛协议帧接收 | 当前逻辑只识别自定义 `Project_ota.bin` 裸流，不符合 `A5B6...B6A5` 协议 |

正式版串口只建议保留：

| 串口 | 用途 | 默认波特率 | 是否必须 |
|---|---|---:|---|
| USART1 + RS485 | 上位机自动评分、升级、命令收发 | 19200 | 必须 |
| USART0 | 已从正式业务删除，无调试输出 API | 无 | 不参与评分 |
| USART5 | 无 | 无 | 删除 |

### 2.2 OTA 和协议相关

| 当前内容 | 文件 | 建议处理 | 原因 |
|---|---|---|---|
| 自定义 OTA 应用层 | `Function/uart_ota_app.c`、`Function/uart_ota_app.h` | 已从 Keil 编译项移除 | 赛题升级流程是 `0x0501/0x0502/0x0503`，不是 App 启动后发 `OTA485: ready` 再裸发自定义包 |
| 自定义 OTA 镜像协议 | `Protocol/ota_image_protocol.c`、`Protocol/ota_image_protocol.h` | 删除 | 当前魔术字和 64 字节头部不符合大赛 bin 格式 |
| OTA 打包工具 | `tools/pack_ota_image.c`、`tools/pack_ota_image.exe`、`tools/test_header_bin_ota_static.py` | 已从正式仓库删除 | 大赛下发固件包，不需要本工程自定义头部包，也不再保留旧方案静态测试 |
| Keil After Build 打包命令 | `project/2026706296.uvprojx` | 删除 `pack_ota_image.exe` 调用 | 避免生成和提交误导性 `Project_ota.bin` |

当前新增的比赛协议链路：

| 工程 | 命令 | 当前处理 |
|---|---|---|
| App | `0x0501` | 校验帧后回复 OK，写入 Bootloader 等待标志 `updateStatus=0x02`，随后软件复位 |
| Bootloader | `0x0502` | 接收不封帧 bin，校验前 4 字节魔术字 `5AA5C33C`，把魔术字后的 App payload 写入 `0x08051000` |
| Bootloader | `0x0503` | 先回复 OK，再复用备份/搬运/CRC/回滚核心，把下载区 payload 搬到 `0x08011000` |

### 2.3 外部 Flash 与文件系统

| 当前内容 | 文件 | 建议处理 | 原因 |
|---|---|---|---|
| SMARTFS 文件系统 | `Driver/GD25QXX/smartfs_port.c`、`Driver/GD25QXX/smartfs_port.h` | 删除或不编译 | 赛题只需要参数和告警持久化，不需要目录和文件操作 |
| littlefs | `Driver/GD25QXX/lfs.c`、`lfs.h`、`lfs_util.c`、`lfs_util.h` | 删除或不编译 | 文件系统体积大、接口复杂，评分项不需要 |
| SMARTFS 启动自检 | `SMART_STORAGE_BOOT_SELF_TEST_ENABLE` | 删除或置 0 | 启动时格式化/自检会影响上电时序 |
| USART0 文件系统 Shell | `Function/usart_app.c` | 删除 | 与 SMARTFS 绑定，比赛无用 |

推荐持久化方案：

| 方案 | 建议 | 说明 |
|---|---|---|
| 内部 Flash 参数区 | 推荐 | 使用 `0x08010000` 参数区里的 `UserConfig` 或预留区保存比赛参数和最近 10 条告警 |
| 外部 Flash 裸扇区 | 可选 | 如果想证明 SPI Flash 存储能力，可保留 `gd25qxx.c/h`，但只做简单原始扇区读写，不上文件系统 |
| 外部 Flash 文件系统 | 不推荐 | 对赛题过重，增加启动耗时、代码体积和调试风险 |

如果选择内部参数区保存比赛参数，必须保证 App 和 Bootloader 使用完全一致的 4KB 参数区布局，不能随意改 `BootParam_t` 前 256 字节字段偏移。

### 2.4 LED 与按键

| 当前内容 | 文件 | 建议处理 | 原因 |
|---|---|---|---|
| 6 路 LED 状态数组 `ucLed[6]` | `Function/led_app.c` | 替换为 2 个明确状态灯 | 赛题只要求系统状态灯和采集工作灯 |
| LED3~LED6 业务逻辑 | `Function/led_app.c`、`Function/btn_app.c` | 删除 | 自动评分不需要 |
| 按键扫描和按键触发睡眠 | `Driver/KEY/*`、`Function/btn_app.*` | 删除或不编译 | 赛题睡眠由串口命令 `0x03AA` 触发，不由按键触发 |
| KEYW/EXTI 唤醒链路 | `Driver/KEY/*`、`Driver/POWER/bsp_power.c` | 删除或重写 | 赛题要求 RTC 闹钟 10s 自动唤醒，不是按键唤醒 |

正式版 LED 建议只用两个：

| LED | 建议映射 | 赛题含义 | 行为 |
|---|---|---|---|
| LED1 | `PD10` | 系统状态指示灯 | 进入 App 后每 1s 翻转一次 |
| LED2 | `PD11` | 采集工作指示灯 | 自动采集上报期间常亮，其余熄灭 |

`Driver/LED/bsp_led.*` 可以保留底层 GPIO 宏，但 `Function/led_app.*` 应改成只控制 LED1 和 LED2。

### 2.5 OLED 显示

| 当前内容 | 文件 | 建议处理 | 原因 |
|---|---|---|---|
| 显示按键状态 | `Function/oled_app.c` | 删除 | 赛题不要求 |
| 显示 `uwTick` | `Function/oled_app.c` | 删除 | 赛题不要求 |
| 显示 ADC 电压调试值 | `Function/oled_app.c` | 删除 | 赛题不要求 |
| 显示 RTC 第 4 行 | `Function/rtc_app.c` | 删除或并入调试宏 | 赛题要求 OLED 始终双行 |

正式版 OLED 只显示两行：

| 行号 | 内容 | 来源 |
|---|---|---|
| 第 1 行 | 队伍编号 | 固定宏，例如 `CIMC_TEAM_ID_TEXT` |
| 第 2 行 | `Bootloader` / `AutoSample` / `IDLE` | 当前运行区域和 App 状态 |

App 区显示规则：

| App 状态 | OLED 第二行 |
|---|---|
| 自动采集上报中 | `AutoSample` |
| 其他状态 | `IDLE` |

Bootloader 区显示规则：

| Bootloader 状态 | OLED 第二行 |
|---|---|
| Bootloader 正在运行 | `Bootloader` |

### 2.6 ADC、DAC、PT100

| 当前内容 | 文件 | 建议处理 | 原因 |
|---|---|---|---|
| `adc_task()` 把 CH0 直接写到 DAC | `Function/adc_app.c` | 删除该直通逻辑 | 赛题 DAC 输出由 `0x0301` 命令设置，不应被电位器自动覆盖 |
| ADC0 双通道 DMA | `Driver/ANALOG/bsp_analog.*` | 保留 | CH0 电位器、CH1 DAC 回读需要 |
| DAC 初始化 | `Driver/ANALOG/bsp_analog.*` | 保留 | `0x0301` 设置 DAC 输出需要 |
| PT100 采样换算 | `Function/gd30ad3344_pt100_app.*`、`Driver/GD30AD3344/*` | 保留并接入协议查询 | CH2 外部 ADC/PT100 必须测 |
| PT100 调试日志 | `gd30ad3344_pt100_task()` 中调试文本 | 已删除 | 自动评分不需要串口调试日志 |

正式版应新增统一采样接口：

| 通道 | 数据来源 | 返回值 |
|---|---|---|
| CH0 | 板载电位器 ADC 原始值换算 | `原始采样值 * CH0 变比` 的 float 大端 |
| CH1 | DAC 输出回读 ADC 换算 | `原始采样值 * CH1 变比` 的 float 大端 |
| CH2 | GD30AD3344 + PT100 换算温度 | 温度 float 大端 |

### 2.7 低功耗

| 当前内容 | 文件 | 建议处理 | 原因 |
|---|---|---|---|
| KEY1/KEY2/KEY3 触发 Sleep/DeepSleep/Standby | `Function/btn_app.c` | 删除 | 赛题通过串口命令触发睡眠 |
| KEYW/EXTI 唤醒 | `Driver/POWER/bsp_power.c` | 删除或不用 | 赛题要求 RTC 闹钟 10s 自动唤醒 |
| 复杂 Standby 确认流程 | `Driver/POWER/bsp_power.c` | 删除 | 自动评分不需要人工按键确认 |

正式版只需要一个睡眠接口：

| 接口 | 行为 |
|---|---|
| `cimc_power_enter_rtc_sleep_10s()` | 回复 OK 后进入 MCU 深度睡眠，RTC 闹钟 10s 唤醒，唤醒后 USART1/RS485 输出字符串 `instrument wakeup` |

## 3. App 工程必须新增的内容

### 3.1 推荐新增文件

| 新文件 | 层级 | 作用 |
|---|---|---|
| `Protocol/cimc_protocol.h` | Protocol | 定义帧格式、命令字、帧类型、错误码、CRC16 接口 |
| `Protocol/cimc_protocol.c` | Protocol | ASCII HEX 解码/编码、CRC-16-Modbus、帧校验、应答组帧 |
| `Function/cimc_params.h` | Function | 定义设备 ID、波特率、变比、阈值、告警配置、版本号 |
| `Function/cimc_params.c` | Function | 参数区加载、默认值、写回、CRC 校验 |
| `Function/cimc_sample.h` | Function | 定义 CH0/CH1/CH2 采样接口 |
| `Function/cimc_sample.c` | Function | ADC/DAC/PT100 数据换算、变比生效、阈值判断 |
| `Function/cimc_alarm.h` | Function | 定义告警记录结构和接口 |
| `Function/cimc_alarm.c` | Function | 最近 10 条告警记录、主动上报、查询、清空 |
| `Function/cimc_device.h` | Function | 定义设备运行状态、自动上报状态 |
| `Function/cimc_device.c` | Function | 命令分发、自动上报、睡眠、重启、升级请求 |
| `Function/cimc_display.h` | Function | 定义 OLED 状态接口 |
| `Function/cimc_display.c` | Function | 双行 OLED 显示和状态刷新 |
| `Function/cimc_led.h` | Function | 定义两个赛题 LED |
| `Function/cimc_led.c` | Function | 1s 系统灯闪烁、自动采集灯控制 |

### 3.2 必须实现的协议命令

| 分类 | 命令 | 必须实现 |
|---|---|---|
| 系统管理 | `0x0101` 重启 | 是 |
| 系统管理 | `0x0104` 查询固件版本 | 是，初始版本 `2.0.1.0` |
| 系统管理 | `0x0105` 设置时间 | 是 |
| 系统管理 | `0x0106` 查询时间 | 是 |
| 系统管理 | `0x0111` 查询设备 ID | 是 |
| 系统管理 | `0x0112` 查询波特率 | 是 |
| 系统管理 | `0x01A1` 设置设备 ID | 是，持久化 |
| 系统管理 | `0x01A2` 设置波特率 | 是，先 OK 后切换并持久化 |
| 数据类 | `0x0201` 查询 CH0 | 是 |
| 数据类 | `0x0202` 查询 CH1 | 是 |
| 数据类 | `0x0221` 查询 CH2 PT100 | 是 |
| 数据类 | `0x0241` 设置 CH0 变比 | 是，立即生效并持久化 |
| 数据类 | `0x0242` 设置 CH1 变比 | 是，立即生效并持久化 |
| 数据类 | `0x0261` 设置上报间隔 | 是 |
| 控制类 | `0x0301` 设置 DAC | 是 |
| 控制类 | `0x0302` 开始自动上报 | 是 |
| 控制类 | `0x0303` 停止自动上报 | 是 |
| 控制类 | `0x03AA` 睡眠 10s | 是 |
| 参数类 | `0x0400` 读取 CH0/CH1 阈值 | 是 |
| 参数类 | `0x0401` 读取 CH0 阈值 | 是 |
| 参数类 | `0x0402` 读取 CH1 阈值 | 是 |
| 参数类 | `0x0411` 写 CH0 阈值 | 是，持久化 |
| 参数类 | `0x0412` 写 CH1 阈值 | 是，持久化 |
| 升级类 | `0x0501` 升级请求 | 是，回复 OK 后复位进 Bootloader |
| 告警类 | `0x0601` 是否主动上报告警 | 是 |
| 告警类 | `0x0602` 查询告警记录 | 是，字符串输出 |
| 告警类 | `0x0603` 清除告警 | 是 |
| 特殊帧 | `0xFFFF` 广播寻找设备 | 是 |
| 特殊帧 | `0x8888` 心跳/上线通知 | 是 |
| 错误帧 | `0xEEEE` 错误应答 | 是 |

## 4. Bootloader 工程应删除或停用的内容

| 当前内容 | 文件 | 建议处理 | 原因 |
|---|---|---|---|
| 上电立即打印 BootLoader 调试日志 | `Function/Function.c` | 正式版删除或调试宏关闭 | 赛题要求无升级请求时不输出 |
| 固定 `delay_1ms(1000)` 调试窗口 | `Function/Function.c` | 删除 | 赛题要求无升级请求时静默 5s 后跳 App |
| 只初始化 USART0 | `Driver/USART/bsp_usart.*` | 删除 USART0 初始化，正式版只初始化 USART1/RS485 | 大赛通信和升级必须走 USART1/RS485 |
| 只依赖参数区搬运 | `Function/Function.c` | 保留搬运逻辑，但新增串口接收固件逻辑 | 赛题要求 Bootloader 接收大赛 bin |
| 未编译的 USB/FatFS 库目录 | `Library/GD32F4xx_usb_library`、`Library/Third_Party/fat_fs` | 可从提交包中移除 | 当前 Bootloader 工程未使用，减少压缩包体积和干扰 |
| Bootloader LED 驱动 | `Driver/LED/*` | 可删除 | 赛题 Bootloader 不要求 LED，只要求 OLED 显示 `Bootloader` |

## 5. Bootloader 工程必须新增的内容

| 新增内容 | 建议位置 | 说明 |
|---|---|---|
| USART1/RS485 驱动 | `Driver/USART/bsp_usart.*` | 复用 App 的 PD5/PD6/PE8 映射，默认 19200 |
| RS485 方向控制 | `Driver/USART/bsp_usart.*` | 发送前拉发送态，发送完成后回接收态 |
| 比赛协议解析 | `Protocol/cimc_protocol.*` 或 Bootloader 内精简版 | 至少支持 `0x0502`、`0x0503`、错误帧 |
| 固件接收状态机 | `Function/Function.c` 或新增 `Function/boot_ota.c` | `0x0502` 后约 500ms 接收 bin，按 256B 切片写入暂存区 |
| 魔术字校验 | Bootloader OTA 模块 | 大赛 bin 前 4 字节必须为 `5AA5C33C`，错误回复 ERROR 帧 |
| 执行升级命令 | Bootloader OTA 模块 | 收到 `0x0503` 先回复 OK，再搬运暂存区到 App 区 |
| 10s 倒计时提示 | Bootloader 主流程 | 只有 App 发升级请求后重启进 Bootloader 才输出指定字符串 |
| 5s 静默跳转 | Bootloader 主流程 | 普通上电无升级请求时不输出，等待 5s 后跳 App |
| OLED Bootloader 显示 | Bootloader OLED 驱动或精简 I2C 显示 | Bootloader 区域 OLED 第二行显示 `Bootloader` |

Bootloader 当前已有的搬运和校验逻辑可以继续复用：

| 当前函数 | 是否保留 | 用途 |
|---|---|---|
| `Backup_Transport()` | 保留 | 升级前备份旧 App |
| `Download_Transport()` | 保留并调整入口条件 | 把暂存区固件搬运到 App 区 |
| `Restore_Backup_To_App()` | 保留 | 升级失败时回滚旧 App |
| `iap_load_app()` | 保留 | 跳转 App 前设置 VTOR/MSP、清中断 |
| `internal_flash_*()` | 保留 | 内部 Flash 擦写读 |

## 6. Keil 工程移除清单

### 6.1 App 工程建议从 `project/2026706296.uvprojx` 移除

| 分组 | 文件 | 处理 |
|---|---|---|
| Protocol | `Protocol/ota_image_protocol.c` | 移除 |
| Function | `Function/uart_ota_app.c` | 移除 |
| Function | `Function/usart_app.c` | 保留，但已改为 USART1/RS485 帧缓存和 `cimc_protocol` 调用入口 |
| Function | `Function/btn_app.c` | 移除 |
| Driver | `Driver/GD25QXX/smartfs_port.c` | 移除 |
| Driver | `Driver/GD25QXX/lfs.c` | 移除 |
| Driver | `Driver/GD25QXX/lfs_util.c` | 移除 |
| Driver | `Driver/KEY/bsp_key.c` | 移除 |
| Driver | `Driver/POWER/bsp_power.c` | 删除或替换为精简 RTC 睡眠模块 |
| Build After | `tools/pack_ota_image.exe ...` | 移除 |
| Protocol | `Protocol/cimc_protocol.c` | 新增并编译 |

当前正式版选择内部参数区保存参数和升级交接信息，因此同时移除：

| 文件 | 处理 |
|---|---|
| `Driver/GD25QXX/gd25qxx.c`、`gd25qxx.h` | 移除 |
| `Driver/GD25QXX/*` 全目录 | 移除 |
| `Driver/STORAGE/bsp_storage.c`、`bsp_storage.h` | 仅保留 GD30AD3344 初始化，不能再包含 GD25QXX 初始化 |

如果后续确实要用外部 Flash 裸扇区保存告警，需要重新评审容量、掉电一致性和评分收益，再只引入最小裸读写驱动；不得恢复 SMARTFS/littlefs 文件系统。

### 6.2 Bootloader 工程建议从 `project/2026706296.uvprojx` 移除

| 分组 | 文件/目录 | 处理 |
|---|---|---|
| Driver | `Driver/LED/bsp_led.c` | 不用 LED 时移除 |
| Protocol | `Protocol/boot_cimc_protocol.c` | 新增并编译，用于 0x0502/0x0503 |
| Library | USB 相关库 | 未编译可从提交包删除 |
| Library | FatFS 相关库 | 未编译可从提交包删除 |
| USART | USART0 调试输出 | 已删除；正式版只初始化 USART1/RS485，C 库输出桩直接丢弃 |

## 7. 推荐最终 App 任务表

当前 `scheduler_task[]` 需要从演示任务表改成比赛任务表。

| 任务 | 周期 | 作用 |
|---|---:|---|
| `uart_task` + `cimc_protocol_process_ascii_frame` | 5ms | 处理 USART1/RS485 接收帧和命令分发 |
| `cimc_sample_task` | 50ms~200ms | 更新 CH0/CH1/CH2 缓存，检查阈值 |
| `cimc_auto_report_task` | 100ms | 判断是否到达 1s/3s/5s 上报时间 |
| `cimc_led_task` | 20ms | 系统 LED 1s 闪烁，采集 LED 状态输出 |
| `cimc_display_task` | 100ms | OLED 双行显示 |
| `rtc_task` | 可并入采样或协议 | 只提供时间读取，不再单独显示第 4 行 |

应删除的任务：

| 当前任务 | 处理 |
|---|---|
| `btn_task` | 删除 |
| `uart_task` | 保留为正式协议入口，不再做 USART0/SMARTFS Shell |
| `uart_ota_task` | 删除 |
| 当前 `oled_task` | 替换 |
| 当前 `led_task` | 替换 |
| 当前 `adc_task` | 替换为采样/DAC 命令逻辑 |

## 8. 推荐最终外设占用

| 外设 | 是否保留 | 用途 |
|---|---|---|
| USART1 + RS485 | 保留 | 唯一正式通信通道 |
| USART0 | 删除 | 正式版不初始化、不接收、不输出；旧源码仅可作为未编译残留 |
| USART5 | 删除 | 无评分用途 |
| ADC0 + DMA | 保留 | CH0 电位器、CH1 DAC 回读 |
| DAC0 OUT0 | 保留 | `0x0301` 设置输出 |
| RTC | 保留 | 时间戳、设置时间、睡眠唤醒 |
| OLED/I2C | 保留 | 双行状态显示 |
| LED1/LED2 | 保留 | 系统状态灯、采集状态灯 |
| LED3~LED6 | 删除业务使用 | 赛题不需要 |
| KEY1~KEY6/KEYW | 删除 | 赛题不需要按键操作 |
| GD30AD3344/SPI | 保留 | PT100 外部 ADC |
| GD25QXX/SPI Flash | 删除 | 当前正式版不使用外部 Flash；后续如需裸存储必须重新评审后最小化引入 |
| SMARTFS/littlefs | 删除 | 赛题不需要文件系统 |

## 9. 推荐实施顺序

| 顺序 | 动作 | 验证方法 |
|---:|---|---|
| 1 | App 删除 USART0 Shell、自定义 OTA、SMARTFS/littlefs、按键任务、USART5 | Keil 编译通过，App 仍能启动 |
| 2 | App 新增 `cimc_protocol.*`，实现 ASCII HEX 帧和 CRC16 | 用串口工具发送广播寻址帧，收到心跳帧 |
| 3 | App 新增参数模块，默认波特率 19200、版本 2.0.1.0、默认 ID | 查询 ID、波特率、版本正确 |
| 4 | App 接入 CH0/CH1/CH2、DAC、阈值、变比 | 上位机单次查询数据合法 |
| 5 | App 实现自动上报和告警 | 自动上报期间只响应停止命令，告警记录可查可清 |
| 6 | App 实现睡眠 10s 和唤醒字符串 | 睡眠后 10s 输出 `instrument wakeup` |
| 7 | Bootloader 新增 USART1/RS485 和比赛协议 | `0x0502` / `0x0503` 能被 Bootloader 正确识别 |
| 8 | Bootloader 实现大赛 bin 接收、魔术字校验、搬运 | 错误固件返回 ERROR，正确固件升级成功 |
| 9 | 清理所有正式版调试输出和文档 | 无升级请求时 Bootloader 静默，App 只按协议输出 |

## 10. 最终结论

正式评测版不应该继续保留“演示大而全”的结构。建议按下面规则裁剪：

| 类别 | 最终建议 |
|---|---|
| LED | 只用 2 个，LED1 系统 1s 闪烁，LED2 自动采集常亮 |
| 串口 | 只用 USART1/RS485 做正式通信，USART0/USART5 删除 |
| 外部 Flash | 文件系统删除；如不需要外部裸存储，整个 GD25QXX 也可删除 |
| 按键 | 删除，不参与自动评分 |
| OLED | 只保留双行显示 |
| OTA | 删除自定义 OTA，改成大赛 `0x0501/0x0502/0x0503` 流程 |
| 参数 | 使用内部参数区或外部 Flash 裸记录，不使用文件系统 |
| Bootloader | 保留搬运/回滚核心，新增 RS485 协议接收和大赛 bin 校验 |
