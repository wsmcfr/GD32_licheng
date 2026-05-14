#ifndef LFS_PORT_H
#define LFS_PORT_H

#include "lfs.h"

/*
 * 宏作用：
 *   定义 GD25Q16 的物理容量，单位为字节。
 * 说明：
 *   该值必须与实际焊接的 SPI Flash 容量一致；改动容量前必须同步检查
 *   LittleFS block_count、裸 Flash 测试保留区和工程文档。
 */
#define LFS_FLASH_TOTAL_SIZE            (2U * 1024U * 1024U)

/*
 * 宏作用：
 *   定义 GD25Qxx 4KB 扇区擦除粒度，LittleFS 的 block_size 与该值保持一致。
 */
#define LFS_FLASH_SECTOR_SIZE           (4U * 1024U)

/*
 * 宏作用：
 *   定义 GD25Qxx 页编程粒度，LittleFS 的 read/prog/cache 粒度与该值保持一致。
 */
#define LFS_FLASH_PAGE_SIZE             (256U)

/*
 * 宏作用：
 *   在 Flash 末尾保留 1 个 4KB 扇区给可选裸 Flash 冒烟测试。
 * 说明：
 *   LittleFS 不会使用该扇区，避免启用 test_spi_flash() 时擦除文件系统超级块、
 *   目录元数据或业务文件。生产固件仍建议保持裸测试关闭。
 */
#define LFS_FLASH_RAW_TEST_RESERVED_SIZE LFS_FLASH_SECTOR_SIZE

/*
 * 宏作用：
 *   定义 LittleFS 实际可管理的 Flash 范围大小，单位为字节。
 */
#define LFS_FLASH_FS_SIZE               (LFS_FLASH_TOTAL_SIZE - LFS_FLASH_RAW_TEST_RESERVED_SIZE)

/*
 * 宏作用：
 *   定义可选裸 Flash 测试的起始地址，固定指向 LittleFS 管理区之外的末尾扇区。
 */
#define LFS_FLASH_RAW_TEST_START_ADDR   LFS_FLASH_FS_SIZE

/*
 * 宏作用：
 *   定义 LittleFS 可管理的逻辑块数量。
 */
#define LFS_BLOCK_COUNT                 (LFS_FLASH_FS_SIZE / LFS_FLASH_SECTOR_SIZE)

/*
 * 宏作用：
 *   定义 LittleFS lookahead 缓冲区字节数。
 * 说明：
 *   LittleFS 要求该值单位是“字节”且为 8 的倍数；64 字节可覆盖 512 个块，
 *   与当前 511 个可用块匹配，并避免配置值大于实际缓冲区导致内存越界。
 */
#define LFS_LOOKAHEAD_SIZE              (64U)

/*
 * 宏作用：
 *   控制启动阶段是否执行 LittleFS 文件系统冒烟测试。
 * 说明：
 *   置 1 会在启动时挂载文件系统，并写入/读回一个小测试文件；量产固件如需减少
 *   启动写入次数，可改为 0，但首次接入和硬件验证阶段建议保持启用。
 */
#define LFS_STORAGE_BOOT_SELF_TEST_ENABLE 1U

/*
 * 函数作用：
 *   初始化 LittleFS 配置对象，将 GD25QXX Flash 映射为 LittleFS 块设备。
 * 参数说明：
 *   cfg：待初始化的 LittleFS 配置对象指针，必须非空。
 * 返回值说明：
 *   LFS_ERR_OK：表示配置完成。
 *   LFS_ERR_INVAL：表示 cfg 为空指针。
 */
int lfs_storage_init(struct lfs_config *cfg);

/*
 * 函数作用：
 *   执行 GD25QXX LittleFS 冒烟测试，覆盖挂载、必要时格式化、文件写入、关闭、
 *   重新打开读取和内容校验。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   LFS_ERR_OK：表示文件系统挂载和文件往返校验通过。
 *   其他 LFS_ERR_*：表示初始化、格式化、挂载或文件读写校验失败。
 */
int lfs_storage_self_test(void);

#endif /* LFS_PORT_H */
