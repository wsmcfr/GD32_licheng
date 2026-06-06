# Flash 分区原理与 OTA 容量限制说明

## 1. 当前结论

当前工程按题目要求重新规划为：

| 项目 | 当前值 |
|---|---|
| BootLoader | `0x08000000 ~ 0x0800FFFF`，`64KB` |
| 参数区 | `0x08010000 ~ 0x08010FFF`，`4KB` |
| App 运行区 | `0x08011000 ~ 0x08030FFF`，`128KB` |
| App 备份区 | `0x08031000 ~ 0x08050FFF`，`128KB` |
| App 缓存区 | `0x08051000 ~ 0x08070FFF`，`128KB` |
| 未使用区 | `0x08071000 ~ 0x0807FFFF`，`60KB` |
| 当前升级 payload 上限 | `128KB`，也就是剥离大赛 bin 前 4 字节魔术字后的 App payload 不能超过 `0x00020000` 字节 |

正式赛题版不再使用旧 `Project_ota.bin` 64 字节头部。Bootloader 在 `0x0502` 后接收大赛 bin 原始字节流，先校验前 4 字节魔术字 `5AA5C33C`，再把魔术字后面的 App payload 写入下载缓存区。

---

## 2. 地址推导

内部 Flash 从 `0x08000000` 开始，本工程只使用 512KB 区间：

```text
0x08000000
├─ BootLoader          64KB  = 16 页
0x08010000
├─ 参数区               4KB  = 1 页
0x08011000
├─ App 运行区          128KB = 32 页
0x08031000
├─ App 备份区          128KB = 32 页
0x08051000
├─ App 缓存区          128KB = 32 页
0x08071000
├─ 未使用区             60KB = 15 页
0x08080000
```

| 计算 | 结果 | 含义 |
|---|---:|---|
| `0x08000000 + 64KB` | `0x08010000` | BootLoader 结束，参数区开始 |
| `0x08010000 + 4KB` | `0x08011000` | 参数区结束，App 运行区开始 |
| `0x08011000 + 128KB` | `0x08031000` | 运行区结束，备份区开始 |
| `0x08031000 + 128KB` | `0x08051000` | 备份区结束，缓存区开始 |
| `0x08051000 + 128KB` | `0x08071000` | 缓存区结束，后续暂不使用 |

所有分区都按 `4KB` Flash 页对齐。备份区必须从 `0x08031000` 开始；如果仍用旧地址 `0x08033000`，128KB 备份区会延伸到 `0x08052FFF`，直接覆盖 `0x08051000` 开始的缓存区。

---

## 3. 各分区职责

| 分区 | 谁写入 | 谁读取 | 关键约束 |
|---|---|---|---|
| BootLoader 区 | 烧录器 / BootLoader 工程 | MCU 复位入口 | App 不能擦写或覆盖 |
| 参数区 | App 提交升级信息，BootLoader 清标志 | BootLoader 启动读取 | App/BootLoader 的结构体字段偏移必须一致 |
| App 运行区 | BootLoader 搬运新 App，或烧录器直接写入 | BootLoader 跳转，CPU 运行 | Keil IROM 必须是 `0x08011000 / 0x020000` |
| App 备份区 | BootLoader 升级前备份旧 App | BootLoader 失败时恢复 | 起始地址固定 `0x08031000`，大小 `128KB` |
| App 缓存区 | Bootloader 在 `0x0502` 后接收大赛 bin 并写入 | BootLoader 升级时搬运 | 只保存剥离 `5AA5C33C` 后的 App payload |
| 未使用区 | 无 | 无 | 当前不参与 OTA，后续改动必须同步代码和文档 |

---

## 4. 为什么 App 上限是 128KB

当前在线升级需要三块等大的 App 工作区：

| 工作区 | 为什么必须容纳完整 App |
|---|---|
| App 运行区 | 新 App 最终要在这里运行 |
| App 备份区 | 升级前要保存旧 App，搬运失败时恢复 |
| App 缓存区 | App 接收新 App payload 后先暂存在这里 |

因此 `image_size` 必须同时满足：

```text
1 <= image_size <= 128KB
```

超过 `128KB` 时：

| 层级 | 行为 |
|---|---|
| App `project/2026706296.uvprojx` | IROM 限制为 `0x020000`，只生成 `Project.bin`，不再生成旧头部包 |
| App `Protocol/cimc_protocol.c` | `0x0501` 只写 Bootloader 等待标志并复位，不接收完整固件 |
| BootLoader `Protocol/boot_cimc_protocol.c` | 校验 `5AA5C33C` 魔术字，并拒绝 payload 超过下载区 |
| BootLoader `Function/Function.c` | 拒绝 `appSize > BOOT_APP_REGION_SIZE` 的搬运任务 |
| Keil 工程 | IROM 限制为 `0x020000`，超出会构建失败或生成越界镜像 |

---

## 5. OTA 数据流

| 顺序 | 执行方 | 动作 | 地址 |
|---:|---|---|---|
| 1 | Keil | App 工程生成 `Project.bin`，正式构建不再生成 `Project_ota.bin` | `IROM = 0x08011000/0x020000` |
| 2 | App | 收到 `0x0501` 后回复 OK，写参数区等待标志并复位 | `0x08010000` |
| 3 | BootLoader | 普通上电静默等待 5s；检测到等待标志后开启 10s 升级窗口 | `0x08010000` |
| 4 | BootLoader | 收到 `0x0502` 后接收大赛 bin 原始字节流 | USART1/RS485 |
| 5 | BootLoader | 校验前 4 字节魔术字 `5AA5C33C`，只把后续 payload 写入缓存区 | `0x08051000` |
| 6 | BootLoader | 收到 `0x0503` 后先回复 OK，再备份当前运行区 | `0x08011000 -> 0x08031000` |
| 7 | BootLoader | 搬运缓存区新 App 到运行区 | `0x08051000 -> 0x08011000` |
| 8 | BootLoader | 对正式 App 区重新计算 CRC32 | `0x08011000` |
| 9 | BootLoader | 失败时尽量恢复旧 App，成功时清标志并复位 | `0x08031000 -> 0x08011000` |

---

## 6. 相关文件

| 文件 | 必须保持一致的内容 |
|---|---|
| [User/boot_app_config.h](D:/GD32/2026706296/User/boot_app_config.h:1) | `BOOT_APP_START_ADDRESS = 0x08011000`，`BOOT_APP_FLASH_SIZE = 0x00020000` |
| [Driver/BOOTLOADER/bootloader_port.h](D:/GD32/2026706296/Driver/BOOTLOADER/bootloader_port.h:1) | 参数区、备份区、缓存区和 128KB 上限 |
| [Protocol/cimc_protocol.c](D:/GD32/2026706296/Protocol/cimc_protocol.c:1) | `0x0501` 升级请求入口 |
| [project/2026706296.uvprojx](D:/GD32/2026706296/project/2026706296.uvprojx:1) | IROM 和 `Project.bin` 输出设置 |
| [D:\GD32\2026706296_bootloader\Protocol\boot_cimc_protocol.c](D:/GD32/2026706296_bootloader/Protocol/boot_cimc_protocol.c:1) | `0x0502/0x0503`、魔术字和 bin 接收 |
| [D:\GD32\2026706296_bootloader\Function\Function.c](D:/GD32/2026706296_bootloader/Function/Function.c:1) | BootLoader 搬运、备份、恢复地址 |
| [D:\GD32\2026706296_bootloader\Driver\BootLoader\BootConfig.h](D:/GD32/2026706296_bootloader/Driver/BootLoader/BootConfig.h:1) | 参数区起始地址和默认字段说明 |

---

## 7. 常见误解

| 误解 | 正确理解 |
|---|---|
| 三块 128KB 意味着要发送三个文件 | 错，上位机只在 `0x0502` 后发送一个大赛 bin |
| App 会直接把新固件写到运行区 | 错，App 只写等待标志；固件接收和正式搬运由 BootLoader 完成 |
| 大赛 bin 文件总大小必须小于 128KB | 不准确，限制的是剥离 4 字节魔术字后的 payload；大赛 bin 总大小可以是 `128KB + 4B` |
| 旧的 `0x0800D000/152KB` 还能继续混用 | 不行，当前代码、Keil IROM、BootLoader 和文档统一为 `0x08011000/128KB` |
| 备份区可以从 `0x08033000` 开始 | 不行，当前 128KB 备份区必须从 `0x08031000` 开始，否则会覆盖缓存区 |

---

## 8. 验证命令

```powershell
rg -n "Project_ota\.bin|pack_ota_image" project BootLoader_APP_接入说明.md OTA头部BIN升级说明.md
```

若有 Keil 环境，应执行：

```powershell
& 'E:\Keil_v5\UV4\UV4.exe' -b 'project\2026706296.uvprojx' -j0
Select-String -Path 'project\output\Project.build_log.htm' -Pattern 'Program Size|Error\(s\)|Warning\(s\)'
```
