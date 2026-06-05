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
| 当前 OTA payload 上限 | `128KB`，也就是原始 `Project.bin` 不能超过 `0x00020000` 字节 |

`Project_ota.bin` 比 `Project.bin` 多 64 字节 OTA 头部；容量限制看的是头部后的 App payload，也就是 `Project.bin` 本身。

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
| App 缓存区 | App 接收 `Project_ota.bin` payload 时写入 | BootLoader 升级时搬运 | ready 前整区预擦，接收时只做编程 |
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
| `tools/pack_ota_image.c` | 默认拒绝打包超出 `OTA_IMAGE_MAX_SIZE` 的 payload |
| App `Protocol/ota_image_protocol.c` | 校验头部时拒绝 `image_size > BOOTLOADER_PORT_DOWNLOAD_MAX_SIZE` |
| BootLoader `Function/Function.c` | 拒绝 `appSize > BOOT_APP_REGION_SIZE` 的搬运任务 |
| Keil 工程 | IROM 限制为 `0x020000`，超出会构建失败或生成越界镜像 |

---

## 5. OTA 数据流

| 顺序 | 执行方 | 动作 | 地址 |
|---:|---|---|---|
| 1 | Keil / 打包工具 | 生成 `Project.bin`，再加 64 字节头部生成 `Project_ota.bin` | `load_addr = 0x08011000` |
| 2 | App | 收到合法头部后，把 payload 流式写入缓存区 | `0x08051000` |
| 3 | App | 回读缓存区计算 CRC32 | `0x08051000` |
| 4 | App | 写参数区并置升级标志 | `0x08010000` |
| 5 | BootLoader | 复位后读取参数区 | `0x08010000` |
| 6 | BootLoader | 备份当前运行区 | `0x08011000 -> 0x08031000` |
| 7 | BootLoader | 搬运缓存区新 App 到运行区 | `0x08051000 -> 0x08011000` |
| 8 | BootLoader | 对正式 App 区重新计算 CRC32 | `0x08011000` |
| 9 | BootLoader | 失败时尽量恢复旧 App | `0x08031000 -> 0x08011000` |

---

## 6. 相关文件

| 文件 | 必须保持一致的内容 |
|---|---|
| [User/boot_app_config.h](D:/GD32/2026706296/User/boot_app_config.h:1) | `BOOT_APP_START_ADDRESS = 0x08011000`，`BOOT_APP_FLASH_SIZE = 0x00020000` |
| [Driver/BOOTLOADER/bootloader_port.h](D:/GD32/2026706296/Driver/BOOTLOADER/bootloader_port.h:1) | 参数区、备份区、缓存区和 128KB 上限 |
| [Protocol/ota_image_protocol.c](D:/GD32/2026706296/Protocol/ota_image_protocol.c:1) | OTA 头部地址、容量、向量表校验 |
| [tools/pack_ota_image.c](D:/GD32/2026706296/tools/pack_ota_image.c:1) | 默认 `load_addr` 和最大 payload |
| [project/2026706296.uvprojx](D:/GD32/2026706296/project/2026706296.uvprojx:1) | IROM 和 After Build 打包地址 |
| [D:\GD32\2026706296_bootloader\Function\Function.c](D:/GD32/2026706296_bootloader/Function/Function.c:1) | BootLoader 搬运、备份、恢复地址 |
| [D:\GD32\2026706296_bootloader\Driver\BootLoader\BootConfig.h](D:/GD32/2026706296_bootloader/Driver/BootLoader/BootConfig.h:1) | 参数区起始地址和默认字段说明 |

---

## 7. 常见误解

| 误解 | 正确理解 |
|---|---|
| 三块 128KB 意味着要发送三个文件 | 错，上位机仍然只原始发送一个 `Project_ota.bin` |
| App 会直接把新固件写到运行区 | 错，App 只写缓存区，正式搬运由 BootLoader 完成 |
| `Project_ota.bin` 文件总大小必须小于 128KB | 不准确，限制的是 payload；`Project_ota.bin` 总大小可以是 `128KB + 64B` |
| 旧的 `0x0800D000/152KB` 还能继续混用 | 不行，当前代码、Keil IROM、BootLoader 和文档统一为 `0x08011000/128KB` |
| 备份区可以从 `0x08033000` 开始 | 不行，当前 128KB 备份区必须从 `0x08031000` 开始，否则会覆盖缓存区 |

---

## 8. 验证命令

```powershell
gcc -std=c99 -Wall -Wextra -Werror tools\pack_ota_image.c -o tools\pack_ota_image.exe
python -m unittest tools.test_header_bin_ota_static
python tools/test_static_optimizations.py
```

若有 Keil 环境，还应执行：

```powershell
& 'E:\Keil_v5\UV4\UV4.exe' -b 'project\2026706296.uvprojx' -j0
Select-String -Path 'project\output\Project.build_log.htm' -Pattern 'Program Size|Error\(s\)|Warning\(s\)'
```
