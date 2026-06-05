#include "smartfs_port.h"
#include "gd25qxx.h"
#include <stdio.h>
#include <string.h>

/*
 * 宏作用：
 *   定义本移植层元数据的固定标识和版本号。
 * 说明：
 *   旧 LittleFS 或空白 Flash 不会包含该魔数，启动检查失败后会触发 SMARTFS 格式化。
 */
#define SMARTFS_MAGIC                     0x534D4653UL
#define SMARTFS_VERSION                   1U
#define SMARTFS_ENTRY_STATE_ACTIVE        0xA5A55A5AUL
#define SMARTFS_ENTRY_STATE_DELETED       0x00000000UL
#define SMARTFS_INVALID_SECTOR            0xFFFFU
#define SMARTFS_BLOCK_MAP_FREE            0xFFFFU
#define SMARTFS_BLOCK_MAP_META            0xFFFEU
#define SMARTFS_BLOCK_MAP_END             0xFFFDU

/*
 * 结构体作用：
 *   描述 SMARTFS 单个文件或目录的持久化元数据。
 * 成员说明：
 *   state：目录项状态，区分空闲、有效和删除。
 *   type：目录项类型，取 SMART_STORAGE_TYPE_REG 或 SMART_STORAGE_TYPE_DIR。
 *   parent：父目录项索引；根目录父索引指向自身。
 *   first_sector：普通文件第一数据扇区；空文件和目录取 SMARTFS_INVALID_SECTOR。
 *   size：普通文件内容字节数；目录保持 0。
 *   name：目录项名称，不包含父路径，根目录名称为空字符串。
 */
typedef struct
{
    uint32_t state;
    uint16_t type;
    uint16_t parent;
    uint16_t first_sector;
    uint16_t reserved0;
    uint32_t size;
    char name[SMART_STORAGE_NAME_MAX + 1U];
} smartfs_disk_entry_t;

/*
 * 结构体作用：
 *   描述 SMARTFS 持久化元数据头部。
 * 成员说明：
 *   magic：文件系统魔数。
 *   version：元数据格式版本。
 *   sequence：提交序号，双副本加载时选择序号更新的有效副本。
 *   total_sectors：当前格式化时识别到的物理扇区数量。
 *   data_start_sector：数据区起始物理扇区。
 *   entry_count：固定目录项槽位数量。
 *   crc32：从本字段之后到元数据镜像末尾的 CRC32。
 */
typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t sequence;
    uint16_t total_sectors;
    uint16_t data_start_sector;
    uint16_t entry_count;
    uint16_t reserved0;
    uint32_t crc32;
} smartfs_disk_header_t;

/*
 * 结构体作用：
 *   描述保存在 Flash 元数据副本中的完整 SMARTFS 镜像。
 * 成员说明：
 *   header：镜像头部，包含魔数、版本、提交序号和 CRC。
 *   entries：固定数量目录项表，根目录固定使用 0 号槽位。
 *   block_next：数据扇区链表，索引为物理扇区号；元数据扇区标记为 META。
 */
typedef struct
{
    smartfs_disk_header_t header;
    smartfs_disk_entry_t entries[SMART_STORAGE_MAX_ENTRIES];
    uint16_t block_next[SMARTFS_BLOCK_COUNT];
} smartfs_image_t;

/*
 * 结构体作用：
 *   描述一次路径解析后得到的父目录、末级名称和目标目录项索引。
 * 成员说明：
 *   parent_index：末级名称所在父目录项索引。
 *   entry_index：目标路径目录项索引；不存在时为 -1。
 *   leaf_name：目标路径末级名称；根目录为空字符串。
 *   is_root：目标路径是否为根目录。
 */
typedef struct
{
    uint16_t parent_index;
    int16_t entry_index;
    char leaf_name[SMART_STORAGE_NAME_MAX + 1U];
    uint8_t is_root;
} smartfs_path_lookup_t;

/*
 * 变量作用：
 *   SMARTFS 元数据运行时镜像。
 * 说明：
 *   本模块所有目录、文件和扇区链状态都在该静态镜像中操作，提交时整体写入 Flash
 *   元数据副本，避免在低层存储路径使用堆内存。
 */
static smartfs_image_t g_smartfs_image;

/*
 * 变量作用：
 *   SMARTFS 元数据读回校验和双副本加载使用的静态临时镜像。
 * 说明：
 *   单个元数据镜像约 6KB，不能在函数栈上反复创建副本，否则会放大裸机任务栈风险。
 */
static smartfs_image_t g_smartfs_temp_image;

/*
 * 变量作用：
 *   SMARTFS 元数据加载状态标记。
 * 说明：
 *   运行时命令每次先确保元数据已加载；格式化或提交失败时会清零，防止继续使用旧状态。
 */
static uint8_t g_smartfs_loaded = 0U;

/*
 * 变量作用：
 *   文件追加与扇区搬运使用的 4KB 静态工作缓冲区。
 * 说明：
 *   追加写需要把旧链中的部分内容复制到新链，使用静态缓冲可以避免组件层依赖
 *   App 层 UART 缓冲区，也符合低层存储路径不使用堆内存的约束。
 */
static uint8_t g_smartfs_sector_buffer[SMARTFS_FLASH_SECTOR_SIZE];

/*
 * 函数作用：
 *   计算从指定字节流得到的 CRC32 校验值。
 * 主要流程：
 *   1. 使用常见多项式 0xEDB88320 对每个字节滚动计算。
 *   2. 输入为空且长度为 0 时返回空数据 CRC。
 * 参数说明：
 *   data：待计算数据指针；当 length 大于 0 时必须非空。
 *   length：待计算字节数，单位为字节。
 * 返回值说明：
 *   返回计算得到的 CRC32 值。
 */
static uint32_t prv_smartfs_crc32(const uint8_t *data, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t i;
    uint8_t bit;

    if ((NULL == data) && (length > 0U)) {
        return 0U;
    }

    for (i = 0U; i < length; i++) {
        crc ^= (uint32_t)data[i];
        for (bit = 0U; bit < 8U; bit++) {
            if (0U != (crc & 1UL)) {
                crc = (crc >> 1U) ^ 0xEDB88320UL;
            } else {
                crc >>= 1U;
            }
        }
    }

    return ~crc;
}

/*
 * 函数作用：
 *   判断传入路径是否为当前 SMARTFS 支持的有效绝对路径。
 * 主要流程：
 *   1. 要求路径非空且以 `/` 开头。
 *   2. 限制总长度必须小于固定路径缓冲区。
 * 参数说明：
 *   path：待校验路径字符串，必须非空。
 * 返回值说明：
 *   true：表示路径满足当前模块要求。
 *   false：表示路径为空、不是绝对路径或长度超限。
 */
static bool prv_smartfs_is_valid_abs_path(const char *path)
{
    size_t path_len;

    if ((NULL == path) || ('/' != path[0])) {
        return false;
    }

    path_len = strlen(path);
    if ((0U == path_len) || (path_len >= SMART_STORAGE_PATH_BUFFER_SIZE)) {
        return false;
    }

    return true;
}

/*
 * 函数作用：
 *   将输入路径复制到固定缓冲区，并裁掉非根路径末尾多余的 `/`。
 * 参数说明：
 *   src_path：源绝对路径字符串，必须非空。
 *   dst_path：目标路径缓冲区，必须非空。
 *   dst_size：目标缓冲区总长度，单位为字节。
 * 返回值说明：
 *   SMART_STORAGE_ERR_OK：表示复制和归一化成功。
 *   SMART_STORAGE_ERR_INVAL：表示路径格式或缓冲区参数无效。
 */
static int prv_smartfs_copy_normalized_path(const char *src_path, char *dst_path, uint32_t dst_size)
{
    size_t path_len;

    if ((NULL == dst_path) || (0U == dst_size) || !prv_smartfs_is_valid_abs_path(src_path)) {
        return SMART_STORAGE_ERR_INVAL;
    }

    path_len = strlen(src_path);
    if (path_len >= (size_t)dst_size) {
        return SMART_STORAGE_ERR_INVAL;
    }

    memcpy(dst_path, src_path, path_len + 1U);
    while ((path_len > 1U) && ('/' == dst_path[path_len - 1U])) {
        dst_path[path_len - 1U] = '\0';
        path_len--;
    }

    return SMART_STORAGE_ERR_OK;
}

/*
 * 函数作用：
 *   判断目录项索引是否指向一个有效的活动项。
 * 参数说明：
 *   index：待检查的目录项索引。
 * 返回值说明：
 *   true：索引在范围内且目录项为活动状态。
 *   false：索引越界或目录项未处于活动状态。
 */
static bool prv_smartfs_entry_is_active(uint16_t index)
{
    if (index >= SMART_STORAGE_MAX_ENTRIES) {
        return false;
    }

    return (SMARTFS_ENTRY_STATE_ACTIVE == g_smartfs_image.entries[index].state);
}

/*
 * 函数作用：
 *   在指定父目录下查找同名活动目录项。
 * 参数说明：
 *   parent_index：父目录项索引，必须指向有效目录。
 *   name：待查找的末级名称，必须非空。
 * 返回值说明：
 *   非负值：表示匹配目录项索引。
 *   -1：表示没有找到。
 */
static int16_t prv_smartfs_find_child(uint16_t parent_index, const char *name)
{
    uint16_t i;

    if ((NULL == name) || ('\0' == name[0])) {
        return -1;
    }

    for (i = 1U; i < SMART_STORAGE_MAX_ENTRIES; i++) {
        if ((SMARTFS_ENTRY_STATE_ACTIVE == g_smartfs_image.entries[i].state) &&
            (parent_index == g_smartfs_image.entries[i].parent) &&
            (0 == strcmp(g_smartfs_image.entries[i].name, name))) {
            return (int16_t)i;
        }
    }

    return -1;
}

/*
 * 函数作用：
 *   查找一个可用于新建文件或目录的空闲目录项槽位。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   非负值：表示空闲槽位索引。
 *   -1：表示目录项表已经耗尽。
 */
static int16_t prv_smartfs_find_free_entry(void)
{
    uint16_t i;

    for (i = 1U; i < SMART_STORAGE_MAX_ENTRIES; i++) {
        if (SMARTFS_ENTRY_STATE_ACTIVE != g_smartfs_image.entries[i].state) {
            return (int16_t)i;
        }
    }

    return -1;
}

/*
 * 函数作用：
 *   解析绝对路径，得到目标项索引、父目录索引和末级名称。
 * 主要流程：
 *   1. 先归一化路径并特判根目录。
 *   2. 逐段在当前目录下查找子项，中间段必须存在且为目录。
 *   3. 最后一段允许不存在，并把父目录和末级名称返回给创建类操作。
 * 参数说明：
 *   path：待解析绝对路径，必须非空。
 *   lookup：解析结果输出指针，必须非空。
 * 返回值说明：
 *   SMART_STORAGE_ERR_OK：表示路径解析完成；目标不存在时 entry_index 为 -1。
 *   SMART_STORAGE_ERR_NOENT：表示中间目录不存在。
 *   SMART_STORAGE_ERR_NOTDIR：表示中间路径不是目录。
 *   SMART_STORAGE_ERR_INVAL：表示路径格式、名称长度或参数无效。
 */
static int prv_smartfs_lookup_path(const char *path, smartfs_path_lookup_t *lookup)
{
    char normalized[SMART_STORAGE_PATH_BUFFER_SIZE];
    char *cursor;
    char *slash;
    uint16_t current_index = 0U;
    int16_t child_index;
    size_t segment_len;
    int err;

    if ((NULL == lookup) || !prv_smartfs_is_valid_abs_path(path)) {
        return SMART_STORAGE_ERR_INVAL;
    }

    memset(lookup, 0, sizeof(*lookup));
    lookup->entry_index = -1;

    err = prv_smartfs_copy_normalized_path(path, normalized, sizeof(normalized));
    if (SMART_STORAGE_ERR_OK != err) {
        return err;
    }

    if (0 == strcmp(normalized, "/")) {
        lookup->is_root = 1U;
        lookup->parent_index = 0U;
        lookup->entry_index = 0;
        lookup->leaf_name[0] = '\0';
        return SMART_STORAGE_ERR_OK;
    }

    cursor = &normalized[1];
    for (;;) {
        slash = strchr(cursor, '/');
        if (NULL == slash) {
            segment_len = strlen(cursor);
        } else {
            segment_len = (size_t)(slash - cursor);
        }

        if ((0U == segment_len) || (segment_len > SMART_STORAGE_NAME_MAX)) {
            return SMART_STORAGE_ERR_INVAL;
        }

        memcpy(lookup->leaf_name, cursor, segment_len);
        lookup->leaf_name[segment_len] = '\0';
        child_index = prv_smartfs_find_child(current_index, lookup->leaf_name);

        if (NULL == slash) {
            lookup->parent_index = current_index;
            lookup->entry_index = child_index;
            return SMART_STORAGE_ERR_OK;
        }

        if (child_index < 0) {
            return SMART_STORAGE_ERR_NOENT;
        }

        if (SMART_STORAGE_TYPE_DIR != g_smartfs_image.entries[(uint16_t)child_index].type) {
            return SMART_STORAGE_ERR_NOTDIR;
        }

        current_index = (uint16_t)child_index;
        cursor = slash + 1;
    }
}

/*
 * 函数作用：
 *   校验并规范化数据扇区占用表，确保元数据区和所有文件链都可信。
 * 主要流程：
 *   1. 先确认元数据区被标记为 META，空闲数据区可保持 FREE。
 *   2. 遍历所有活动普通文件，沿持久化链表检查每个数据扇区。
 *   3. 发现链表越界、回环或重复占用时返回损坏错误。
 *   4. 没有被任何文件引用的非空闲数据扇区会回收为 FREE，避免断电残留孤儿块。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   SMART_STORAGE_ERR_OK：表示扇区占用表校验成功。
 *   SMART_STORAGE_ERR_CORRUPT：表示元数据中的文件链不可信。
 */
static int prv_smartfs_validate_block_map(void)
{
    uint8_t visited[SMARTFS_BLOCK_COUNT];
    uint16_t sector;
    uint16_t next_sector;
    uint16_t i;
    uint32_t sectors_needed;
    uint32_t chain_index;

    memset(visited, 0, sizeof(visited));
    for (i = 0U; i < SMARTFS_DATA_START_SECTOR; i++) {
        g_smartfs_image.block_next[i] = SMARTFS_BLOCK_MAP_META;
        visited[i] = 1U;
    }

    for (i = 0U; i < SMART_STORAGE_MAX_ENTRIES; i++) {
        if ((SMARTFS_ENTRY_STATE_ACTIVE != g_smartfs_image.entries[i].state) ||
            (SMART_STORAGE_TYPE_REG != g_smartfs_image.entries[i].type)) {
            continue;
        }

        if (0U == g_smartfs_image.entries[i].size) {
            if (SMARTFS_INVALID_SECTOR != g_smartfs_image.entries[i].first_sector) {
                return SMART_STORAGE_ERR_CORRUPT;
            }
            continue;
        }

        if (SMARTFS_INVALID_SECTOR == g_smartfs_image.entries[i].first_sector) {
            return SMART_STORAGE_ERR_CORRUPT;
        }

        sectors_needed = g_smartfs_image.entries[i].size / SMARTFS_FLASH_SECTOR_SIZE;
        if (0U != (g_smartfs_image.entries[i].size % SMARTFS_FLASH_SECTOR_SIZE)) {
            sectors_needed++;
        }

        if ((0U == sectors_needed) || (sectors_needed > SMARTFS_DATA_SECTOR_COUNT)) {
            return SMART_STORAGE_ERR_CORRUPT;
        }

        sector = g_smartfs_image.entries[i].first_sector;
        for (chain_index = 0U; chain_index < sectors_needed; chain_index++) {
            if ((sector < SMARTFS_DATA_START_SECTOR) || (sector >= SMARTFS_BLOCK_COUNT)) {
                return SMART_STORAGE_ERR_CORRUPT;
            }

            if (0U != visited[sector]) {
                return SMART_STORAGE_ERR_CORRUPT;
            }

            visited[sector] = 1U;
            next_sector = g_smartfs_image.block_next[sector];

            /*
             * 文件大小决定链表应占用的精确扇区数。
             * 提前结束或额外链接都说明元数据不可信，不能继续挂载。
             */
            if ((chain_index + 1U) == sectors_needed) {
                if (SMARTFS_BLOCK_MAP_END != next_sector) {
                    return SMART_STORAGE_ERR_CORRUPT;
                }
                break;
            }

            if ((next_sector < SMARTFS_DATA_START_SECTOR) || (next_sector >= SMARTFS_BLOCK_COUNT)) {
                return SMART_STORAGE_ERR_CORRUPT;
            }

            sector = next_sector;
        }
    }

    for (i = SMARTFS_DATA_START_SECTOR; i < SMARTFS_BLOCK_COUNT; i++) {
        if (0U == visited[i]) {
            g_smartfs_image.block_next[i] = SMARTFS_BLOCK_MAP_FREE;
        }
    }

    return SMART_STORAGE_ERR_OK;
}

/*
 * 函数作用：
 *   校验目录项表的父子关系、名称和类型字段是否符合当前 SMARTFS 约束。
 * 主要流程：
 *   1. 根目录必须固定在 0 号槽位，并且父索引指向自身。
 *   2. 普通目录项必须拥有非空名称，父项必须是活动目录。
 *   3. 同一父目录下不允许出现重名活动项，避免路径解析结果不确定。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   SMART_STORAGE_ERR_OK：表示目录项表结构可信。
 *   SMART_STORAGE_ERR_CORRUPT：表示目录项类型、父子关系或名称冲突异常。
 */
static int prv_smartfs_validate_entries(void)
{
    uint16_t i;
    uint16_t j;
    uint16_t parent_index;
    uint16_t guard;

    if ((SMARTFS_ENTRY_STATE_ACTIVE != g_smartfs_image.entries[0].state) ||
        (SMART_STORAGE_TYPE_DIR != g_smartfs_image.entries[0].type) ||
        (0U != g_smartfs_image.entries[0].parent) ||
        (SMARTFS_INVALID_SECTOR != g_smartfs_image.entries[0].first_sector) ||
        (0U != g_smartfs_image.entries[0].size) ||
        ('\0' != g_smartfs_image.entries[0].name[0])) {
        return SMART_STORAGE_ERR_CORRUPT;
    }

    for (i = 1U; i < SMART_STORAGE_MAX_ENTRIES; i++) {
        if (SMARTFS_ENTRY_STATE_ACTIVE != g_smartfs_image.entries[i].state) {
            continue;
        }

        if ((SMART_STORAGE_TYPE_REG != g_smartfs_image.entries[i].type) &&
            (SMART_STORAGE_TYPE_DIR != g_smartfs_image.entries[i].type)) {
            return SMART_STORAGE_ERR_CORRUPT;
        }

        if (i == g_smartfs_image.entries[i].parent) {
            return SMART_STORAGE_ERR_CORRUPT;
        }

        if ((g_smartfs_image.entries[i].parent >= SMART_STORAGE_MAX_ENTRIES) ||
            !prv_smartfs_entry_is_active(g_smartfs_image.entries[i].parent) ||
            (SMART_STORAGE_TYPE_DIR != g_smartfs_image.entries[g_smartfs_image.entries[i].parent].type)) {
            return SMART_STORAGE_ERR_CORRUPT;
        }

        /*
         * 继续向上追溯父目录，确认每个活动项最终都挂在根目录下。
         * 这样可以提前拒绝父目录环，避免后续递归计算目录大小时无限递归。
         */
        parent_index = g_smartfs_image.entries[i].parent;
        guard = 0U;
        while (0U != parent_index) {
            if ((parent_index >= SMART_STORAGE_MAX_ENTRIES) ||
                !prv_smartfs_entry_is_active(parent_index) ||
                (SMART_STORAGE_TYPE_DIR != g_smartfs_image.entries[parent_index].type)) {
                return SMART_STORAGE_ERR_CORRUPT;
            }

            parent_index = g_smartfs_image.entries[parent_index].parent;
            guard++;
            if (guard > SMART_STORAGE_MAX_ENTRIES) {
                return SMART_STORAGE_ERR_CORRUPT;
            }
        }

        if (('\0' == g_smartfs_image.entries[i].name[0]) ||
            ('\0' != g_smartfs_image.entries[i].name[SMART_STORAGE_NAME_MAX])) {
            return SMART_STORAGE_ERR_CORRUPT;
        }

        if (SMART_STORAGE_TYPE_DIR == g_smartfs_image.entries[i].type) {
            if ((SMARTFS_INVALID_SECTOR != g_smartfs_image.entries[i].first_sector) ||
                (0U != g_smartfs_image.entries[i].size)) {
                return SMART_STORAGE_ERR_CORRUPT;
            }
        }

        for (j = (uint16_t)(i + 1U); j < SMART_STORAGE_MAX_ENTRIES; j++) {
            if ((SMARTFS_ENTRY_STATE_ACTIVE == g_smartfs_image.entries[j].state) &&
                (g_smartfs_image.entries[i].parent == g_smartfs_image.entries[j].parent) &&
                (0 == strcmp(g_smartfs_image.entries[i].name, g_smartfs_image.entries[j].name))) {
                return SMART_STORAGE_ERR_CORRUPT;
            }
        }
    }

    return SMART_STORAGE_ERR_OK;
}

/*
 * 函数作用：
 *   初始化空白文件系统镜像的数据扇区占用表。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
static void prv_smartfs_reset_block_map(void)
{
    uint16_t i;

    for (i = 0U; i < SMARTFS_BLOCK_COUNT; i++) {
        if (i < SMARTFS_DATA_START_SECTOR) {
            g_smartfs_image.block_next[i] = SMARTFS_BLOCK_MAP_META;
        } else {
            g_smartfs_image.block_next[i] = SMARTFS_BLOCK_MAP_FREE;
        }
    }
}

/*
 * 函数作用：
 *   统计当前数据区已经被文件占用的扇区数量。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   返回已经占用的数据扇区数量，不包含元数据区。
 */
static uint32_t prv_smartfs_count_used_data_blocks(void)
{
    uint32_t used_count = 0U;
    uint16_t i;

    for (i = SMARTFS_DATA_START_SECTOR; i < SMARTFS_BLOCK_COUNT; i++) {
        if (SMARTFS_BLOCK_MAP_FREE != g_smartfs_image.block_next[i]) {
            used_count++;
        }
    }

    return used_count;
}

/*
 * 函数作用：
 *   从空闲数据区中分配一个 4KB 物理扇区。
 * 参数说明：
 *   out_sector：分配到的物理扇区号输出指针，必须非空。
 * 返回值说明：
 *   SMART_STORAGE_ERR_OK：表示分配成功。
 *   SMART_STORAGE_ERR_NOSPC：表示没有可用数据扇区。
 *   SMART_STORAGE_ERR_INVAL：表示输出指针为空。
 */
static int prv_smartfs_alloc_sector(uint16_t *out_sector)
{
    uint16_t sector;

    if (NULL == out_sector) {
        return SMART_STORAGE_ERR_INVAL;
    }

    for (sector = SMARTFS_DATA_START_SECTOR; sector < SMARTFS_BLOCK_COUNT; sector++) {
        if (SMARTFS_BLOCK_MAP_FREE == g_smartfs_image.block_next[sector]) {
            g_smartfs_image.block_next[sector] = SMARTFS_BLOCK_MAP_END;
            *out_sector = sector;
            return SMART_STORAGE_ERR_OK;
        }
    }

    return SMART_STORAGE_ERR_NOSPC;
}

/*
 * 函数作用：
 *   擦除并释放指定文件的数据扇区链。
 * 参数说明：
 *   first_sector：文件第一数据扇区；为空文件时为 SMARTFS_INVALID_SECTOR。
 * 返回值说明：
 *   SMART_STORAGE_ERR_OK：表示释放成功。
 *   SMART_STORAGE_ERR_CORRUPT：表示链表越界或损坏。
 */
static int prv_smartfs_release_chain(uint16_t first_sector)
{
    uint16_t sector = first_sector;
    uint16_t next_sector;
    uint16_t guard = 0U;

    if (SMARTFS_INVALID_SECTOR == first_sector) {
        return SMART_STORAGE_ERR_OK;
    }

    while (SMARTFS_BLOCK_MAP_END != sector) {
        if ((sector < SMARTFS_DATA_START_SECTOR) || (sector >= SMARTFS_BLOCK_COUNT)) {
            return SMART_STORAGE_ERR_CORRUPT;
        }

        next_sector = g_smartfs_image.block_next[sector];
        if ((SMARTFS_BLOCK_MAP_END != next_sector) &&
            ((next_sector < SMARTFS_DATA_START_SECTOR) || (next_sector >= SMARTFS_BLOCK_COUNT))) {
            return SMART_STORAGE_ERR_CORRUPT;
        }

        if(0 != spi_flash_sector_erase((uint32_t)sector * SMARTFS_FLASH_SECTOR_SIZE)) {
            return SMART_STORAGE_ERR_IO;
        }
        g_smartfs_image.block_next[sector] = SMARTFS_BLOCK_MAP_FREE;
        sector = next_sector;

        guard++;
        if (guard > SMARTFS_DATA_SECTOR_COUNT) {
            return SMART_STORAGE_ERR_CORRUPT;
        }
    }

    return SMART_STORAGE_ERR_OK;
}

/*
 * 函数作用：
 *   从文件数据链读取指定长度内容到调用者缓冲区。
 * 参数说明：
 *   first_sector：文件第一数据扇区；空文件可为 SMARTFS_INVALID_SECTOR。
 *   file_size：文件总长度，单位为字节。
 *   buffer：接收缓冲区，必须非空。
 * 返回值说明：
 *   SMART_STORAGE_ERR_OK：表示读回成功。
 *   SMART_STORAGE_ERR_CORRUPT：表示文件链越界或长度与链表不匹配。
 *   SMART_STORAGE_ERR_INVAL：表示参数无效。
 */
static int prv_smartfs_read_chain(uint16_t first_sector, uint32_t file_size, uint8_t *buffer)
{
    uint32_t remaining = file_size;
    uint32_t copy_len;
    uint16_t sector = first_sector;
    uint16_t guard = 0U;
    uint8_t *write_ptr = buffer;

    if ((NULL == buffer) && (file_size > 0U)) {
        return SMART_STORAGE_ERR_INVAL;
    }

    if (0U == file_size) {
        return SMART_STORAGE_ERR_OK;
    }

    while (remaining > 0U) {
        if ((sector < SMARTFS_DATA_START_SECTOR) || (sector >= SMARTFS_BLOCK_COUNT)) {
            return SMART_STORAGE_ERR_CORRUPT;
        }

        copy_len = remaining;
        if (copy_len > SMARTFS_FLASH_SECTOR_SIZE) {
            copy_len = SMARTFS_FLASH_SECTOR_SIZE;
        }

        spi_flash_buffer_read(write_ptr,
                              (uint32_t)sector * SMARTFS_FLASH_SECTOR_SIZE,
                              (uint16_t)copy_len);
        write_ptr += copy_len;
        remaining -= copy_len;

        if (0U == remaining) {
            break;
        }

        sector = g_smartfs_image.block_next[sector];
        guard++;
        if (guard > SMARTFS_DATA_SECTOR_COUNT) {
            return SMART_STORAGE_ERR_CORRUPT;
        }
    }

    return SMART_STORAGE_ERR_OK;
}

/*
 * 函数作用：
 *   从文件数据链的指定偏移处读取一段连续内容。
 * 主要流程：
 *   1. 先按 4KB 扇区跨度跳到目标起始偏移所在的数据扇区。
 *   2. 按当前扇区剩余空间分段读取，跨扇区时沿链表继续前进。
 *   3. 对每一次链表跳转做越界和回环保护，避免损坏元数据导致死循环。
 * 参数说明：
 *   first_sector：文件第一数据扇区；读取长度大于 0 时必须是有效数据扇区。
 *   offset：文件内起始偏移，单位为字节。
 *   buffer：接收缓冲区，必须非空。
 *   length：待读取长度，单位为字节。
 * 返回值说明：
 *   SMART_STORAGE_ERR_OK：表示读取成功。
 *   SMART_STORAGE_ERR_INVAL：表示参数无效。
 *   SMART_STORAGE_ERR_CORRUPT：表示文件链越界或长度与链表不匹配。
 */
static int prv_smartfs_read_range(uint16_t first_sector,
                                  uint32_t offset,
                                  uint8_t *buffer,
                                  uint32_t length)
{
    uint16_t sector = first_sector;
    uint32_t sector_offset = offset;
    uint32_t remaining = length;
    uint32_t copy_len;
    uint16_t guard = 0U;
    uint8_t *write_ptr = buffer;

    if ((NULL == buffer) && (length > 0U)) {
        return SMART_STORAGE_ERR_INVAL;
    }

    if (0U == length) {
        return SMART_STORAGE_ERR_OK;
    }

    while (sector_offset >= SMARTFS_FLASH_SECTOR_SIZE) {
        if ((sector < SMARTFS_DATA_START_SECTOR) || (sector >= SMARTFS_BLOCK_COUNT)) {
            return SMART_STORAGE_ERR_CORRUPT;
        }

        sector = g_smartfs_image.block_next[sector];
        sector_offset -= SMARTFS_FLASH_SECTOR_SIZE;
        guard++;
        if (guard > SMARTFS_DATA_SECTOR_COUNT) {
            return SMART_STORAGE_ERR_CORRUPT;
        }
    }

    while (remaining > 0U) {
        if ((sector < SMARTFS_DATA_START_SECTOR) || (sector >= SMARTFS_BLOCK_COUNT)) {
            return SMART_STORAGE_ERR_CORRUPT;
        }

        copy_len = SMARTFS_FLASH_SECTOR_SIZE - sector_offset;
        if (copy_len > remaining) {
            copy_len = remaining;
        }

        spi_flash_buffer_read(write_ptr,
                              ((uint32_t)sector * SMARTFS_FLASH_SECTOR_SIZE) + sector_offset,
                              (uint16_t)copy_len);
        write_ptr += copy_len;
        remaining -= copy_len;
        sector_offset = 0U;

        if (0U == remaining) {
            break;
        }

        sector = g_smartfs_image.block_next[sector];
        guard++;
        if (guard > SMARTFS_DATA_SECTOR_COUNT) {
            return SMART_STORAGE_ERR_CORRUPT;
        }
    }

    return SMART_STORAGE_ERR_OK;
}

/*
 * 函数作用：
 *   为一段数据分配扇区链并写入 Flash。
 * 主要流程：
 *   1. 按数据长度逐扇区分配空闲物理扇区。
 *   2. 每个扇区写入前先擦除，最后不足 4KB 的扇区只写有效字节。
 *   3. 写入失败路径会释放已经分配的链，避免元数据表残留孤儿扇区。
 * 参数说明：
 *   data：待写入数据；当 length 大于 0 时必须非空。
 *   length：待写入字节数，单位为字节。
 *   out_first_sector：写入成功后的首扇区输出指针，必须非空。
 * 返回值说明：
 *   SMART_STORAGE_ERR_OK：表示数据链写入成功。
 *   SMART_STORAGE_ERR_NOSPC：表示数据区空间不足。
 *   SMART_STORAGE_ERR_INVAL：表示参数无效。
 */
static int prv_smartfs_write_chain(const uint8_t *data, uint32_t length, uint16_t *out_first_sector)
{
    uint32_t remaining = length;
    uint32_t offset = 0U;
    uint32_t chunk_len;
    uint16_t first_sector = SMARTFS_INVALID_SECTOR;
    uint16_t previous_sector = SMARTFS_INVALID_SECTOR;
    uint16_t current_sector = SMARTFS_INVALID_SECTOR;
    int err;

    if ((NULL == out_first_sector) || ((NULL == data) && (length > 0U))) {
        return SMART_STORAGE_ERR_INVAL;
    }

    *out_first_sector = SMARTFS_INVALID_SECTOR;
    if (0U == length) {
        return SMART_STORAGE_ERR_OK;
    }

    while (remaining > 0U) {
        err = prv_smartfs_alloc_sector(&current_sector);
        if (SMART_STORAGE_ERR_OK != err) {
            (void)prv_smartfs_release_chain(first_sector);
            return err;
        }

        if (SMARTFS_INVALID_SECTOR == first_sector) {
            first_sector = current_sector;
        }

        if (SMARTFS_INVALID_SECTOR != previous_sector) {
            g_smartfs_image.block_next[previous_sector] = current_sector;
        }

        chunk_len = remaining;
        if (chunk_len > SMARTFS_FLASH_SECTOR_SIZE) {
            chunk_len = SMARTFS_FLASH_SECTOR_SIZE;
        }

        if(0 != spi_flash_sector_erase((uint32_t)current_sector * SMARTFS_FLASH_SECTOR_SIZE)) {
            (void)prv_smartfs_release_chain(first_sector);
            return SMART_STORAGE_ERR_IO;
        }
        if(0 != spi_flash_buffer_write((uint8_t *)&data[offset],
                                       (uint32_t)current_sector * SMARTFS_FLASH_SECTOR_SIZE,
                                       (uint16_t)chunk_len)) {
            (void)prv_smartfs_release_chain(first_sector);
            return SMART_STORAGE_ERR_IO;
        }

        previous_sector = current_sector;
        offset += chunk_len;
        remaining -= chunk_len;
    }

    if (SMARTFS_INVALID_SECTOR != previous_sector) {
        g_smartfs_image.block_next[previous_sector] = SMARTFS_BLOCK_MAP_END;
    }

    *out_first_sector = first_sector;
    return SMART_STORAGE_ERR_OK;
}

/*
 * 函数作用：
 *   复制旧文件链内容并追加新数据，生成一条新的数据扇区链。
 * 主要流程：
 *   1. 按新文件长度逐扇区分配目标扇区。
 *   2. 每个目标扇区先从旧链读取原有字节，再补充本次追加数据。
 *   3. 新链完整写入后才交给调用者替换目录项，降低追加失败时旧目录项被破坏的概率。
 * 参数说明：
 *   old_first_sector：旧文件第一数据扇区；空文件为 SMARTFS_INVALID_SECTOR。
 *   original_size：旧文件长度，单位为字节。
 *   append_data：待追加数据；当 append_length 大于 0 时必须非空。
 *   append_length：待追加数据长度，单位为字节。
 *   out_first_sector：新链第一数据扇区输出指针，必须非空。
 * 返回值说明：
 *   SMART_STORAGE_ERR_OK：表示新数据链生成成功。
 *   其他 SMART_STORAGE_ERR_*：表示空间不足、旧链损坏或参数无效。
 */
static int prv_smartfs_append_chain(uint16_t old_first_sector,
                                    uint32_t original_size,
                                    const uint8_t *append_data,
                                    uint32_t append_length,
                                    uint16_t *out_first_sector)
{
    uint32_t new_size = original_size + append_length;
    uint32_t new_offset = 0U;
    uint32_t old_offset = 0U;
    uint32_t append_offset = 0U;
    uint32_t old_copy_len;
    uint32_t append_copy_len;
    uint32_t chunk_len;
    uint16_t first_sector = SMARTFS_INVALID_SECTOR;
    uint16_t previous_sector = SMARTFS_INVALID_SECTOR;
    uint16_t current_sector = SMARTFS_INVALID_SECTOR;
    int err;

    if ((NULL == out_first_sector) || ((NULL == append_data) && (append_length > 0U))) {
        return SMART_STORAGE_ERR_INVAL;
    }

    *out_first_sector = SMARTFS_INVALID_SECTOR;
    if (0U == new_size) {
        return SMART_STORAGE_ERR_OK;
    }

    while (new_offset < new_size) {
        err = prv_smartfs_alloc_sector(&current_sector);
        if (SMART_STORAGE_ERR_OK != err) {
            (void)prv_smartfs_release_chain(first_sector);
            return err;
        }

        if (SMARTFS_INVALID_SECTOR == first_sector) {
            first_sector = current_sector;
        }

        if (SMARTFS_INVALID_SECTOR != previous_sector) {
            g_smartfs_image.block_next[previous_sector] = current_sector;
        }

        memset(g_smartfs_sector_buffer, 0xFF, sizeof(g_smartfs_sector_buffer));
        chunk_len = new_size - new_offset;
        if (chunk_len > SMARTFS_FLASH_SECTOR_SIZE) {
            chunk_len = SMARTFS_FLASH_SECTOR_SIZE;
        }

        old_copy_len = 0U;
        if (old_offset < original_size) {
            old_copy_len = original_size - old_offset;
            if (old_copy_len > chunk_len) {
                old_copy_len = chunk_len;
            }

            err = prv_smartfs_read_range(old_first_sector, old_offset, g_smartfs_sector_buffer, old_copy_len);
            if (SMART_STORAGE_ERR_OK != err) {
                (void)prv_smartfs_release_chain(first_sector);
                return err;
            }
            old_offset += old_copy_len;
        }

        append_copy_len = chunk_len - old_copy_len;
        if (append_copy_len > 0U) {
            memcpy(&g_smartfs_sector_buffer[old_copy_len],
                   &append_data[append_offset],
                   append_copy_len);
            append_offset += append_copy_len;
        }

        if(0 != spi_flash_sector_erase((uint32_t)current_sector * SMARTFS_FLASH_SECTOR_SIZE)) {
            (void)prv_smartfs_release_chain(first_sector);
            return SMART_STORAGE_ERR_IO;
        }
        if(0 != spi_flash_buffer_write(g_smartfs_sector_buffer,
                                       (uint32_t)current_sector * SMARTFS_FLASH_SECTOR_SIZE,
                                       (uint16_t)chunk_len)) {
            (void)prv_smartfs_release_chain(first_sector);
            return SMART_STORAGE_ERR_IO;
        }

        previous_sector = current_sector;
        new_offset += chunk_len;
    }

    if (SMARTFS_INVALID_SECTOR != previous_sector) {
        g_smartfs_image.block_next[previous_sector] = SMARTFS_BLOCK_MAP_END;
    }

    *out_first_sector = first_sector;
    return SMART_STORAGE_ERR_OK;
}

/*
 * 函数作用：
 *   计算指定元数据镜像的 CRC32。
 * 参数说明：
 *   image：待计算的元数据镜像指针，必须非空。
 * 返回值说明：
 *   返回从 header.crc32 之后到镜像末尾的 CRC32 值。
 */
static uint32_t prv_smartfs_image_crc(const smartfs_image_t *image)
{
    const uint8_t *start;
    uint32_t offset;
    uint32_t length;

    if (NULL == image) {
        return 0U;
    }

    offset = (uint32_t)((const uint8_t *)&image->header.crc32 - (const uint8_t *)image);
    offset += sizeof(image->header.crc32);
    start = ((const uint8_t *)image) + offset;
    length = (uint32_t)sizeof(*image) - offset;
    return prv_smartfs_crc32(start, length);
}

/*
 * 函数作用：
 *   校验元数据镜像头部和 CRC 是否符合当前工程格式。
 * 参数说明：
 *   image：待校验元数据镜像指针，必须非空。
 * 返回值说明：
 *   SMART_STORAGE_ERR_OK：表示镜像有效。
 *   SMART_STORAGE_ERR_CORRUPT：表示魔数、版本、几何信息或 CRC 不匹配。
 */
static int prv_smartfs_validate_image(const smartfs_image_t *image)
{
    uint32_t crc;

    if (NULL == image) {
        return SMART_STORAGE_ERR_INVAL;
    }

    if ((SMARTFS_MAGIC != image->header.magic) ||
        (SMARTFS_VERSION != image->header.version) ||
        (sizeof(smartfs_disk_header_t) != image->header.header_size) ||
        (SMARTFS_BLOCK_COUNT != image->header.total_sectors) ||
        (SMARTFS_DATA_START_SECTOR != image->header.data_start_sector) ||
        (SMART_STORAGE_MAX_ENTRIES != image->header.entry_count)) {
        return SMART_STORAGE_ERR_CORRUPT;
    }

    crc = prv_smartfs_image_crc(image);
    if (crc != image->header.crc32) {
        return SMART_STORAGE_ERR_CORRUPT;
    }

    if ((SMARTFS_ENTRY_STATE_ACTIVE != image->entries[0].state) ||
        (SMART_STORAGE_TYPE_DIR != image->entries[0].type) ||
        (0U != image->entries[0].parent)) {
        return SMART_STORAGE_ERR_CORRUPT;
    }

    return SMART_STORAGE_ERR_OK;
}

/*
 * 函数作用：
 *   从指定元数据副本读取完整 SMARTFS 镜像。
 * 参数说明：
 *   copy_index：元数据副本编号，取值范围为 0 到 SMARTFS_META_COPY_COUNT-1。
 *   image：镜像输出指针，必须非空。
 * 返回值说明：
 *   SMART_STORAGE_ERR_OK：表示读取完成且镜像校验通过。
 *   其他 SMART_STORAGE_ERR_*：表示参数错误或镜像损坏。
 */
static int prv_smartfs_read_meta_copy(uint8_t copy_index, smartfs_image_t *image)
{
    uint32_t copy_addr;
    uint32_t offset = 0U;
    uint32_t copy_bytes;
    uint32_t chunk_len;

    if ((copy_index >= SMARTFS_META_COPY_COUNT) || (NULL == image)) {
        return SMART_STORAGE_ERR_INVAL;
    }

    memset(image, 0xFF, sizeof(*image));
    copy_addr = (uint32_t)copy_index * SMARTFS_META_COPY_SECTOR_COUNT * SMARTFS_FLASH_SECTOR_SIZE;
    copy_bytes = (uint32_t)sizeof(*image);

    while (offset < copy_bytes) {
        chunk_len = copy_bytes - offset;
        if (chunk_len > SMARTFS_FLASH_SECTOR_SIZE) {
            chunk_len = SMARTFS_FLASH_SECTOR_SIZE;
        }

        spi_flash_buffer_read(((uint8_t *)image) + offset,
                              copy_addr + offset,
                              (uint16_t)chunk_len);
        offset += chunk_len;
    }

    return prv_smartfs_validate_image(image);
}

/*
 * 函数作用：
 *   擦除并写入指定元数据副本。
 * 主要流程：
 *   1. 擦除副本占用的全部 4KB 扇区。
 *   2. 按镜像实际长度分片写入 Flash。
 *   3. 读回副本并重新校验，确保提交确实落盘。
 * 参数说明：
 *   copy_index：元数据副本编号，取值范围为 0 到 SMARTFS_META_COPY_COUNT-1。
 *   image：待写入镜像指针，必须非空。
 * 返回值说明：
 *   SMART_STORAGE_ERR_OK：表示写入和读回校验成功。
 *   其他 SMART_STORAGE_ERR_*：表示参数、写入或读回校验失败。
 */
static int prv_smartfs_write_meta_copy(uint8_t copy_index, const smartfs_image_t *image)
{
    uint32_t copy_addr;
    uint32_t offset = 0U;
    uint32_t copy_bytes;
    uint32_t chunk_len;
    uint8_t sector_offset;
    int err;

    if ((copy_index >= SMARTFS_META_COPY_COUNT) || (NULL == image)) {
        return SMART_STORAGE_ERR_INVAL;
    }

    copy_addr = (uint32_t)copy_index * SMARTFS_META_COPY_SECTOR_COUNT * SMARTFS_FLASH_SECTOR_SIZE;
    for (sector_offset = 0U; sector_offset < SMARTFS_META_COPY_SECTOR_COUNT; sector_offset++) {
        if(0 != spi_flash_sector_erase(copy_addr + ((uint32_t)sector_offset * SMARTFS_FLASH_SECTOR_SIZE))) {
            return SMART_STORAGE_ERR_IO;
        }
    }

    copy_bytes = (uint32_t)sizeof(*image);
    while (offset < copy_bytes) {
        chunk_len = copy_bytes - offset;
        if (chunk_len > SMARTFS_FLASH_SECTOR_SIZE) {
            chunk_len = SMARTFS_FLASH_SECTOR_SIZE;
        }

        if(0 != spi_flash_buffer_write((uint8_t *)(((const uint8_t *)image) + offset),
                                       copy_addr + offset,
                                       (uint16_t)chunk_len)) {
            return SMART_STORAGE_ERR_IO;
        }
        offset += chunk_len;
    }

    err = prv_smartfs_read_meta_copy(copy_index, &g_smartfs_temp_image);
    if (SMART_STORAGE_ERR_OK != err) {
        return err;
    }

    if (0 != memcmp(&g_smartfs_temp_image, image, sizeof(*image))) {
        return SMART_STORAGE_ERR_IO;
    }

    return SMART_STORAGE_ERR_OK;
}

/*
 * 函数作用：
 *   提交当前运行时元数据镜像到 Flash 双副本中的下一副本。
 * 主要流程：
 *   1. 更新提交序号和 CRC。
 *   2. 按序号在两个副本之间轮换写入，避免只反复擦写同一元数据区。
 *   3. 写入失败时清除加载标记，阻止后续命令继续基于未确认状态运行。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   SMART_STORAGE_ERR_OK：表示提交成功。
 *   其他 SMART_STORAGE_ERR_*：表示元数据写入或读回校验失败。
 */
static int prv_smartfs_commit(void)
{
    uint8_t copy_index;
    int err;

    g_smartfs_image.header.magic = SMARTFS_MAGIC;
    g_smartfs_image.header.version = SMARTFS_VERSION;
    g_smartfs_image.header.header_size = (uint16_t)sizeof(smartfs_disk_header_t);
    g_smartfs_image.header.total_sectors = SMARTFS_BLOCK_COUNT;
    g_smartfs_image.header.data_start_sector = SMARTFS_DATA_START_SECTOR;
    g_smartfs_image.header.entry_count = SMART_STORAGE_MAX_ENTRIES;
    g_smartfs_image.header.sequence++;
    g_smartfs_image.header.crc32 = 0U;
    g_smartfs_image.header.crc32 = prv_smartfs_image_crc(&g_smartfs_image);

    copy_index = (uint8_t)(g_smartfs_image.header.sequence % SMARTFS_META_COPY_COUNT);
    err = prv_smartfs_write_meta_copy(copy_index, &g_smartfs_image);
    if (SMART_STORAGE_ERR_OK != err) {
        g_smartfs_loaded = 0U;
        return err;
    }

    g_smartfs_loaded = 1U;
    return SMART_STORAGE_ERR_OK;
}

/*
 * 函数作用：
 *   从 Flash 双元数据副本中加载序号最新的有效 SMARTFS 镜像。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   SMART_STORAGE_ERR_OK：表示加载成功。
 *   SMART_STORAGE_ERR_CORRUPT：表示两个副本都无效。
 */
static int prv_smartfs_load(void)
{
    uint32_t copy0_sequence = 0U;
    uint32_t copy1_sequence = 0U;
    int err0;
    int err1;
    int err;

    err0 = prv_smartfs_read_meta_copy(0U, &g_smartfs_image);
    if (SMART_STORAGE_ERR_OK == err0) {
        copy0_sequence = g_smartfs_image.header.sequence;
    }

    err1 = prv_smartfs_read_meta_copy(1U, &g_smartfs_temp_image);
    if (SMART_STORAGE_ERR_OK == err1) {
        copy1_sequence = g_smartfs_temp_image.header.sequence;
    }

    if ((SMART_STORAGE_ERR_OK != err0) && (SMART_STORAGE_ERR_OK != err1)) {
        g_smartfs_loaded = 0U;
        return SMART_STORAGE_ERR_CORRUPT;
    }

    if ((SMART_STORAGE_ERR_OK == err1) &&
        ((SMART_STORAGE_ERR_OK != err0) || (copy1_sequence > copy0_sequence))) {
        memcpy(&g_smartfs_image, &g_smartfs_temp_image, sizeof(g_smartfs_image));
    }

    err = prv_smartfs_validate_entries();
    if (SMART_STORAGE_ERR_OK != err) {
        g_smartfs_loaded = 0U;
        return err;
    }

    err = prv_smartfs_validate_block_map();
    if (SMART_STORAGE_ERR_OK != err) {
        g_smartfs_loaded = 0U;
        return err;
    }

    g_smartfs_loaded = 1U;
    return SMART_STORAGE_ERR_OK;
}

/*
 * 函数作用：
 *   确保 SMARTFS 元数据已经加载到运行时镜像。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   SMART_STORAGE_ERR_OK：表示元数据已可用。
 *   其他 SMART_STORAGE_ERR_*：表示元数据无效或加载失败。
 */
static int prv_smartfs_ensure_loaded(void)
{
    if (0U != g_smartfs_loaded) {
        return SMART_STORAGE_ERR_OK;
    }

    return prv_smartfs_load();
}

/*
 * 函数作用：
 *   初始化一个空白 SMARTFS 元数据镜像。
 * 参数说明：
 *   sequence：初始提交序号，通常为 0。
 * 返回值说明：
 *   无返回值。
 */
static void prv_smartfs_init_empty_image(uint32_t sequence)
{
    memset(&g_smartfs_image, 0xFF, sizeof(g_smartfs_image));

    g_smartfs_image.header.magic = SMARTFS_MAGIC;
    g_smartfs_image.header.version = SMARTFS_VERSION;
    g_smartfs_image.header.header_size = (uint16_t)sizeof(smartfs_disk_header_t);
    g_smartfs_image.header.sequence = sequence;
    g_smartfs_image.header.total_sectors = SMARTFS_BLOCK_COUNT;
    g_smartfs_image.header.data_start_sector = SMARTFS_DATA_START_SECTOR;
    g_smartfs_image.header.entry_count = SMART_STORAGE_MAX_ENTRIES;
    g_smartfs_image.header.reserved0 = 0U;
    g_smartfs_image.header.crc32 = 0U;

    memset(g_smartfs_image.entries, 0xFF, sizeof(g_smartfs_image.entries));
    g_smartfs_image.entries[0].state = SMARTFS_ENTRY_STATE_ACTIVE;
    g_smartfs_image.entries[0].type = SMART_STORAGE_TYPE_DIR;
    g_smartfs_image.entries[0].parent = 0U;
    g_smartfs_image.entries[0].first_sector = SMARTFS_INVALID_SECTOR;
    g_smartfs_image.entries[0].reserved0 = 0U;
    g_smartfs_image.entries[0].size = 0U;
    g_smartfs_image.entries[0].name[0] = '\0';

    prv_smartfs_reset_block_map();
}

/*
 * 函数作用：
 *   递归统计指定目录项下所有普通文件内容字节数。
 * 参数说明：
 *   entry_index：待统计目录项索引，可以是文件或目录。
 *   out_size：统计结果输出指针，必须非空。
 * 返回值说明：
 *   SMART_STORAGE_ERR_OK：表示统计成功。
 *   SMART_STORAGE_ERR_INVAL：表示索引或输出指针无效。
 */
static int prv_smartfs_calculate_entry_size(uint16_t entry_index, uint32_t *out_size)
{
    uint16_t i;
    uint32_t child_size;
    int err;

    if ((NULL == out_size) || !prv_smartfs_entry_is_active(entry_index)) {
        return SMART_STORAGE_ERR_INVAL;
    }

    if (SMART_STORAGE_TYPE_REG == g_smartfs_image.entries[entry_index].type) {
        *out_size = g_smartfs_image.entries[entry_index].size;
        return SMART_STORAGE_ERR_OK;
    }

    *out_size = 0U;
    for (i = 1U; i < SMART_STORAGE_MAX_ENTRIES; i++) {
        if ((SMARTFS_ENTRY_STATE_ACTIVE == g_smartfs_image.entries[i].state) &&
            (entry_index == g_smartfs_image.entries[i].parent)) {
            child_size = 0U;
            err = prv_smartfs_calculate_entry_size(i, &child_size);
            if (SMART_STORAGE_ERR_OK != err) {
                return err;
            }
            *out_size += child_size;
        }
    }

    return SMART_STORAGE_ERR_OK;
}

/*
 * 函数作用：
 *   递归把指定目录项标记为已删除，并收集后续需要释放的数据链。
 * 主要流程：
 *   1. 目录先递归标记全部子项，普通文件记录其首数据扇区。
 *   2. 只修改元数据目录项状态，不立即擦除数据扇区。
 *   3. 调用者应先提交删除后的元数据，再释放收集到的数据链。
 * 参数说明：
 *   entry_index：待标记目录项索引，不能为根目录 0。
 *   release_list：待释放文件链首扇区列表，必须非空。
 *   release_count：列表中当前有效数量输入输出指针，必须非空。
 *   release_capacity：release_list 可容纳的元素数量。
 * 返回值说明：
 *   SMART_STORAGE_ERR_OK：表示标记成功。
 *   SMART_STORAGE_ERR_INVAL：表示索引、列表或容量无效。
 *   SMART_STORAGE_ERR_NOSPC：表示待释放文件数量超过列表容量。
 */
static int prv_smartfs_mark_entry_deleted_recursive(uint16_t entry_index,
                                                    uint16_t *release_list,
                                                    uint16_t *release_count,
                                                    uint16_t release_capacity)
{
    uint16_t i;
    int err;

    if ((0U == entry_index) || !prv_smartfs_entry_is_active(entry_index) ||
        (NULL == release_list) || (NULL == release_count) ||
        (0U == release_capacity)) {
        return SMART_STORAGE_ERR_INVAL;
    }

    if (SMART_STORAGE_TYPE_DIR == g_smartfs_image.entries[entry_index].type) {
        for (i = 1U; i < SMART_STORAGE_MAX_ENTRIES; i++) {
            if ((SMARTFS_ENTRY_STATE_ACTIVE == g_smartfs_image.entries[i].state) &&
                (entry_index == g_smartfs_image.entries[i].parent)) {
                err = prv_smartfs_mark_entry_deleted_recursive(i,
                                                               release_list,
                                                               release_count,
                                                               release_capacity);
                if (SMART_STORAGE_ERR_OK != err) {
                    return err;
                }
            }
        }
    } else {
        if (*release_count >= release_capacity) {
            return SMART_STORAGE_ERR_NOSPC;
        }

        release_list[*release_count] = g_smartfs_image.entries[entry_index].first_sector;
        (*release_count)++;
    }

    memset(&g_smartfs_image.entries[entry_index], 0xFF, sizeof(g_smartfs_image.entries[entry_index]));
    g_smartfs_image.entries[entry_index].state = SMARTFS_ENTRY_STATE_DELETED;
    return SMART_STORAGE_ERR_OK;
}

/*
 * 函数作用：
 *   将数据覆盖写入指定文件，调用者可选择追加模式。
 * 主要流程：
 *   1. 加载元数据并解析路径。
 *   2. 追加模式下先读出旧内容，和新内容合并后重写数据链。
 *   3. 覆盖模式下先写入新链，再释放旧链，避免空间不足时先破坏旧文件。
 *   4. 更新目录项并提交元数据，提交成功后再擦除旧链。
 * 参数说明：
 *   path：目标文件路径，必须非空。
 *   data：待写入或追加的数据；当 length 大于 0 时必须非空。
 *   length：待写入或追加字节数，单位为字节。
 *   append_mode：1 表示追加写，0 表示覆盖写。
 * 返回值说明：
 *   SMART_STORAGE_ERR_OK：表示写入成功。
 *   其他 SMART_STORAGE_ERR_*：表示路径、空间、读取、写入或提交失败。
 */
static int prv_smartfs_write_file_internal(const char *path,
                                           const uint8_t *data,
                                           uint32_t length,
                                           uint8_t append_mode)
{
    smartfs_path_lookup_t lookup;
    smartfs_disk_entry_t *entry;
    uint16_t new_first_sector = SMARTFS_INVALID_SECTOR;
    uint16_t old_first_sector = SMARTFS_INVALID_SECTOR;
    uint32_t new_size = length;
    uint32_t original_size = 0U;
    uint32_t blocks_needed;
    int16_t new_entry_index;
    int err;

    if (!prv_smartfs_is_valid_abs_path(path) || ((NULL == data) && (length > 0U))) {
        return SMART_STORAGE_ERR_INVAL;
    }

    err = prv_smartfs_ensure_loaded();
    if (SMART_STORAGE_ERR_OK != err) {
        return err;
    }

    err = prv_smartfs_lookup_path(path, &lookup);
    if (SMART_STORAGE_ERR_NOENT == err) {
        return SMART_STORAGE_ERR_NOENT;
    }

    if (SMART_STORAGE_ERR_OK != err) {
        return err;
    }

    if (0U != lookup.is_root) {
        return SMART_STORAGE_ERR_ISDIR;
    }

    if (lookup.entry_index >= 0) {
        entry = &g_smartfs_image.entries[(uint16_t)lookup.entry_index];
        if (SMART_STORAGE_TYPE_DIR == entry->type) {
            return SMART_STORAGE_ERR_ISDIR;
        }
        original_size = entry->size;
        old_first_sector = entry->first_sector;
    } else {
        new_entry_index = prv_smartfs_find_free_entry();
        if (new_entry_index < 0) {
            return SMART_STORAGE_ERR_NOSPC;
        }
        lookup.entry_index = new_entry_index;
    }

    if (0U != append_mode) {
        if (UINT32_MAX - original_size < length) {
            return SMART_STORAGE_ERR_FBIG;
        }
        new_size = original_size + length;
    }

    blocks_needed = (new_size + SMARTFS_FLASH_SECTOR_SIZE - 1U) / SMARTFS_FLASH_SECTOR_SIZE;
    if (blocks_needed > SMARTFS_DATA_SECTOR_COUNT) {
        return SMART_STORAGE_ERR_FBIG;
    }

    if (0U != append_mode) {
        err = prv_smartfs_append_chain(old_first_sector,
                                       original_size,
                                       data,
                                       length,
                                       &new_first_sector);
        if (SMART_STORAGE_ERR_OK != err) {
            return err;
        }

    } else {
        err = prv_smartfs_write_chain(data, length, &new_first_sector);
        if (SMART_STORAGE_ERR_OK != err) {
            return err;
        }
    }

    entry = &g_smartfs_image.entries[(uint16_t)lookup.entry_index];
    if (SMARTFS_ENTRY_STATE_ACTIVE != entry->state) {
        /*
         * 新建文件只有在数据链已经完整写入后才占用目录项。
         * 这样空间不足或旧链读取失败时，RAM 元数据里不会残留未提交的空文件。
         */
        memset(entry, 0, sizeof(*entry));
        entry->state = SMARTFS_ENTRY_STATE_ACTIVE;
        entry->type = SMART_STORAGE_TYPE_REG;
        entry->parent = lookup.parent_index;
        entry->reserved0 = 0U;
        (void)strcpy(entry->name, lookup.leaf_name);
    }

    entry->first_sector = new_first_sector;
    entry->size = new_size;
    err = prv_smartfs_commit();
    if (SMART_STORAGE_ERR_OK != err) {
        (void)prv_smartfs_release_chain(new_first_sector);
        g_smartfs_loaded = 0U;
        return err;
    }

    err = prv_smartfs_release_chain(old_first_sector);
    if (SMART_STORAGE_ERR_OK != err) {
        g_smartfs_loaded = 0U;
        return err;
    }

    err = prv_smartfs_commit();
    if (SMART_STORAGE_ERR_OK != err) {
        g_smartfs_loaded = 0U;
    }

    return err;
}

/*
 * 函数作用：
 *   检查 SMARTFS 元数据；当介质未格式化或元数据无效时格式化整片文件系统。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   SMART_STORAGE_ERR_OK：表示文件系统已经可用。
 *   其他 SMART_STORAGE_ERR_*：表示格式化、元数据提交或读回校验失败。
 */
int smart_storage_init(void)
{
    int err;

    err = prv_smartfs_load();
    if (SMART_STORAGE_ERR_OK == err) {
        return SMART_STORAGE_ERR_OK;
    }

    my_printf(DEBUG_USART, "SMARTFS: metadata invalid, format whole flash\r\n");
    return smart_storage_format();
}

/*
 * 函数作用：
 *   格式化 GD25Q16 上的 SMARTFS 文件系统，整片 2MB Flash 都归文件系统管理。
 * 主要流程：
 *   1. 擦除 512 个 4KB 物理扇区，包括旧 LittleFS 数据和旧裸测保留区。
 *   2. 初始化根目录和空闲数据块表。
 *   3. 提交元数据副本并重新加载校验。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   SMART_STORAGE_ERR_OK：表示格式化成功。
 *   其他 SMART_STORAGE_ERR_*：表示元数据提交或读回校验失败。
 */
int smart_storage_format(void)
{
    uint16_t sector;
    int err;

    g_smartfs_loaded = 0U;
    for (sector = 0U; sector < SMARTFS_BLOCK_COUNT; sector++) {
        if(0 != spi_flash_sector_erase((uint32_t)sector * SMARTFS_FLASH_SECTOR_SIZE)) {
            return SMART_STORAGE_ERR_IO;
        }
    }

    prv_smartfs_init_empty_image(0U);
    err = prv_smartfs_commit();
    if (SMART_STORAGE_ERR_OK != err) {
        return err;
    }

    g_smartfs_loaded = 0U;
    return prv_smartfs_load();
}

/*
 * 函数作用：
 *   在不触发自动格式化的前提下，把一段数据覆盖写入指定 SMARTFS 文件。
 * 参数说明：
 *   path：目标文件路径，必须是 SMARTFS 内的有效绝对路径。
 *   data：待写入数据缓冲区；当 length 大于 0 时必须非空。
 *   length：待写入字节数，单位为字节。
 * 返回值说明：
 *   SMART_STORAGE_ERR_OK：表示文件写入成功。
 *   其他 SMART_STORAGE_ERR_*：表示路径、空间、写入或提交失败。
 */
int smart_storage_write_file(const char *path, const uint8_t *data, uint32_t length)
{
    return prv_smartfs_write_file_internal(path, data, length, 0U);
}

/*
 * 函数作用：
 *   在不触发自动格式化的前提下，把一段数据追加写入指定 SMARTFS 文件尾部。
 * 参数说明：
 *   path：目标文件路径，必须是 SMARTFS 内的有效绝对路径。
 *   data：待追加的数据缓冲区；当 length 大于 0 时必须非空。
 *   length：待追加字节数，单位为字节。
 * 返回值说明：
 *   SMART_STORAGE_ERR_OK：表示文件追加写入成功。
 *   其他 SMART_STORAGE_ERR_*：表示路径、空间、写入或提交失败。
 */
int smart_storage_append_file(const char *path, const uint8_t *data, uint32_t length)
{
    return prv_smartfs_write_file_internal(path, data, length, 1U);
}

/*
 * 函数作用：
 *   在不触发自动格式化的前提下，完整读取指定 SMARTFS 文件内容到调用者缓冲区。
 * 参数说明：
 *   path：目标文件路径，必须是 SMARTFS 内的有效绝对路径。
 *   buffer：接收文件内容的缓冲区，必须非空。
 *   buffer_size：接收缓冲区总长度，单位为字节；必须为结尾 '\0' 预留 1 字节。
 *   out_length：实际读回的文件长度输出指针，必须非空。
 * 返回值说明：
 *   SMART_STORAGE_ERR_OK：表示文件读取成功。
 *   SMART_STORAGE_ERR_NOENT：表示目标文件不存在。
 *   SMART_STORAGE_ERR_FBIG：表示目标文件大于当前接收缓冲区可承载大小。
 *   其他 SMART_STORAGE_ERR_*：表示路径、读取或数据链校验失败。
 */
int smart_storage_read_file(const char *path, uint8_t *buffer, uint32_t buffer_size, uint32_t *out_length)
{
    smartfs_path_lookup_t lookup;
    smartfs_disk_entry_t *entry;
    int err;

    if (!prv_smartfs_is_valid_abs_path(path) ||
        (NULL == buffer) || (0U == buffer_size) || (NULL == out_length)) {
        return SMART_STORAGE_ERR_INVAL;
    }

    buffer[0] = '\0';
    *out_length = 0U;

    err = prv_smartfs_ensure_loaded();
    if (SMART_STORAGE_ERR_OK != err) {
        return err;
    }

    err = prv_smartfs_lookup_path(path, &lookup);
    if (SMART_STORAGE_ERR_NOENT == err) {
        return SMART_STORAGE_ERR_NOENT;
    }

    if (SMART_STORAGE_ERR_OK != err) {
        return err;
    }

    if (lookup.entry_index < 0) {
        return SMART_STORAGE_ERR_NOENT;
    }

    entry = &g_smartfs_image.entries[(uint16_t)lookup.entry_index];
    if (SMART_STORAGE_TYPE_REG != entry->type) {
        return SMART_STORAGE_ERR_ISDIR;
    }

    if (entry->size >= buffer_size) {
        return SMART_STORAGE_ERR_FBIG;
    }

    err = prv_smartfs_read_chain(entry->first_sector, entry->size, buffer);
    if (SMART_STORAGE_ERR_OK != err) {
        return err;
    }

    buffer[entry->size] = '\0';
    *out_length = entry->size;
    return SMART_STORAGE_ERR_OK;
}

/*
 * 函数作用：
 *   在不触发自动格式化的前提下，查询 SMARTFS 容量信息和指定文件状态。
 * 参数说明：
 *   path：需要查询的目标文件路径；允许为空指针或空字符串。
 *   info：信息输出结构体指针，必须非空。
 * 返回值说明：
 *   SMART_STORAGE_ERR_OK：表示信息查询成功。
 *   其他 SMART_STORAGE_ERR_*：表示元数据加载或路径查询失败。
 */
int smart_storage_get_info(const char *path, smart_storage_info_t *info)
{
    smartfs_path_lookup_t lookup;
    smartfs_disk_entry_t *entry;
    uint32_t used_data_blocks;
    int err;

    if (NULL == info) {
        return SMART_STORAGE_ERR_INVAL;
    }

    memset(info, 0, sizeof(*info));
    err = prv_smartfs_ensure_loaded();
    if (SMART_STORAGE_ERR_OK != err) {
        return err;
    }

    used_data_blocks = prv_smartfs_count_used_data_blocks();
    info->block_size = SMARTFS_FLASH_SECTOR_SIZE;
    info->block_count = SMARTFS_BLOCK_COUNT;
    info->total_bytes = SMARTFS_FLASH_FS_SIZE;
    info->used_blocks = SMARTFS_META_TOTAL_SECTOR_COUNT + used_data_blocks;
    info->used_bytes = info->used_blocks * SMARTFS_FLASH_SECTOR_SIZE;

    if ((NULL != path) && ('\0' != path[0])) {
        if (!prv_smartfs_is_valid_abs_path(path)) {
            return SMART_STORAGE_ERR_INVAL;
        }

        err = prv_smartfs_lookup_path(path, &lookup);
        if (SMART_STORAGE_ERR_OK != err) {
            return err;
        }

        if (lookup.entry_index >= 0) {
            entry = &g_smartfs_image.entries[(uint16_t)lookup.entry_index];
            if (SMART_STORAGE_TYPE_REG != entry->type) {
                return SMART_STORAGE_ERR_ISDIR;
            }
            info->file_exists = 1U;
            info->file_size = entry->size;
        }
    }

    return SMART_STORAGE_ERR_OK;
}

/*
 * 函数作用：
 *   在不触发自动格式化的前提下，查询指定路径当前是否存在、是文件还是目录以及大小。
 * 参数说明：
 *   path：待查询的绝对路径，必须非空。
 *   info：路径信息输出结构体指针，必须非空。
 * 返回值说明：
 *   SMART_STORAGE_ERR_OK：表示查询完成。
 *   其他 SMART_STORAGE_ERR_*：表示路径格式或元数据加载失败。
 */
int smart_storage_get_path_info(const char *path, smart_storage_path_info_t *info)
{
    smartfs_path_lookup_t lookup;
    smartfs_disk_entry_t *entry;
    int err;

    if (!prv_smartfs_is_valid_abs_path(path) || (NULL == info)) {
        return SMART_STORAGE_ERR_INVAL;
    }

    memset(info, 0, sizeof(*info));
    err = prv_smartfs_ensure_loaded();
    if (SMART_STORAGE_ERR_OK != err) {
        return err;
    }

    err = prv_smartfs_lookup_path(path, &lookup);
    if (SMART_STORAGE_ERR_NOENT == err) {
        /*
         * 查询接口面向串口壳层：无论缺的是末级文件还是中间目录，
         * 都通过 exists=0 表示“目标不可达”，创建类接口仍直接使用
         * prv_smartfs_lookup_path() 保留父目录缺失错误。
         */
        info->exists = 0U;
        return SMART_STORAGE_ERR_OK;
    }

    if (SMART_STORAGE_ERR_OK != err) {
        return err;
    }

    if (lookup.entry_index < 0) {
        info->exists = 0U;
        return SMART_STORAGE_ERR_OK;
    }

    entry = &g_smartfs_image.entries[(uint16_t)lookup.entry_index];
    info->exists = 1U;
    info->type = entry->type;
    err = prv_smartfs_calculate_entry_size((uint16_t)lookup.entry_index, &info->size);
    if (SMART_STORAGE_ERR_OK != err) {
        return err;
    }

    return SMART_STORAGE_ERR_OK;
}

/*
 * 函数作用：
 *   在不触发自动格式化的前提下，遍历指定目录下的所有有效目录项。
 * 参数说明：
 *   path：目标目录绝对路径，必须非空。
 *   callback：目录项回调函数，可为空；为空时仅统计目录项个数。
 *   context：回调透传上下文指针，可为空。
 *   out_count：输出有效目录项个数的指针，可为空。
 * 返回值说明：
 *   SMART_STORAGE_ERR_OK：表示目录遍历成功。
 *   其他 SMART_STORAGE_ERR_*：表示路径、元数据加载或目录统计失败。
 */
int smart_storage_list_dir(const char *path,
                           smart_storage_dir_list_callback_t callback,
                           void *context,
                           uint32_t *out_count)
{
    smartfs_path_lookup_t lookup;
    smartfs_disk_entry_t *parent_entry;
    smart_storage_dir_entry_t entry;
    uint32_t count = 0U;
    uint16_t i;
    int err;

    if (!prv_smartfs_is_valid_abs_path(path)) {
        return SMART_STORAGE_ERR_INVAL;
    }

    if (NULL != out_count) {
        *out_count = 0U;
    }

    err = prv_smartfs_ensure_loaded();
    if (SMART_STORAGE_ERR_OK != err) {
        return err;
    }

    err = prv_smartfs_lookup_path(path, &lookup);
    if (SMART_STORAGE_ERR_OK != err) {
        return err;
    }

    if (lookup.entry_index < 0) {
        return SMART_STORAGE_ERR_NOENT;
    }

    parent_entry = &g_smartfs_image.entries[(uint16_t)lookup.entry_index];
    if (SMART_STORAGE_TYPE_DIR != parent_entry->type) {
        return SMART_STORAGE_ERR_NOTDIR;
    }

    for (i = 1U; i < SMART_STORAGE_MAX_ENTRIES; i++) {
        if ((SMARTFS_ENTRY_STATE_ACTIVE != g_smartfs_image.entries[i].state) ||
            ((uint16_t)lookup.entry_index != g_smartfs_image.entries[i].parent)) {
            continue;
        }

        memset(&entry, 0, sizeof(entry));
        entry.info.type = g_smartfs_image.entries[i].type;
        if (SMART_STORAGE_TYPE_REG == entry.info.type) {
            entry.info.size = g_smartfs_image.entries[i].size;
            entry.display_size = entry.info.size;
        } else {
            entry.info.size = 0U;
            err = prv_smartfs_calculate_entry_size(i, &entry.display_size);
            if (SMART_STORAGE_ERR_OK != err) {
                return err;
            }
        }

        (void)strncpy(entry.info.name,
                      g_smartfs_image.entries[i].name,
                      SMART_STORAGE_NAME_MAX);
        entry.info.name[SMART_STORAGE_NAME_MAX] = '\0';
        count++;

        if (NULL != callback) {
            callback(&entry, context);
        }
    }

    if (NULL != out_count) {
        *out_count = count;
    }

    return SMART_STORAGE_ERR_OK;
}

/*
 * 函数作用：
 *   在不触发自动格式化的前提下，在指定路径创建一个目录。
 * 参数说明：
 *   path：待创建目录的绝对路径，必须非空。
 * 返回值说明：
 *   SMART_STORAGE_ERR_OK：表示目录创建成功。
 *   SMART_STORAGE_ERR_EXIST：表示目标路径已存在。
 *   其他 SMART_STORAGE_ERR_*：表示路径、空间或元数据提交失败。
 */
int smart_storage_mkdir(const char *path)
{
    smartfs_path_lookup_t lookup;
    smartfs_disk_entry_t *entry;
    int16_t free_index;
    int err;

    if (!prv_smartfs_is_valid_abs_path(path)) {
        return SMART_STORAGE_ERR_INVAL;
    }

    err = prv_smartfs_ensure_loaded();
    if (SMART_STORAGE_ERR_OK != err) {
        return err;
    }

    err = prv_smartfs_lookup_path(path, &lookup);
    if (SMART_STORAGE_ERR_OK != err) {
        return err;
    }

    if ((0U != lookup.is_root) || (lookup.entry_index >= 0)) {
        return SMART_STORAGE_ERR_EXIST;
    }

    free_index = prv_smartfs_find_free_entry();
    if (free_index < 0) {
        return SMART_STORAGE_ERR_NOSPC;
    }

    entry = &g_smartfs_image.entries[(uint16_t)free_index];
    memset(entry, 0, sizeof(*entry));
    entry->state = SMARTFS_ENTRY_STATE_ACTIVE;
    entry->type = SMART_STORAGE_TYPE_DIR;
    entry->parent = lookup.parent_index;
    entry->first_sector = SMARTFS_INVALID_SECTOR;
    entry->reserved0 = 0U;
    entry->size = 0U;
    (void)strcpy(entry->name, lookup.leaf_name);

    err = prv_smartfs_commit();
    if (SMART_STORAGE_ERR_OK != err) {
        g_smartfs_loaded = 0U;
    }

    return err;
}

/*
 * 函数作用：
 *   在不触发自动格式化的前提下创建一个空文件；若文件已存在则保持原内容不变。
 * 参数说明：
 *   path：目标文件绝对路径，必须非空。
 * 返回值说明：
 *   SMART_STORAGE_ERR_OK：表示文件已存在或已成功创建。
 *   SMART_STORAGE_ERR_ISDIR：表示目标路径已存在且是目录。
 *   其他 SMART_STORAGE_ERR_*：表示路径、空间或元数据提交失败。
 */
int smart_storage_touch_file(const char *path)
{
    smartfs_path_lookup_t lookup;
    smartfs_disk_entry_t *entry;
    int16_t free_index;
    int err;

    if (!prv_smartfs_is_valid_abs_path(path)) {
        return SMART_STORAGE_ERR_INVAL;
    }

    err = prv_smartfs_ensure_loaded();
    if (SMART_STORAGE_ERR_OK != err) {
        return err;
    }

    err = prv_smartfs_lookup_path(path, &lookup);
    if (SMART_STORAGE_ERR_OK != err) {
        return err;
    }

    if (0U != lookup.is_root) {
        return SMART_STORAGE_ERR_ISDIR;
    }

    if (lookup.entry_index >= 0) {
        if (SMART_STORAGE_TYPE_REG == g_smartfs_image.entries[(uint16_t)lookup.entry_index].type) {
            return SMART_STORAGE_ERR_OK;
        }
        return SMART_STORAGE_ERR_ISDIR;
    }

    free_index = prv_smartfs_find_free_entry();
    if (free_index < 0) {
        return SMART_STORAGE_ERR_NOSPC;
    }

    entry = &g_smartfs_image.entries[(uint16_t)free_index];
    memset(entry, 0, sizeof(*entry));
    entry->state = SMARTFS_ENTRY_STATE_ACTIVE;
    entry->type = SMART_STORAGE_TYPE_REG;
    entry->parent = lookup.parent_index;
    entry->first_sector = SMARTFS_INVALID_SECTOR;
    entry->reserved0 = 0U;
    entry->size = 0U;
    (void)strcpy(entry->name, lookup.leaf_name);

    err = prv_smartfs_commit();
    if (SMART_STORAGE_ERR_OK != err) {
        g_smartfs_loaded = 0U;
    }

    return err;
}

/*
 * 函数作用：
 *   在不触发自动格式化的前提下删除指定路径；普通文件直接删除，目录递归删除全部子项。
 * 参数说明：
 *   path：待删除目标的绝对路径，必须非空，且不允许为根目录 `/`。
 * 返回值说明：
 *   SMART_STORAGE_ERR_OK：表示目标已经成功删除。
 *   SMART_STORAGE_ERR_NOENT：表示目标路径不存在。
 *   SMART_STORAGE_ERR_INVAL：表示路径无效，或尝试删除根目录 `/`。
 *   其他 SMART_STORAGE_ERR_*：表示元数据加载、数据擦除或元数据提交失败。
 */
int smart_storage_remove_path(const char *path)
{
    smartfs_path_lookup_t lookup;
    uint16_t release_list[SMART_STORAGE_MAX_ENTRIES];
    uint16_t release_count = 0U;
    uint16_t i;
    int err;

    if (!prv_smartfs_is_valid_abs_path(path)) {
        return SMART_STORAGE_ERR_INVAL;
    }

    err = prv_smartfs_ensure_loaded();
    if (SMART_STORAGE_ERR_OK != err) {
        return err;
    }

    err = prv_smartfs_lookup_path(path, &lookup);
    if (SMART_STORAGE_ERR_OK != err) {
        return err;
    }

    if (0U != lookup.is_root) {
        return SMART_STORAGE_ERR_INVAL;
    }

    if (lookup.entry_index < 0) {
        return SMART_STORAGE_ERR_NOENT;
    }

    err = prv_smartfs_mark_entry_deleted_recursive((uint16_t)lookup.entry_index,
                                                   release_list,
                                                   &release_count,
                                                   SMART_STORAGE_MAX_ENTRIES);
    if (SMART_STORAGE_ERR_OK != err) {
        g_smartfs_loaded = 0U;
        return err;
    }

    err = prv_smartfs_commit();
    if (SMART_STORAGE_ERR_OK != err) {
        g_smartfs_loaded = 0U;
        return err;
    }

    for (i = 0U; i < release_count; i++) {
        err = prv_smartfs_release_chain(release_list[i]);
        if (SMART_STORAGE_ERR_OK != err) {
            g_smartfs_loaded = 0U;
            return err;
        }
    }

    err = prv_smartfs_commit();
    if (SMART_STORAGE_ERR_OK != err) {
        g_smartfs_loaded = 0U;
    }

    return err;
}

/*
 * 函数作用：
 *   执行 GD25QXX SMARTFS 冒烟测试，覆盖检查/必要时格式化、目录创建、文件写入、
 *   重新读取、目录大小统计和递归删除。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   SMART_STORAGE_ERR_OK：表示文件系统读写和目录操作校验通过。
 *   其他 SMART_STORAGE_ERR_*：表示初始化、格式化或文件目录操作失败。
 */
int smart_storage_self_test(void)
{
    static const char test_dir[] = "/selftest";
    static const char test_file[] = "/selftest/hello.txt";
    static const char test_text[] = "SMARTFS self-test OK";
    uint8_t readback[64];
    uint32_t read_length = 0U;
    smart_storage_path_info_t info;
    int err;

    err = smart_storage_init();
    if (SMART_STORAGE_ERR_OK != err) {
        my_printf(DEBUG_USART, "SMARTFS: init failed (%d)\r\n", err);
        return err;
    }

    (void)smart_storage_remove_path(test_dir);

    err = smart_storage_mkdir(test_dir);
    if (SMART_STORAGE_ERR_OK != err) {
        my_printf(DEBUG_USART, "SMARTFS: mkdir self-test dir failed (%d)\r\n", err);
        return err;
    }

    err = smart_storage_write_file(test_file,
                                   (const uint8_t *)test_text,
                                   (uint32_t)strlen(test_text));
    if (SMART_STORAGE_ERR_OK != err) {
        my_printf(DEBUG_USART, "SMARTFS: write self-test file failed (%d)\r\n", err);
        return err;
    }

    err = smart_storage_read_file(test_file, readback, sizeof(readback), &read_length);
    if (SMART_STORAGE_ERR_OK != err) {
        my_printf(DEBUG_USART, "SMARTFS: read self-test file failed (%d)\r\n", err);
        return err;
    }

    if ((read_length != strlen(test_text)) ||
        (0 != memcmp(readback, test_text, read_length))) {
        my_printf(DEBUG_USART, "SMARTFS: self-test content mismatch\r\n");
        return SMART_STORAGE_ERR_CORRUPT;
    }

    err = smart_storage_get_path_info(test_dir, &info);
    if ((SMART_STORAGE_ERR_OK != err) || (0U == info.exists) ||
        (SMART_STORAGE_TYPE_DIR != info.type) || (info.size != read_length)) {
        my_printf(DEBUG_USART, "SMARTFS: self-test stat mismatch (%d)\r\n", err);
        return (SMART_STORAGE_ERR_OK != err) ? err : SMART_STORAGE_ERR_CORRUPT;
    }

    err = smart_storage_remove_path(test_dir);
    if (SMART_STORAGE_ERR_OK != err) {
        my_printf(DEBUG_USART, "SMARTFS: cleanup self-test dir failed (%d)\r\n", err);
        return err;
    }

    my_printf(DEBUG_USART, "SMARTFS: self-test PASS: %s=%s\r\n", test_file, readback);
    return SMART_STORAGE_ERR_OK;
}
