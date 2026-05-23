#ifndef SMARTFS_PORT_H
#define SMARTFS_PORT_H

#include <stdbool.h>
#include <stdint.h>

/*
 * 宏作用：
 *   定义 GD25Q16 的物理容量，单位为字节。
 * 说明：
 *   本工程把整片 2MB SPI Flash 都纳入 SMARTFS 管理，不再在末尾保留裸 Flash
 *   测试扇区；修改容量时必须同步检查分区、工程文档和串口 df 输出。
 */
#define SMARTFS_FLASH_TOTAL_SIZE              (2U * 1024U * 1024U)

/*
 * 宏作用：
 *   定义 SMARTFS 管理区大小。
 * 说明：
 *   用户要求取消末尾 4KB 裸测保留区，因此文件系统管理区等于 Flash 物理总容量。
 */
#define SMARTFS_FLASH_FS_SIZE                 SMARTFS_FLASH_TOTAL_SIZE

/*
 * 宏作用：
 *   定义 GD25Qxx 4KB 扇区擦除粒度，也是 SMARTFS 的最小物理块单位。
 */
#define SMARTFS_FLASH_SECTOR_SIZE             (4U * 1024U)

/*
 * 宏作用：
 *   定义 GD25Qxx 页编程粒度，底层写入仍按芯片页边界拆分。
 */
#define SMARTFS_FLASH_PAGE_SIZE               (256U)

/*
 * 宏作用：
 *   定义整片 Flash 的物理扇区数量。
 */
#define SMARTFS_BLOCK_COUNT                   (SMARTFS_FLASH_FS_SIZE / SMARTFS_FLASH_SECTOR_SIZE)

/*
 * 宏作用：
 *   定义元数据双副本布局。
 * 说明：
 *   每个副本占 4 个扇区，两个副本轮换提交；其余扇区全部作为文件数据块池。
 */
#define SMARTFS_META_COPY_COUNT               2U
#define SMARTFS_META_COPY_SECTOR_COUNT        4U
#define SMARTFS_META_TOTAL_SECTOR_COUNT       (SMARTFS_META_COPY_COUNT * SMARTFS_META_COPY_SECTOR_COUNT)
#define SMARTFS_DATA_START_SECTOR             SMARTFS_META_TOTAL_SECTOR_COUNT
#define SMARTFS_DATA_SECTOR_COUNT             (SMARTFS_BLOCK_COUNT - SMARTFS_DATA_START_SECTOR)

/*
 * 宏作用：
 *   定义 SMARTFS 目录项和路径长度限制。
 * 说明：
 *   这些限制用于静态元数据表，避免低层存储路径使用堆内存。
 */
#define SMART_STORAGE_MAX_ENTRIES             64U
#define SMART_STORAGE_NAME_MAX                63U
#define SMART_STORAGE_PATH_BUFFER_SIZE        256U

/*
 * 宏作用：
 *   控制启动阶段是否执行 SMARTFS 文件系统冒烟测试。
 * 说明：
 *   置 1 会在启动时检查/必要时格式化 SMARTFS，并写入读回一个小测试文件。
 */
#define SMART_STORAGE_BOOT_SELF_TEST_ENABLE   1U

/*
 * 宏作用：
 *   定义 shell 层可见的路径类型。
 */
#define SMART_STORAGE_TYPE_REG                1U
#define SMART_STORAGE_TYPE_DIR                2U

/*
 * 宏作用：
 *   定义 SMARTFS 端口统一错误码。
 * 说明：
 *   取值沿用常见负 errno 风格，便于串口日志直接定位失败类别。
 */
#define SMART_STORAGE_ERR_OK                  0
#define SMART_STORAGE_ERR_IO                  (-5)
#define SMART_STORAGE_ERR_CORRUPT             (-84)
#define SMART_STORAGE_ERR_NOENT               (-2)
#define SMART_STORAGE_ERR_EXIST               (-17)
#define SMART_STORAGE_ERR_NOTDIR              (-20)
#define SMART_STORAGE_ERR_ISDIR               (-21)
#define SMART_STORAGE_ERR_INVAL               (-22)
#define SMART_STORAGE_ERR_FBIG                (-27)
#define SMART_STORAGE_ERR_NOSPC               (-28)

/*
 * 结构体作用：
 *   汇总 SMARTFS 文件系统和指定文件的基础信息，供串口调试口查询显示。
 * 成员说明：
 *   total_bytes：当前 SMARTFS 管理区总容量，单位为字节。
 *   used_bytes：当前文件系统已经占用的容量，单位为字节，包含元数据区。
 *   block_size：SMARTFS 物理块大小，单位为字节。
 *   block_count：SMARTFS 管理区物理块数量。
 *   used_blocks：当前已经占用的物理块数量，包含元数据块和文件数据块。
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
} smart_storage_info_t;

/*
 * 结构体作用：
 *   描述 SMARTFS 中某一路径当前的存在性、类型和展示用大小信息。
 * 成员说明：
 *   exists：目标路径是否存在，1 表示存在，0 表示不存在。
 *   type：目标路径类型；存在时取值为 SMART_STORAGE_TYPE_REG 或 SMART_STORAGE_TYPE_DIR。
 *   size：目标路径展示大小，单位为字节；普通文件为文件长度，目录为递归文件内容总字节数。
 */
typedef struct
{
    uint8_t exists;
    uint16_t type;
    uint32_t size;
} smart_storage_path_info_t;

/*
 * 结构体作用：
 *   描述 SMARTFS 目录项的名称、类型和原始大小。
 * 成员说明：
 *   type：目录项类型。
 *   size：普通文件为文件长度，目录保持 0。
 *   name：目录项名称，不包含父路径。
 */
typedef struct
{
    uint16_t type;
    uint32_t size;
    char name[SMART_STORAGE_NAME_MAX + 1U];
} smart_storage_entry_info_t;

/*
 * 结构体作用：
 *   描述目录遍历时回调给上层的单个目录项信息。
 * 成员说明：
 *   info：目录项原始信息；普通文件时 size 为文件长度，目录时 size 保持 0。
 *   display_size：目录壳层展示用大小；普通文件为文件长度，目录为递归文件内容总字节数。
 */
typedef struct
{
    smart_storage_entry_info_t info;
    uint32_t display_size;
} smart_storage_dir_entry_t;

/*
 * 类型作用：
 *   定义目录遍历时每读到一个有效目录项就回调一次的处理函数类型。
 * 参数说明：
 *   entry：当前目录项完整信息指针，内部包含名称、类型、原始大小和展示用大小。
 *   context：由调用者透传的用户上下文指针，可为空。
 * 返回值说明：
 *   无返回值。
 */
typedef void (*smart_storage_dir_list_callback_t)(const smart_storage_dir_entry_t *entry, void *context);

/*
 * 函数作用：
 *   检查 SMARTFS 元数据；当介质未格式化或元数据无效时格式化整片文件系统。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   SMART_STORAGE_ERR_OK：表示文件系统已经可用。
 *   其他 SMART_STORAGE_ERR_*：表示格式化、元数据提交或读回校验失败。
 */
int smart_storage_init(void);

/*
 * 函数作用：
 *   格式化 GD25Q16 上的 SMARTFS 文件系统，整片 2MB Flash 都归文件系统管理。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   SMART_STORAGE_ERR_OK：表示格式化和元数据提交成功。
 *   其他 SMART_STORAGE_ERR_*：表示底层擦除、写入或校验失败。
 */
int smart_storage_format(void);

/*
 * 函数作用：
 *   在不触发自动格式化的前提下，把一段数据覆盖写入指定 SMARTFS 文件。
 * 参数说明：
 *   path：目标文件路径，必须是 SMARTFS 内的有效绝对路径，例如 "/log/demo.txt"。
 *   data：待写入数据缓冲区；当 length 大于 0 时必须非空。
 *   length：待写入字节数，单位为字节；为 0 时表示把目标文件截断为空文件。
 * 返回值说明：
 *   SMART_STORAGE_ERR_OK：表示文件写入成功。
 *   其他 SMART_STORAGE_ERR_*：表示路径、空间、写入或元数据提交失败。
 */
int smart_storage_write_file(const char *path, const uint8_t *data, uint32_t length);

/*
 * 函数作用：
 *   在不触发自动格式化的前提下，把一段数据追加写入指定 SMARTFS 文件尾部。
 * 参数说明：
 *   path：目标文件路径，必须是 SMARTFS 内的有效绝对路径，例如 "/log/demo.txt"。
 *   data：待追加的数据缓冲区；当 length 大于 0 时必须非空。
 *   length：待追加字节数，单位为字节；为 0 时表示保持文件原样不追加内容。
 * 返回值说明：
 *   SMART_STORAGE_ERR_OK：表示文件追加写入成功。
 *   其他 SMART_STORAGE_ERR_*：表示路径、空间、写入或元数据提交失败。
 */
int smart_storage_append_file(const char *path, const uint8_t *data, uint32_t length);

/*
 * 函数作用：
 *   在不触发自动格式化的前提下，完整读取指定 SMARTFS 文件内容到调用者缓冲区。
 * 参数说明：
 *   path：目标文件路径，必须是 SMARTFS 内的有效绝对路径。
 *   buffer：接收文件内容的缓冲区，必须非空。
 *   buffer_size：接收缓冲区总长度，单位为字节；函数会为文本回显预留结尾 '\0'。
 *   out_length：实际读回的文件长度输出指针，必须非空。
 * 返回值说明：
 *   SMART_STORAGE_ERR_OK：表示文件读取成功。
 *   SMART_STORAGE_ERR_NOENT：表示目标文件不存在。
 *   SMART_STORAGE_ERR_FBIG：表示目标文件大于当前接收缓冲区可承载大小。
 *   其他 SMART_STORAGE_ERR_*：表示路径、读取或数据链校验失败。
 */
int smart_storage_read_file(const char *path, uint8_t *buffer, uint32_t buffer_size, uint32_t *out_length);

/*
 * 函数作用：
 *   在不触发自动格式化的前提下，查询 SMARTFS 容量信息和指定文件状态。
 * 参数说明：
 *   path：需要查询的目标文件路径；允许为空指针或空字符串，此时只返回容量信息。
 *   info：信息输出结构体指针，必须非空。
 * 返回值说明：
 *   SMART_STORAGE_ERR_OK：表示信息查询成功。
 *   其他 SMART_STORAGE_ERR_*：表示元数据加载或路径查询失败。
 */
int smart_storage_get_info(const char *path, smart_storage_info_t *info);

/*
 * 函数作用：
 *   在不触发自动格式化的前提下，查询指定路径当前是否存在、是文件还是目录以及大小。
 * 参数说明：
 *   path：待查询的绝对路径，必须非空。
 *   info：路径信息输出结构体指针，必须非空。
 * 返回值说明：
 *   SMART_STORAGE_ERR_OK：表示查询完成；即使路径不存在，也会通过 info->exists=0 返回。
 *   其他 SMART_STORAGE_ERR_*：表示路径格式或元数据加载失败。
 */
int smart_storage_get_path_info(const char *path, smart_storage_path_info_t *info);

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
                           uint32_t *out_count);

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
int smart_storage_mkdir(const char *path);

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
int smart_storage_touch_file(const char *path);

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
int smart_storage_remove_path(const char *path);

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
int smart_storage_self_test(void);

#endif /* SMARTFS_PORT_H */
