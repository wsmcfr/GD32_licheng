# 当前工程作为 BootLoader App 的接入说明

## 1. 当前目标

| 项目 | 说明 |
|---|---|
| 当前工程角色 | App 工程，不再从 `0x08000000` 独立启动 |
| 参考 BootLoader | `D:\GD32\04 例程模板\04 例程模板\27_BootLoader_Two_Stage` |
| App 链接地址 | `0x0800D000` |
| App 最大运行区 | `0x00063000` |
| 当前已完成内容 | 当前工程已经按 App 地址链接，并在启动时重定位中断向量表；复制到本工程内的 BootLoader 已按当前 App 优化搬运逻辑；App 侧已支持 USART0 接收 `.uota` 升级包 |
| 仍需注意内容 | 首次导入 OTA 功能时仍需先用 SWD/烧录器把带 OTA 接收逻辑的 App 写到 `0x0800D000`；后续升级发送 `Project.uota`，不要直接发送 `Project.bin` |

## 2. Flash 分区关系

| 分区 | 起始地址 | 大小 | 用途 |
|---|---:|---:|---|
| BootLoader 区 | `0x08000000` | `48KB` | MCU 复位后先运行 BootLoader |
| 参数区 | `0x0800C000` | `4KB` | 保存升级标志、App 地址、App 大小、CRC 等参数 |
| App 区 | `0x0800D000` | `0x63000` | 当前工程的链接和运行地址 |
| 下载缓存区 | `0x08073000` | `52KB` | 旧 App 接收新固件后，先把固件写到这里 |

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
| `USER/App/usart_app.c` | 新增 USART0 `.uota` 升级包解析、CRC 校验、下载缓存区写入和参数区写入 | App 收到完整升级包后写 `0x08073000` 和 `0x0800C000`，再复位交给 BootLoader 搬运 |
| `tools/make_uart_ota_packet.py` | 根据 `Project.bin` 生成 `Project.uota` | 给串口助手发送文件使用，自动写入包头、固件长度和 CRC32 |

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
| 运行中升级 App | 当前工程已支持 USART0 `.uota` 单包升级 | App 接收 `.uota`，写入 `0x08073000` 和参数区，再复位交给 BootLoader 搬运 |

也就是说，官方 Two Stage 例程里，BootLoader 本身不是直接串口收文件的程序；串口接收升级包的逻辑属于 App 侧。本工程已在当前 App 的 `USER/App/usart_app.c` 中实现 USART0 `.uota` 接收。

## 6. 当前工程输出文件怎么用

| 输出文件 | 用途 |
|---|---|
| `MDK/output/Project.axf` | Keil 调试用 ELF/AXF 文件 |
| `MDK/output/Project.hex` | 带地址信息，可用于烧录工具直接写到 `0x0800D000` |
| `MDK/output/Project.bin` | 纯二进制 App 镜像，用于生成 `.uota` 升级包，不要直接通过串口助手发送 |
| `MDK/output/Project.uota` | USART0 在线升级发送文件，格式为 `16B 包头 + Project.bin` |

如果通过当前 App 侧 USART0 在线升级，应该发送 `Project.uota`，不要直接发送 `Project.bin` 或 `Project.hex`。

## 6.1 生成并发送串口升级包

| 步骤 | 操作 | 说明 |
|---|---|---|
| 1 | 在 Keil 中重新编译当前 App 工程 | 构建后生成 `MDK/output/Project.bin` |
| 2 | 打开 PowerShell 并进入工程根目录 | 路径为 `D:\GD32\2026706296` |
| 3 | 执行 `python tools\make_uart_ota_packet.py --version 0x00000002` | 根据当前 `Project.bin` 生成 `MDK/output/Project.uota`，版本号可按实际递增 |
| 4 | 串口助手选择 `MDK/output/Project.uota` 并发送文件 | 串口参数保持 `115200-8N1`，发送 `.uota` 而不是 `.bin` |
| 5 | 等待 App 打印 `OTA: ready, reset to BootLoader` | App 已写入下载缓存区和参数区，并准备复位 |
| 6 | 观察 BootLoader 打印 `app crc32 check pass` 和 `app update success` | 表示 BootLoader 已完成搬运并清除升级标志 |

命令示例：

```powershell
cd D:\GD32\2026706296
python tools\make_uart_ota_packet.py --version 0x00000002
```

当前 `.uota` 包头格式：

| 偏移 | 字段 | 长度 | 说明 |
|---:|---|---:|---|
| `0x00` | `magic` | 4B | 小端 `0x5AA5C33C`，用于识别升级包 |
| `0x04` | `appVersion` | 4B | 小端版本号，由 `--version` 指定 |
| `0x08` | `firmwareSize` | 4B | `Project.bin` 原始长度 |
| `0x0C` | `firmwareCRC32` | 4B | `Project.bin` 的 CRC32 |
| `0x10` | `firmware` | N B | `Project.bin` 原始内容 |

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
| `BootConfig.h/c` 参数结构 | 不单独引入官方文件 | 当前 App 在 `usart_app.c` 内维护与 BootLoader 参数区兼容的最小结构，避免额外 Keil 工程依赖 |
| 串口接收新固件、写 `0x08073000` 缓存区 | 已实现 | USART0 接收 `.uota` 包后写下载缓存区，并用 CRC32 校验写入结果 |
| 软件复位让 BootLoader 搬运 | 已实现 | App 写入参数区后复位，BootLoader 根据 `updateStatus=0x01`、`updateFlag=0x5A` 搬运 |

## 9. 当前工程接入后的启动注意事项

| 注意点 | 原因 |
|---|---|
| App 必须先设置 `SCB->VTOR` | 否则中断仍可能跳到 BootLoader 的向量表 |
| App 必须重新打开全局中断 | 官方 BootLoader 跳转前会 `__disable_irq()`，普通函数跳转不会自动恢复 PRIMASK |
| 不要把 App 链接回 `0x08000000` | `0x08000000` 必须留给 BootLoader |
| SWD 调试时不要整片擦除 | 整片擦除会把 BootLoader 和参数区一起擦掉 |
| 在线升级必须发送 `.uota` | `.uota` 带包头、固件大小和 CRC；直接发送 `.bin` 不会触发当前 App 的 OTA 流程 |

## 10. 当前运行中升级流程

| 步骤 | 当前实现 |
|---|---|
| 1 | `tools/make_uart_ota_packet.py` 根据 `Project.bin` 生成 `Project.uota` |
| 2 | App 的 USART0 IDLE + DMA 接收 `.uota`，允许 USB 转串口拆成多个小块后累积 |
| 3 | App 校验包头、固件长度、固件 CRC32 和 App 向量表 |
| 4 | App 擦除并写入下载缓存区 `0x08073000`，再读回计算 CRC32 |
| 5 | App 写入参数区 `0x0800C000`：`magicWord/updateStatus/updateFlag/appSize/appCRC32/appVersion` |
| 6 | App 软件复位，BootLoader 搬运缓存区固件到 `0x0800D000` |
| 7 | BootLoader 搬运后重新计算 CRC32，成功后清除 `updateStatus/updateFlag` 并跳转新 App |

限制说明：当前实现仍使用内部 Flash 下载缓存区，单个 `Project.bin` 必须不超过 `52KB`。如果后续 App 超过该大小，需要重新规划下载缓存区，或改成外部 Flash/SD 卡分包升级。
