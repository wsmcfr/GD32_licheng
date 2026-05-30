#include "uart_ota_ymodem.h"

/*
 * 宏作用：
 *   定义 YModem 协议控制字节、包长度和等待时间。
 * 说明：
 *   纸飞机调试助手的 YModem 发送一般使用 128B 首包和 1K 数据包；
 *   接收端同时兼容 SOH(128B) 和 STX(1024B)，以适配不同串口工具。
 */
#define UART_OTA_YMODEM_SOH                  0x01U
#define UART_OTA_YMODEM_STX                  0x02U
#define UART_OTA_YMODEM_EOT                  0x04U
#define UART_OTA_YMODEM_ACK                  0x06U
#define UART_OTA_YMODEM_NAK                  0x15U
#define UART_OTA_YMODEM_CAN                  0x18U
#define UART_OTA_YMODEM_CRC_REQ              0x43U
#define UART_OTA_YMODEM_PACKET_128_SIZE      128U
#define UART_OTA_YMODEM_PACKET_1K_SIZE        1024U
#define UART_OTA_YMODEM_FRAME_OVERHEAD       5U
#define UART_OTA_YMODEM_FRAME_128_TOTAL_SIZE (UART_OTA_YMODEM_PACKET_128_SIZE + UART_OTA_YMODEM_FRAME_OVERHEAD)
#define UART_OTA_YMODEM_FRAME_1K_TOTAL_SIZE  (UART_OTA_YMODEM_PACKET_1K_SIZE + UART_OTA_YMODEM_FRAME_OVERHEAD)
#define UART_OTA_YMODEM_POLL_PERIOD_MS       1000U
#define UART_OTA_YMODEM_START_WINDOW_MS      30000U
#define UART_OTA_YMODEM_FINISH_WAIT_MS       1500U
#define UART_OTA_YMODEM_DEFAULT_VERSION      0x00000001UL

/*
 * 枚举作用：
 *   表示 YModem OTA 会话当前阶段。
 * 说明：
 *   接收过程必须严格按“等待首包 -> 接收数据 -> 等待结束空包/完成”推进，
 *   防止普通串口数据被误写入下载缓存区。
 */
typedef enum
{
    UART_OTA_YMODEM_STATE_IDLE = 0,
    UART_OTA_YMODEM_STATE_RECEIVING,
    UART_OTA_YMODEM_STATE_WAIT_END_EMPTY,
    UART_OTA_YMODEM_STATE_DONE
} uart_ota_ymodem_state_t;

/*
 * 结构体作用：
 *   保存一次 YModem OTA 会话的运行状态。
 * 成员说明：
 *   state：当前 YModem 会话阶段。
 *   firmware_size：从首包文件信息解析出的 Project.bin 长度。
 *   received_size：已经写入下载缓存区的固件字节数。
 *   running_crc：接收过程中累计的未取反 CRC32 中间值。
 *   expected_block：下一包期望的 YModem block 编号，首个数据包为 1。
 *   vector_checked：是否已经校验过固件首包向量表。
 *   eot_seen：是否已经收到过第一次 EOT，用于兼容标准 YModem 的双 EOT 结束流程。
 *   flash_verified：下载区回读 CRC 是否已经通过，避免超时完成时重复校验。
 *   poll_enabled：是否允许空闲态发送 'C'；只有收到显式启动命令后才置 1。
 *   final_crc32：完整固件 CRC32，写参数区和日志使用。
 *   last_poll_ms：上一次向上位机发送 'C' 请求的毫秒时间戳。
 *   poll_deadline_ms：显式启动命令打开的等待窗口到期时间。
 *   finish_deadline_ms：进入结束等待态后的超时点，兼容不发送最终空包的工具。
 */
typedef struct
{
    uart_ota_ymodem_state_t state;
    uint32_t firmware_size;
    uint32_t received_size;
    uint32_t running_crc;
    uint8_t expected_block;
    uint8_t vector_checked;
    uint8_t eot_seen;
    uint8_t flash_verified;
    uint8_t poll_enabled;
    uint32_t final_crc32;
    uint32_t last_poll_ms;
    uint32_t poll_deadline_ms;
    uint32_t finish_deadline_ms;
} uart_ota_ymodem_session_t;

/* YModem 会话状态只在调度任务上下文中更新，中断层不直接解析协议。 */
static uart_ota_ymodem_session_t g_uart_ota_ymodem_session = {0};

/*
 * 函数作用：
 *   通过 RS485/USART1 发送一个 YModem 控制字符。
 * 参数说明：
 *   code：待发送的 YModem 控制字节，例如 ACK、NAK 或 'C'。
 * 返回值说明：
 *   无返回值。
 */
static void prv_uart_ota_ymodem_send_code(uint8_t code)
{
    /*
     * RS485 半双工发送必须先切到发送态；bsp_usart_send_buffer()
     * 会等待 TC，确认最后一位移出后再由这里恢复接收态。
     */
    bsp_rs485_direction_transmit();
    (void)bsp_usart_send_buffer(UART_OTA_USART, &code, 1U);
    bsp_rs485_direction_receive();
}

/*
 * 函数作用：
 *   计算 YModem 数据区使用的 CRC16-CCITT。
 * 参数说明：
 *   data：参与 CRC16 计算的缓冲区。
 *   length：参与计算的字节数。
 * 返回值说明：
 *   返回 YModem 协议要求的 CRC16 值；参数非法时返回 0。
 */
static uint16_t prv_uart_ota_ymodem_crc16(const uint8_t *data, uint32_t length)
{
    uint16_t crc = 0U;
    uint32_t index;
    uint8_t bit_index;

    if((NULL == data) && (length > 0U)){
        return 0U;
    }

    for(index = 0U; index < length; index++){
        crc ^= (uint16_t)data[index] << 8U;
        for(bit_index = 0U; bit_index < 8U; bit_index++){
            if(0U != (crc & 0x8000U)){
                crc = (uint16_t)((crc << 1U) ^ 0x1021U);
            }else{
                crc = (uint16_t)(crc << 1U);
            }
        }
    }

    return crc;
}

/*
 * 函数作用：
 *   判断给定长度的 YModem 数据区是否为空包。
 * 参数说明：
 *   data：YModem 数据区起始地址。
 *   length：数据区长度，通常是 128 或 1024。
 * 返回值说明：
 *   1：整包全为 0，表示 YModem 首包取消或结束空包。
 *   0：数据区至少包含一个非 0 字节。
 */
static uint8_t prv_uart_ota_ymodem_is_empty_payload(const uint8_t *data, uint32_t length)
{
    uint32_t index;

    if(NULL == data){
        return 0U;
    }

    for(index = 0U; index < length; index++){
        if(0U != data[index]){
            return 0U;
        }
    }

    return 1U;
}

/*
 * 函数作用：
 *   从 YModem 首包的文件名字符串后解析十进制文件大小。
 * 参数说明：
 *   payload：首包 128/1024 字节数据区。
 *   payload_length：数据区长度。
 *   firmware_size：输出解析出的固件大小，单位为字节。
 * 返回值说明：
 *   1：成功解析出非 0 文件大小。
 *   0：首包格式不完整或大小非法。
 */
static uint8_t prv_uart_ota_ymodem_parse_file_size(const uint8_t *payload,
                                                   uint32_t payload_length,
                                                   uint32_t *firmware_size)
{
    uint32_t index = 0U;
    uint32_t size = 0U;
    uint8_t has_digit = 0U;

    if((NULL == payload) || (NULL == firmware_size) || (0U == payload_length)){
        return 0U;
    }

    /* 首包第一个字符串是文件名，空文件名表示传输结束空包。 */
    while((index < payload_length) && (0U != payload[index])){
        index++;
    }
    if((0U == index) || ((index + 1U) >= payload_length)){
        return 0U;
    }

    index++;
    while((index < payload_length) && (' ' == payload[index])){
        index++;
    }

    while((index < payload_length) &&
          (payload[index] >= (uint8_t)'0') &&
          (payload[index] <= (uint8_t)'9')){
        has_digit = 1U;
        size = (size * 10U) + (uint32_t)(payload[index] - (uint8_t)'0');
        index++;
    }

    if((0U == has_digit) || (0U == size)){
        return 0U;
    }

    *firmware_size = size;
    return 1U;
}

/*
 * 函数作用：
 *   将 YModem 会话状态恢复到初始空闲态。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void uart_ota_ymodem_reset_runtime(void)
{
    memset(&g_uart_ota_ymodem_session, 0, sizeof(g_uart_ota_ymodem_session));
    g_uart_ota_ymodem_session.state = UART_OTA_YMODEM_STATE_IDLE;
    g_uart_ota_ymodem_session.running_crc = 0xFFFFFFFFUL;
    g_uart_ota_ymodem_session.expected_block = 1U;
}

/*
 * 函数作用：
 *   响应上位机文本命令，打开 YModem 接收请求窗口。
 * 主要流程：
 *   1. 当前处于空闲态时才重新初始化 YModem 状态，避免升级过程中误复位接收进度。
 *   2. 设置 poll_enabled，并把 last_poll_ms 调整到“已超时”状态，让下一轮立即发送 'C'。
 *   3. 设置窗口截止时间，防止用户未真正选择文件时设备长期占用 RS485 发送。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void uart_ota_ymodem_request_start(void)
{
    uint32_t now_ms;

    if(UART_OTA_YMODEM_STATE_IDLE != g_uart_ota_ymodem_session.state){
        return;
    }

    now_ms = timebase_get_ms32();
    uart_ota_ymodem_reset_runtime();
    g_uart_ota_ymodem_session.poll_enabled = 1U;
    g_uart_ota_ymodem_session.last_poll_ms = now_ms - UART_OTA_YMODEM_POLL_PERIOD_MS;
    g_uart_ota_ymodem_session.poll_deadline_ms = now_ms + UART_OTA_YMODEM_START_WINDOW_MS;
}

/*
 * 函数作用：
 *   进入 YModem 数据接收态，并准备内部 Flash 下载缓存区。
 * 参数说明：
 *   payload：YModem 首包数据区，用于解析文件名和大小。
 *   payload_length：首包数据区长度。
 * 返回值说明：
 *   UART_OTA_YMODEM_RESULT_FRAME_CONSUMED：首包处理成功。
 *   其它结果：首包格式、固件大小或 Flash 擦除失败。
 */
static uint8_t prv_uart_ota_ymodem_process_header(const uint8_t *payload, uint32_t payload_length)
{
    uint32_t firmware_size = 0U;
    bootloader_port_status_t status;

    if(0U != prv_uart_ota_ymodem_is_empty_payload(payload, payload_length)){
        prv_uart_ota_ymodem_send_code(UART_OTA_YMODEM_ACK);
        uart_ota_ymodem_reset_runtime();
        return UART_OTA_YMODEM_RESULT_FRAME_CONSUMED;
    }

    if(0U == prv_uart_ota_ymodem_parse_file_size(payload, payload_length, &firmware_size)){
        prv_uart_ota_ymodem_send_code(UART_OTA_YMODEM_CAN);
        return UART_OTA_YMODEM_RESULT_BAD_LENGTH;
    }

    if((0U == firmware_size) || (firmware_size > BOOTLOADER_PORT_DOWNLOAD_MAX_SIZE)){
        my_printf(DEBUG_USART,
                  "YMODEM: bad size=%lu max=%lu\r\n",
                  (unsigned long)firmware_size,
                  (unsigned long)BOOTLOADER_PORT_DOWNLOAD_MAX_SIZE);
        prv_uart_ota_ymodem_send_code(UART_OTA_YMODEM_CAN);
        return UART_OTA_YMODEM_RESULT_BAD_LENGTH;
    }

    /*
     * 首包确认固件大小后立即擦除下载缓存区。擦除期间关闭 USART1 中断，
     * 防止上位机在缓存区还没准备好时继续发送第一包数据。
     */
    nvic_irq_disable(USART1_IRQn);
    status = bootloader_port_prepare_download_area(firmware_size);
    nvic_irq_enable(USART1_IRQn, 1U, 0U);
    if(BOOTLOADER_PORT_STATUS_OK != status){
        prv_uart_ota_ymodem_send_code(UART_OTA_YMODEM_CAN);
        return UART_OTA_YMODEM_RESULT_FLASH_ERROR;
    }

    uart_ota_ymodem_reset_runtime();
    g_uart_ota_ymodem_session.state = UART_OTA_YMODEM_STATE_RECEIVING;
    g_uart_ota_ymodem_session.firmware_size = firmware_size;
    my_printf(DEBUG_USART, "YMODEM: start size=%lu\r\n", (unsigned long)firmware_size);

    /*
     * 标准 YModem 首包处理完成后，先 ACK 首包，再发送 'C' 要求发送端开始数据包。
     */
    prv_uart_ota_ymodem_send_code(UART_OTA_YMODEM_ACK);
    prv_uart_ota_ymodem_send_code(UART_OTA_YMODEM_CRC_REQ);
    return UART_OTA_YMODEM_RESULT_FRAME_CONSUMED;
}

/*
 * 函数作用：
 *   验证 YModem 数据包序号是否符合当前接收进度。
 * 参数说明：
 *   block_number：当前包序号。
 *   block_inverse：当前包序号反码。
 * 返回值说明：
 *   1：序号和反码合法且符合期望。
 *   0：序号非法、重复或乱序。
 */
static uint8_t prv_uart_ota_ymodem_check_block_number(uint8_t block_number, uint8_t block_inverse)
{
    if((uint8_t)(block_number + block_inverse) != 0xFFU){
        return 0U;
    }

    if(block_number != g_uart_ota_ymodem_session.expected_block){
        return 0U;
    }

    return 1U;
}

/*
 * 函数作用：
 *   判断当前数据包是否是上一个数据包的重发。
 * 参数说明：
 *   block_number：当前包序号。
 *   block_inverse：当前包序号反码。
 * 返回值说明：
 *   1：当前包是上一包重发，通常表示上一次 ACK 丢失，应重新 ACK 但不重复写 Flash。
 *   0：当前包不是上一包重发。
 */
static uint8_t prv_uart_ota_ymodem_is_duplicate_block(uint8_t block_number, uint8_t block_inverse)
{
    uint8_t previous_block;

    if((uint8_t)(block_number + block_inverse) != 0xFFU){
        return 0U;
    }

    previous_block = (uint8_t)(g_uart_ota_ymodem_session.expected_block - 1U);
    if((g_uart_ota_ymodem_session.state == UART_OTA_YMODEM_STATE_RECEIVING) &&
       (block_number == previous_block)){
        return 1U;
    }

    return 0U;
}

/*
 * 函数作用：
 *   处理一个 YModem 数据包，并把有效固件字节写入下载缓存区。
 * 参数说明：
 *   payload：YModem 数据区起始地址。
 *   payload_length：数据区长度，可能大于本次剩余固件长度。
 * 返回值说明：
 *   UART_OTA_YMODEM_RESULT_FRAME_CONSUMED：数据包写入成功。
 *   其它结果：状态、向量表或 Flash 写入失败。
 */
static uint8_t prv_uart_ota_ymodem_process_data(const uint8_t *payload, uint32_t payload_length)
{
    uint32_t remaining;
    uint32_t write_length;
    uint32_t stack_addr = 0U;
    uint32_t entry_addr = 0U;
    bootloader_port_status_t status;

    if((g_uart_ota_ymodem_session.state != UART_OTA_YMODEM_STATE_RECEIVING) ||
       (NULL == payload) ||
       (g_uart_ota_ymodem_session.received_size >= g_uart_ota_ymodem_session.firmware_size)){
        prv_uart_ota_ymodem_send_code(UART_OTA_YMODEM_CAN);
        return UART_OTA_YMODEM_RESULT_BAD_LENGTH;
    }

    remaining = g_uart_ota_ymodem_session.firmware_size - g_uart_ota_ymodem_session.received_size;
    write_length = payload_length;
    if(write_length > remaining){
        /*
         * YModem 最后一包会用 0x1A 或 0x00 填充到 128/1024 字节，
         * 这里只写真实固件长度，填充区不参与下载缓存区 CRC。
         */
        write_length = remaining;
    }

    if(0U == g_uart_ota_ymodem_session.vector_checked){
        status = bootloader_port_validate_firmware_vector(payload,
                                                          write_length,
                                                          &stack_addr,
                                                          &entry_addr);
        if(BOOTLOADER_PORT_STATUS_OK != status){
            my_printf(DEBUG_USART,
                      "YMODEM: invalid vector stack=0x%08lx entry=0x%08lx\r\n",
                      (unsigned long)stack_addr,
                      (unsigned long)entry_addr);
            prv_uart_ota_ymodem_send_code(UART_OTA_YMODEM_CAN);
            return UART_OTA_YMODEM_RESULT_BAD_VECTOR;
        }
        g_uart_ota_ymodem_session.vector_checked = 1U;
    }

    nvic_irq_disable(USART1_IRQn);
    status = bootloader_port_write_download_chunk(g_uart_ota_ymodem_session.received_size,
                                                  payload,
                                                  write_length);
    nvic_irq_enable(USART1_IRQn, 1U, 0U);
    if(BOOTLOADER_PORT_STATUS_OK != status){
        prv_uart_ota_ymodem_send_code(UART_OTA_YMODEM_CAN);
        return UART_OTA_YMODEM_RESULT_FLASH_ERROR;
    }

    g_uart_ota_ymodem_session.running_crc =
        bootloader_port_crc32_update(g_uart_ota_ymodem_session.running_crc, payload, write_length);
    g_uart_ota_ymodem_session.received_size += write_length;
    g_uart_ota_ymodem_session.expected_block++;
    prv_uart_ota_ymodem_send_code(UART_OTA_YMODEM_ACK);

    return UART_OTA_YMODEM_RESULT_FRAME_CONSUMED;
}

/*
 * 函数作用：
 *   校验下载缓存区并写入 BootLoader 参数区。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   UART_OTA_YMODEM_RESULT_SUCCESS：参数区写入成功，外层可以复位。
 *   其它结果：接收长度、CRC 或参数区写入失败。
 */
static uint8_t prv_uart_ota_ymodem_finalize(void)
{
    uint32_t flash_crc32;
    bootloader_port_status_t status;

    if(g_uart_ota_ymodem_session.received_size != g_uart_ota_ymodem_session.firmware_size){
        prv_uart_ota_ymodem_send_code(UART_OTA_YMODEM_CAN);
        return UART_OTA_YMODEM_RESULT_BAD_LENGTH;
    }

    g_uart_ota_ymodem_session.final_crc32 =
        g_uart_ota_ymodem_session.running_crc ^ 0xFFFFFFFFUL;
    flash_crc32 = bootloader_port_calc_download_crc32(g_uart_ota_ymodem_session.firmware_size);
    if(flash_crc32 != g_uart_ota_ymodem_session.final_crc32){
        my_printf(DEBUG_USART,
                  "YMODEM: flash crc fail calc=0x%08lx expect=0x%08lx\r\n",
                  (unsigned long)flash_crc32,
                  (unsigned long)g_uart_ota_ymodem_session.final_crc32);
        prv_uart_ota_ymodem_send_code(UART_OTA_YMODEM_CAN);
        return UART_OTA_YMODEM_RESULT_VERIFY_ERROR;
    }

    nvic_irq_disable(USART1_IRQn);
    status = bootloader_port_write_upgrade_info(UART_OTA_YMODEM_DEFAULT_VERSION,
                                                g_uart_ota_ymodem_session.firmware_size,
                                                g_uart_ota_ymodem_session.final_crc32);
    nvic_irq_enable(USART1_IRQn, 1U, 0U);
    if(BOOTLOADER_PORT_STATUS_OK != status){
        prv_uart_ota_ymodem_send_code(UART_OTA_YMODEM_CAN);
        return UART_OTA_YMODEM_RESULT_FLASH_ERROR;
    }

    g_uart_ota_ymodem_session.flash_verified = 1U;
    g_uart_ota_ymodem_session.state = UART_OTA_YMODEM_STATE_DONE;
    my_printf(DEBUG_USART,
              "YMODEM: ready size=%lu crc=0x%08lx\r\n",
              (unsigned long)g_uart_ota_ymodem_session.firmware_size,
              (unsigned long)g_uart_ota_ymodem_session.final_crc32);
    return UART_OTA_YMODEM_RESULT_SUCCESS;
}

/*
 * 函数作用：
 *   周期性维护 YModem 接收状态。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   UART_OTA_YMODEM_RESULT_SUCCESS：结束等待超时后固件已可交给 BootLoader。
 *   UART_OTA_YMODEM_RESULT_FRAME_CONSUMED：无需要外层处理的维护动作。
 */
uint8_t uart_ota_ymodem_send_poll(void)
{
    uint32_t now_ms = timebase_get_ms32();

    if(UART_OTA_YMODEM_STATE_IDLE == g_uart_ota_ymodem_session.state){
        if(0U == g_uart_ota_ymodem_session.poll_enabled){
            return UART_OTA_YMODEM_RESULT_FRAME_CONSUMED;
        }

        if((uint32_t)(now_ms - g_uart_ota_ymodem_session.poll_deadline_ms) <
           0x80000000UL){
            /*
             * YModem 启动命令只打开一个有限等待窗口。
             * 如果用户没有继续选择文件，超时后回到静默态，避免不升级时持续刷 C。
             */
            g_uart_ota_ymodem_session.poll_enabled = 0U;
            return UART_OTA_YMODEM_RESULT_FRAME_CONSUMED;
        }

        if((uint32_t)(now_ms - g_uart_ota_ymodem_session.last_poll_ms) >=
           UART_OTA_YMODEM_POLL_PERIOD_MS){
            g_uart_ota_ymodem_session.last_poll_ms = now_ms;
            prv_uart_ota_ymodem_send_code(UART_OTA_YMODEM_CRC_REQ);
        }
    }else if((UART_OTA_YMODEM_STATE_WAIT_END_EMPTY == g_uart_ota_ymodem_session.state) ||
             ((UART_OTA_YMODEM_STATE_RECEIVING == g_uart_ota_ymodem_session.state) &&
              (0U != g_uart_ota_ymodem_session.eot_seen))){
        if((uint32_t)(now_ms - g_uart_ota_ymodem_session.finish_deadline_ms) <
           0x80000000UL){
            /*
             * 标准 YModem 在 EOT 后还会发送一个空首包作为会话结束确认。
             * 有些串口工具在 ACK 后就停止，这里用短超时兼容这类工具，
             * 避免固件已经完整写入却迟迟不复位。
             */
            return prv_uart_ota_ymodem_finalize();
        }
    }else{
        /* 其它状态由收到的数据包推进，周期维护不做额外动作。 */
    }

    return UART_OTA_YMODEM_RESULT_FRAME_CONSUMED;
}

/*
 * 函数作用：
 *   尝试解析一个完整 YModem 数据帧。
 * 参数说明：
 *   packet：USART1 IDLE 中断移交的一帧原始数据。
 *   packet_length：packet 的有效字节数。
 * 返回值说明：
 *   返回当前 YModem 帧处理结果。
 */
uint8_t uart_ota_ymodem_try_process_packet(const uint8_t *packet, uint32_t packet_length)
{
    uint8_t header;
    uint8_t block_number;
    uint8_t block_inverse;
    uint32_t payload_length;
    uint32_t expected_length;
    uint16_t packet_crc;
    uint16_t calc_crc;
    const uint8_t *payload;

    if((NULL == packet) || (0U == packet_length)){
        return UART_OTA_YMODEM_RESULT_NOT_PACKET;
    }

    header = packet[0];
    if(UART_OTA_YMODEM_EOT == header){
        if(UART_OTA_YMODEM_STATE_RECEIVING != g_uart_ota_ymodem_session.state){
            return UART_OTA_YMODEM_RESULT_NOT_PACKET;
        }

        /*
         * 标准 YModem 使用两次 EOT 结束文件：第一次 EOT 接收端回 NAK，
         * 发送端随后再发一次 EOT，接收端才 ACK 并请求最终空包。
         * 这样可以兼容纸飞机调试助手以及常见终端工具的标准发送流程。
         */
        if(0U == g_uart_ota_ymodem_session.eot_seen){
            g_uart_ota_ymodem_session.eot_seen = 1U;
            g_uart_ota_ymodem_session.finish_deadline_ms =
                timebase_get_ms32() + UART_OTA_YMODEM_FINISH_WAIT_MS;
            prv_uart_ota_ymodem_send_code(UART_OTA_YMODEM_NAK);
            return UART_OTA_YMODEM_RESULT_FRAME_CONSUMED;
        }

        prv_uart_ota_ymodem_send_code(UART_OTA_YMODEM_ACK);
        prv_uart_ota_ymodem_send_code(UART_OTA_YMODEM_CRC_REQ);
        g_uart_ota_ymodem_session.state = UART_OTA_YMODEM_STATE_WAIT_END_EMPTY;
        g_uart_ota_ymodem_session.finish_deadline_ms =
            timebase_get_ms32() + UART_OTA_YMODEM_FINISH_WAIT_MS;
        return UART_OTA_YMODEM_RESULT_FRAME_CONSUMED;
    }

    if((UART_OTA_YMODEM_SOH != header) && (UART_OTA_YMODEM_STX != header)){
        return UART_OTA_YMODEM_RESULT_NOT_PACKET;
    }

    payload_length = (UART_OTA_YMODEM_SOH == header) ?
                     UART_OTA_YMODEM_PACKET_128_SIZE :
                     UART_OTA_YMODEM_PACKET_1K_SIZE;
    expected_length = payload_length + UART_OTA_YMODEM_FRAME_OVERHEAD;
    if(packet_length != expected_length){
        prv_uart_ota_ymodem_send_code(UART_OTA_YMODEM_NAK);
        return UART_OTA_YMODEM_RESULT_BAD_LENGTH;
    }

    block_number = packet[1];
    block_inverse = packet[2];
    payload = &packet[3];
    packet_crc = ((uint16_t)packet[expected_length - 2U] << 8U) |
                 (uint16_t)packet[expected_length - 1U];
    calc_crc = prv_uart_ota_ymodem_crc16(payload, payload_length);
    if(calc_crc != packet_crc){
        prv_uart_ota_ymodem_send_code(UART_OTA_YMODEM_NAK);
        return UART_OTA_YMODEM_RESULT_VERIFY_ERROR;
    }

    if((0U == block_number) &&
       (g_uart_ota_ymodem_session.state != UART_OTA_YMODEM_STATE_RECEIVING)){
        if((uint8_t)(block_number + block_inverse) != 0xFFU){
            prv_uart_ota_ymodem_send_code(UART_OTA_YMODEM_NAK);
            return UART_OTA_YMODEM_RESULT_VERIFY_ERROR;
        }
        if(UART_OTA_YMODEM_STATE_WAIT_END_EMPTY == g_uart_ota_ymodem_session.state){
            prv_uart_ota_ymodem_send_code(UART_OTA_YMODEM_ACK);
            return prv_uart_ota_ymodem_finalize();
        }

        if(UART_OTA_YMODEM_STATE_IDLE != g_uart_ota_ymodem_session.state){
            prv_uart_ota_ymodem_send_code(UART_OTA_YMODEM_NAK);
            return UART_OTA_YMODEM_RESULT_BAD_LENGTH;
        }

        return prv_uart_ota_ymodem_process_header(payload, payload_length);
    }

    if(0U == prv_uart_ota_ymodem_check_block_number(block_number, block_inverse)){
        if(0U != prv_uart_ota_ymodem_is_duplicate_block(block_number, block_inverse)){
            /*
             * 上一包已经写入 Flash，但 ACK 可能在半双工链路上丢失。
             * 对重复包只重发 ACK，不重复写 Flash，避免破坏下载缓存区进度。
             */
            prv_uart_ota_ymodem_send_code(UART_OTA_YMODEM_ACK);
            return UART_OTA_YMODEM_RESULT_FRAME_CONSUMED;
        }
        prv_uart_ota_ymodem_send_code(UART_OTA_YMODEM_NAK);
        return UART_OTA_YMODEM_RESULT_BAD_LENGTH;
    }

    return prv_uart_ota_ymodem_process_data(payload, payload_length);
}
