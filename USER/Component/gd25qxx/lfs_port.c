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

/*
 * 变量作用：
 *   LittleFS 块分配查找缓存。
 * 说明：
 *   LittleFS 要求 lookahead_buffer 以 32 位对齐；使用 uint32_t 数组承载，
 *   再通过 sizeof() 按字节数传入配置，避免 uint8_t 数组潜在的未对齐风险。
 */
static uint32_t lfs_lookahead_buffer[LFS_LOOKAHEAD_SIZE / sizeof(uint32_t)];

/*
 * 变量作用：
 *   LittleFS 文件对象专用静态缓存，供 lfs_file_opencfg() 使用。
 * 说明：
 *   LittleFS 在文件打开时若未传入文件缓存，会调用 lfs_malloc() 分配 cache_size
 *   字节；本工程低层存储路径禁止依赖堆，因此显式提供该缓存。
 */
static uint8_t lfs_file_buffer[LFS_FLASH_PAGE_SIZE];

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

    /*
     * LittleFS 正常会保证 block/off/size 不越界；这里仍做防御式检查，
     * 目的是防止配置损坏或未来调用约束变化时越过文件系统分区，擦写到
     * 末尾裸 Flash 测试保留区。
     */
    if ((block >= LFS_BLOCK_COUNT) || (off > LFS_FLASH_SECTOR_SIZE) ||
        (size > (LFS_FLASH_SECTOR_SIZE - off)) || (NULL == buffer)) {
        return LFS_ERR_INVAL;
    }

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

    /*
     * 写入前确认目标仍在 LittleFS 管理区内，避免错误参数覆盖裸测保留区或
     * Flash 地址空间外的区域。
     */
    if ((block >= LFS_BLOCK_COUNT) || (off > LFS_FLASH_SECTOR_SIZE) ||
        (size > (LFS_FLASH_SECTOR_SIZE - off)) || (NULL == buffer)) {
        return LFS_ERR_INVAL;
    }

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

    /*
     * 擦除是最危险的操作，必须强制限制在 LittleFS 分区内。这样即使上层
     * 文件系统对象异常，也不会擦到保留给裸 Flash 测试的末尾扇区。
     */
    if (block >= LFS_BLOCK_COUNT) {
        return LFS_ERR_INVAL;
    }

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
    cfg->lookahead_size = sizeof(lfs_lookahead_buffer);
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

/*
 * 函数作用：
 *   挂载 GD25QXX 上的 LittleFS；如果介质未格式化或超级块无效，则先格式化
 *   LittleFS 管理区，再重新挂载。
 * 主要流程：
 *   1. 按当前 Flash 几何参数初始化 LittleFS 配置。
 *   2. 直接尝试挂载，保护已经存在的有效文件系统。
 *   3. 只有挂载失败时才格式化，避免每次启动擦写全盘导致寿命下降。
 *   4. 格式化成功后再次挂载，确保后续文件操作在已挂载状态执行。
 * 参数说明：
 *   lfs：LittleFS 运行时对象指针，必须非空。
 *   cfg：LittleFS 配置对象指针，必须非空。
 * 返回值说明：
 *   LFS_ERR_OK：表示挂载成功。
 *   LFS_ERR_INVAL：表示入参为空。
 *   其他 LFS_ERR_*：表示配置、格式化或挂载失败。
 */
static int lfs_storage_mount_or_format(lfs_t *lfs, struct lfs_config *cfg)
{
    int err;

    if ((NULL == lfs) || (NULL == cfg)) {
        return LFS_ERR_INVAL;
    }

    memset(lfs, 0, sizeof(*lfs));
    memset(cfg, 0, sizeof(*cfg));

    err = lfs_storage_init(cfg);
    if (LFS_ERR_OK != err) {
        my_printf(DEBUG_USART, "LFS: config init failed (%d)\r\n", err);
        return err;
    }

    err = lfs_mount(lfs, cfg);
    if (LFS_ERR_OK == err) {
        my_printf(DEBUG_USART,
                  "LFS: mount ok, size=%luKB, blocks=%lu\r\n",
                  (unsigned long)(LFS_FLASH_FS_SIZE / 1024U),
                  (unsigned long)LFS_BLOCK_COUNT);
        return LFS_ERR_OK;
    }

    /*
     * 挂载失败通常表示首次使用、Flash 为空或文件系统元数据损坏。这里只在
     * 文件系统层格式化 LittleFS 管理区，不触碰末尾裸测保留扇区。
     */
    my_printf(DEBUG_USART, "LFS: mount failed (%d), formatting fs area...\r\n", err);
    memset(lfs, 0, sizeof(*lfs));
    err = lfs_format(lfs, cfg);
    if (LFS_ERR_OK != err) {
        my_printf(DEBUG_USART, "LFS: format failed (%d)\r\n", err);
        return err;
    }

    memset(lfs, 0, sizeof(*lfs));
    err = lfs_mount(lfs, cfg);
    if (LFS_ERR_OK != err) {
        my_printf(DEBUG_USART, "LFS: remount failed (%d)\r\n", err);
        return err;
    }

    my_printf(DEBUG_USART, "LFS: format and mount ok\r\n");
    return LFS_ERR_OK;
}

/*
 * 函数作用：
 *   执行 GD25QXX LittleFS 冒烟测试，确认文件系统可以完成挂载、文件读取，
 *   并在测试文件缺失或内容不匹配时完成一次写入修复。
 * 主要流程：
 *   1. 挂载 LittleFS，必要时只格式化 LittleFS 管理区。
 *   2. 优先只读打开测试文件；内容正确时不写 Flash，减少启动擦写次数。
 *   3. 文件缺失、读取失败或内容不匹配时，才覆盖写入固定测试内容。
 *   4. 写入后重新只读打开并校验，最后卸载文件系统释放运行时状态。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   LFS_ERR_OK：表示文件系统读写往返校验成功。
 *   其他 LFS_ERR_*：表示对应文件系统步骤失败。
 */
int lfs_storage_self_test(void)
{
    static const char test_path[] = "/boot_check.txt";
    static const char test_msg[] = "GD25QXX_LITTLEFS_BOOT_CHECK";
    lfs_t lfs;
    struct lfs_config cfg;
    struct lfs_file_config file_cfg;
    lfs_file_t file;
    char readback[sizeof(test_msg)];
    lfs_ssize_t io_len;
    int err;
    int close_err;
    bool need_rewrite;

    my_printf(DEBUG_USART, "LFS: self-test start\r\n");

    memset(&file_cfg, 0, sizeof(file_cfg));
    file_cfg.buffer = lfs_file_buffer;

    err = lfs_storage_mount_or_format(&lfs, &cfg);
    if (LFS_ERR_OK != err) {
        return err;
    }

    need_rewrite = false;

    /*
     * 先走只读校验路径。正常情况下启动只读文件，不产生新的擦写；只有文件缺失、
     * 内容异常或读取失败时，才进入后续覆盖写入路径。
     */
    memset(&file, 0, sizeof(file));
    err = lfs_file_opencfg(&lfs, &file, test_path, LFS_O_RDONLY, &file_cfg);
    if (LFS_ERR_OK != err) {
        my_printf(DEBUG_USART, "LFS: self-test file missing or unreadable (%d), rewrite\r\n", err);
        need_rewrite = true;
    } else {
        memset(readback, 0, sizeof(readback));
        io_len = lfs_file_read(&lfs, &file, readback, (lfs_size_t)(sizeof(readback) - 1U));
        close_err = lfs_file_close(&lfs, &file);
        if (LFS_ERR_OK != close_err) {
            my_printf(DEBUG_USART, "LFS: close(read) failed (%d)\r\n", close_err);
            (void)lfs_unmount(&lfs);
            return close_err;
        }

        if ((io_len == (lfs_ssize_t)strlen(test_msg)) &&
            (0 == memcmp(readback, test_msg, strlen(test_msg)))) {
            err = lfs_unmount(&lfs);
            if (LFS_ERR_OK != err) {
                my_printf(DEBUG_USART, "LFS: unmount failed (%d)\r\n", err);
                return err;
            }

            my_printf(DEBUG_USART, "LFS: self-test PASS(read-only): %s=%s\r\n", test_path, readback);
            return LFS_ERR_OK;
        }

        /*
         * 文件存在但内容不匹配时，不直接返回失败，而是覆盖写入标准测试内容；
         * 这样首次升级到 LittleFS 自检逻辑时可以自动修复旧测试文件。
         */
        my_printf(DEBUG_USART,
                  "LFS: self-test file mismatch (ret=%ld), rewrite\r\n",
                  (long)io_len);
        need_rewrite = true;
    }

    if (!need_rewrite) {
        (void)lfs_unmount(&lfs);
        return LFS_ERR_IO;
    }

    memset(&file, 0, sizeof(file));
    err = lfs_file_opencfg(&lfs,
                           &file,
                           test_path,
                           LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC,
                           &file_cfg);
    if (LFS_ERR_OK != err) {
        my_printf(DEBUG_USART, "LFS: open(write) failed (%d)\r\n", err);
        (void)lfs_unmount(&lfs);
        return err;
    }

    io_len = lfs_file_write(&lfs, &file, test_msg, (lfs_size_t)strlen(test_msg));
    if (io_len != (lfs_ssize_t)strlen(test_msg)) {
        /*
         * LittleFS 写接口返回实际写入字节数或负错误码；长度不一致时立即关闭
         * 句柄并返回错误，避免把部分写成功误判成文件系统可用。
         */
        my_printf(DEBUG_USART,
                  "LFS: write failed (ret=%ld, expect=%lu)\r\n",
                  (long)io_len,
                  (unsigned long)strlen(test_msg));
        close_err = lfs_file_close(&lfs, &file);
        (void)close_err;
        (void)lfs_unmount(&lfs);
        return (io_len < 0) ? (int)io_len : LFS_ERR_IO;
    }

    err = lfs_file_close(&lfs, &file);
    if (LFS_ERR_OK != err) {
        my_printf(DEBUG_USART, "LFS: close(write) failed (%d)\r\n", err);
        (void)lfs_unmount(&lfs);
        return err;
    }

    memset(&file, 0, sizeof(file));
    err = lfs_file_opencfg(&lfs, &file, test_path, LFS_O_RDONLY, &file_cfg);
    if (LFS_ERR_OK != err) {
        my_printf(DEBUG_USART, "LFS: open(read) failed (%d)\r\n", err);
        (void)lfs_unmount(&lfs);
        return err;
    }

    memset(readback, 0, sizeof(readback));
    io_len = lfs_file_read(&lfs, &file, readback, (lfs_size_t)(sizeof(readback) - 1U));
    close_err = lfs_file_close(&lfs, &file);
    if (LFS_ERR_OK != close_err) {
        my_printf(DEBUG_USART, "LFS: close(read) failed (%d)\r\n", close_err);
        (void)lfs_unmount(&lfs);
        return close_err;
    }

    if (io_len != (lfs_ssize_t)strlen(test_msg)) {
        my_printf(DEBUG_USART,
                  "LFS: read failed (ret=%ld, expect=%lu)\r\n",
                  (long)io_len,
                  (unsigned long)strlen(test_msg));
        (void)lfs_unmount(&lfs);
        return (io_len < 0) ? (int)io_len : LFS_ERR_IO;
    }

    if (0 != memcmp(readback, test_msg, strlen(test_msg))) {
        my_printf(DEBUG_USART, "LFS: verify failed\r\n");
        (void)lfs_unmount(&lfs);
        return LFS_ERR_CORRUPT;
    }

    err = lfs_unmount(&lfs);
    if (LFS_ERR_OK != err) {
        my_printf(DEBUG_USART, "LFS: unmount failed (%d)\r\n", err);
        return err;
    }

    my_printf(DEBUG_USART, "LFS: self-test PASS: %s=%s\r\n", test_path, readback);
    return LFS_ERR_OK;
}

