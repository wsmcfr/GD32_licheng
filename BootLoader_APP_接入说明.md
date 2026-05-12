# 当前工程作为 BootLoader App 的接入说明

## 1. 当前目标

| 项目 | 说明 |
|---|---|
| 当前工程角色 | App 工程，不再从 `0x08000000` 独立启动 |
| 参考 BootLoader | `D:\GD32\04 例程模板\04 例程模板\27_BootLoader_Two_Stage` |
| App 链接地址 | `0x0800D000` |
| App 最大运行区 | `0x00063000` |
| 当前已完成内容 | 当前工程已经按 App 地址链接，并在启动时重定位中断向量表；复制到本工程内的 BootLoader 已按当前 App 优化搬运逻辑 |
| 仍需注意内容 | 当前 App 暂未移植“运行中接收升级包”的协议；首次联调可先用 SWD/烧录器把 App 写到 `0x0800D000` |

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
| 运行中升级 App | 当前工程还没有完整移植升级接收协议 | 官方例程是旧 App 接收串口升级包，写入 `0x08073000` 和参数区，再复位交给 BootLoader 搬运 |

也就是说，官方 Two Stage 例程里，BootLoader 本身不是直接串口收文件的程序；串口接收升级包的逻辑在官方 `27_1_App\Function\Function.c` 里面。

## 6. 当前工程输出文件怎么用

| 输出文件 | 用途 |
|---|---|
| `MDK/output/Project.axf` | Keil 调试用 ELF/AXF 文件 |
| `MDK/output/Project.hex` | 带地址信息，可用于烧录工具直接写到 `0x0800D000` |
| `MDK/output/Project.bin` | 纯二进制固件，适合通过升级协议写入下载缓存区 |

如果通过官方 App 侧升级协议发送固件，应该发送 `Project.bin` 的内容，而不是直接把 HEX 文本当作固件数据发送。

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
| `BootConfig.h/c` 参数结构 | 暂时不必须 | 当前 App 只作为被 BootLoader 跳转运行的 App，不需要自己写升级参数 |
| 串口接收新固件、写 `0x08073000` 缓存区 | 暂时不必须 | 这是“运行中升级”的 App 侧功能，后续要做串口升级时再移植，并建议改成分包协议 |
| 软件复位让 BootLoader 搬运 | 暂时不必须 | 只有 App 自己接收完新固件后，才需要写参数并复位交给 BootLoader |

## 9. 当前工程接入后的启动注意事项

| 注意点 | 原因 |
|---|---|
| App 必须先设置 `SCB->VTOR` | 否则中断仍可能跳到 BootLoader 的向量表 |
| App 必须重新打开全局中断 | 官方 BootLoader 跳转前会 `__disable_irq()`，普通函数跳转不会自动恢复 PRIMASK |
| 不要把 App 链接回 `0x08000000` | `0x08000000` 必须留给 BootLoader |
| SWD 调试时不要整片擦除 | 整片擦除会把 BootLoader 和参数区一起擦掉 |
| 当前工程暂未移植运行中升级接收协议 | 现在重点是让当前工程能作为 App 被跳转运行，并生成可下载镜像 |

## 10. 下一步如果要做“运行中升级”

| 步骤 | 需要做的事 |
|---|---|
| 1 | 在当前工程新增内部 Flash 写入模块，封装擦除、写入、读取和边界检查 |
| 2 | 复用或重写官方 App 的升级包解析逻辑，建议改成分包协议 |
| 3 | 收到完整固件后写入 `0x08073000` 下载缓存区 |
| 4 | 计算固件 CRC32，并把 `appSize/appCRC32/updateFlag/updateStatus` 写入 `0x0800C000` 参数区 |
| 5 | 软件复位，让 BootLoader 搬运缓存区固件到 `0x0800D000` |
| 6 | 如果后续 App bin 超过 `52KB`，重新规划下载缓存区或改成外部 Flash/SD 卡分包升级 |
