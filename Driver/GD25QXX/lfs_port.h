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
 * 结构体作用：
 *   汇总 LittleFS 文件系统和指定文件的基础信息，供串口调试口查询显示。
 * 成员说明：
 *   total_bytes：当前 LittleFS 可管理区域总容量，单位为字节。
 *   used_bytes：当前文件系统已经占用的容量，单位为字节。
 *   block_size：LittleFS 逻辑块大小，单位为字节。
 *   block_count：LittleFS 逻辑块数量。
 *   used_blocks：当前文件系统已经占用的逻辑块数量。
 *   file_size：目标文件大小，单位为字节；文件不存在时为 0。
 *   file_exists：目标文件是否存在，1 表示存在，0 表示不存在。
 */
typedef struct
{
    uint32_t total_bytes;
    uint32_t used_bytes;
    uint32_t block_size;
    uint32_t block_count;
    uint32_t used_blocks;
    uint32_t file_size;
    uint8_t file_exists;
} lfs_storage_info_t;

/*
 * 结构体作用：
 *   描述 LittleFS 中某一路径当前的存在性、类型和展示用大小信息。
 * 成员说明：
 *   exists：目标路径是否存在，1 表示存在，0 表示不存在。
 *   type：目标路径类型；存在时取值通常为 `LFS_TYPE_REG` 或 `LFS_TYPE_DIR`。
 *   size：目标路径展示大小，单位为字节；普通文件时为文件长度，目录时为该目录下全部文件内容的递归总字节数。
 */
typedef struct
{
    uint8_t exists;
    uint16_t type;
    uint32_t size;
} lfs_storage_path_info_t;

/*
 * 结构体作用：
 *   描述目录遍历时回调给上层的单个目录项信息。
 * 成员说明：
 *   info：LittleFS 原始目录项信息；普通文件时 `info.size` 为文件长度，目录时保持 0。
 *   display_size：目录壳层展示用大小，单位为字节；普通文件时为文件长度，目录时为该目录下全部文件内容的递归总字节数。
 */
typedef struct
{
    struct lfs_info info;
    uint32_t display_size;
} lfs_storage_dir_entry_t;

/*
 * 类型作用：
 *   定义目录遍历时每读到一个有效目录项就回调一次的处理函数类型。
 * 参数说明：
 *   entry：当前目录项完整信息指针，内部包含名称、类型、原始文件信息和展示用大小。
 *   context：由调用者透传的用户上下文指针，可为空。
 * 返回值说明：
 *   无返回值。
 */
typedef void (*lfs_storage_dir_list_callback_t)(const lfs_storage_dir_entry_t *entry, void *context);

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
 *   在不触发自动格式化的前提下，把一段数据覆盖写入指定 LittleFS 文件。
 * 参数说明：
 *   path：目标文件路径，必须是 LittleFS 内的有效绝对路径，例如 "/log/demo.txt"。
 *   data：待写入数据缓冲区；当 length 大于 0 时必须非空。
 *   length：待写入字节数，单位为字节；为 0 时表示把目标文件截断为空文件。
 * 返回值说明：
 *   LFS_ERR_OK：表示文件写入并正常关闭、卸载成功。
 *   其他 LFS_ERR_*：表示挂载、打开、写入、关闭或卸载失败。
 */
int lfs_storage_write_file(const char *path, const uint8_t *data, uint32_t length);

/*
 * 函数作用：
 *   在不触发自动格式化的前提下，把一段数据追加写入指定 LittleFS 文件尾部。
 * 参数说明：
 *   path：目标文件路径，必须是 LittleFS 内的有效绝对路径，例如 "/log/demo.txt"。
 *   data：待追加的数据缓冲区；当 length 大于 0 时必须非空。
 *   length：待追加字节数，单位为字节；为 0 时表示保持文件原样不追加内容。
 * 返回值说明：
 *   LFS_ERR_OK：表示文件追加写入并正常关闭、卸载成功。
 *   其他 LFS_ERR_*：表示挂载、打开、写入、关闭或卸载失败。
 */
int lfs_storage_append_file(const char *path, const uint8_t *data, uint32_t length);

/*
 * 函数作用：
 *   在不触发自动格式化的前提下，完整读取指定 LittleFS 文件内容到调用者缓冲区。
 * 参数说明：
 *   path：目标文件路径，必须是 LittleFS 内的有效绝对路径。
 *   buffer：接收文件内容的缓冲区，必须非空。
 *   buffer_size：接收缓冲区总长度，单位为字节；至少需要大于 0，函数会为文本回显预留结尾 '\0'。
 *   out_length：实际读回的文件长度输出指针，必须非空。
 * 返回值说明：
 *   LFS_ERR_OK：表示文件读取成功，buffer 中已写入内容并补结尾 '\0'。
 *   LFS_ERR_NOENT：表示目标文件不存在。
 *   LFS_ERR_FBIG：表示目标文件大于当前接收缓冲区可承载的大小。
 *   其他 LFS_ERR_*：表示挂载、打开、读取、关闭或卸载失败。
 */
int lfs_storage_read_file(const char *path, uint8_t *buffer, uint32_t buffer_size, uint32_t *out_length);

/*
 * 函数作用：
 *   在不触发自动格式化的前提下，查询 LittleFS 文件系统基础容量信息和指定文件状态。
 * 参数说明：
 *   path：需要查询的目标文件路径；允许为空指针或空字符串，此时只返回文件系统容量信息。
 *   info：信息输出结构体指针，必须非空。
 * 返回值说明：
 *   LFS_ERR_OK：表示信息查询成功；若目标文件不存在，则 info->file_exists 会被置 0。
 *   其他 LFS_ERR_*：表示挂载、容量统计、文件查询或卸载失败。
 */
int lfs_storage_get_info(const char *path, lfs_storage_info_t *info);

/*
 * 函数作用：
 *   在不触发自动格式化的前提下，查询指定路径当前是否存在、是文件还是目录以及大小。
 * 参数说明：
 *   path：待查询的绝对路径，必须非空。
 *   info：路径信息输出结构体指针，必须非空。
 * 返回值说明：
 *   LFS_ERR_OK：表示查询完成；即使路径不存在，也会通过 info->exists=0 返回。
 *   其他 LFS_ERR_*：表示挂载、查询或卸载失败。
 */
int lfs_storage_get_path_info(const char *path, lfs_storage_path_info_t *info);

/*
 * 函数作用：
 *   在不触发自动格式化的前提下，遍历指定目录下的所有有效目录项。
 * 参数说明：
 *   path：目标目录绝对路径，必须非空。
 *   callback：目录项回调函数，可为空；为空时仅统计目录项个数。
 *   context：回调透传上下文指针，可为空。
 *   out_count：输出有效目录项个数的指针，可为空。
 * 返回值说明：
 *   LFS_ERR_OK：表示目录遍历成功。
 *   其他 LFS_ERR_*：表示挂载、打开目录、遍历、关闭目录或卸载失败。
 */
int lfs_storage_list_dir(const char *path,
                         lfs_storage_dir_list_callback_t callback,
                         void *context,
                         uint32_t *out_count);

/*
 * 函数作用：
 *   在不触发自动格式化的前提下，在指定路径创建一个目录。
 * 参数说明：
 *   path：待创建目录的绝对路径，必须非空。
 * 返回值说明：
 *   LFS_ERR_OK：表示目录创建成功。
 *   LFS_ERR_EXIST：表示目标路径已存在。
 *   其他 LFS_ERR_*：表示挂载、创建目录或卸载失败。
 */
int lfs_storage_mkdir(const char *path);

/*
 * 函数作用：
 *   在不触发自动格式化的前提下，创建一个空文件；若文件已存在则保持原内容不变。
 * 参数说明：
 *   path：目标文件绝对路径，必须非空。
 * 返回值说明：
 *   LFS_ERR_OK：表示文件已存在或已成功创建。
 *   其他 LFS_ERR_*：表示挂载、查询、打开、关闭或卸载失败。
 */
int lfs_storage_touch_file(const char *path);

/*
 * 函数作用：
 *   在不触发自动格式化的前提下，删除指定路径；普通文件直接删除，目录则递归删除其全部子项后再删除目录本身。
 * 参数说明：
 *   path：待删除目标的绝对路径，必须非空，且不允许为根目录 `/`。
 * 返回值说明：
 *   LFS_ERR_OK：表示目标已经成功删除。
 *   LFS_ERR_NOENT：表示目标路径不存在。
 *   LFS_ERR_INVAL：表示路径无效，或尝试删除根目录 `/`。
 *   其他 LFS_ERR_*：表示挂载、遍历、删除或卸载失败。
 */
int lfs_storage_remove_path(const char *path);

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
