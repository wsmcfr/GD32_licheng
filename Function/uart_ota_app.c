#include "uart_ota_app.h"

__IO uint8_t uart_ota_rx_flag = 0U;
__IO uint32_t uart_ota_irq_count = 0U;
__IO uint32_t uart_ota_overwrite_count = 0U;
__IO uint16_t uart_ota_last_irq_length = 0U;
__IO uint32_t uart_ota_ring_read_index = 0U;

/*
 * 宏作用：
 *   定义头部 bin OTA 协议的固定字段和日志限流参数。
 * 说明：
 *   Project_ota.bin 固定格式为 64 字节 OTA 头部 + 原始 App bin。现场发送时只需要
 *   使用串口工具的原始/直接发送文件功能，不依赖额外引导命令或握手流程。
 */
#define UART_OTA_IMAGE_MAGIC          0x474F5441UL
#define UART_OTA_TRACE_LIMIT          6U
#define UART_OTA_TASK_DRAIN_LIMIT     (UART_OTA_RING_BUFFER_SIZE / UART_OTA_STREAM_WINDOW_SIZE)
#define UART_OTA_VECTOR_BYTES         8U

/*
 * 枚举作用：
 *   描述头部 bin OTA 接收状态。
 * 成员说明：
 *   UART_OTA_STATE_ERASING_DOWNLOAD：ready 前正在预擦下载区。
 *   UART_OTA_STATE_WAIT_HEADER：等待并解析 64 字节 OTA 头部。
 *   UART_OTA_STATE_RECEIVING_PAYLOAD：头部合法，继续接收 App payload。
 *   UART_OTA_STATE_READY_TO_COMMIT：payload 已完整接收，等待任务层执行 Flash 操作。
 *   UART_OTA_STATE_COMMITTING：正在擦写下载区和参数区，防止重复进入。
 *   UART_OTA_STATE_ERROR：协议或校验失败，本轮升级作废。
 */
typedef enum
{
    UART_OTA_STATE_ERASING_DOWNLOAD = 0,
    UART_OTA_STATE_WAIT_HEADER,
    UART_OTA_STATE_RECEIVING_PAYLOAD,
    UART_OTA_STATE_READY_TO_COMMIT,
    UART_OTA_STATE_COMMITTING,
    UART_OTA_STATE_ERROR
} uart_ota_state_t;

/*
 * 结构体作用：
 *   保存 Project_ota.bin 头部解析后的关键字段。
 * 成员说明：
 *   magic：固定魔数，用于判断发送的文件是否是 OTA 镜像。
 *   header_size：头部长度，当前固定为 64 字节。
 *   image_size：后续 App payload 的真实长度。
 *   load_addr：payload 最终写入的 App 地址，必须是 0x0800D000。
 *   version：升级版本号，写入 BootLoader 参数区。
 *   image_crc32：对 App payload 计算的 CRC32。
 *   flags：预留标志位，当前必须为 0。
 *   header_crc32：头部自身 CRC32，计算时该字段置 0。
 *   stack_addr：App 向量表第 0 项 MSP 初值，用于日志和校验。
 *   entry_addr：App 向量表第 1 项 Reset_Handler 地址，用于日志和校验。
 */
typedef struct
{
    uint32_t magic;
    uint32_t header_size;
    uint32_t image_size;
    uint32_t load_addr;
    uint32_t version;
    uint32_t image_crc32;
    uint32_t flags;
    uint32_t header_crc32;
    uint32_t stack_addr;
    uint32_t entry_addr;
} uart_ota_image_header_t;

/*
 * 结构体作用：
 *   保存一次头部 bin OTA 接收会话的运行态。
 * 成员说明：
 *   state：当前接收状态。
 *   header：解析后的 OTA 头部字段。
 *   header_bytes：当前已接收的头部字节数。
 *   received_size：当前已接收的 payload 字节数。
 *   running_crc：payload 接收过程中的未取反 CRC32 中间值。
 *   trace_count：已打印的接收摘要数量，用于限制日志刷屏。
 *   error_code：最近一次错误原因，用于调试日志。
 *   download_area_ready：下载缓存区是否已在 ready 前完成整区预擦。
 *   flash_dirty：本轮 payload 是否已经写入下载区；写脏后不能无擦除重试。
 */
typedef struct
{
    uart_ota_state_t state;
    uart_ota_image_header_t header;
    uint32_t header_bytes;
    uint32_t received_size;
    uint32_t running_crc;
    uint8_t trace_count;
    uint32_t error_code;
    uint8_t download_area_ready;
    uint8_t flash_dirty;
} uart_ota_session_t;

/* OTA 接收头部临时缓存，只保存固定 64 字节头，不保存整包头外数据。 */
static uint8_t g_uart_ota_header_buffer[UART_OTA_IMAGE_HEADER_SIZE] = {0};

/* OTA 错误态重同步时缓存 magic 前缀，允许 4 字节 magic 被 DMA 拆成多块。 */
static uint8_t g_uart_ota_resync_magic_buffer[4] = {0};

/* OTA 错误态重同步 magic 当前已经匹配的字节数。 */
static uint8_t g_uart_ota_resync_magic_bytes = 0U;

/* OTA payload 前 8 字节向量表缓存，用于流式写 Flash 后复核 MSP 和入口地址。 */
static uint8_t g_uart_ota_vector_buffer[UART_OTA_VECTOR_BYTES] = {0};

/* OTA 会话状态只在中断和任务共享，进入任务提交时会临时关闭相关中断保护。 */
static volatile uart_ota_session_t g_uart_ota_session = {0};

/*
 * 函数作用：
 *   从小端字节序缓冲区读取 32 位无符号整数。
 * 参数说明：
 *   data：指向至少 4 字节有效数据的缓冲区。
 * 返回值说明：
 *   返回解析出的 32 位数值；data 为空时返回 0。
 */
static uint32_t prv_uart_ota_read_u32_le(const uint8_t *data)
{
    if(NULL == data){
        return 0U;
    }

    return ((uint32_t)data[0]) |
           ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) |
           ((uint32_t)data[3] << 24U);
}

/*
 * 函数作用：
 *   计算一段数据的标准 CRC32。
 * 参数说明：
 *   data：待计算数据起始地址；length 大于 0 时必须非空。
 *   length：待计算字节数。
 * 返回值说明：
 *   返回标准 CRC32 值；非法空指针输入返回 0。
 */
static uint32_t prv_uart_ota_crc32_calc(const uint8_t *data, uint32_t length)
{
    uint32_t crc;

    if((NULL == data) && (length > 0U)){
        return 0U;
    }

    crc = bootloader_port_crc32_update(0xFFFFFFFFUL, data, length);
    return crc ^ 0xFFFFFFFFUL;
}

/*
 * 函数作用：
 *   读取 USART1 circular DMA 当前硬件写指针。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   返回 DMA 即将写入的环形缓冲偏移，范围为 0..UART_OTA_RING_BUFFER_SIZE-1。
 */
static uint32_t prv_uart_ota_dma_write_index(void)
{
    uint32_t remaining;
    uint32_t write_index;

    /*
     * circular DMA 的剩余计数会在环尾自动重装。用“总长度 - 剩余计数”
     * 可以得到当前硬件写指针；刚好回绕时结果等于总长度，需要归零。
     */
    remaining = dma_transfer_number_get(USART1_RX_DMA_PERIPH, USART1_RX_DMA_CHANNEL);
    if(remaining > UART_OTA_RING_BUFFER_SIZE){
        remaining = UART_OTA_RING_BUFFER_SIZE;
    }

    write_index = UART_OTA_RING_BUFFER_SIZE - remaining;
    if(write_index >= UART_OTA_RING_BUFFER_SIZE){
        write_index = 0U;
    }

    return write_index;
}

/*
 * 函数作用：
 *   计算 circular DMA 中尚未被任务层消费的字节数。
 * 参数说明：
 *   read_index：任务层当前读指针，范围必须小于 UART_OTA_RING_BUFFER_SIZE。
 *   write_index：DMA 当前写指针，范围必须小于 UART_OTA_RING_BUFFER_SIZE。
 * 返回值说明：
 *   返回可安全读取的字节数；如果读写指针相等，返回 0。
 */
static uint32_t prv_uart_ota_ring_available(uint32_t read_index, uint32_t write_index)
{
    if(read_index <= write_index){
        return write_index - read_index;
    }

    return UART_OTA_RING_BUFFER_SIZE - read_index + write_index;
}

/*
 * 函数作用：
 *   把 64 字节 OTA 头部缓存解析到结构体。
 * 参数说明：
 *   header：输出结构体。
 * 返回值说明：
 *   1：解析成功。
 *   0：输出参数为空。
 */
static uint8_t prv_uart_ota_parse_header(uart_ota_image_header_t *header)
{
    if(NULL == header){
        return 0U;
    }

    header->magic = prv_uart_ota_read_u32_le(&g_uart_ota_header_buffer[0]);
    header->header_size = prv_uart_ota_read_u32_le(&g_uart_ota_header_buffer[4]);
    header->image_size = prv_uart_ota_read_u32_le(&g_uart_ota_header_buffer[8]);
    header->load_addr = prv_uart_ota_read_u32_le(&g_uart_ota_header_buffer[12]);
    header->version = prv_uart_ota_read_u32_le(&g_uart_ota_header_buffer[16]);
    header->image_crc32 = prv_uart_ota_read_u32_le(&g_uart_ota_header_buffer[20]);
    header->flags = prv_uart_ota_read_u32_le(&g_uart_ota_header_buffer[24]);
    header->header_crc32 = prv_uart_ota_read_u32_le(&g_uart_ota_header_buffer[28]);
    header->stack_addr = prv_uart_ota_read_u32_le(&g_uart_ota_header_buffer[32]);
    header->entry_addr = prv_uart_ota_read_u32_le(&g_uart_ota_header_buffer[36]);

    return 1U;
}

/*
 * 函数作用：
 *   校验 OTA 头部字段、头部 CRC 和 payload 向量表元数据。
 * 参数说明：
 *   header：待校验的 OTA 头部字段。
 * 返回值说明：
 *   BOOTLOADER_PORT_STATUS_OK：头部合法。
 *   BOOTLOADER_PORT_STATUS_BAD_PARAM：头部字段、大小或 CRC 不合法。
 *   BOOTLOADER_PORT_STATUS_BAD_VECTOR：头部记录的向量表不满足 App 跳转要求。
 */
static bootloader_port_status_t prv_uart_ota_validate_header(const uart_ota_image_header_t *header)
{
    uint8_t header_for_crc[UART_OTA_IMAGE_HEADER_SIZE];
    uint32_t calc_header_crc32;
    uint32_t app_region_end;

    if(NULL == header){
        return BOOTLOADER_PORT_STATUS_BAD_PARAM;
    }

    memcpy(header_for_crc, g_uart_ota_header_buffer, sizeof(header_for_crc));
    /*
     * header_crc32 字段自身不参与头部 CRC 计算，必须临时清零后再算。
     * 这样接收端和打包工具对同一头部会得到完全一致的结果。
     */
    header_for_crc[28] = 0U;
    header_for_crc[29] = 0U;
    header_for_crc[30] = 0U;
    header_for_crc[31] = 0U;
    calc_header_crc32 = prv_uart_ota_crc32_calc(header_for_crc, sizeof(header_for_crc));

    if((UART_OTA_IMAGE_MAGIC != header->magic) ||
       (UART_OTA_IMAGE_HEADER_SIZE != header->header_size) ||
       (0U == header->image_size) ||
       (header->image_size > BOOTLOADER_PORT_DOWNLOAD_MAX_SIZE) ||
       (BOOT_APP_START_ADDRESS != header->load_addr) ||
       (0U != header->flags) ||
       (calc_header_crc32 != header->header_crc32)){
        return BOOTLOADER_PORT_STATUS_BAD_PARAM;
    }

    if((header->stack_addr < 0x20000000UL) || (header->stack_addr >= 0x20030000UL)){
        return BOOTLOADER_PORT_STATUS_BAD_VECTOR;
    }

    app_region_end = BOOT_APP_START_ADDRESS + BOOTLOADER_PORT_APP_MAX_SIZE;
    if((0U == (header->entry_addr & 1UL)) ||
       ((header->entry_addr & ~1UL) < BOOT_APP_START_ADDRESS) ||
       ((header->entry_addr & ~1UL) >= app_region_end)){
        return BOOTLOADER_PORT_STATUS_BAD_VECTOR;
    }

    return BOOTLOADER_PORT_STATUS_OK;
}

/*
 * 函数作用：
 *   将 OTA 会话恢复到等待头部状态。
 * 参数说明：
 *   keep_trace：非 0 表示保留当前日志计数，0 表示重新打开前几帧摘要日志。
 * 返回值说明：
 *   无返回值。
 */
static void prv_uart_ota_reset_session(uint8_t keep_trace)
{
    uint8_t trace_count = g_uart_ota_session.trace_count;
    uint8_t download_area_ready = g_uart_ota_session.download_area_ready;

    memset((void *)&g_uart_ota_session, 0, sizeof(g_uart_ota_session));
    g_uart_ota_session.state = (0U != download_area_ready) ?
                               UART_OTA_STATE_WAIT_HEADER :
                               UART_OTA_STATE_ERASING_DOWNLOAD;
    g_uart_ota_session.running_crc = 0xFFFFFFFFUL;
    g_uart_ota_session.download_area_ready = download_area_ready;
    if(0U != keep_trace){
        g_uart_ota_session.trace_count = trace_count;
    }
}

/*
 * 函数作用：
 *   清空错误态下用于重新同步 OTA magic 的临时缓存。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
static void prv_uart_ota_clear_resync_magic(void)
{
    g_uart_ota_resync_magic_bytes = 0U;
    memset(g_uart_ota_resync_magic_buffer, 0, sizeof(g_uart_ota_resync_magic_buffer));
}

/*
 * 函数作用：
 *   记录 OTA 错误并进入错误态，防止错误文件继续写入升级缓存。
 * 参数说明：
 *   error_code：错误来源编码，通常使用 bootloader_port_status_t 或自定义数值。
 * 返回值说明：
 *   无返回值。
 */
static void prv_uart_ota_enter_error(uint32_t error_code)
{
    g_uart_ota_session.error_code = error_code;
    g_uart_ota_session.state = UART_OTA_STATE_ERROR;
    prv_uart_ota_clear_resync_magic();
}

/*
 * 函数作用：
 *   在 OTA 错误态中尝试用下一包数据重新同步到合法头部。
 * 主要流程：
 *   1. 只在当前状态为 UART_OTA_STATE_ERROR 时工作。
 *   2. 逐字节匹配 OTA 头部 magic，允许 magic 被 DMA 拆成多个小块。
 *   3. 命中 magic 后预填头部缓存前 4 字节，重置会话并让调用者继续解析后续头部。
 * 参数说明：
 *   data：本次任务层取出的连续字节缓冲区。
 *   length：本次缓冲区有效字节数。
 *   consumed_prefix：输出本次为了补齐 magic 已经消耗的字节数。
 * 返回值说明：
 *   1：表示已经重新同步到等待头部状态，调用者可以继续解析本包。
 *   0：表示当前仍不能重新同步，本包应被忽略。
 */
static uint8_t prv_uart_ota_try_resync_from_error(const uint8_t *data,
                                                  uint16_t length,
                                                  uint32_t *consumed_prefix)
{
    static const uint8_t magic_bytes[4] = {0x41U, 0x54U, 0x4FU, 0x47U};
    uint32_t offset = 0U;

    if(NULL != consumed_prefix){
        *consumed_prefix = 0U;
    }

    if(UART_OTA_STATE_ERROR == g_uart_ota_session.state){
        /* 当前处于错误态，继续尝试用新 OTA 文件头重新同步。 */
    }else{
        return 1U;
    }

    if(0U != g_uart_ota_session.flash_dirty){
        /*
         * payload 或 Flash 写入阶段失败时，下载区内容已经不是全 0xFF。
         * 上位机又无法等待我们重新擦除，所以必须复位后重新走 ready 前预擦流程。
         */
        return 0U;
    }

    if((NULL == data) || (0U == length) || (NULL == consumed_prefix)){
        return 0U;
    }

    /*
     * 错误态下只接受一份新 OTA 文件从头开始发送。
     * 这里按 magic 前缀逐字节推进，不在数据块中间扫描 magic，避免在损坏
     * payload 的中间字节里误判成新的 OTA 文件。
     */
    while((offset < (uint32_t)length) && (g_uart_ota_resync_magic_bytes < 4U)){
        if(data[offset] != magic_bytes[g_uart_ota_resync_magic_bytes]){
            prv_uart_ota_clear_resync_magic();
            return 0U;
        }

        g_uart_ota_resync_magic_buffer[g_uart_ota_resync_magic_bytes] = data[offset];
        g_uart_ota_resync_magic_bytes++;
        offset++;
    }

    if(g_uart_ota_resync_magic_bytes < 4U){
        return 0U;
    }

    my_printf(DEBUG_USART,
              "OTA: resync after error code=%lu\r\n",
              (unsigned long)g_uart_ota_session.error_code);
    prv_uart_ota_reset_session(1U);
    memset(g_uart_ota_header_buffer, 0, sizeof(g_uart_ota_header_buffer));
    memcpy(g_uart_ota_header_buffer, g_uart_ota_resync_magic_buffer, sizeof(g_uart_ota_resync_magic_buffer));
    g_uart_ota_session.header_bytes = 4U;
    *consumed_prefix = offset;
    prv_uart_ota_clear_resync_magic();
    return 1U;
}

/*
 * 函数作用：
 *   把接收到的字节复制到头部缓存，头部满 64 字节后解析并校验。
 * 参数说明：
 *   data：当前接收数据块起始地址。
 *   length：当前接收数据块长度。
 *   consumed：输出本函数实际消费的字节数。
 * 返回值说明：
 *   BOOTLOADER_PORT_STATUS_OK：头部仍在接收中或已经合法切到 payload 状态。
 *   其它状态：头部字段、CRC 或向量表非法。
 */
static bootloader_port_status_t prv_uart_ota_consume_header(const uint8_t *data,
                                                            uint32_t length,
                                                            uint32_t *consumed)
{
    uint32_t remaining;
    uint32_t copy_length;
    bootloader_port_status_t status;

    if((NULL == data) || (NULL == consumed)){
        return BOOTLOADER_PORT_STATUS_BAD_PARAM;
    }

    *consumed = 0U;
    remaining = UART_OTA_IMAGE_HEADER_SIZE - g_uart_ota_session.header_bytes;
    copy_length = length;
    if(copy_length > remaining){
        copy_length = remaining;
    }

    memcpy(&g_uart_ota_header_buffer[g_uart_ota_session.header_bytes], data, copy_length);
    g_uart_ota_session.header_bytes += copy_length;
    *consumed = copy_length;

    if(g_uart_ota_session.header_bytes < UART_OTA_IMAGE_HEADER_SIZE){
        return BOOTLOADER_PORT_STATUS_OK;
    }

    if(0U == prv_uart_ota_parse_header((uart_ota_image_header_t *)&g_uart_ota_session.header)){
        return BOOTLOADER_PORT_STATUS_BAD_PARAM;
    }

    status = prv_uart_ota_validate_header((const uart_ota_image_header_t *)&g_uart_ota_session.header);
    if(BOOTLOADER_PORT_STATUS_OK != status){
        return status;
    }

    g_uart_ota_session.state = UART_OTA_STATE_RECEIVING_PAYLOAD;
    g_uart_ota_session.running_crc = 0xFFFFFFFFUL;
    g_uart_ota_session.received_size = 0U;
    my_printf(DEBUG_USART,
              "OTA: header ok size=%lu version=0x%08lx crc=0x%08lx\r\n",
              (unsigned long)g_uart_ota_session.header.image_size,
              (unsigned long)g_uart_ota_session.header.version,
              (unsigned long)g_uart_ota_session.header.image_crc32);

    return BOOTLOADER_PORT_STATUS_OK;
}

/*
 * 函数作用：
 *   把一段 payload 数据流式写入已预擦的内部 Flash 下载区。
 * 参数说明：
 *   offset：本段数据在完整 payload 中的起始偏移，单位为字节。
 *   data：本段 payload 数据起始地址。
 *   length：本段 payload 数据长度，单位为字节。
 * 返回值说明：
 *   BOOTLOADER_PORT_STATUS_OK：本段写入成功。
 *   BOOTLOADER_PORT_STATUS_BAD_PARAM：下载区未预擦、参数非法或越界。
 *   BOOTLOADER_PORT_STATUS_FLASH_ERROR：内部 Flash 编程失败。
 */
static bootloader_port_status_t prv_uart_ota_stream_flash_write(uint32_t offset,
                                                                const uint8_t *data,
                                                                uint32_t length)
{
    if((0U == g_uart_ota_session.download_area_ready) ||
       (NULL == data) ||
       (0U == length) ||
       ((offset + length) > g_uart_ota_session.header.image_size)){
        return BOOTLOADER_PORT_STATUS_BAD_PARAM;
    }

    g_uart_ota_session.flash_dirty = 1U;
    return bootloader_port_write_download_chunk(offset, data, length);
}

/*
 * 函数作用：
 *   消费 OTA payload 字节并更新接收进度与运行 CRC。
 * 参数说明：
 *   data：当前 payload 数据块起始地址。
 *   length：当前 payload 数据块长度。
 *   consumed：输出本函数实际消费的字节数。
 * 返回值说明：
 *   BOOTLOADER_PORT_STATUS_OK：payload 写入下载区成功，完整时会切到待提交状态。
 *   BOOTLOADER_PORT_STATUS_BAD_PARAM：数据超过头部声明长度或 CRC 不匹配。
 *   BOOTLOADER_PORT_STATUS_BAD_VECTOR：payload 实际向量表与头部字段不一致或非法。
 */
static bootloader_port_status_t prv_uart_ota_consume_payload(const uint8_t *data,
                                                             uint32_t length,
                                                             uint32_t *consumed)
{
    uint32_t remaining;
    uint32_t copy_length;
    uint32_t final_crc32;
    uint32_t stack_addr;
    uint32_t entry_addr;
    uint32_t vector_remaining;
    uint32_t vector_copy_len;
    bootloader_port_status_t status;

    if((NULL == data) || (NULL == consumed)){
        return BOOTLOADER_PORT_STATUS_BAD_PARAM;
    }

    *consumed = 0U;
    remaining = g_uart_ota_session.header.image_size - g_uart_ota_session.received_size;
    copy_length = length;
    if(copy_length > remaining){
        copy_length = remaining;
    }
    if(0U == copy_length){
        return BOOTLOADER_PORT_STATUS_BAD_PARAM;
    }

    if(g_uart_ota_session.received_size < UART_OTA_VECTOR_BYTES){
        /*
         * 流式写入后 RAM 中不再保存完整 payload，因此只缓存向量表前 8 字节。
         * 这足够在收满后复核 payload 实际 MSP/Reset_Handler 是否与头部一致。
         */
        vector_remaining = UART_OTA_VECTOR_BYTES - g_uart_ota_session.received_size;
        vector_copy_len = copy_length;
        if(vector_copy_len > vector_remaining){
            vector_copy_len = vector_remaining;
        }
        memcpy(&g_uart_ota_vector_buffer[g_uart_ota_session.received_size],
               data,
               vector_copy_len);
    }

    g_uart_ota_session.running_crc =
        bootloader_port_crc32_update(g_uart_ota_session.running_crc, data, copy_length);

    status = prv_uart_ota_stream_flash_write(g_uart_ota_session.received_size,
                                             data,
                                             copy_length);
    if(BOOTLOADER_PORT_STATUS_OK != status){
        return status;
    }

    g_uart_ota_session.received_size += copy_length;
    *consumed = copy_length;

    if(g_uart_ota_session.received_size < g_uart_ota_session.header.image_size){
        return BOOTLOADER_PORT_STATUS_OK;
    }

    status = bootloader_port_validate_firmware_vector(g_uart_ota_vector_buffer,
                                                      sizeof(g_uart_ota_vector_buffer),
                                                      &stack_addr,
                                                      &entry_addr);
    if(BOOTLOADER_PORT_STATUS_OK != status){
        return BOOTLOADER_PORT_STATUS_BAD_VECTOR;
    }
    if((stack_addr != g_uart_ota_session.header.stack_addr) ||
       (entry_addr != g_uart_ota_session.header.entry_addr)){
        return BOOTLOADER_PORT_STATUS_BAD_VECTOR;
    }

    final_crc32 = g_uart_ota_session.running_crc ^ 0xFFFFFFFFUL;
    if(final_crc32 != g_uart_ota_session.header.image_crc32){
        my_printf(DEBUG_USART,
                  "OTA: payload crc fail calc=0x%08lx expect=0x%08lx\r\n",
                  (unsigned long)final_crc32,
                  (unsigned long)g_uart_ota_session.header.image_crc32);
        return BOOTLOADER_PORT_STATUS_BAD_PARAM;
    }

    g_uart_ota_session.state = UART_OTA_STATE_READY_TO_COMMIT;
    my_printf(DEBUG_USART,
              "OTA: payload ok size=%lu crc=0x%08lx\r\n",
              (unsigned long)g_uart_ota_session.header.image_size,
              (unsigned long)g_uart_ota_session.header.image_crc32);

    return BOOTLOADER_PORT_STATUS_OK;
}

/*
 * 函数作用：
 *   向 RS485/USART1 OTA 专用口发送一次上电探测串，用于确认 TX 线和串口号是否正确。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void uart_ota_emit_startup_probe(void)
{
    static const uint8_t probe_message[] = "OTA485: ready, send Project_ota.bin raw\r\n";

    /*
     * 启动探测只发送一次文本，之后接收端不再主动发起任何文件传输握手。
     * 这样现场工具只需要选择 Project_ota.bin 原始/直接发送即可。
     */
    bsp_rs485_direction_transmit();
    (void)bsp_usart_send_buffer(UART_OTA_USART,
                                probe_message,
                                (uint16_t)(sizeof(probe_message) - 1U));
    bsp_rs485_direction_receive();
}

/*
 * 函数作用：
 *   在发送 OTA ready 前预擦完整下载区，确保后续连续裸流只需要 Flash 编程。
 * 主要流程：
 *   1. 将 OTA 会话切换到预擦状态，避免任务层误解析旧 DMA 数据。
 *   2. 按下载区最大容量擦除 152KB 缓存区。
 *   3. 擦除成功后清空 DMA ring 读指针和诊断计数，进入等待头部状态。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   1：下载区预擦成功，可以向上位机发 ready。
 *   0：下载区预擦失败，禁止发 ready，避免收到裸流后无法可靠写入。
 */
uint8_t uart_ota_prepare_download_area_before_ready(void)
{
    bootloader_port_status_t status;

    g_uart_ota_session.state = UART_OTA_STATE_ERASING_DOWNLOAD;
    my_printf(DEBUG_USART,
              "OTA: pre-erase download area size=%lu\r\n",
              (unsigned long)BOOTLOADER_PORT_DOWNLOAD_MAX_SIZE);

    status = bootloader_port_prepare_download_area(BOOTLOADER_PORT_DOWNLOAD_MAX_SIZE);
    if(BOOTLOADER_PORT_STATUS_OK != status){
        my_printf(DEBUG_USART, "OTA: pre-erase failed status=%u\r\n", (unsigned int)status);
        prv_uart_ota_enter_error((uint32_t)status);
        return 0U;
    }

    __disable_irq();
    uart_ota_rx_flag = 0U;
    uart_ota_irq_count = 0U;
    uart_ota_overwrite_count = 0U;
    uart_ota_last_irq_length = 0U;
    uart_ota_ring_read_index = prv_uart_ota_dma_write_index();
    __enable_irq();

    g_uart_ota_session.download_area_ready = 1U;
    g_uart_ota_session.flash_dirty = 0U;
    prv_uart_ota_reset_session(0U);
    memset(g_uart_ota_header_buffer, 0, sizeof(g_uart_ota_header_buffer));
    memset(g_uart_ota_vector_buffer, 0, sizeof(g_uart_ota_vector_buffer));
    prv_uart_ota_clear_resync_magic();

    my_printf(DEBUG_USART, "OTA: pre-erase ok\r\n");
    return 1U;
}

/*
 * 函数作用：
 *   对外提供 OTA 运行态复位接口，便于系统初始化后恢复到干净状态。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void uart_ota_reset_runtime(void)
{
    __disable_irq();
    uart_ota_rx_flag = 0U;
    uart_ota_irq_count = 0U;
    uart_ota_overwrite_count = 0U;
    uart_ota_last_irq_length = 0U;
    uart_ota_ring_read_index = prv_uart_ota_dma_write_index();
    prv_uart_ota_reset_session(0U);
    __enable_irq();

    memset(g_uart_ota_header_buffer, 0, sizeof(g_uart_ota_header_buffer));
    memset(g_uart_ota_vector_buffer, 0, sizeof(g_uart_ota_vector_buffer));
    prv_uart_ota_clear_resync_magic();
}

/*
 * 函数作用：
 *   从 USART1 circular DMA 环形缓冲中取出一段连续裸流数据。
 * 参数说明：
 *   data：输出缓冲区，必须至少可写 UART_OTA_STREAM_WINDOW_SIZE 字节。
 *   length：输出本次取出的有效字节数。
 * 返回值说明：
 *   1：成功取出一段数据。
 *   0：当前没有新数据或参数非法。
 */
static uint8_t uart_ota_take_ring_bytes(uint8_t *data, uint16_t *length)
{
    uint32_t read_index;
    uint32_t write_index;
    uint32_t available;
    uint32_t contiguous;
    uint32_t copy_length;

    if((NULL == data) || (NULL == length)){
        return 0U;
    }

    *length = 0U;
    __disable_irq();
    read_index = uart_ota_ring_read_index;
    write_index = prv_uart_ota_dma_write_index();
    available = prv_uart_ota_ring_available(read_index, write_index);

    if(available >= (UART_OTA_RING_BUFFER_SIZE - UART_OTA_STREAM_WINDOW_SIZE)){
        /*
         * 读指针落后过多说明任务层可能已经被 DMA 追上。此时继续解析只会得到
         * 被覆盖的旧/新混合数据，必须进入错误态，避免写入损坏镜像。
         */
        uart_ota_overwrite_count++;
        prv_uart_ota_enter_error((uint32_t)BOOTLOADER_PORT_STATUS_BAD_PARAM);
        __enable_irq();
        return 0U;
    }

    if(available > 0U){
        contiguous = UART_OTA_RING_BUFFER_SIZE - read_index;
        copy_length = available;
        if(copy_length > contiguous){
            copy_length = contiguous;
        }
        if(copy_length > UART_OTA_STREAM_WINDOW_SIZE){
            copy_length = UART_OTA_STREAM_WINDOW_SIZE;
        }

        memcpy(data, &usart1_rxbuffer[read_index], copy_length);
        read_index += copy_length;
        if(read_index >= UART_OTA_RING_BUFFER_SIZE){
            read_index = 0U;
        }
        uart_ota_ring_read_index = read_index;
        *length = (uint16_t)copy_length;
    }

    if(uart_ota_ring_read_index == prv_uart_ota_dma_write_index()){
        uart_ota_rx_flag = 0U;
    }
    __enable_irq();

    return (0U != *length) ? 1U : 0U;
}

/*
 * 函数作用：
 *   处理任务层取出的 OTA 裸流字节。
 * 主要流程：
 *   1. 等待并解析固定 64 字节头部。
 *   2. 头部合法后把 App payload 流式写入已预擦下载区。
 *   3. payload 完整后校验流式 CRC 和向量表，切到待提交状态。
 * 参数说明：
 *   data：本次接收到的连续字节。
 *   length：本次接收字节数。
 * 返回值说明：
 *   无返回值。
 */
void uart_ota_feed_rx_bytes(const uint8_t *data, uint16_t length)
{
    uint32_t offset = 0U;
    uint32_t consumed = 0U;
    bootloader_port_status_t status;

    if((NULL == data) || (0U == length)){
        return;
    }

    if(0U == prv_uart_ota_try_resync_from_error(data, length, &offset)){
        return;
    }

    if(g_uart_ota_session.trace_count < UART_OTA_TRACE_LIMIT){
        my_printf(DEBUG_USART,
                  "OTA: rx chunk len=%u state=%u received=%lu irq=%lu ovw=%lu\r\n",
                  length,
                  (unsigned int)g_uart_ota_session.state,
                  (unsigned long)g_uart_ota_session.received_size,
                  (unsigned long)uart_ota_irq_count,
                  (unsigned long)uart_ota_overwrite_count);
        g_uart_ota_session.trace_count++;
    }

    while(offset < (uint32_t)length){
        consumed = 0U;
        if(UART_OTA_STATE_WAIT_HEADER == g_uart_ota_session.state){
            status = prv_uart_ota_consume_header(&data[offset],
                                                 (uint32_t)length - offset,
                                                 &consumed);
            if(BOOTLOADER_PORT_STATUS_OK != status){
                my_printf(DEBUG_USART, "OTA: bad header status=%u\r\n", (unsigned int)status);
                prv_uart_ota_enter_error((uint32_t)status);
                return;
            }
            offset += consumed;
        }else if(UART_OTA_STATE_RECEIVING_PAYLOAD == g_uart_ota_session.state){
            status = prv_uart_ota_consume_payload(&data[offset],
                                                  (uint32_t)length - offset,
                                                  &consumed);
            if(BOOTLOADER_PORT_STATUS_OK != status){
                my_printf(DEBUG_USART, "OTA: payload failed status=%u\r\n", (unsigned int)status);
                prv_uart_ota_enter_error((uint32_t)status);
                return;
            }
            offset += consumed;
        }else{
            /*
             * 成功收满后如果串口工具还附带了尾部字节，直接忽略。
             * 当前协议以头部 image_size 为唯一长度来源，不能继续接收污染下一轮状态。
             */
            return;
        }

        if(0U == consumed){
            return;
        }
    }
}

/*
 * 函数作用：
 *   把已完整接收并校验通过的 App payload 写入下载区，随后写 BootLoader 参数区。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   BOOTLOADER_PORT_STATUS_OK：下载区和参数区都准备完毕。
 *   其它状态：下载区回读 CRC 或参数区写入失败。
 */
static bootloader_port_status_t prv_uart_ota_commit_payload_to_bootloader(void)
{
    uint32_t flash_crc32;
    bootloader_port_status_t status;

    g_uart_ota_session.state = UART_OTA_STATE_COMMITTING;

    my_printf(DEBUG_USART,
              "OTA: commit start size=%lu\r\n",
              (unsigned long)g_uart_ota_session.header.image_size);

    /*
     * payload 已经在接收过程中写入下载区。这里只做回读 CRC，确认最终交给
     * BootLoader 搬运的 Flash 内容与 OTA 文件 payload 完全一致。
     */
    flash_crc32 = bootloader_port_calc_download_crc32(g_uart_ota_session.header.image_size);
    if(flash_crc32 != g_uart_ota_session.header.image_crc32){
        my_printf(DEBUG_USART,
                  "OTA: download crc fail calc=0x%08lx expect=0x%08lx\r\n",
                  (unsigned long)flash_crc32,
                  (unsigned long)g_uart_ota_session.header.image_crc32);
        return BOOTLOADER_PORT_STATUS_FLASH_ERROR;
    }

    status = bootloader_port_write_upgrade_info(g_uart_ota_session.header.version,
                                                g_uart_ota_session.header.image_size,
                                                g_uart_ota_session.header.image_crc32);
    return status;
}

/*
 * 函数作用：
 *   周期性处理 OTA 提交流程。
 * 主要流程：
 *   1. 接收中断只提示 circular DMA 有新数据，任务层按读写指针取 512B 窗口。
 *   2. payload 接收时边算 CRC 边写入已预擦下载区，完整后只回读 CRC 并写参数区。
 *   3. 参数区写入成功后短延时复位，让 BootLoader 搬运下载区固件。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void uart_ota_task(void)
{
    uint8_t rx_window[UART_OTA_STREAM_WINDOW_SIZE];
    uint16_t rx_length = 0U;
    uint8_t drained_count = 0U;
    bootloader_port_status_t status;

    /*
     * RS485 裸流 OTA 使用 circular DMA 后，任务层根据 DMA 写指针消费环形缓冲。
     * 单次调用最多处理 32KB，以便在连续裸流下快速追上输入，同时仍保留上限，
     * 防止异常输入让本任务长期占用合作式调度器。
     */
    while(drained_count < UART_OTA_TASK_DRAIN_LIMIT) {
        if(0U == uart_ota_take_ring_bytes(rx_window, &rx_length)) {
            break;
        }
        uart_ota_feed_rx_bytes(rx_window, rx_length);
        drained_count++;

        if(UART_OTA_STATE_READY_TO_COMMIT == g_uart_ota_session.state) {
            break;
        }
    }

    if(UART_OTA_STATE_READY_TO_COMMIT != g_uart_ota_session.state){
        return;
    }

    /*
     * 进入提交阶段时完整 payload 已经写入下载区。此时关闭 OTA 接收中断，
     * 避免串口工具尾部杂字节或新一轮裸流污染已经完成的升级会话。
     */
    nvic_irq_disable(USART1_IRQn);
    nvic_irq_disable(DMA0_Channel5_IRQn);
    status = prv_uart_ota_commit_payload_to_bootloader();
    if(BOOTLOADER_PORT_STATUS_OK != status){
        my_printf(DEBUG_USART, "OTA: commit failed status=%u\r\n", (unsigned int)status);
        prv_uart_ota_enter_error((uint32_t)status);
        nvic_irq_enable(USART1_IRQn, 1U, 0U);
        nvic_irq_enable(DMA0_Channel5_IRQn, 1U, 1U);
        return;
    }

    my_printf(DEBUG_USART,
              "OTA: ready, reset to BootLoader size=%lu version=0x%08lx\r\n",
              (unsigned long)g_uart_ota_session.header.image_size,
              (unsigned long)g_uart_ota_session.header.version);

    delay_ms(50U);
    bootloader_port_request_upgrade_reset();
}
