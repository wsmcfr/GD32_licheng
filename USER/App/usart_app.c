#include "usart_app.h"

__IO uint8_t rx_flag = 0;
__IO uint16_t uart_dma_length = 0;
uint8_t uart_dma_buffer[UART_APP_DMA_BUFFER_SIZE] = {0};
__IO uint8_t rs485_rx_flag = 0;
__IO uint16_t rs485_dma_length = 0;
uint8_t rs485_dma_buffer[UART_APP_RS485_BUFFER_SIZE] = {0};
/*
 * 变量作用：
 *   任务层专用快照缓冲区。
 * 说明：
 *   先从 ISR 共享缓冲区复制，再执行阻塞发送，避免发送过程中共享缓冲区被下一次中断改写。
 */
static uint8_t uart_forward_buffer[UART_APP_RS485_BUFFER_SIZE] = {0};
static uint8_t rs485_forward_buffer[UART_APP_RS485_BUFFER_SIZE] = {0};

/*
 * 宏作用：
 *   定义 USART 轮询等待超时计数上限。
 * 说明：
 *   阻塞发送必须有退出条件，否则目标串口异常时会卡死周期任务和主循环。
 */
#define UART_APP_TX_WAIT_TIMEOUT       1000000UL

/*
 * 宏作用：
 *   定义 my_printf() 的格式化缓冲区长度。
 * 说明：
 *   USART0 升级接收缓冲已经扩大到 52KB 级别，但日志格式化不应占用同等
 *   栈空间，因此这里单独限制调试日志单行长度。
 */
#define UART_APP_PRINTF_BUFFER_SIZE    512U

/*
 * 宏作用：
 *   定义 App 侧串口升级协议的固定魔术字。
 * 说明：
 *   上位机发送的升级文件必须以小端 0x5AA5C33C 开头，避免普通串口转发数据
 *   被误当成固件写入片内 Flash。
 */
#define UART_OTA_MAGIC                 0x5AA5C33CUL

/*
 * 宏作用：
 *   定义升级文件包头长度。
 * 说明：
 *   包头布局为 magic(4B，小端) + appVersion(4B，小端) +
 *   firmwareSize(4B，小端) + firmwareCRC32(4B，小端)，后续全部字节为
 *   Project.bin 原始内容。
 */
#define UART_OTA_HEADER_SIZE           16U

/* BootLoader 固定参数区地址，App 写入升级参数后复位交给 BootLoader 搬运。 */
#define UART_OTA_PARAM_ADDR            0x0800C000UL

/* BootLoader 下载缓存区地址，App 先把新固件写入这里。 */
#define UART_OTA_DOWNLOAD_ADDR         0x08073000UL

/* 下载缓存区容量必须与 BootLoader 侧 APP_DOWNLOAD_MAX_SIZE 保持一致。 */
#define UART_OTA_DOWNLOAD_MAX_SIZE     (52U * 1024U)

/* 当前 App 的运行地址，BootLoader 搬运完成后从这里跳转。 */
#define UART_OTA_APP_START_ADDR        BOOT_APP_START_ADDRESS

/* BootLoader 参数区固定占用 4KB，擦写时按整页处理以保留结构布局一致。 */
#define UART_OTA_PARAM_SIZE            (4U * 1024U)

/* GD32F4xx 当前 BootLoader/App 约定按 4KB 页擦除下载缓存区和参数区。 */
#define UART_OTA_FLASH_PAGE_SIZE       4096U

/*
 * 枚举作用：
 *   表示串口升级包处理结果。
 * 说明：
 *   任务层根据返回值决定是继续普通 RS485 转发、打印错误，还是复位交给
 *   BootLoader 完成固件搬运。
 */
typedef enum
{
    UART_OTA_RESULT_NOT_PACKET = 0,
    UART_OTA_RESULT_SUCCESS,
    UART_OTA_RESULT_BAD_LENGTH,
    UART_OTA_RESULT_BAD_VECTOR,
    UART_OTA_RESULT_FLASH_ERROR,
    UART_OTA_RESULT_VERIFY_ERROR,
    UART_OTA_RESULT_WAIT_MORE
} uart_ota_result_t;

/*
 * 结构体作用：
 *   描述 BootLoader 主参数区的 256 字节布局。
 * 说明：
 *   该布局必须与 BootLoader_Two_Stage 的 BootParam_t 保持二进制兼容；
 *   App 侧只主动写升级控制字段和 App 固件字段，其它字段保留原值或默认值。
 */
typedef struct __attribute__((packed))
{
    uint32_t magicWord;
    uint16_t version;
    uint16_t structSize;
    uint32_t buildDate;
    uint32_t reserved0;
    uint8_t updateFlag;
    uint8_t updateMode;
    uint8_t updateStatus;
    uint8_t updateProgress;
    uint32_t updateCount;
    uint32_t lastUpdateTime;
    uint32_t reserved1;
    uint32_t appSize;
    uint32_t appCRC32;
    uint32_t appVersion;
    uint32_t appBuildDate;
    uint32_t appStartAddr;
    uint32_t appEntryAddr;
    uint32_t appStackAddr;
    uint32_t reserved2;
    uint32_t bootVersion;
    uint32_t bootCRC32;
    uint32_t bootSize;
    uint32_t reserved3;
    uint32_t runTimestamp;
    uint32_t resetCount;
    uint16_t lastResetReason;
    uint16_t bootFailCount;
    uint32_t totalRuntime;
    uint32_t wdtResetCount;
    uint32_t hardFaultCount;
    uint32_t lastErrorCode;
    uint32_t reserved4;
    uint32_t backupFlag;
    uint32_t backupAddr;
    uint32_t backupSize;
    uint32_t backupCRC32;
    uint32_t backupVersion;
    uint32_t backupDate;
    uint32_t reserved5[2];
    uint32_t securityFlag;
    uint32_t encryptKey;
    uint32_t authCode;
    uint32_t reserved6;
    uint8_t deviceID[16];
    uint8_t productModel[16];
    uint8_t serialNumber[16];
    uint32_t hwVersion;
    uint32_t cpuID;
    uint16_t flashSize;
    uint16_t ramSize;
    uint32_t clockFreq;
    uint32_t reserved7[4];
    uint32_t reserved8[2];
    uint32_t paramCRC32;
    uint32_t tailMagic;
} uart_ota_boot_param_t;

/*
 * 结构体作用：
 *   描述 BootLoader 参数区 4KB 内当前 App 需要读写的整体布局。
 * 成员说明：
 *   boot_param：BootLoader 主参数区，包含升级标志、固件大小和 CRC。
 *   boot_param_reserved：主参数备份，保持官方 Two Stage 示例的布局兼容。
 *   update_log：升级日志区，当前只保留原有内容，不在本流程里追加日志。
 *   user_config：用户配置区，保留原有参数。
 *   calib_data：校准数据区，保留原有参数。
 */
typedef struct __attribute__((packed))
{
    uart_ota_boot_param_t boot_param;
    uart_ota_boot_param_t boot_param_reserved;
    uint8_t update_log[1024];
    uint8_t user_config[512];
    uint8_t calib_data[512];
    uint8_t reserved_tail[UART_OTA_PARAM_SIZE -
                          (sizeof(uart_ota_boot_param_t) * 2U) -
                          1024U -
                          512U -
                          512U];
} uart_ota_parameter_t;

static uint8_t uart_ota_param_buffer[UART_OTA_PARAM_SIZE] = {0};

/*
 * 函数作用：
 *   等待指定 USART 标志变为 SET，并在异常情况下超时返回。
 * 主要流程：
 *   1. 循环读取目标 USART 的指定标志位。
 *   2. 标志置位则返回 1。
 *   3. 计数耗尽仍未置位则返回 0，避免串口异常时卡死主循环。
 * 参数说明：
 *   usart_periph：目标 USART 外设编号，例如 USART0 或 USART1。
 *   flag：需要等待变为 SET 的 USART 状态标志，例如 USART_FLAG_TBE 或 USART_FLAG_TC。
 * 返回值说明：
 *   1：表示目标标志在超时前变为 SET。
 *   0：表示等待超时，调用者应停止本次发送或返回已发送长度。
 */
static uint8_t prv_uart_wait_flag_set(uint32_t usart_periph, usart_flag_enum flag)
{
    uint32_t timeout = UART_APP_TX_WAIT_TIMEOUT;

    while(RESET == usart_flag_get(usart_periph, flag)){
        if(0U == timeout){
            return 0U;
        }
        /* 这里使用简单递减计数，避免在串口异常时永久停留在轮询循环。 */
        timeout--;
    }

    return 1U;
}

/*
 * 函数作用：
 *   通过阻塞轮询方式向指定串口发送一段原始字节流。
 * 主要流程：
 *   1. 检查数据指针和长度，空数据直接返回 0。
 *   2. 逐字节等待 TBE 后写入 USART_DATA。
 *   3. 发送完所有字节后等待 TC，确认最后 1 字节已经完全移出移位寄存器。
 * 参数说明：
 *   usart_periph：目标 USART 外设编号。
 *   data：待发送数据起始地址，必须指向至少 length 字节的有效缓冲区。
 *   length：待发送字节数，单位为字节。
 * 返回值说明：
 *   返回实际完成发送流程的字节数。
 *   若等待 TBE 超时，返回超时前已经写入 USART 的字节数。
 *   若等待最终 TC 超时，返回 length，表示数据已写入 USART 但最终完成标志未确认。
 * 说明：
 *   不使用 usart_flag_clear() 清除 TC，该函数在 GD32 库中直接写
 *   ~BIT(6) 到 USART_STAT0 寄存器，会污染保留位(bit10-31)导致外设异常。
 *   TC 的正确清除方式"读 STAT0 + 写 DATA"已由 for 循环中的 TBE 等待
 *   (读) + usart_data_transmit(写) 隐式完成。
 */
static uint16_t prv_uart_send_buffer(uint32_t usart_periph, const uint8_t *data, uint16_t length)
{
    uint16_t index;

    if((NULL == data) || (0U == length)){
        return 0U;
    }

    for(index = 0U; index < length; index++){
        if(0U == prv_uart_wait_flag_set(usart_periph, USART_FLAG_TBE)){
            return index;
        }
        /* TBE 置位后再写 DATA，保证不会覆盖仍在发送缓冲中的上一字节。 */
        usart_data_transmit(usart_periph, data[index]);
    }

    if(0U == prv_uart_wait_flag_set(usart_periph, USART_FLAG_TC)){
        return index;
    }

    return length;
}

/*
 * 函数作用：
 *   将一段原始字节流通过 RS485 总线发送出去。
 * 主要流程：
 *   1. 发送前拉高 PE8，让 MAX3485 进入发送状态。
 *   2. 复用阻塞串口发送函数，把数据发到 USART1。
 *   3. 等待最后一个字节完成后拉低 PE8，释放 RS485 总线回到接收状态。
 * 参数说明：
 *   data：待发送数据起始地址，必须指向至少 length 字节的有效缓冲区。
 *   length：待发送字节数，单位为字节。
 * 返回值说明：
 *   返回实际完成发送流程的字节数。
 *   0 表示参数无效或未能开始发送。
 */
static uint16_t prv_rs485_send_buffer(const uint8_t *data, uint16_t length)
{
    uint16_t sent_length;

    if((NULL == data) || (0U == length)){
        return 0U;
    }

    bsp_rs485_direction_transmit();
    sent_length = prv_uart_send_buffer(RS485_USART, data, length);
    /* 等待发送函数返回后再切回接收态，避免最后一字节还在移位时提前释放 DE。 */
    bsp_rs485_direction_receive();

    return sent_length;
}

/*
 * 函数作用：
 *   从小端字节序缓冲区读取 32 位无符号整数。
 * 主要流程：
 *   1. 按低地址低字节的协议约定读取 4 个字节。
 *   2. 组合为 MCU 本地 uint32_t 值。
 * 参数说明：
 *   data：指向至少 4 字节有效数据的缓冲区。
 * 返回值说明：
 *   返回组合后的 32 位整数；若 data 为空则返回 0。
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
 *   判断短数据是否可能是 OTA 魔术字的前缀。
 * 主要流程：
 *   1. 按 OTA 小端魔术字拆出前 4 个字节。
 *   2. 将当前已收到的 1~3 字节逐个比较。
 *   3. 只有全部匹配时才认为需要继续等待后续包头。
 * 参数说明：
 *   data：当前 USART0 累积接收缓冲区。
 *   len：当前缓冲区长度，仅用于 1~3 字节的前缀判断。
 * 返回值说明：
 *   1：当前数据仍可能是 OTA 包头前缀。
 *   0：当前数据不可能是 OTA 包，应按普通串口数据处理。
 */
static uint8_t prv_uart_ota_is_magic_prefix(const uint8_t *data, uint32_t len)
{
    uint8_t magic_bytes[4] = {
        (uint8_t)(UART_OTA_MAGIC & 0xFFU),
        (uint8_t)((UART_OTA_MAGIC >> 8U) & 0xFFU),
        (uint8_t)((UART_OTA_MAGIC >> 16U) & 0xFFU),
        (uint8_t)((UART_OTA_MAGIC >> 24U) & 0xFFU)
    };
    uint32_t i;

    if((NULL == data) || (0U == len) || (len > sizeof(magic_bytes))){
        return 0U;
    }

    for(i = 0U; i < len; i++){
        if(data[i] != magic_bytes[i]){
            return 0U;
        }
    }

    return 1U;
}

/*
 * 函数作用：
 *   以 BootLoader 相同算法计算固件 CRC32。
 * 主要流程：
 *   1. 使用初值 0xFFFFFFFF 逐字节更新 CRC。
 *   2. 每字节按多项式 0xEDB88320 处理 8 位。
 *   3. 末尾取反得到最终 CRC32。
 * 参数说明：
 *   data：待计算数据起始地址，必须指向 len 字节有效数据。
 *   len：待计算数据长度，单位为字节。
 * 返回值说明：
 *   返回 CRC32 值；若 data 为空且 len 非 0，返回 0。
 */
static uint32_t prv_uart_ota_crc32_calc(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t i;
    uint32_t j;

    if((NULL == data) && (len > 0U)){
        return 0U;
    }

    for(i = 0U; i < len; i++){
        crc ^= data[i];
        for(j = 0U; j < 8U; j++){
            if(0U != (crc & 1U)){
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
 *   判断固件镜像开头的向量表是否像一个可跳转 App。
 * 主要流程：
 *   1. 从 bin 第 0 项读取 MSP 初值。
 *   2. 从 bin 第 1 项读取 Reset_Handler 入口。
 *   3. 校验 MSP 是否位于主 SRAM，入口是否位于 App 区且为 Thumb 地址。
 * 参数说明：
 *   firmware：指向 Project.bin 内容起始地址，也就是 App 向量表起始处。
 *   firmware_size：固件长度，至少需要包含 8 字节向量表。
 * 返回值说明：
 *   1：向量表合法。
 *   0：长度不足、指针为空、MSP/入口地址不符合 BootLoader 跳转要求。
 */
static uint8_t prv_uart_ota_is_valid_firmware_vector(const uint8_t *firmware, uint32_t firmware_size)
{
    uint32_t stack_addr;
    uint32_t entry_addr;
    uint32_t app_region_end = UART_OTA_APP_START_ADDR + 0x00063000UL;

    if((NULL == firmware) || (firmware_size < 8U)){
        return 0U;
    }

    stack_addr = prv_uart_ota_read_u32_le(&firmware[0]);
    entry_addr = prv_uart_ota_read_u32_le(&firmware[4]);

    /*
     * BootLoader 侧会检查 MSP 位于 0x20000000~0x20030000。
     * App 侧提前拒绝明显错误文件，可以避免写入无效镜像后反复启动失败。
     */
    if((stack_addr < 0x20000000UL) || (stack_addr >= 0x20030000UL)){
        my_printf(DEBUG_USART, "OTA: invalid stack 0x%08x\r\n", stack_addr);
        return 0U;
    }

    /*
     * Cortex-M Reset_Handler 入口最低位应为 1，表示 Thumb 状态。
     * 地址主体必须落在 App 运行区内，不能指向 BootLoader 或参数区。
     */
    if((0U == (entry_addr & 1U)) ||
       ((entry_addr & ~1UL) < UART_OTA_APP_START_ADDR) ||
       ((entry_addr & ~1UL) >= app_region_end)){
        my_printf(DEBUG_USART, "OTA: invalid entry 0x%08x\r\n", entry_addr);
        return 0U;
    }

    return 1U;
}

/*
 * 函数作用：
 *   清除片内 Flash 控制器的常见完成和错误标志。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
static void prv_uart_ota_flash_clear_flags(void)
{
    fmc_flag_clear(FMC_FLAG_END);
    fmc_flag_clear(FMC_FLAG_WPERR);
    fmc_flag_clear(FMC_FLAG_PGSERR);
    fmc_flag_clear(FMC_FLAG_PGMERR);
    fmc_flag_clear(FMC_FLAG_OPERR);
    fmc_flag_clear(FMC_FLAG_RDDERR);
}

/*
 * 函数作用：
 *   按 4KB 页擦除指定 Flash 区间。
 * 主要流程：
 *   1. 根据数据长度向上取整计算需要擦除的页数。
 *   2. 逐页调用 fmc_page_erase()。
 *   3. 任一页擦除失败立即停止并返回错误。
 * 参数说明：
 *   start_addr：待擦除区域起始地址，必须按页起始地址传入。
 *   length：待擦除数据长度，单位为字节；为 0 时不执行擦除。
 * 返回值说明：
 *   1：全部页擦除成功或 length 为 0。
 *   0：至少一页擦除失败。
 */
static uint8_t prv_uart_ota_flash_erase_pages(uint32_t start_addr, uint32_t length)
{
    uint32_t erase_pages;
    uint32_t i;
    fmc_state_enum state;

    if(0U == length){
        return 1U;
    }

    erase_pages = (length + UART_OTA_FLASH_PAGE_SIZE - 1U) / UART_OTA_FLASH_PAGE_SIZE;
    for(i = 0U; i < erase_pages; i++){
        prv_uart_ota_flash_clear_flags();
        state = fmc_page_erase(start_addr + (i * UART_OTA_FLASH_PAGE_SIZE));
        if(FMC_READY != state){
            my_printf(DEBUG_USART, "OTA: erase fail page:%d state:%d\r\n", i, state);
            return 0U;
        }
    }

    return 1U;
}

/*
 * 函数作用：
 *   将一段字节数据写入片内 Flash。
 * 主要流程：
 *   1. 逐字节调用 fmc_byte_program() 写入。
 *   2. 每次写入检查返回状态。
 *   3. 失败时打印失败偏移并返回错误。
 * 参数说明：
 *   start_addr：Flash 目标起始地址。
 *   data：待写入数据起始地址，必须指向 length 字节有效内容。
 *   length：待写入长度，单位为字节。
 * 返回值说明：
 *   1：全部字节写入成功。
 *   0：参数无效或某个字节写入失败。
 */
static uint8_t prv_uart_ota_flash_write_bytes(uint32_t start_addr, const uint8_t *data, uint32_t length)
{
    uint32_t i;
    fmc_state_enum state;

    if((NULL == data) && (length > 0U)){
        return 0U;
    }

    for(i = 0U; i < length; i++){
        state = fmc_byte_program(start_addr + i, data[i]);
        if(FMC_READY != state){
            my_printf(DEBUG_USART, "OTA: write fail offset:%d state:%d\r\n", i, state);
            return 0U;
        }
    }

    return 1U;
}

/*
 * 函数作用：
 *   对 Flash 中的指定区域重新计算 CRC32。
 * 主要流程：
 *   1. 逐字节从 Flash 地址读取内容。
 *   2. 使用与固件缓存相同的 CRC32 算法更新。
 * 参数说明：
 *   start_addr：Flash 起始地址。
 *   length：参与计算的数据长度，单位为字节。
 * 返回值说明：
 *   返回 Flash 区域的 CRC32。
 */
static uint32_t prv_uart_ota_flash_crc32_calc(uint32_t start_addr, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t i;
    uint32_t j;
    uint8_t data;

    for(i = 0U; i < length; i++){
        data = *(volatile uint8_t *)(start_addr + i);
        crc ^= data;
        for(j = 0U; j < 8U; j++){
            if(0U != (crc & 1U)){
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
 *   写入 BootLoader 参数区，通知 BootLoader 下次复位执行升级搬运。
 * 主要流程：
 *   1. 先读取原 4KB 参数区，保留用户配置、校准数据等字段。
 *   2. 如果参数区为空或魔术字无效，使用默认参数初始化必要结构。
 *   3. 填入升级标志、固件大小、CRC、App 地址和版本。
 *   4. 整页擦除参数区并写回 4KB 内容。
 * 参数说明：
 *   app_version：升级包头中的 App 版本号。
 *   firmware_size：Project.bin 原始固件长度。
 *   firmware_crc32：Project.bin 原始固件 CRC32。
 * 返回值说明：
 *   1：参数区写入成功。
 *   0：擦除或写入失败。
 */
static uint8_t prv_uart_ota_write_boot_param(uint32_t app_version,
                                             uint32_t firmware_size,
                                             uint32_t firmware_crc32)
{
    uint32_t i;
    uart_ota_parameter_t *param = (uart_ota_parameter_t *)uart_ota_param_buffer;

    for(i = 0U; i < UART_OTA_PARAM_SIZE; i++){
        uart_ota_param_buffer[i] = *(volatile uint8_t *)(UART_OTA_PARAM_ADDR + i);
    }

    /*
     * 首次烧录后参数区通常是 0xFF。此时没有可保留的有效配置，
     * 需要先生成默认参数结构，再写入升级控制字段。
     */
    if(UART_OTA_MAGIC != param->boot_param.magicWord){
        memset(uart_ota_param_buffer, 0, sizeof(uart_ota_param_buffer));
        param = (uart_ota_parameter_t *)uart_ota_param_buffer;
        param->boot_param.magicWord = UART_OTA_MAGIC;
        param->boot_param.version = 0x0001U;
        param->boot_param.structSize = sizeof(uart_ota_boot_param_t);
        param->boot_param.updateMode = 0x01U;
        param->boot_param.appStartAddr = UART_OTA_APP_START_ADDR;
        param->boot_param.appEntryAddr = *(volatile uint32_t *)(UART_OTA_APP_START_ADDR + 4U);
        param->boot_param.appStackAddr = *(volatile uint32_t *)(UART_OTA_APP_START_ADDR + 0U);
        param->boot_param.bootVersion = 0x01U;
        param->boot_param.bootSize = 4096U;
        param->boot_param.backupAddr = UART_OTA_PARAM_ADDR + sizeof(uart_ota_boot_param_t);
        param->boot_param.backupSize = sizeof(uart_ota_boot_param_t);
        memcpy(param->boot_param.deviceID, "202601301528", 12U);
        memcpy(param->boot_param.productModel, "000000000001", 12U);
        memcpy(param->boot_param.serialNumber, "100000000000", 12U);
        param->boot_param.hwVersion = 1U;
        param->boot_param.cpuID = 1U;
        param->boot_param.flashSize = 0x400U;
        param->boot_param.ramSize = 0x2FU;
        param->boot_param.clockFreq = 240000000UL;
        param->boot_param.tailMagic = 0xA5A5C3C3UL;
    }

    param->boot_param_reserved = param->boot_param;
    param->boot_param.magicWord = UART_OTA_MAGIC;
    param->boot_param.version = 0x0001U;
    param->boot_param.structSize = sizeof(uart_ota_boot_param_t);
    param->boot_param.updateFlag = 0x5AU;
    param->boot_param.updateMode = 0x01U;
    param->boot_param.updateStatus = 0x01U;
    param->boot_param.updateProgress = 0U;
    param->boot_param.appSize = firmware_size;
    param->boot_param.appCRC32 = firmware_crc32;
    param->boot_param.appVersion = app_version;
    param->boot_param.appStartAddr = UART_OTA_APP_START_ADDR;
    param->boot_param.appStackAddr = *(volatile uint32_t *)(UART_OTA_APP_START_ADDR + 0U);
    param->boot_param.appEntryAddr = *(volatile uint32_t *)(UART_OTA_APP_START_ADDR + 4U);
    param->boot_param.tailMagic = 0xA5A5C3C3UL;

    if(0U == prv_uart_ota_flash_erase_pages(UART_OTA_PARAM_ADDR, UART_OTA_PARAM_SIZE)){
        return 0U;
    }

    if(0U == prv_uart_ota_flash_write_bytes(UART_OTA_PARAM_ADDR,
                                            uart_ota_param_buffer,
                                            UART_OTA_PARAM_SIZE)){
        return 0U;
    }

    return 1U;
}

/*
 * 函数作用：
 *   处理一个完整 USART0 接收帧中的升级包。
 * 主要流程：
 *   1. 校验包头魔术字，非升级包直接返回 NOT_PACKET。
 *   2. 校验长度和固件向量表。
 *   3. 擦除下载缓存区并写入 Project.bin 内容。
 *   4. 读回下载缓存区计算 CRC，确认 Flash 内容与接收内容一致。
 *   5. 写 BootLoader 参数区，设置升级标志。
 * 参数说明：
 *   packet：USART0 接收到的数据缓存，格式为 16 字节包头 + Project.bin。
 *   packet_length：完整数据帧长度，单位为字节。
 * 返回值说明：
 *   UART_OTA_RESULT_NOT_PACKET：不是升级包，调用者可继续普通串口转发。
 *   UART_OTA_RESULT_WAIT_MORE：已经识别为升级包但尚未收全，调用者应继续等待。
 *   UART_OTA_RESULT_SUCCESS：写入下载缓存区和参数区成功，调用者可以复位。
 *   其它值：升级包格式或 Flash 操作失败。
 */
static uart_ota_result_t prv_uart_ota_try_process_packet(const uint8_t *packet, uint32_t packet_length)
{
    uint32_t magic;
    uint32_t app_version;
    uint32_t firmware_size;
    uint32_t firmware_crc32;
    uint32_t expected_crc32;
    uint32_t expected_packet_length;
    uint32_t flash_crc32;
    const uint8_t *firmware;

    if(NULL == packet){
        return UART_OTA_RESULT_NOT_PACKET;
    }

    if(packet_length < 4U){
        if(0U != prv_uart_ota_is_magic_prefix(packet, packet_length)){
            return UART_OTA_RESULT_WAIT_MORE;
        }
        return UART_OTA_RESULT_NOT_PACKET;
    }

    if(packet_length < UART_OTA_HEADER_SIZE){
        magic = prv_uart_ota_read_u32_le(&packet[0]);
        if(UART_OTA_MAGIC == magic){
            return UART_OTA_RESULT_WAIT_MORE;
        }
        return UART_OTA_RESULT_NOT_PACKET;
    }

    magic = prv_uart_ota_read_u32_le(&packet[0]);
    if(UART_OTA_MAGIC != magic){
        return UART_OTA_RESULT_NOT_PACKET;
    }

    app_version = prv_uart_ota_read_u32_le(&packet[4]);
    firmware_size = prv_uart_ota_read_u32_le(&packet[8]);
    expected_crc32 = prv_uart_ota_read_u32_le(&packet[12]);

    if((0U == firmware_size) || (firmware_size > UART_OTA_DOWNLOAD_MAX_SIZE)){
        my_printf(DEBUG_USART, "OTA: bad size %d\r\n", firmware_size);
        return UART_OTA_RESULT_BAD_LENGTH;
    }

    expected_packet_length = UART_OTA_HEADER_SIZE + firmware_size;
    if(packet_length < expected_packet_length){
        return UART_OTA_RESULT_WAIT_MORE;
    }

    if(packet_length > expected_packet_length){
        my_printf(DEBUG_USART,
                  "OTA: extra data length=%d expect=%d\r\n",
                  packet_length,
                  expected_packet_length);
        return UART_OTA_RESULT_BAD_LENGTH;
    }

    firmware = &packet[UART_OTA_HEADER_SIZE];
    if(0U == prv_uart_ota_is_valid_firmware_vector(firmware, firmware_size)){
        return UART_OTA_RESULT_BAD_VECTOR;
    }

    firmware_crc32 = prv_uart_ota_crc32_calc(firmware, firmware_size);
    if(firmware_crc32 != expected_crc32){
        my_printf(DEBUG_USART,
                  "OTA: packet crc fail header=0x%08x calc=0x%08x\r\n",
                  expected_crc32,
                  firmware_crc32);
        return UART_OTA_RESULT_VERIFY_ERROR;
    }

    my_printf(DEBUG_USART,
              "OTA: package version=0x%08x size=%d crc=0x%08x\r\n",
              app_version,
              firmware_size,
              firmware_crc32);

    /*
     * 写片内 Flash 期间关闭 USART0 中断，避免接收新数据覆盖升级缓存；
     * 全局中断不长期关闭，SysTick 等系统节拍仍可运行。
     */
    nvic_irq_disable(USART0_IRQn);

    fmc_unlock();
    if(0U == prv_uart_ota_flash_erase_pages(UART_OTA_DOWNLOAD_ADDR, firmware_size)){
        fmc_lock();
        nvic_irq_enable(USART0_IRQn, 0U, 0U);
        return UART_OTA_RESULT_FLASH_ERROR;
    }

    if(0U == prv_uart_ota_flash_write_bytes(UART_OTA_DOWNLOAD_ADDR, firmware, firmware_size)){
        fmc_lock();
        nvic_irq_enable(USART0_IRQn, 0U, 0U);
        return UART_OTA_RESULT_FLASH_ERROR;
    }

    flash_crc32 = prv_uart_ota_flash_crc32_calc(UART_OTA_DOWNLOAD_ADDR, firmware_size);
    if(flash_crc32 != firmware_crc32){
        my_printf(DEBUG_USART,
                  "OTA: verify fail recv=0x%08x flash=0x%08x\r\n",
                  firmware_crc32,
                  flash_crc32);
        fmc_lock();
        nvic_irq_enable(USART0_IRQn, 0U, 0U);
        return UART_OTA_RESULT_VERIFY_ERROR;
    }

    if(0U == prv_uart_ota_write_boot_param(app_version, firmware_size, firmware_crc32)){
        fmc_lock();
        nvic_irq_enable(USART0_IRQn, 0U, 0U);
        return UART_OTA_RESULT_FLASH_ERROR;
    }

    fmc_lock();
    nvic_irq_enable(USART0_IRQn, 0U, 0U);
    my_printf(DEBUG_USART, "OTA: ready, reset to BootLoader\r\n");

    return UART_OTA_RESULT_SUCCESS;
}

/*
 * 函数作用：
 *   触发 MCU 软件复位，让 BootLoader 读取参数区并执行升级搬运。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值；正常情况下不会返回。
 */
static void prv_uart_ota_system_reset(void)
{
    __set_FAULTMASK(1);
    NVIC_SystemReset();
}

/*
 * 函数作用：
 *   将中断上下文产生的一帧数据原子地转存到任务层私有缓冲区。
 * 主要流程：
 *   1. 关总中断，防止复制过程中 ISR 再次改写源缓冲区和长度标志。
 *   2. 按有效长度把共享缓冲区复制到任务层专用缓冲区。
 *   3. 清除共享长度和完成标志，然后恢复中断。
 * 说明：
 *   这里只在很短的时间内屏蔽中断，复制完成后再做阻塞串口发送，
 *   既保证数据一致性，也避免长时间阻塞中断响应。
 * 参数：
 *   ready_flag     表示该帧是否已经准备好。
 *   shared_length  表示 ISR 记录的有效长度。
 *   shared_buffer  表示 ISR 使用的共享缓冲区。
 *   task_buffer    表示任务层私有缓冲区。
 *   task_buf_size  表示任务层私有缓冲区大小。
 * 返回值说明：
 *   返回成功取出的有效字节数。
 *   0 表示当前没有可处理的新帧，或参数无效。
 */
static uint16_t prv_uart_take_frame(__IO uint8_t *ready_flag,
                                    __IO uint16_t *shared_length,
                                    uint8_t *shared_buffer,
                                    uint8_t *task_buffer,
                                    uint16_t task_buf_size)
{
    uint16_t valid_length = 0U;

    if((NULL == ready_flag) || (NULL == shared_length) ||
       (NULL == shared_buffer) || (NULL == task_buffer) ||
       (0U == task_buf_size)){
        return 0U;
    }

    __disable_irq();
    if(0U != *ready_flag){
        valid_length = *shared_length;
        /* 再做一次目标缓冲区长度钳位，避免 ISR 或未来调用点传入异常长度。 */
        if(valid_length > task_buf_size){
            valid_length = task_buf_size;
        }
        if(valid_length > 0U){
            memcpy(task_buffer, shared_buffer, valid_length);
        }
        /* 清标志必须和复制共享数据处于同一个临界区，避免任务层重复消费同一帧。 */
        *shared_length = 0U;
        *ready_flag = 0U;
    }
    __enable_irq();

    return valid_length;
}

/*
 * 函数作用：
 *   通过阻塞发送的方式向指定串口输出格式化字符串。
 * 主要流程：
 *   1. 使用 vsnprintf 将可变参数格式化到固定长度缓冲区。
 *   2. 根据格式化结果计算实际发送长度，超长时发送已截断的缓冲区内容。
 *   3. 通过 prv_uart_send_buffer() 发送到指定 USART。
 * 参数说明：
 *   usart_periph：目标 USART 外设编号。
 *   format：printf 风格格式字符串，后续可变参数必须与格式占位符匹配。
 * 返回值说明：
 *   正数或 0：vsnprintf 返回的完整格式化长度，可能大于实际发送长度。
 *   负数：格式化失败，函数不发送数据并原样返回该错误值。
 */
int my_printf(uint32_t usart_periph, const char *format, ...)
{
    char buffer[UART_APP_PRINTF_BUFFER_SIZE];
    va_list arg;
    int len;
    uint16_t send_length;

    va_start(arg, format);
    len = vsnprintf(buffer, sizeof(buffer), format, arg);
    va_end(arg);

    if(len <= 0){
        return len;
    }

    if(len >= (int)sizeof(buffer)){
        /* vsnprintf 已经截断缓冲区，发送时保留结尾 '\0' 位置不作为串口数据。 */
        send_length = (uint16_t)(sizeof(buffer) - 1U);
    }else{
        send_length = (uint16_t)len;
    }

    (void)prv_uart_send_buffer(usart_periph, (const uint8_t *)buffer, send_length);

    return len;
}

/*
 * 函数作用：
 *   周期性处理 USART0 与 RS485 的双向透明转发。
 * 主要流程：
 *   1. 若 RS485 收到一帧，则原样转发到默认调试串口 USART0。
 *   2. 若 USART0 收到一帧，则原样转发到 RS485 总线。
 * 说明：
 *   这里不再插入 "RS485 RX:" 等调试文本，也不对 USART0 做本地回显，
 *   避免上位机看到附加字符或误判为桥接链路异常。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void uart_task(void)
{
    uint16_t rx_length;
    uint16_t rs485_rx_length;
    uint32_t rx_snapshot_length;
    uart_ota_result_t ota_result;

    /*
     * USART1/RS485 接收处理：
     *   中断层只负责把 DMA 数据搬到 rs485_dma_buffer 并置位标志；
     *   应用层在这里原样转发到 USART0，保持桥接数据透明。
     */
    rs485_rx_length = prv_uart_take_frame(&rs485_rx_flag,
                                          &rs485_dma_length,
                                          rs485_dma_buffer,
                                          rs485_forward_buffer,
                                          (uint16_t)sizeof(rs485_forward_buffer));
    if(rs485_rx_length > 0U){
        /* RS485 到调试串口保持原样转发，确保桥接链路对上位机透明。 */
        (void)prv_uart_send_buffer(DEBUG_USART, rs485_forward_buffer, rs485_rx_length);
    }

    /*
     * USART0 接收处理：
     *   上位机发来的数据被转发到 RS485 总线；发送函数内部负责方向脚切换和 TC 等待。
     */
    __disable_irq();
    if(0U != rx_flag){
        rx_snapshot_length = uart_dma_length;
        rx_flag = 0U;
    }else{
        rx_snapshot_length = 0U;
    }
    __enable_irq();

    if(rx_snapshot_length > 0U){
        ota_result = prv_uart_ota_try_process_packet(uart_dma_buffer, rx_snapshot_length);
        if(UART_OTA_RESULT_SUCCESS == ota_result){
            /*
             * 参数区已经写好后再复位，BootLoader 会在下一次启动时根据
             * updateStatus/updateFlag 搬运下载缓存区里的新固件。
             */
            __disable_irq();
            uart_dma_length = 0U;
            __enable_irq();
            delay_ms(50);
            prv_uart_ota_system_reset();
        }else if(UART_OTA_RESULT_NOT_PACKET == ota_result){
            rx_length = (uint16_t)rx_snapshot_length;
            if(rx_length > sizeof(uart_forward_buffer)){
                rx_length = (uint16_t)sizeof(uart_forward_buffer);
            }
            memcpy(uart_forward_buffer, uart_dma_buffer, rx_length);
            /*
             * 普通串口数据已经处理完毕，清空 USART0 累积长度；
             * OTA 包则由 WAIT_MORE 分支保留数据，等待后续小块补齐。
             */
            __disable_irq();
            if(rx_snapshot_length == uart_dma_length){
                uart_dma_length = 0U;
            }
            __enable_irq();
            (void)prv_rs485_send_buffer(uart_forward_buffer, rx_length);
        }else if(UART_OTA_RESULT_WAIT_MORE == ota_result){
            /* 已识别为 OTA 包但还没有收全，保留 uart_dma_buffer 继续累积。 */
        }else{
            /*
             * OTA 包格式错误时丢弃当前累积内容，避免后续普通数据继续拼接到
             * 一段已经无效的升级数据后面。
             */
            __disable_irq();
            if(rx_snapshot_length == uart_dma_length){
                uart_dma_length = 0U;
            }
            __enable_irq();
            my_printf(DEBUG_USART, "OTA: failed result=%d\r\n", ota_result);
        }
    }
}
