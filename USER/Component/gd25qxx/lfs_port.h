/*
 * 文件作用：
 *   声明 GD25QXX Flash 的 LittleFS 移植层参数和配置初始化接口。
 * 版权说明：
 *   Copyright (c) 2024 米醋电子工作室。
 */
#ifndef LFS_PORT_H
#define LFS_PORT_H

#include "lfs.h"

/* 定义 Flash 总容量，当前按 GD25Q16 16Mbit/2MB 配置。 */
#define LFS_FLASH_TOTAL_SIZE (2 * 1024 * 1024)
/* 定义 Flash 擦除扇区大小，LittleFS 的 block_size 与该值保持一致。 */
#define LFS_FLASH_SECTOR_SIZE (4 * 1024)
/* 定义 Flash 页大小，LittleFS 的 read_size/prog_size/cache_size 与该值保持一致。 */
#define LFS_FLASH_PAGE_SIZE (256)

#define LFS_BLOCK_COUNT (LFS_FLASH_TOTAL_SIZE / LFS_FLASH_SECTOR_SIZE)

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

#endif /* LFS_PORT_H */
