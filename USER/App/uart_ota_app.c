#include "uart_ota_app.h"

__IO uint8_t uart_ota_rx_flag = 0U;
__IO uint16_t uart_ota_dma_length = 0U;
uint8_t uart_ota_dma_buffer[UART_OTA_FRAME_BUFFER_SIZE] = {0};
__IO uint32_t uart_ota_irq_count = 0U;
__IO uint32_t uart_ota_overwrite_count = 0U;
__IO uint16_t uart_ota_last_irq_length = 0U;

/*
 * 宏作用：
 *   定义当前 OTA 协议使用的魔术字、帧类型和缓冲限制。
 * 说明：
 *   这些常量必须与上位机 `tools/make_uart_ota_packet.py` 保持一致。
 */
#define UART_OTA_STREAM_MAGIC          0xA55A5AA5UL
#define UART_OTA_FRAME_START           1U
#define UART_OTA_FRAME_DATA            2U
#define UART_OTA_FRAME_END             3U
#define UART_OTA_FRAME_ACK_BASE        0x80U
#define UART_OTA_START_FRAME_SIZE      24U
#define UART_OTA_DATA_HEADER_SIZE      24U
#define UART_OTA_END_FRAME_SIZE        16U
#define UART_OTA_ACK_FRAME_SIZE        20U
#define UART_OTA_STREAM_CHUNK_SIZE     512U

/*
 * 枚举作用：
 *   表示 OTA 帧处理结果。
 * 说明：
 *   任务层根据该结果决定是继续等待下一帧、回 ACK，还是触发软件复位。
 */
typedef enum
{
    UART_OTA_RESULT_NOT_PACKET = 0,
    UART_OTA_RESULT_SUCCESS,
    UART_OTA_RESULT_BAD_LENGTH,
    UART_OTA_RESULT_BAD_VECTOR,
    UART_OTA_RESULT_FLASH_ERROR,
    UART_OTA_RESULT_VERIFY_ERROR,
    UART_OTA_RESULT_WAIT_MORE,
    UART_OTA_RESULT_FRAME_CONSUMED
} uart_ota_result_t;

/*
 * 枚举作用：
 *   表示 OTA 会话当前阶段。
 * 说明：
 *   OTA 必须严格按 START -> DATA* -> END 顺序运行，防止乱序帧污染下载区。
 */
typedef enum
{
    UART_OTA_SESSION_IDLE = 0,
    UART_OTA_SESSION_RECEIVING
} uart_ota_session_state_t;

/*
 * 结构体作用：
 *   保存一次 OTA 会话的运行态。
 * 成员说明：
 *   state：当前是否处于 OTA 接收中。
 *   app_version：本次升级固件版本号。
 *   firmware_size：完整固件长度。
 *   firmware_crc32：完整固件目标 CRC32。
 *   received_size：当前已成功写入下载区的字节数。
 *   next_seq：下一帧期望 DATA 序号。
 *   running_crc：边收边算的未取反 CRC32 中间值。
 *   vector_checked：首包向量表是否已经完成校验。
 */
typedef struct
{
    uart_ota_session_state_t state;
    uint32_t app_version;
    uint32_t firmware_size;
    uint32_t firmware_crc32;
    uint32_t received_size;
    uint32_t next_seq;
    uint32_t running_crc;
    uint8_t vector_checked;
} uart_ota_session_t;

/* OTA 任务层私有快照缓冲，避免共享缓冲区在处理过程中被下一帧覆盖。 */
static uint8_t g_uart_ota_frame_buffer[UART_OTA_FRAME_BUFFER_SIZE] = {0};

/* 当前 OTA 会话状态，只在任务上下文更新，中断层不直接改协议状态。 */
static uart_ota_session_t g_uart_ota_session = {0};

/*
 * 变量作用：
 *   控制 OTA 接收摘要日志的条数，避免成功升级时把 USART0 日志口刷满。
 * 说明：
 *   每次系统初始化或唤醒后先保留前 3 帧的摘要，足够判断 USART2 是否收到了
 *   START/DATA 帧，以及收到的数据头是否符合当前协议。
 */
static uint8_t g_uart_ota_rx_trace_budget = 0U;

/*
 * 函数作用：
 *   从小端字节序缓冲区读取 32 位无符号整数。
 * 参数说明：
 *   data：指向至少 4 字节有效数据的缓冲区。
 * 返回值说明：
 *   返回解析出的 32 位整数；若 data 为空则返回 0。
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
 *   按小端格式向目标缓冲区写入 32 位无符号整数。
 * 参数说明：
 *   data：目标缓冲区，必须至少可写 4 字节。
 *   value：待写入的 32 位数值。
 * 返回值说明：
 *   无返回值。
 */
static void prv_uart_ota_write_u32_le(uint8_t *data, uint32_t value)
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
 *   判断一段短数据是否仍可能是 OTA 魔术字前缀。
 * 参数说明：
 *   data：当前已收到的前缀数据。
 *   length：当前前缀长度，只允许 1~4 字节。
 * 返回值说明：
 *   1：前缀仍可能属于合法 OTA 帧。
 *   0：当前数据不可能是 OTA 帧开头。
 */
static uint8_t prv_uart_ota_is_magic_prefix(const uint8_t *data, uint32_t length)
{
    uint8_t magic_bytes[4] = {
        (uint8_t)(UART_OTA_STREAM_MAGIC & 0xFFU),
        (uint8_t)((UART_OTA_STREAM_MAGIC >> 8U) & 0xFFU),
        (uint8_t)((UART_OTA_STREAM_MAGIC >> 16U) & 0xFFU),
        (uint8_t)((UART_OTA_STREAM_MAGIC >> 24U) & 0xFFU)
    };
    uint32_t index;

    if((NULL == data) || (0U == length) || (length > sizeof(magic_bytes))){
        return 0U;
    }

    for(index = 0U; index < length; index++){
        if(data[index] != magic_bytes[index]){
            return 0U;
        }
    }

    return 1U;
}

/*
 * 函数作用：
 *   发送 OTA ACK 帧到 USART2，让上位机按“发送一帧、等待 ACK”的节流方式继续。
 * 参数说明：
 *   frame_type：被应答的原始帧类型。
 *   status：处理结果，0 表示成功。
 *   value0：ACK 附带值 0。
 *   value1：ACK 附带值 1。
 * 返回值说明：
 *   无返回值。
 */
static void prv_uart_ota_send_ack(uint32_t frame_type,
                                  uint32_t status,
                                  uint32_t value0,
                                  uint32_t value1)
{
    uint8_t ack[UART_OTA_ACK_FRAME_SIZE];

    prv_uart_ota_write_u32_le(&ack[0], UART_OTA_STREAM_MAGIC);
    prv_uart_ota_write_u32_le(&ack[4], UART_OTA_FRAME_ACK_BASE | frame_type);
    prv_uart_ota_write_u32_le(&ack[8], status);
    prv_uart_ota_write_u32_le(&ack[12], value0);
    prv_uart_ota_write_u32_le(&ack[16], value1);
    (void)bsp_usart_send_buffer(UART_OTA_USART, ack, (uint16_t)sizeof(ack));
}

/*
 * 函数作用：
 *   向 USART2 OTA 专用口发送一次纯文本探测串，用于确认 TX 线和串口号是否正确。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void uart_ota_emit_startup_probe(void)
{
    static const uint8_t probe_message[] = "OTA2: ready\r\n";

    (void)bsp_usart_send_buffer(UART_OTA_USART,
                                probe_message,
                                (uint16_t)(sizeof(probe_message) - 1U));
}

/*
 * 函数作用：
 *   将 OTA 会话状态恢复到空闲态。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
static void prv_uart_ota_reset_session(void)
{
    memset(&g_uart_ota_session, 0, sizeof(g_uart_ota_session));
    g_uart_ota_session.state = UART_OTA_SESSION_IDLE;
    g_uart_ota_session.running_crc = 0xFFFFFFFFUL;
}

/*
 * 函数作用：
 *   从 OTA 共享缓冲区原子地取出一帧数据到任务层私有缓冲区。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   返回成功取出的有效字节数；0 表示当前没有新帧。
 */
static uint16_t prv_uart_ota_take_frame(void)
{
    uint16_t valid_length = 0U;

    __disable_irq();
    if(0U != uart_ota_rx_flag){
        valid_length = uart_ota_dma_length;
        if(valid_length > sizeof(g_uart_ota_frame_buffer)){
            valid_length = (uint16_t)sizeof(g_uart_ota_frame_buffer);
        }
        if(valid_length > 0U){
            memcpy(g_uart_ota_frame_buffer, uart_ota_dma_buffer, valid_length);
        }
        /* 复制和清标志放在同一临界区，避免任务层重复消费同一帧。 */
        uart_ota_dma_length = 0U;
        uart_ota_rx_flag = 0U;
    }
    __enable_irq();

    return valid_length;
}

/*
 * 函数作用：
 *   把当前收到的 USART2 帧摘要打印到 USART0 日志口，便于定位 OTA 首帧是否进链路。
 * 参数说明：
 *   packet：本次从共享缓冲区复制出来的一帧数据。
 *   packet_length：本次帧的有效字节数。
 * 返回值说明：
 *   无返回值。
 */
static void prv_uart_ota_log_frame_summary(const uint8_t *packet, uint16_t packet_length)
{
    uint32_t magic = 0U;
    uint32_t frame_type = 0U;

    if((NULL != packet) && (packet_length >= 4U)){
        magic = prv_uart_ota_read_u32_le(&packet[0]);
    }
    if((NULL != packet) && (packet_length >= 8U)){
        frame_type = prv_uart_ota_read_u32_le(&packet[4]);
    }

    my_printf(DEBUG_USART,
              "OTA: rx irq=%lu len=%u last_irq_len=%u magic=0x%08lx type=0x%08lx ovw=%lu\r\n",
              (unsigned long)uart_ota_irq_count,
              packet_length,
              uart_ota_last_irq_length,
              (unsigned long)magic,
              (unsigned long)frame_type,
              (unsigned long)uart_ota_overwrite_count);
}

/*
 * 函数作用：
 *   处理 OTA START 帧并初始化下载区和会话状态。
 * 参数说明：
 *   frame：START 帧起始地址。
 *   frame_length：当前帧长度。
 * 返回值说明：
 *   UART_OTA_RESULT_FRAME_CONSUMED：START 帧已成功处理。
 *   其它结果：长度、CRC 或下载区准备失败。
 */
static uart_ota_result_t prv_uart_ota_process_start(const uint8_t *frame, uint32_t frame_length)
{
    uint32_t app_version;
    uint32_t firmware_size;
    uint32_t firmware_crc32;
    uint32_t header_crc32;
    uint32_t calc_crc32;
    bootloader_port_status_t status;

    if(frame_length != UART_OTA_START_FRAME_SIZE){
        prv_uart_ota_send_ack(UART_OTA_FRAME_START, UART_OTA_RESULT_BAD_LENGTH, 0U, 0U);
        return UART_OTA_RESULT_BAD_LENGTH;
    }

    app_version = prv_uart_ota_read_u32_le(&frame[8]);
    firmware_size = prv_uart_ota_read_u32_le(&frame[12]);
    firmware_crc32 = prv_uart_ota_read_u32_le(&frame[16]);
    header_crc32 = prv_uart_ota_read_u32_le(&frame[20]);
    calc_crc32 = bootloader_port_crc32_calc(frame, 20U);
    if(calc_crc32 != header_crc32){
        prv_uart_ota_send_ack(UART_OTA_FRAME_START, UART_OTA_RESULT_VERIFY_ERROR, calc_crc32, header_crc32);
        return UART_OTA_RESULT_VERIFY_ERROR;
    }

    if((0U == firmware_size) || (firmware_size > BOOTLOADER_PORT_DOWNLOAD_MAX_SIZE)){
        my_printf(DEBUG_USART, "OTA: bad stream size %d\r\n", firmware_size);
        prv_uart_ota_send_ack(UART_OTA_FRAME_START,
                              UART_OTA_RESULT_BAD_LENGTH,
                              firmware_size,
                              BOOTLOADER_PORT_DOWNLOAD_MAX_SIZE);
        return UART_OTA_RESULT_BAD_LENGTH;
    }

    /*
     * 擦下载区期间先关 USART2 中断，避免上位机在上一帧还未完全处理完时继续灌入新数据。
     * 这里只保护 OTA 专用通道，不影响 USART0 的日志输出。
     */
    nvic_irq_disable(USART2_IRQn);
    status = bootloader_port_prepare_download_area(firmware_size);
    nvic_irq_enable(USART2_IRQn, 1U, 1U);
    if(BOOTLOADER_PORT_STATUS_OK != status){
        prv_uart_ota_send_ack(UART_OTA_FRAME_START, UART_OTA_RESULT_FLASH_ERROR, 0U, 0U);
        return UART_OTA_RESULT_FLASH_ERROR;
    }

    prv_uart_ota_reset_session();
    g_uart_ota_session.state = UART_OTA_SESSION_RECEIVING;
    g_uart_ota_session.app_version = app_version;
    g_uart_ota_session.firmware_size = firmware_size;
    g_uart_ota_session.firmware_crc32 = firmware_crc32;

    my_printf(DEBUG_USART,
              "OTA: stream start version=0x%08x size=%d crc=0x%08x\r\n",
              app_version,
              firmware_size,
              firmware_crc32);
    prv_uart_ota_send_ack(UART_OTA_FRAME_START, 0U, UART_OTA_STREAM_CHUNK_SIZE, firmware_size);

    return UART_OTA_RESULT_FRAME_CONSUMED;
}

/*
 * 函数作用：
 *   处理 OTA DATA 帧并把分包直接写入下载缓存区。
 * 参数说明：
 *   frame：DATA 帧起始地址。
 *   frame_length：当前帧总长度。
 * 返回值说明：
 *   UART_OTA_RESULT_FRAME_CONSUMED：当前分包写入成功。
 *   其它结果：序号、偏移、CRC、向量表或 Flash 写入失败。
 */
static uart_ota_result_t prv_uart_ota_process_data(const uint8_t *frame, uint32_t frame_length)
{
    uint32_t seq;
    uint32_t offset;
    uint32_t chunk_length;
    uint32_t chunk_crc32;
    uint32_t calc_crc32;
    uint32_t stack_addr = 0U;
    uint32_t entry_addr = 0U;
    const uint8_t *chunk;
    bootloader_port_status_t status;

    if((g_uart_ota_session.state != UART_OTA_SESSION_RECEIVING) ||
       (frame_length < UART_OTA_DATA_HEADER_SIZE)){
        prv_uart_ota_send_ack(UART_OTA_FRAME_DATA, UART_OTA_RESULT_BAD_LENGTH, 0U, g_uart_ota_session.received_size);
        return UART_OTA_RESULT_BAD_LENGTH;
    }

    seq = prv_uart_ota_read_u32_le(&frame[8]);
    offset = prv_uart_ota_read_u32_le(&frame[12]);
    chunk_length = prv_uart_ota_read_u32_le(&frame[16]);
    chunk_crc32 = prv_uart_ota_read_u32_le(&frame[20]);
    if((0U == chunk_length) ||
       (chunk_length > UART_OTA_STREAM_CHUNK_SIZE) ||
       (frame_length != (UART_OTA_DATA_HEADER_SIZE + chunk_length))){
        prv_uart_ota_send_ack(UART_OTA_FRAME_DATA, UART_OTA_RESULT_BAD_LENGTH, seq, g_uart_ota_session.received_size);
        return UART_OTA_RESULT_BAD_LENGTH;
    }

    if((seq != g_uart_ota_session.next_seq) ||
       (offset != g_uart_ota_session.received_size) ||
       ((offset + chunk_length) > g_uart_ota_session.firmware_size)){
        prv_uart_ota_send_ack(UART_OTA_FRAME_DATA, UART_OTA_RESULT_BAD_LENGTH, seq, g_uart_ota_session.received_size);
        return UART_OTA_RESULT_BAD_LENGTH;
    }

    chunk = &frame[UART_OTA_DATA_HEADER_SIZE];
    calc_crc32 = bootloader_port_crc32_calc(chunk, chunk_length);
    if(calc_crc32 != chunk_crc32){
        prv_uart_ota_send_ack(UART_OTA_FRAME_DATA, UART_OTA_RESULT_VERIFY_ERROR, seq, calc_crc32);
        return UART_OTA_RESULT_VERIFY_ERROR;
    }

    if(0U == g_uart_ota_session.vector_checked){
        status = bootloader_port_validate_firmware_vector(chunk,
                                                          chunk_length,
                                                          &stack_addr,
                                                          &entry_addr);
        if(BOOTLOADER_PORT_STATUS_OK != status){
            my_printf(DEBUG_USART,
                      "OTA: invalid vector stack=0x%08x entry=0x%08x\r\n",
                      stack_addr,
                      entry_addr);
            prv_uart_ota_send_ack(UART_OTA_FRAME_DATA, UART_OTA_RESULT_BAD_VECTOR, seq, offset);
            return UART_OTA_RESULT_BAD_VECTOR;
        }
        /*
         * 只在首包校验一次向量表即可，因为 App 镜像的开头就是向量表区域。
         * 首包合法后，后续 DATA 帧只需关注顺序、偏移和 CRC。
         */
        g_uart_ota_session.vector_checked = 1U;
    }

    nvic_irq_disable(USART2_IRQn);
    status = bootloader_port_write_download_chunk(offset, chunk, chunk_length);
    nvic_irq_enable(USART2_IRQn, 1U, 1U);
    if(BOOTLOADER_PORT_STATUS_OK != status){
        prv_uart_ota_send_ack(UART_OTA_FRAME_DATA, UART_OTA_RESULT_FLASH_ERROR, seq, offset);
        return UART_OTA_RESULT_FLASH_ERROR;
    }

    g_uart_ota_session.running_crc = bootloader_port_crc32_update(g_uart_ota_session.running_crc,
                                                                  chunk,
                                                                  chunk_length);
    g_uart_ota_session.received_size += chunk_length;
    g_uart_ota_session.next_seq++;
    prv_uart_ota_send_ack(UART_OTA_FRAME_DATA, 0U, seq, g_uart_ota_session.received_size);

    return UART_OTA_RESULT_FRAME_CONSUMED;
}

/*
 * 函数作用：
 *   处理 OTA END 帧，完成整包校验、参数区写入并准备复位。
 * 参数说明：
 *   frame：END 帧起始地址。
 *   frame_length：当前帧长度。
 * 返回值说明：
 *   UART_OTA_RESULT_SUCCESS：下载区和参数区都已准备完毕。
 *   其它结果：长度、CRC 或参数区写入失败。
 */
static uart_ota_result_t prv_uart_ota_process_end(const uint8_t *frame, uint32_t frame_length)
{
    uint32_t firmware_size;
    uint32_t firmware_crc32;
    uint32_t running_crc32;
    uint32_t flash_crc32;
    bootloader_port_status_t status;

    if((g_uart_ota_session.state != UART_OTA_SESSION_RECEIVING) ||
       (frame_length != UART_OTA_END_FRAME_SIZE)){
        prv_uart_ota_send_ack(UART_OTA_FRAME_END, UART_OTA_RESULT_BAD_LENGTH, 0U, g_uart_ota_session.received_size);
        return UART_OTA_RESULT_BAD_LENGTH;
    }

    firmware_size = prv_uart_ota_read_u32_le(&frame[8]);
    firmware_crc32 = prv_uart_ota_read_u32_le(&frame[12]);
    if((firmware_size != g_uart_ota_session.firmware_size) ||
       (firmware_crc32 != g_uart_ota_session.firmware_crc32) ||
       (g_uart_ota_session.received_size != g_uart_ota_session.firmware_size)){
        prv_uart_ota_send_ack(UART_OTA_FRAME_END, UART_OTA_RESULT_BAD_LENGTH, firmware_size, g_uart_ota_session.received_size);
        return UART_OTA_RESULT_BAD_LENGTH;
    }

    running_crc32 = g_uart_ota_session.running_crc ^ 0xFFFFFFFFUL;
    if(running_crc32 != g_uart_ota_session.firmware_crc32){
        prv_uart_ota_send_ack(UART_OTA_FRAME_END,
                              UART_OTA_RESULT_VERIFY_ERROR,
                              running_crc32,
                              g_uart_ota_session.firmware_crc32);
        return UART_OTA_RESULT_VERIFY_ERROR;
    }

    /*
     * 第二层校验直接从下载缓存区回读，确认“写入内部 Flash 的结果”与整包 CRC 一致。
     */
    flash_crc32 = bootloader_port_calc_download_crc32(g_uart_ota_session.firmware_size);
    if(flash_crc32 != g_uart_ota_session.firmware_crc32){
        prv_uart_ota_send_ack(UART_OTA_FRAME_END,
                              UART_OTA_RESULT_VERIFY_ERROR,
                              flash_crc32,
                              g_uart_ota_session.firmware_crc32);
        return UART_OTA_RESULT_VERIFY_ERROR;
    }

    nvic_irq_disable(USART2_IRQn);
    status = bootloader_port_write_upgrade_info(g_uart_ota_session.app_version,
                                                g_uart_ota_session.firmware_size,
                                                g_uart_ota_session.firmware_crc32);
    nvic_irq_enable(USART2_IRQn, 1U, 1U);
    if(BOOTLOADER_PORT_STATUS_OK != status){
        prv_uart_ota_send_ack(UART_OTA_FRAME_END, UART_OTA_RESULT_FLASH_ERROR, 0U, 0U);
        return UART_OTA_RESULT_FLASH_ERROR;
    }

    prv_uart_ota_send_ack(UART_OTA_FRAME_END,
                          0U,
                          g_uart_ota_session.firmware_size,
                          g_uart_ota_session.firmware_crc32);
    my_printf(DEBUG_USART, "OTA: ready, reset to BootLoader\r\n");

    return UART_OTA_RESULT_SUCCESS;
}

/*
 * 函数作用：
 *   尝试把一帧 USART2 数据按 OTA 协议解析并处理。
 * 参数说明：
 *   packet：USART2 本次 IDLE 中断移交的数据缓冲区。
 *   packet_length：本次移交的数据长度。
 * 返回值说明：
 *   返回当前帧处理结果，用于决定是否继续等待下一帧或触发复位。
 */
static uart_ota_result_t prv_uart_ota_try_process_packet(const uint8_t *packet, uint32_t packet_length)
{
    uint32_t magic;
    uint32_t frame_type;

    if(NULL == packet){
        return UART_OTA_RESULT_NOT_PACKET;
    }

    if(packet_length < 4U){
        if(0U != prv_uart_ota_is_magic_prefix(packet, packet_length)){
            return UART_OTA_RESULT_WAIT_MORE;
        }
        return UART_OTA_RESULT_NOT_PACKET;
    }

    magic = prv_uart_ota_read_u32_le(&packet[0]);
    if(UART_OTA_STREAM_MAGIC != magic){
        return UART_OTA_RESULT_NOT_PACKET;
    }

    if(packet_length < 8U){
        return UART_OTA_RESULT_WAIT_MORE;
    }

    frame_type = prv_uart_ota_read_u32_le(&packet[4]);
    if(UART_OTA_FRAME_START == frame_type){
        return prv_uart_ota_process_start(packet, packet_length);
    }
    if(UART_OTA_FRAME_DATA == frame_type){
        return prv_uart_ota_process_data(packet, packet_length);
    }
    if(UART_OTA_FRAME_END == frame_type){
        return prv_uart_ota_process_end(packet, packet_length);
    }

    return UART_OTA_RESULT_BAD_LENGTH;
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
    uart_ota_dma_length = 0U;
    uart_ota_irq_count = 0U;
    uart_ota_overwrite_count = 0U;
    uart_ota_last_irq_length = 0U;
    __enable_irq();
    memset(uart_ota_dma_buffer, 0, sizeof(uart_ota_dma_buffer));
    memset(g_uart_ota_frame_buffer, 0, sizeof(g_uart_ota_frame_buffer));
    g_uart_ota_rx_trace_budget = 3U;
    prv_uart_ota_reset_session();
}

/*
 * 函数作用：
 *   周期性处理 USART2 OTA 分包接收、ACK 回包和升级交接。
 * 主要流程：
 *   1. 取出 USART2 中断层移交的一帧数据。
 *   2. 先尝试按 OTA 协议解析；不是 OTA 帧则忽略，不污染普通串口链路。
 *   3. 成功接收 END 后延时发送稳定，再软件复位交给 BootLoader。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void uart_ota_task(void)
{
    uint16_t frame_length;
    uart_ota_result_t result;

    frame_length = prv_uart_ota_take_frame();
    if(0U == frame_length){
        return;
    }

    /*
     * 只打印前几帧的摘要，优先用于定位：
     * 1. USART2 是否真的收到了字节；
     * 2. 第一帧是否具备正确的 magic 和 frame type；
     * 3. 是否存在“任务层还没消费，ISR 又覆盖了上一帧”的风险。
     */
    if(0U != g_uart_ota_rx_trace_budget){
        prv_uart_ota_log_frame_summary(g_uart_ota_frame_buffer, frame_length);
        g_uart_ota_rx_trace_budget--;
    }

    result = prv_uart_ota_try_process_packet(g_uart_ota_frame_buffer, frame_length);
    if(UART_OTA_RESULT_SUCCESS == result){
        /*
         * ACK 和日志先发出去，再交给 BootLoader 复位接手。
         * 这里保留一个短延时窗口，避免上位机还没看到 END ACK 就立刻断链。
         */
        delay_ms(50);
        bootloader_port_request_upgrade_reset();
    }else if(UART_OTA_RESULT_FRAME_CONSUMED == result){
        /* 当前帧已经消费完毕，等待上位机发下一帧即可。 */
    }else if(UART_OTA_RESULT_NOT_PACKET == result){
        /* USART2 已被定义为 OTA 专用口，普通数据在这里直接忽略。 */
    }else if(UART_OTA_RESULT_WAIT_MORE == result){
        /* 理论上上位机会整帧发送；若收到短前缀，这里保守等待下一轮。 */
    }else{
        my_printf(DEBUG_USART,
                  "OTA: failed result=%d len=%u irq=%lu ovw=%lu\r\n",
                  result,
                  frame_length,
                  (unsigned long)uart_ota_irq_count,
                  (unsigned long)uart_ota_overwrite_count);
        prv_uart_ota_reset_session();
    }
}
