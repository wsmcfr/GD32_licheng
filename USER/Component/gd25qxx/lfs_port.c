#include "lfs_port.h"
#include "gd25qxx.h"
#include <stdio.h>

/*
 * 变量作用：
 *   LittleFS 静态缓存区，分别服务读缓存、写缓存和块查找缓存。
 * 说明：
 *   本工程低层存储路径不使用动态内存，缓存大小由 lfs_port.h 中的 Flash 参数统一决定。
 */
static uint8_t lfs_read_buffer[LFS_FLASH_PAGE_SIZE];
static uint8_t lfs_prog_buffer[LFS_FLASH_PAGE_SIZE];
static uint8_t lfs_lookahead_buffer[256 / 8];

/*
 * 函数作用：
 *   LittleFS 块设备读接口，把 Flash 指定块内偏移的数据读入调用者缓冲区。
 * 参数说明：
 *   c：LittleFS 配置对象，本移植层不需要使用其 context。
 *   block：LittleFS 逻辑块号，按 LFS_FLASH_SECTOR_SIZE 映射到 Flash 地址。
 *   off：块内偏移，单位为字节。
 *   buffer：读出数据目标缓冲区，调用者需保证至少容纳 size 字节。
 *   size：读取长度，单位为字节。
 * 返回值说明：
 *   LFS_ERR_OK：表示底层读操作已提交完成。
 */
static int lfs_deskio_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size)
{
    (void)c;
    spi_flash_buffer_read(buffer, (block * LFS_FLASH_SECTOR_SIZE) + off, size);
    return LFS_ERR_OK;
}

/*
 * 函数作用：
 *   LittleFS 块设备写接口，把调用者缓冲区数据写入 Flash 指定块内偏移。
 * 参数说明：
 *   c：LittleFS 配置对象，本移植层不需要使用其 context。
 *   block：LittleFS 逻辑块号，按 LFS_FLASH_SECTOR_SIZE 映射到 Flash 地址。
 *   off：块内偏移，单位为字节。
 *   buffer：待写入数据源缓冲区，调用者需保证至少包含 size 字节。
 *   size：写入长度，单位为字节。
 * 返回值说明：
 *   LFS_ERR_OK：表示底层写操作已提交完成。
 */
static int lfs_deskio_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size)
{
    (void)c;
    spi_flash_buffer_write((uint8_t *)buffer, (block * LFS_FLASH_SECTOR_SIZE) + off, size);
    return LFS_ERR_OK;
}

/*
 * 函数作用：
 *   LittleFS 块设备擦除接口，擦除指定逻辑块对应的 Flash 扇区。
 * 参数说明：
 *   c：LittleFS 配置对象，本移植层不需要使用其 context。
 *   block：LittleFS 逻辑块号，按 LFS_FLASH_SECTOR_SIZE 映射到 Flash 扇区地址。
 * 返回值说明：
 *   LFS_ERR_OK：表示底层擦除操作已提交完成。
 */
static int lfs_deskio_erase(const struct lfs_config *c, lfs_block_t block)
{
    (void)c;
    spi_flash_sector_erase(block * LFS_FLASH_SECTOR_SIZE);
    return LFS_ERR_OK;
}

/*
 * 函数作用：
 *   LittleFS 块设备同步接口，用于确认此前写入和擦除已经落到 Flash。
 * 参数说明：
 *   c：LittleFS 配置对象，本移植层不需要使用其 context。
 * 返回值说明：
 *   LFS_ERR_OK：表示当前 GD25QXX 驱动的阻塞写/擦除流程已经完成。
 */
static int lfs_deskio_sync(const struct lfs_config *c)
{
    (void)c;
    /*
     * 当前 gd25qxx 写入/擦除接口为阻塞式实现，函数返回时已经等待内部写完成。
     * 若未来替换为异步驱动，需要在这里补充等待 Flash WIP 清零的逻辑。
     */
    return LFS_ERR_OK;
}

/*
 * 函数作用：
 *   初始化 LittleFS 配置对象，将 GD25QXX Flash 映射为 LittleFS 块设备。
 * 主要流程：
 *   1. 检查配置对象指针，避免空指针写入。
 *   2. 绑定 read/prog/erase/sync 四个块设备操作函数。
 *   3. 填入 Flash 读写粒度、块大小、块数量和静态缓存区。
 * 参数说明：
 *   cfg：待初始化的 LittleFS 配置对象指针，必须非空。
 * 返回值说明：
 *   LFS_ERR_OK：表示配置对象初始化完成。
 *   LFS_ERR_INVAL：表示 cfg 为空指针，未写入任何配置。
 */
int lfs_storage_init(struct lfs_config *cfg)
{
    if (!cfg)
        return LFS_ERR_INVAL;

    /* 该移植层只绑定单个片上 SPI Flash，不需要额外 context 区分实例。 */
    cfg->context = NULL;

    /* LittleFS 通过这组回调访问底层 Flash，业务层不直接操作物理地址。 */
    cfg->read = lfs_deskio_read;
    cfg->prog = lfs_deskio_prog;
    cfg->erase = lfs_deskio_erase;
    cfg->sync = lfs_deskio_sync;

    /* 所有几何参数必须与实际 Flash 和 gd25qxx 驱动擦写粒度保持一致。 */
    cfg->read_size = LFS_FLASH_PAGE_SIZE;
    cfg->prog_size = LFS_FLASH_PAGE_SIZE;
    cfg->block_size = LFS_FLASH_SECTOR_SIZE;
    cfg->block_count = LFS_BLOCK_COUNT;
    cfg->cache_size = LFS_FLASH_PAGE_SIZE;
    cfg->lookahead_size = sizeof(lfs_lookahead_buffer) * 8;
    cfg->block_cycles = 500;

    /* 显式提供静态缓存，避免 LittleFS 运行期从堆上申请内存。 */
    cfg->read_buffer = lfs_read_buffer;
    cfg->prog_buffer = lfs_prog_buffer;
    cfg->lookahead_buffer = lfs_lookahead_buffer;

    /* 置 0 表示沿用 LittleFS 编译期默认限制，减少本移植层重复定义。 */
    cfg->name_max = 0;
    cfg->file_max = 0;
    cfg->attr_max = 0;

    return LFS_ERR_OK;
}


