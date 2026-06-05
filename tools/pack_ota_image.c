/*
 * 文件作用：
 *   将 Keil/fromelf 生成的原始 App bin 打包为“OTA 头部 + App payload”的升级镜像。
 * 说明：
 *   现场升级时只发送本工具输出的 Project_ota.bin；BootLoader App 接收端会解析
 *   64 字节头部，校验长度、目标地址、向量表和 payload CRC32 后再交给 BootLoader。
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OTA_IMAGE_MAGIC              0x474F5441UL
#define OTA_IMAGE_HEADER_SIZE        64UL
#define OTA_IMAGE_MAX_SIZE           (128UL * 1024UL)
#define OTA_DEFAULT_VERSION          0x00000001UL
#define OTA_DEFAULT_LOAD_ADDR        0x08011000UL
#define OTA_APP_REGION_SIZE          0x00020000UL
#define OTA_SRAM_START               0x20000000UL
#define OTA_SRAM_END                 0x20030000UL

/*
 * 函数作用：
 *   从字符串解析 32 位无符号整数，支持十进制和 0x 前缀十六进制。
 * 参数说明：
 *   text：待解析的命令行参数字符串。
 *   value：输出解析后的 32 位数值。
 * 返回值说明：
 *   0：解析成功。
 *   -1：参数为空、存在非法字符或数值超过 32 位范围。
 */
static int parse_u32_arg(const char *text, uint32_t *value)
{
    char *end_ptr = NULL;
    unsigned long parsed;

    if((NULL == text) || (NULL == value)){
        return -1;
    }

    errno = 0;
    parsed = strtoul(text, &end_ptr, 0);
    if((0 != errno) || (end_ptr == text) || ('\0' != *end_ptr) ||
       (parsed > 0xFFFFFFFFUL)){
        return -1;
    }

    *value = (uint32_t)parsed;
    return 0;
}

/*
 * 函数作用：
 *   从小端字节序缓冲区读取 32 位无符号整数。
 * 参数说明：
 *   data：指向至少 4 字节有效数据的缓冲区。
 * 返回值说明：
 *   返回解析出的 32 位数值；data 为空时返回 0。
 */
static uint32_t read_u32_le(const uint8_t *data)
{
    if(NULL == data){
        return 0UL;
    }

    return ((uint32_t)data[0]) |
           ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) |
           ((uint32_t)data[3] << 24U);
}

/*
 * 函数作用：
 *   按小端字节序写入 32 位无符号整数。
 * 参数说明：
 *   data：目标缓冲区，必须至少可写 4 字节。
 *   value：待写入的 32 位数值。
 * 返回值说明：
 *   无返回值。
 */
static void write_u32_le(uint8_t *data, uint32_t value)
{
    if(NULL == data){
        return;
    }

    data[0] = (uint8_t)(value & 0xFFU);
    data[1] = (uint8_t)((value >> 8U) & 0xFFU);
    data[2] = (uint8_t)((value >> 16U) & 0xFFU);
    data[3] = (uint8_t)((value >> 24U) & 0xFFU);
}

/*
 * 函数作用：
 *   计算标准 CRC32，用于 payload 和 OTA 头部校验。
 * 参数说明：
 *   data：参与 CRC32 计算的字节缓冲区；length 大于 0 时不能为空。
 *   length：参与计算的字节数。
 * 返回值说明：
 *   返回标准 CRC32 值；非法空指针输入返回 0。
 */
static uint32_t crc32_calc(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFUL;
    size_t index;
    uint8_t bit_index;

    if((NULL == data) && (length > 0U)){
        return 0UL;
    }

    for(index = 0U; index < length; index++){
        crc ^= data[index];
        for(bit_index = 0U; bit_index < 8U; bit_index++){
            if(0U != (crc & 1UL)){
                crc = (crc >> 1U) ^ 0xEDB88320UL;
            }else{
                crc >>= 1U;
            }
        }
    }

    return crc ^ 0xFFFFFFFFUL;
}

/*
 * 函数作用：
 *   校验原始 App bin 的 Cortex-M 向量表是否符合当前分区约束。
 * 参数说明：
 *   firmware：原始 App bin 内容。
 *   firmware_size：原始 App bin 字节数。
 *   load_addr：App 正式运行地址，必须与 Keil IROM 和 BootLoader 目标地址一致。
 *   stack_addr：输出向量表第 0 项 MSP 初值。
 *   entry_addr：输出向量表第 1 项 Reset_Handler 地址。
 * 返回值说明：
 *   0：向量表合法。
 *   -1：固件太短、栈地址非法、入口不是 Thumb 地址或入口不在 App 区。
 */
static int validate_app_vector(const uint8_t *firmware,
                               size_t firmware_size,
                               uint32_t load_addr,
                               uint32_t *stack_addr,
                               uint32_t *entry_addr)
{
    uint32_t stack_value;
    uint32_t entry_value;
    uint32_t app_region_end;

    if((NULL == firmware) || (firmware_size < 8U) ||
       (NULL == stack_addr) || (NULL == entry_addr)){
        return -1;
    }

    stack_value = read_u32_le(&firmware[0]);
    entry_value = read_u32_le(&firmware[4]);
    app_region_end = load_addr + OTA_APP_REGION_SIZE;

    if((stack_value < OTA_SRAM_START) || (stack_value >= OTA_SRAM_END)){
        return -1;
    }

    if((0U == (entry_value & 1UL)) ||
       ((entry_value & ~1UL) < load_addr) ||
       ((entry_value & ~1UL) >= app_region_end)){
        return -1;
    }

    *stack_addr = stack_value;
    *entry_addr = entry_value;
    return 0;
}

/*
 * 函数作用：
 *   读取整个输入 bin 文件到内存。
 * 参数说明：
 *   path：输入文件路径。
 *   buffer：输出分配得到的文件内容缓冲区；调用者负责 free。
 *   size：输出文件长度。
 * 返回值说明：
 *   0：读取成功。
 *   -1：打开、定位、分配或读取失败。
 */
static int read_file_all(const char *path, uint8_t **buffer, size_t *size)
{
    FILE *fp;
    long file_size;
    uint8_t *data;

    if((NULL == path) || (NULL == buffer) || (NULL == size)){
        return -1;
    }

    fp = fopen(path, "rb");
    if(NULL == fp){
        return -1;
    }

    if(0 != fseek(fp, 0L, SEEK_END)){
        fclose(fp);
        return -1;
    }
    file_size = ftell(fp);
    if(file_size <= 0L){
        fclose(fp);
        return -1;
    }
    if(0 != fseek(fp, 0L, SEEK_SET)){
        fclose(fp);
        return -1;
    }

    data = (uint8_t *)malloc((size_t)file_size);
    if(NULL == data){
        fclose(fp);
        return -1;
    }

    if(fread(data, 1U, (size_t)file_size, fp) != (size_t)file_size){
        free(data);
        fclose(fp);
        return -1;
    }

    fclose(fp);
    *buffer = data;
    *size = (size_t)file_size;
    return 0;
}

/*
 * 函数作用：
 *   根据原始 App 信息生成 64 字节 OTA 镜像头。
 * 参数说明：
 *   header：输出头部缓冲区，必须至少 64 字节。
 *   firmware：原始 App bin 内容。
 *   firmware_size：原始 App bin 字节数。
 *   version：写入 OTA 头部和 BootLoader 参数区的版本号。
 *   load_addr：App 正式写入地址。
 *   stack_addr：原始 App 向量表 MSP 值。
 *   entry_addr：原始 App 向量表 Reset_Handler 值。
 * 返回值说明：
 *   无返回值。
 */
static void build_ota_header(uint8_t *header,
                             const uint8_t *firmware,
                             size_t firmware_size,
                             uint32_t version,
                             uint32_t load_addr,
                             uint32_t stack_addr,
                             uint32_t entry_addr)
{
    uint32_t header_crc32;

    memset(header, 0, OTA_IMAGE_HEADER_SIZE);
    write_u32_le(&header[0], OTA_IMAGE_MAGIC);
    write_u32_le(&header[4], OTA_IMAGE_HEADER_SIZE);
    write_u32_le(&header[8], (uint32_t)firmware_size);
    write_u32_le(&header[12], load_addr);
    write_u32_le(&header[16], version);
    write_u32_le(&header[20], crc32_calc(firmware, firmware_size));
    write_u32_le(&header[24], 0UL);
    write_u32_le(&header[28], 0UL);
    write_u32_le(&header[32], stack_addr);
    write_u32_le(&header[36], entry_addr);

    /*
     * header_crc32 计算时自身字段必须为 0。
     * 接收端按同样规则校验，可发现头部任一关键字段被串口传输破坏。
     */
    header_crc32 = crc32_calc(header, OTA_IMAGE_HEADER_SIZE);
    write_u32_le(&header[28], header_crc32);
}

/*
 * 函数作用：
 *   把 OTA 头部和原始 App payload 顺序写入输出文件。
 * 参数说明：
 *   path：输出文件路径。
 *   header：64 字节 OTA 头部。
 *   firmware：原始 App bin 内容。
 *   firmware_size：原始 App bin 字节数。
 * 返回值说明：
 *   0：写入成功。
 *   -1：打开或写入失败。
 */
static int write_ota_image(const char *path,
                           const uint8_t *header,
                           const uint8_t *firmware,
                           size_t firmware_size)
{
    FILE *fp;

    if((NULL == path) || (NULL == header) || (NULL == firmware)){
        return -1;
    }

    fp = fopen(path, "wb");
    if(NULL == fp){
        return -1;
    }

    if(fwrite(header, 1U, OTA_IMAGE_HEADER_SIZE, fp) != OTA_IMAGE_HEADER_SIZE){
        fclose(fp);
        return -1;
    }
    if(fwrite(firmware, 1U, firmware_size, fp) != firmware_size){
        fclose(fp);
        return -1;
    }

    fclose(fp);
    return 0;
}

/*
 * 函数作用：
 *   打印命令行使用方法。
 * 参数说明：
 *   program：当前程序名。
 * 返回值说明：
 *   无返回值。
 */
static void print_usage(const char *program)
{
    fprintf(stderr,
            "usage: %s <input_app_bin> <output_ota_bin> [version] [load_addr]\n"
            "example: %s project/output/Project.bin project/output/Project_ota.bin 0x00000001 0x08011000\n",
            (NULL != program) ? program : "pack_ota_image",
            (NULL != program) ? program : "pack_ota_image");
}

/*
 * 函数作用：
 *   程序入口，解析参数、读取原始 bin、生成 OTA 头部并输出升级镜像。
 * 参数说明：
 *   argc：命令行参数数量。
 *   argv：命令行参数数组，至少包含输入 bin 和输出 bin。
 * 返回值说明：
 *   0：打包成功。
 *   1：参数、文件、固件向量表或写入失败。
 */
int main(int argc, char **argv)
{
    uint8_t *firmware = NULL;
    size_t firmware_size = 0U;
    uint8_t header[OTA_IMAGE_HEADER_SIZE];
    uint32_t version = OTA_DEFAULT_VERSION;
    uint32_t load_addr = OTA_DEFAULT_LOAD_ADDR;
    uint32_t stack_addr = 0UL;
    uint32_t entry_addr = 0UL;

    if((argc < 3) || (argc > 5)){
        print_usage((argc > 0) ? argv[0] : NULL);
        return 1;
    }

    if((argc >= 4) && (0 != parse_u32_arg(argv[3], &version))){
        fprintf(stderr, "pack ota failed: bad version argument\n");
        return 1;
    }
    if((argc >= 5) && (0 != parse_u32_arg(argv[4], &load_addr))){
        fprintf(stderr, "pack ota failed: bad load address argument\n");
        return 1;
    }

    if(0 != read_file_all(argv[1], &firmware, &firmware_size)){
        fprintf(stderr, "pack ota failed: read input bin failed: %s\n", argv[1]);
        return 1;
    }

    if((0U == firmware_size) || (firmware_size > OTA_IMAGE_MAX_SIZE)){
        fprintf(stderr,
                "pack ota failed: image size %lu exceeds supported range 1..%lu\n",
                (unsigned long)firmware_size,
                (unsigned long)OTA_IMAGE_MAX_SIZE);
        free(firmware);
        return 1;
    }

    if(0 != validate_app_vector(firmware,
                                firmware_size,
                                load_addr,
                                &stack_addr,
                                &entry_addr)){
        fprintf(stderr, "pack ota failed: invalid App vector table\n");
        free(firmware);
        return 1;
    }

    build_ota_header(header,
                     firmware,
                     firmware_size,
                     version,
                     load_addr,
                     stack_addr,
                     entry_addr);

    if(0 != write_ota_image(argv[2], header, firmware, firmware_size)){
        fprintf(stderr, "pack ota failed: write output bin failed: %s\n", argv[2]);
        free(firmware);
        return 1;
    }

    printf("pack ota ok: %s -> %s, image=%lu bytes, ota=%lu bytes, version=0x%08lX, crc=0x%08lX\n",
           argv[1],
           argv[2],
           (unsigned long)firmware_size,
           (unsigned long)(firmware_size + OTA_IMAGE_HEADER_SIZE),
           (unsigned long)version,
           (unsigned long)crc32_calc(firmware, firmware_size));

    free(firmware);
    return 0;
}
