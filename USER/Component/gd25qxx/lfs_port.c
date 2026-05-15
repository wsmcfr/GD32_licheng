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
 * 宏作用：
 *   定义本模块内部路径操作临时缓冲区长度。
 * 说明：
 *   路径缓冲区既要能覆盖 LittleFS 的最大名称长度，也要容纳多级目录完整绝对路径，
 *   因此这里显式给出 256 字节上限，避免在串口命令路径解析时使用栈上动态长度对象。
 */
#define LFS_STORAGE_PATH_BUFFER_SIZE   256U

/*
 * 前置声明作用：
 *   让递归删除和递归目录大小统计这类辅助函数在定义顺序调整后仍满足 AC6/C99 的静态声明要求。
 */
static int prv_lfs_get_path_info_mounted(lfs_t *lfs,
                                         const char *path,
                                         lfs_storage_path_info_t *info);
static int prv_lfs_get_path_info_raw_mounted(lfs_t *lfs,
                                             const char *path,
                                             lfs_storage_path_info_t *info);
static int prv_lfs_calculate_path_size_mounted(lfs_t *lfs,
                                               const char *path,
                                               uint32_t *out_size);
static int prv_lfs_write_file_internal(const char *path,
                                       const uint8_t *data,
                                       uint32_t length,
                                       int open_flags);

/*
 * 函数作用：
 *   初始化 LittleFS 文件打开配置对象，并绑定本工程的静态文件缓存。
 * 主要流程：
 *   1. 清零文件配置结构体，避免残留属性指针影响文件打开流程。
 *   2. 绑定全局静态 file buffer，确保文件访问路径不依赖堆内存。
 * 参数说明：
 *   file_cfg：待初始化的文件配置对象指针，必须非空。
 * 返回值说明：
 *   无返回值。
 */
static void prv_lfs_file_cfg_init(struct lfs_file_config *file_cfg)
{
    if (NULL == file_cfg) {
        return;
    }

    memset(file_cfg, 0, sizeof(*file_cfg));
    file_cfg->buffer = lfs_file_buffer;
}

/*
 * 函数作用：
 *   判断传入路径是否为有效的 LittleFS 绝对路径。
 * 主要流程：
 *   1. 检查路径指针是否为空，以及首字符是否为 '/'。
 *   2. 限制总长度必须在模块允许的固定路径缓冲区范围内。
 * 参数说明：
 *   path：待校验路径字符串，必须非空。
 * 返回值说明：
 *   true：表示路径格式满足当前模块的最小要求。
 *   false：表示路径为空、不是绝对路径或长度超限。
 */
static bool prv_lfs_is_valid_abs_path(const char *path)
{
    size_t path_len;

    if ((NULL == path) || ('/' != path[0])) {
        return false;
    }

    path_len = strlen(path);
    if ((0U == path_len) || (path_len >= LFS_STORAGE_PATH_BUFFER_SIZE)) {
        return false;
    }

    return true;
}

/*
 * 函数作用：
 *   将输入路径复制到固定缓冲区，并可选裁掉末尾多余的 '/'。
 * 主要流程：
 *   1. 先复用绝对路径合法性检查，拒绝空路径和超长路径。
 *   2. 复制到调用者提供的工作缓冲区。
 *   3. 对非根目录路径裁掉末尾连续 '/'，统一后续比较和建目录逻辑。
 * 参数说明：
 *   src_path：源绝对路径字符串，必须非空。
 *   dst_path：目标缓冲区，必须非空。
 *   dst_size：目标缓冲区总长度，单位为字节。
 * 返回值说明：
 *   LFS_ERR_OK：表示复制成功。
 *   LFS_ERR_INVAL：表示路径格式或缓冲区参数无效。
 */
static int prv_lfs_copy_normalized_path(const char *src_path, char *dst_path, uint32_t dst_size)
{
    size_t path_len;

    if ((NULL == dst_path) || (0U == dst_size) || !prv_lfs_is_valid_abs_path(src_path)) {
        return LFS_ERR_INVAL;
    }

    path_len = strlen(src_path);
    if (path_len >= (size_t)dst_size) {
        return LFS_ERR_INVAL;
    }

    memcpy(dst_path, src_path, path_len + 1U);

    while ((path_len > 1U) && ('/' == dst_path[path_len - 1U])) {
        dst_path[path_len - 1U] = '\0';
        path_len--;
    }

    return LFS_ERR_OK;
}

/*
 * 函数作用：
 *   基于父目录绝对路径和目录项名称，拼接得到子项的绝对路径。
 * 主要流程：
 *   1. 先把父目录路径归一化，避免尾部多余 `/` 影响拼接。
 *   2. 根目录子项使用 `/<name>` 形式，其余目录使用 `<parent>/<name>` 形式。
 * 参数说明：
 *   parent_path：父目录绝对路径，必须非空。
 *   child_name：子项名称，必须非空且不能是空串。
 *   child_path：输出子项绝对路径缓冲区，必须非空。
 *   child_path_size：输出缓冲区总长度，单位为字节。
 * 返回值说明：
 *   LFS_ERR_OK：表示拼接成功。
 *   LFS_ERR_INVAL：表示输入参数无效或输出缓冲区不足。
 */
static int prv_lfs_build_child_path(const char *parent_path,
                                    const char *child_name,
                                    char *child_path,
                                    uint32_t child_path_size)
{
    char normalized_parent[LFS_STORAGE_PATH_BUFFER_SIZE];
    int print_ret;
    int err;

    if ((NULL == child_name) || ('\0' == child_name[0]) ||
        (NULL == child_path) || (0U == child_path_size)) {
        return LFS_ERR_INVAL;
    }

    err = prv_lfs_copy_normalized_path(parent_path,
                                       normalized_parent,
                                       sizeof(normalized_parent));
    if (LFS_ERR_OK != err) {
        return err;
    }

    if (0 == strcmp(normalized_parent, "/")) {
        print_ret = snprintf(child_path, child_path_size, "/%s", child_name);
    } else {
        print_ret = snprintf(child_path, child_path_size, "%s/%s", normalized_parent, child_name);
    }

    if ((print_ret <= 0) || ((uint32_t)print_ret >= child_path_size)) {
        return LFS_ERR_INVAL;
    }

    return LFS_ERR_OK;
}

/*
 * 函数作用：
 *   在文件系统已挂载的前提下，只查询路径是否存在、类型以及 LittleFS 原始 size 字段。
 * 主要流程：
 *   1. 调用 `lfs_stat()` 获取路径元信息。
 *   2. 不存在时只返回 exists=0，而不是把它视作流程错误。
 *   3. 存在时记录类型和原始大小；目录保持 LittleFS 原始值，不做递归统计。
 * 参数说明：
 *   lfs：已挂载的 LittleFS 运行时对象指针，必须非空。
 *   path：待查询绝对路径，必须非空。
 *   info：路径信息输出结构体指针，必须非空。
 * 返回值说明：
 *   LFS_ERR_OK：表示查询成功或目标不存在但已正确填充 info。
 *   其他 LFS_ERR_*：表示底层查询失败。
 */
static int prv_lfs_get_path_info_raw_mounted(lfs_t *lfs,
                                             const char *path,
                                             lfs_storage_path_info_t *info)
{
    struct lfs_info lfs_info;
    int err;

    if ((NULL == lfs) || (NULL == path) || (NULL == info)) {
        return LFS_ERR_INVAL;
    }

    memset(info, 0, sizeof(*info));
    memset(&lfs_info, 0, sizeof(lfs_info));

    err = lfs_stat(lfs, path, &lfs_info);
    if (LFS_ERR_NOENT == err) {
        info->exists = 0U;
        return LFS_ERR_OK;
    }

    if (LFS_ERR_OK != err) {
        return err;
    }

    info->exists = 1U;
    info->type = (uint16_t)lfs_info.type;
    info->size = (uint32_t)lfs_info.size;
    return LFS_ERR_OK;
}

/*
 * 函数作用：
 *   在文件系统已挂载的前提下，递归统计指定路径对应的文件内容总字节数。
 * 主要流程：
 *   1. 先查询目标路径类型；普通文件直接返回文件长度。
 *   2. 若目标是目录，则递归遍历所有子项并累计其文件内容字节数。
 *   3. 过滤 `.`、`..`，避免目录自引用和无效项干扰统计。
 * 参数说明：
 *   lfs：已挂载的 LittleFS 运行时对象指针，必须非空。
 *   path：待统计路径的绝对路径，必须非空。
 *   out_size：统计结果输出指针，必须非空。
 * 返回值说明：
 *   LFS_ERR_OK：表示统计成功。
 *   其他 LFS_ERR_*：表示路径无效、查询失败、打开目录失败、读取目录失败或关闭目录失败。
 */
static int prv_lfs_calculate_path_size_mounted(lfs_t *lfs,
                                               const char *path,
                                               uint32_t *out_size)
{
    char normalized_path[LFS_STORAGE_PATH_BUFFER_SIZE];
    char child_path[LFS_STORAGE_PATH_BUFFER_SIZE];
    lfs_storage_path_info_t path_info;
    lfs_dir_t dir;
    struct lfs_info child_info;
    int read_ret;
    int close_err;
    int err;

    if ((NULL == lfs) || (NULL == out_size)) {
        return LFS_ERR_INVAL;
    }

    err = prv_lfs_copy_normalized_path(path, normalized_path, sizeof(normalized_path));
    if (LFS_ERR_OK != err) {
        return err;
    }

    err = prv_lfs_get_path_info_raw_mounted(lfs, normalized_path, &path_info);
    if (LFS_ERR_OK != err) {
        return err;
    }

    if (0U == path_info.exists) {
        return LFS_ERR_NOENT;
    }

    if (LFS_TYPE_REG == path_info.type) {
        *out_size = path_info.size;
        return LFS_ERR_OK;
    }

    *out_size = 0U;
    memset(&dir, 0, sizeof(dir));
    err = lfs_dir_open(lfs, &dir, normalized_path);
    if (LFS_ERR_OK != err) {
        return err;
    }

    for (;;) {
        memset(&child_info, 0, sizeof(child_info));
        read_ret = lfs_dir_read(lfs, &dir, &child_info);
        if (read_ret < 0) {
            close_err = lfs_dir_close(lfs, &dir);
            (void)close_err;
            return read_ret;
        }

        if (0 == read_ret) {
            break;
        }

        if ((0 == strcmp(child_info.name, ".")) || (0 == strcmp(child_info.name, ".."))) {
            continue;
        }

        err = prv_lfs_build_child_path(normalized_path,
                                       child_info.name,
                                       child_path,
                                       sizeof(child_path));
        if (LFS_ERR_OK != err) {
            close_err = lfs_dir_close(lfs, &dir);
            (void)close_err;
            return err;
        }

        /*
         * 目录大小统一定义为“目录树下所有文件内容字节数之和”，
         * 这样 `ls/stat` 的数字列始终保持“字节大小”语义。
         */
        {
            uint32_t child_size = 0U;
            err = prv_lfs_calculate_path_size_mounted(lfs, child_path, &child_size);
            if (LFS_ERR_OK != err) {
                close_err = lfs_dir_close(lfs, &dir);
                (void)close_err;
                return err;
            }
            *out_size += child_size;
        }
    }

    close_err = lfs_dir_close(lfs, &dir);
    if (LFS_ERR_OK != close_err) {
        return close_err;
    }

    return LFS_ERR_OK;
}

/*
 * 函数作用：
 *   在文件系统已挂载的前提下，递归删除指定路径。
 * 主要流程：
 *   1. 先查询目标是文件还是目录；不存在则直接返回 `LFS_ERR_NOENT`。
 *   2. 普通文件直接调用 `lfs_remove()` 删除。
 *   3. 目录则先遍历并递归删除全部直接子项，再删除空目录本身。
 * 参数说明：
 *   lfs：已挂载的 LittleFS 运行时对象指针，必须非空。
 *   path：待删除目标的绝对路径，必须非空，且不允许为根目录 `/`。
 * 返回值说明：
 *   LFS_ERR_OK：表示目标删除成功。
 *   LFS_ERR_NOENT：表示目标不存在。
 *   LFS_ERR_INVAL：表示路径无效，或尝试删除根目录 `/`。
 *   其他 LFS_ERR_*：表示查询、遍历或删除失败。
 */
static int prv_lfs_remove_path_mounted(lfs_t *lfs, const char *path)
{
    char normalized_path[LFS_STORAGE_PATH_BUFFER_SIZE];
    char child_path[LFS_STORAGE_PATH_BUFFER_SIZE];
    lfs_storage_path_info_t path_info;
    lfs_dir_t dir;
    struct lfs_info child_info;
    int read_ret;
    int close_err;
    int err;

    if ((NULL == lfs) || (NULL == path)) {
        return LFS_ERR_INVAL;
    }

    err = prv_lfs_copy_normalized_path(path, normalized_path, sizeof(normalized_path));
    if (LFS_ERR_OK != err) {
        return err;
    }

    if (0 == strcmp(normalized_path, "/")) {
        return LFS_ERR_INVAL;
    }

    err = prv_lfs_get_path_info_raw_mounted(lfs, normalized_path, &path_info);
    if (LFS_ERR_OK != err) {
        return err;
    }

    if (0U == path_info.exists) {
        return LFS_ERR_NOENT;
    }

    if (LFS_TYPE_REG == path_info.type) {
        return lfs_remove(lfs, normalized_path);
    }

    memset(&dir, 0, sizeof(dir));
    err = lfs_dir_open(lfs, &dir, normalized_path);
    if (LFS_ERR_OK != err) {
        return err;
    }

    for (;;) {
        memset(&child_info, 0, sizeof(child_info));
        read_ret = lfs_dir_read(lfs, &dir, &child_info);
        if (read_ret < 0) {
            close_err = lfs_dir_close(lfs, &dir);
            (void)close_err;
            return read_ret;
        }

        if (0 == read_ret) {
            break;
        }

        if ((0 == strcmp(child_info.name, ".")) || (0 == strcmp(child_info.name, ".."))) {
            continue;
        }

        err = prv_lfs_build_child_path(normalized_path,
                                       child_info.name,
                                       child_path,
                                       sizeof(child_path));
        if (LFS_ERR_OK != err) {
            close_err = lfs_dir_close(lfs, &dir);
            (void)close_err;
            return err;
        }

        err = prv_lfs_remove_path_mounted(lfs, child_path);
        if (LFS_ERR_OK != err) {
            close_err = lfs_dir_close(lfs, &dir);
            (void)close_err;
            return err;
        }
    }

    close_err = lfs_dir_close(lfs, &dir);
    if (LFS_ERR_OK != close_err) {
        return close_err;
    }

    return lfs_remove(lfs, normalized_path);
}

/*
 * 函数作用：
 *   查询指定路径是否存在、属于文件还是目录，以及对应的大小。
 * 主要流程：
 *   1. 调用 `lfs_stat()` 获取路径元信息。
 *   2. 不存在时只返回 exists=0，而不是把它视作流程错误。
 *   3. 存在时记录类型和大小；若目标是目录，再补充统计该目录树的文件总字节数。
 * 参数说明：
 *   lfs：已挂载的 LittleFS 运行时对象指针，必须非空。
 *   path：待查询绝对路径，必须非空。
 *   info：路径信息输出结构体指针，必须非空。
 * 返回值说明：
 *   LFS_ERR_OK：表示查询成功或目标不存在但已正确填充 info。
 *   其他 LFS_ERR_*：表示底层查询失败。
 */
static int prv_lfs_get_path_info_mounted(lfs_t *lfs,
                                         const char *path,
                                         lfs_storage_path_info_t *info)
{
    int err;

    err = prv_lfs_get_path_info_raw_mounted(lfs, path, info);
    if (LFS_ERR_OK != err) {
        return err;
    }

    if (0U == info->exists) {
        return LFS_ERR_OK;
    }

    if (LFS_TYPE_DIR == info->type) {
        /*
         * LittleFS 目录项的 size 字段不是“目录内容大小”。
         * 这里显式递归统计目录树下全部文件的总字节数，保证目录和文件都显示“大小”语义。
         */
        err = prv_lfs_calculate_path_size_mounted(lfs, path, &info->size);
        if (LFS_ERR_OK != err) {
            return err;
        }
    } else {
        /*
         * 普通文件沿用 LittleFS 原始字节数，避免目录递归统计路径影响文件查询性能。
         */
        return LFS_ERR_OK;
    }

    return LFS_ERR_OK;
}

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
 *   仅执行 LittleFS 配置初始化与挂载，不在失败时自动格式化文件系统。
 * 主要流程：
 *   1. 清零运行时对象和配置对象，避免复用旧状态。
 *   2. 调用 lfs_storage_init() 绑定块设备回调和静态缓存。
 *   3. 直接尝试挂载；挂载失败时立即返回错误，由上层决定是否报错或格式化。
 * 参数说明：
 *   lfs：LittleFS 运行时对象指针，必须非空。
 *   cfg：LittleFS 配置对象指针，必须非空。
 * 返回值说明：
 *   LFS_ERR_OK：表示挂载成功。
 *   LFS_ERR_INVAL：表示入参为空或配置初始化失败。
 *   其他 LFS_ERR_*：表示挂载失败。
 */
static int lfs_storage_mount_only(lfs_t *lfs, struct lfs_config *cfg)
{
    int err;

    if ((NULL == lfs) || (NULL == cfg)) {
        return LFS_ERR_INVAL;
    }

    memset(lfs, 0, sizeof(*lfs));
    memset(cfg, 0, sizeof(*cfg));

    err = lfs_storage_init(cfg);
    if (LFS_ERR_OK != err) {
        return err;
    }

    return lfs_mount(lfs, cfg);
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

    prv_lfs_file_cfg_init(&file_cfg);

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

/*
 * 函数作用：
 *   承接 LittleFS 普通文件写入公共流程，统一处理覆盖写和追加写。
 * 主要流程：
 *   1. 仅挂载文件系统，运行时挂载失败直接返回错误，禁止隐式清盘。
 *   2. 按调用者指定的 open_flags 打开目标文件，决定是覆盖写还是文件尾追加。
 *   3. 当 length 大于 0 时执行精确长度写入，长度不一致立即报错。
 *   4. 关闭文件并卸载文件系统，避免把运行时状态长期挂在全局。
 * 参数说明：
 *   path：目标文件路径，必须非空。
 *   data：待写入数据缓冲区；当 length 大于 0 时必须非空。
 *   length：待写入字节数，单位为字节。
 *   open_flags：LittleFS 文件打开标志，决定本次写入语义。
 * 返回值说明：
 *   LFS_ERR_OK：表示写入、关闭和卸载全部成功。
 *   LFS_ERR_INVAL：表示参数无效。
 *   其他 LFS_ERR_*：表示挂载、打开、写入、关闭或卸载失败。
 */
static int prv_lfs_write_file_internal(const char *path,
                                       const uint8_t *data,
                                       uint32_t length,
                                       int open_flags)
{
    lfs_t lfs;
    struct lfs_config cfg;
    struct lfs_file_config file_cfg;
    lfs_file_t file;
    lfs_ssize_t io_len;
    int err;
    int close_err;

    if (!prv_lfs_is_valid_abs_path(path) || ((NULL == data) && (length > 0U))) {
        return LFS_ERR_INVAL;
    }

    err = lfs_storage_mount_only(&lfs, &cfg);
    if (LFS_ERR_OK != err) {
        return err;
    }

    prv_lfs_file_cfg_init(&file_cfg);
    memset(&file, 0, sizeof(file));

    err = lfs_file_opencfg(&lfs, &file, path, open_flags, &file_cfg);
    if (LFS_ERR_OK != err) {
        (void)lfs_unmount(&lfs);
        return err;
    }

    if (length > 0U) {
        io_len = lfs_file_write(&lfs, &file, data, (lfs_size_t)length);
        if (io_len != (lfs_ssize_t)length) {
            close_err = lfs_file_close(&lfs, &file);
            (void)close_err;
            (void)lfs_unmount(&lfs);
            return (io_len < 0) ? (int)io_len : LFS_ERR_IO;
        }
    }

    err = lfs_file_close(&lfs, &file);
    if (LFS_ERR_OK != err) {
        (void)lfs_unmount(&lfs);
        return err;
    }

    return lfs_unmount(&lfs);
}

/*
 * 函数作用：
 *   在不触发自动格式化的前提下，把调用者给出的数据覆盖写入指定 LittleFS 文件。
 * 主要流程：
 *   1. 复用公共写文件流程，仅允许“写入 + 创建 + 截断”语义。
 *   2. 原文件存在时先清空旧内容，再写入本次全部数据。
 * 参数说明：
 *   path：目标文件路径，必须非空。
 *   data：待写入数据缓冲区；当 length 大于 0 时必须非空。
 *   length：待写入字节数，单位为字节；为 0 时表示把文件截断为空。
 * 返回值说明：
 *   LFS_ERR_OK：表示写入、关闭和卸载全部成功。
 *   LFS_ERR_INVAL：表示参数无效。
 *   其他 LFS_ERR_*：表示挂载、打开、写入、关闭或卸载失败。
 */
int lfs_storage_write_file(const char *path, const uint8_t *data, uint32_t length)
{
    return prv_lfs_write_file_internal(path,
                                       data,
                                       length,
                                       LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
}

/*
 * 函数作用：
 *   在不触发自动格式化的前提下，把调用者给出的数据追加到指定 LittleFS 文件尾部。
 * 主要流程：
 *   1. 复用公共写文件流程，仅允许“写入 + 创建 + 追加”语义。
 *   2. 目标文件不存在时自动创建；已存在时保留原内容并把新数据接到末尾。
 * 参数说明：
 *   path：目标文件路径，必须非空。
 *   data：待追加数据缓冲区；当 length 大于 0 时必须非空。
 *   length：待追加字节数，单位为字节；为 0 时表示不修改原文件内容。
 * 返回值说明：
 *   LFS_ERR_OK：表示追加写、关闭和卸载全部成功。
 *   LFS_ERR_INVAL：表示参数无效。
 *   其他 LFS_ERR_*：表示挂载、打开、写入、关闭或卸载失败。
 */
int lfs_storage_append_file(const char *path, const uint8_t *data, uint32_t length)
{
    return prv_lfs_write_file_internal(path,
                                       data,
                                       length,
                                       LFS_O_WRONLY | LFS_O_CREAT | LFS_O_APPEND);
}

/*
 * 函数作用：
 *   在不触发自动格式化的前提下，完整读取指定 LittleFS 文件内容到缓冲区。
 * 主要流程：
 *   1. 仅挂载文件系统，失败时直接返回错误。
 *   2. 先用 lfs_stat() 获取目标文件信息，提前判断文件是否存在以及缓冲区是否足够。
 *   3. 以只读方式打开文件，按精确文件大小执行读取。
 *   4. 关闭文件、补结尾 '\0'、记录实际长度，并卸载文件系统。
 * 参数说明：
 *   path：目标文件路径，必须非空。
 *   buffer：接收文件内容的缓冲区，必须非空。
 *   buffer_size：接收缓冲区总长度，单位为字节；必须大于 0。
 *   out_length：实际读回长度输出指针，必须非空。
 * 返回值说明：
 *   LFS_ERR_OK：表示文件已完整读回。
 *   LFS_ERR_NOENT：表示目标文件不存在。
 *   LFS_ERR_FBIG：表示目标文件超出当前缓冲区可容纳范围。
 *   其他 LFS_ERR_*：表示挂载、打开、读取、关闭或卸载失败。
 */
int lfs_storage_read_file(const char *path, uint8_t *buffer, uint32_t buffer_size, uint32_t *out_length)
{
    lfs_t lfs;
    struct lfs_config cfg;
    struct lfs_file_config file_cfg;
    lfs_file_t file;
    struct lfs_info file_info;
    lfs_ssize_t io_len;
    int err;
    int close_err;

    if (!prv_lfs_is_valid_abs_path(path) || (NULL == buffer) || (0U == buffer_size) || (NULL == out_length)) {
        return LFS_ERR_INVAL;
    }

    buffer[0] = '\0';
    *out_length = 0U;

    err = lfs_storage_mount_only(&lfs, &cfg);
    if (LFS_ERR_OK != err) {
        return err;
    }

    memset(&file_info, 0, sizeof(file_info));
    err = lfs_stat(&lfs, path, &file_info);
    if (LFS_ERR_OK != err) {
        (void)lfs_unmount(&lfs);
        return err;
    }

    if (LFS_TYPE_REG != file_info.type) {
        (void)lfs_unmount(&lfs);
        return LFS_ERR_ISDIR;
    }

    /*
     * 这里必须为文本回显预留 1 字节结尾 '\0'，避免后续串口打印越界。
     */
    if (file_info.size >= (lfs_size_t)buffer_size) {
        (void)lfs_unmount(&lfs);
        return LFS_ERR_FBIG;
    }

    prv_lfs_file_cfg_init(&file_cfg);
    memset(&file, 0, sizeof(file));

    err = lfs_file_opencfg(&lfs, &file, path, LFS_O_RDONLY, &file_cfg);
    if (LFS_ERR_OK != err) {
        (void)lfs_unmount(&lfs);
        return err;
    }

    if (file_info.size > 0U) {
        io_len = lfs_file_read(&lfs, &file, buffer, file_info.size);
        if (io_len != (lfs_ssize_t)file_info.size) {
            close_err = lfs_file_close(&lfs, &file);
            (void)close_err;
            (void)lfs_unmount(&lfs);
            return (io_len < 0) ? (int)io_len : LFS_ERR_IO;
        }
    }

    close_err = lfs_file_close(&lfs, &file);
    if (LFS_ERR_OK != close_err) {
        (void)lfs_unmount(&lfs);
        return close_err;
    }

    buffer[file_info.size] = '\0';
    *out_length = (uint32_t)file_info.size;

    return lfs_unmount(&lfs);
}

/*
 * 函数作用：
 *   在不触发自动格式化的前提下，查询当前 LittleFS 容量统计和指定文件状态。
 * 主要流程：
 *   1. 仅挂载文件系统，失败时直接返回错误。
 *   2. 通过配置对象计算总容量，并调用 lfs_fs_size() 获取当前已用块数量。
 *   3. 当调用者提供 path 时，再额外查询目标文件是否存在以及文件大小。
 *   4. 卸载文件系统并返回查询结果。
 * 参数说明：
 *   path：目标文件路径；允许为空，此时只统计文件系统容量信息。
 *   info：查询结果输出结构体指针，必须非空。
 * 返回值说明：
 *   LFS_ERR_OK：表示查询成功。
 *   其他 LFS_ERR_*：表示挂载、容量统计、文件查询或卸载失败。
 */
int lfs_storage_get_info(const char *path, lfs_storage_info_t *info)
{
    lfs_t lfs;
    struct lfs_config cfg;
    struct lfs_info file_info;
    lfs_ssize_t used_blocks;
    int err;

    if (NULL == info) {
        return LFS_ERR_INVAL;
    }

    memset(info, 0, sizeof(*info));

    err = lfs_storage_mount_only(&lfs, &cfg);
    if (LFS_ERR_OK != err) {
        return err;
    }

    info->block_size = cfg.block_size;
    info->block_count = cfg.block_count;
    info->total_bytes = cfg.block_size * cfg.block_count;

    used_blocks = lfs_fs_size(&lfs);
    if (used_blocks < 0) {
        (void)lfs_unmount(&lfs);
        return (int)used_blocks;
    }

    info->used_blocks = (uint32_t)used_blocks;
    info->used_bytes = info->used_blocks * info->block_size;

    if ((NULL != path) && ('\0' != path[0])) {
        if (!prv_lfs_is_valid_abs_path(path)) {
            (void)lfs_unmount(&lfs);
            return LFS_ERR_INVAL;
        }

        memset(&file_info, 0, sizeof(file_info));
        err = lfs_stat(&lfs, path, &file_info);
        if (LFS_ERR_NOENT == err) {
            info->file_exists = 0U;
            info->file_size = 0U;
        } else if (LFS_ERR_OK == err) {
            if (LFS_TYPE_REG != file_info.type) {
                (void)lfs_unmount(&lfs);
                return LFS_ERR_ISDIR;
            }

            info->file_exists = 1U;
            info->file_size = (uint32_t)file_info.size;
        } else {
            (void)lfs_unmount(&lfs);
            return err;
        }
    }

    return lfs_unmount(&lfs);
}

/*
 * 函数作用：
 *   在不触发自动格式化的前提下，查询指定路径当前是否存在、是目录还是文件以及大小。
 * 主要流程：
 *   1. 仅挂载文件系统，失败时直接返回错误。
 *   2. 复用已挂载状态下的路径查询辅助函数获取存在性和类型。
 *   3. 卸载文件系统并把结果返回给上层命令解释器。
 * 参数说明：
 *   path：待查询的绝对路径，必须非空。
 *   info：路径信息输出结构体指针，必须非空。
 * 返回值说明：
 *   LFS_ERR_OK：表示查询完成。
 *   其他 LFS_ERR_*：表示挂载、查询或卸载失败。
 */
int lfs_storage_get_path_info(const char *path, lfs_storage_path_info_t *info)
{
    lfs_t lfs;
    struct lfs_config cfg;
    int err;
    int unmount_err;

    if (!prv_lfs_is_valid_abs_path(path) || (NULL == info)) {
        return LFS_ERR_INVAL;
    }

    err = lfs_storage_mount_only(&lfs, &cfg);
    if (LFS_ERR_OK != err) {
        return err;
    }

    err = prv_lfs_get_path_info_mounted(&lfs, path, info);
    unmount_err = lfs_unmount(&lfs);
    if (LFS_ERR_OK != err) {
        return err;
    }

    return unmount_err;
}

/*
 * 函数作用：
 *   在不触发自动格式化的前提下，遍历指定目录下的有效目录项。
 * 主要流程：
 *   1. 仅挂载文件系统，失败时直接返回错误。
 *   2. 打开目标目录并循环读取目录项，过滤掉 `.` 和 `..`。
 *   3. 对每个有效目录项执行回调，并累计目录项个数。
 *   4. 关闭目录并卸载文件系统，向上层返回最终统计结果。
 * 参数说明：
 *   path：待遍历目录绝对路径，必须非空。
 *   callback：目录项回调函数，可为空。
 *   context：回调透传上下文指针，可为空。
 *   out_count：目录项个数输出指针，可为空。
 * 返回值说明：
 *   LFS_ERR_OK：表示目录遍历成功。
 *   其他 LFS_ERR_*：表示挂载、打开目录、读取目录、关闭目录或卸载失败。
 */
int lfs_storage_list_dir(const char *path,
                         lfs_storage_dir_list_callback_t callback,
                         void *context,
                         uint32_t *out_count)
{
    lfs_t lfs;
    struct lfs_config cfg;
    lfs_dir_t dir;
    lfs_storage_dir_entry_t entry;
    char child_path[LFS_STORAGE_PATH_BUFFER_SIZE];
    uint32_t entry_count;
    int read_ret;
    int err;
    int close_err;

    if (!prv_lfs_is_valid_abs_path(path)) {
        return LFS_ERR_INVAL;
    }

    if (NULL != out_count) {
        *out_count = 0U;
    }

    err = lfs_storage_mount_only(&lfs, &cfg);
    if (LFS_ERR_OK != err) {
        return err;
    }

    memset(&dir, 0, sizeof(dir));
    err = lfs_dir_open(&lfs, &dir, path);
    if (LFS_ERR_OK != err) {
        (void)lfs_unmount(&lfs);
        return err;
    }

    entry_count = 0U;
    for (;;) {
        memset(&entry, 0, sizeof(entry));
        read_ret = lfs_dir_read(&lfs, &dir, &entry.info);
        if (read_ret < 0) {
            close_err = lfs_dir_close(&lfs, &dir);
            (void)close_err;
            (void)lfs_unmount(&lfs);
            return read_ret;
        }

        if (0 == read_ret) {
            break;
        }

        if ((0 == strcmp(entry.info.name, ".")) || (0 == strcmp(entry.info.name, ".."))) {
            continue;
        }

        if ((NULL != callback) && (LFS_TYPE_DIR == entry.info.type)) {
            /*
             * 目录项本身的 size 在 LittleFS 中不表示内容规模。
             * 这里在同一次挂载里补充统计该目录的直接子项数量，供 UART 壳层显示。
             */
            err = prv_lfs_build_child_path(path,
                                           entry.info.name,
                                           child_path,
                                           sizeof(child_path));
            if (LFS_ERR_OK != err) {
                close_err = lfs_dir_close(&lfs, &dir);
                (void)close_err;
                (void)lfs_unmount(&lfs);
                return err;
            }

            err = prv_lfs_calculate_path_size_mounted(&lfs, child_path, &entry.display_size);
            if (LFS_ERR_OK != err) {
                close_err = lfs_dir_close(&lfs, &dir);
                (void)close_err;
                (void)lfs_unmount(&lfs);
                return err;
            }
        } else {
            entry.display_size = (uint32_t)entry.info.size;
        }

        entry_count++;
        if (NULL != callback) {
            callback(&entry, context);
        }
    }

    close_err = lfs_dir_close(&lfs, &dir);
    if (LFS_ERR_OK != close_err) {
        (void)lfs_unmount(&lfs);
        return close_err;
    }

    if (NULL != out_count) {
        *out_count = entry_count;
    }

    return lfs_unmount(&lfs);
}

/*
 * 函数作用：
 *   在不触发自动格式化的前提下，在指定路径创建目录。
 * 主要流程：
 *   1. 仅挂载文件系统，失败时直接返回错误。
 *   2. 先查询路径当前是否已存在，避免把同名文件误判成成功。
 *   3. 路径不存在时直接调用 `lfs_mkdir()` 创建目录。
 *   4. 卸载文件系统并返回结果。
 * 参数说明：
 *   path：待创建目录的绝对路径，必须非空。
 * 返回值说明：
 *   LFS_ERR_OK：表示本次创建成功。
 *   LFS_ERR_EXIST：表示目标路径已存在，无论它是目录还是普通文件。
 *   其他 LFS_ERR_*：表示挂载、查询、创建或卸载失败。
 */
int lfs_storage_mkdir(const char *path)
{
    lfs_t lfs;
    struct lfs_config cfg;
    lfs_storage_path_info_t path_info;
    int err;
    int unmount_err;

    if (!prv_lfs_is_valid_abs_path(path)) {
        return LFS_ERR_INVAL;
    }

    err = lfs_storage_mount_only(&lfs, &cfg);
    if (LFS_ERR_OK != err) {
        return err;
    }

    err = prv_lfs_get_path_info_mounted(&lfs, path, &path_info);
    if (LFS_ERR_OK != err) {
        (void)lfs_unmount(&lfs);
        return err;
    }

    if (0U != path_info.exists) {
        unmount_err = lfs_unmount(&lfs);
        if (LFS_TYPE_DIR == path_info.type) {
            return (LFS_ERR_OK == unmount_err) ? LFS_ERR_EXIST : unmount_err;
        }
        return (LFS_ERR_OK == unmount_err) ? LFS_ERR_EXIST : unmount_err;
    }

    err = lfs_mkdir(&lfs, path);
    unmount_err = lfs_unmount(&lfs);
    if (LFS_ERR_OK != err) {
        return err;
    }

    return unmount_err;
}

/*
 * 函数作用：
 *   在不触发自动格式化的前提下，创建一个空文件；若文件已存在则保持原样返回成功。
 * 主要流程：
 *   1. 仅挂载文件系统，失败时直接返回错误。
 *   2. 先查询目标路径，已存在普通文件则直接成功返回。
 *   3. 若路径不存在，则以 `LFS_O_CREAT | LFS_O_EXCL` 创建空文件。
 *   4. 关闭文件并卸载文件系统。
 * 参数说明：
 *   path：目标文件绝对路径，必须非空。
 * 返回值说明：
 *   LFS_ERR_OK：表示文件已存在或新建成功。
 *   LFS_ERR_ISDIR：表示目标路径已存在且是目录。
 *   其他 LFS_ERR_*：表示挂载、查询、创建、关闭或卸载失败。
 */
int lfs_storage_touch_file(const char *path)
{
    lfs_t lfs;
    struct lfs_config cfg;
    struct lfs_file_config file_cfg;
    lfs_file_t file;
    lfs_storage_path_info_t path_info;
    int err;
    int close_err;
    int unmount_err;

    if (!prv_lfs_is_valid_abs_path(path)) {
        return LFS_ERR_INVAL;
    }

    err = lfs_storage_mount_only(&lfs, &cfg);
    if (LFS_ERR_OK != err) {
        return err;
    }

    err = prv_lfs_get_path_info_mounted(&lfs, path, &path_info);
    if (LFS_ERR_OK != err) {
        (void)lfs_unmount(&lfs);
        return err;
    }

    if (0U != path_info.exists) {
        unmount_err = lfs_unmount(&lfs);
        if (LFS_TYPE_REG == path_info.type) {
            return unmount_err;
        }
        return (LFS_ERR_OK == unmount_err) ? LFS_ERR_ISDIR : unmount_err;
    }

    prv_lfs_file_cfg_init(&file_cfg);
    memset(&file, 0, sizeof(file));
    err = lfs_file_opencfg(&lfs,
                           &file,
                           path,
                           LFS_O_WRONLY | LFS_O_CREAT | LFS_O_EXCL,
                           &file_cfg);
    if (LFS_ERR_OK != err) {
        (void)lfs_unmount(&lfs);
        return err;
    }

    close_err = lfs_file_close(&lfs, &file);
    if (LFS_ERR_OK != close_err) {
        (void)lfs_unmount(&lfs);
        return close_err;
    }

    return lfs_unmount(&lfs);
}

/*
 * 函数作用：
 *   在不触发自动格式化的前提下，删除指定路径；普通文件直接删除，目录递归删除。
 * 主要流程：
 *   1. 仅挂载文件系统，失败时直接返回错误。
 *   2. 拒绝删除根目录 `/`，避免把整个 LittleFS 壳层工作区一次性清空。
 *   3. 在已挂载上下文里递归删除目标路径。
 *   4. 卸载文件系统并把最终状态返回给上层串口命令层。
 * 参数说明：
 *   path：待删除目标的绝对路径，必须非空，且不允许为根目录 `/`。
 * 返回值说明：
 *   LFS_ERR_OK：表示删除成功。
 *   LFS_ERR_NOENT：表示目标不存在。
 *   LFS_ERR_INVAL：表示路径无效，或尝试删除根目录 `/`。
 *   其他 LFS_ERR_*：表示挂载、递归删除或卸载失败。
 */
int lfs_storage_remove_path(const char *path)
{
    lfs_t lfs;
    struct lfs_config cfg;
    char normalized_path[LFS_STORAGE_PATH_BUFFER_SIZE];
    int err;
    int unmount_err;

    if (!prv_lfs_is_valid_abs_path(path)) {
        return LFS_ERR_INVAL;
    }

    err = prv_lfs_copy_normalized_path(path, normalized_path, sizeof(normalized_path));
    if (LFS_ERR_OK != err) {
        return err;
    }

    if (0 == strcmp(normalized_path, "/")) {
        return LFS_ERR_INVAL;
    }

    err = lfs_storage_mount_only(&lfs, &cfg);
    if (LFS_ERR_OK != err) {
        return err;
    }

    err = prv_lfs_remove_path_mounted(&lfs, normalized_path);
    unmount_err = lfs_unmount(&lfs);
    if (LFS_ERR_OK != err) {
        return err;
    }

    return unmount_err;
}

