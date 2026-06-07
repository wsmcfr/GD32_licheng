#include "bootloader_port.h"

/* updateStatus=0x02 表示 App 已收到 0x0501，请 Bootloader 进入大赛串口升级等待窗口。 */
#define BOOTLOADER_PORT_STATUS_WAIT_CONTEST_OTA    0x02U

/* BootLoader 主参数区 256 字节布局。当前 App 只会改升级相关字段，
   其它字段尽量保留原值，确保与官方 Two Stage BootLoader 的二进制布局兼容。 */
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
} bootloader_port_boot_param_t;

/* 当前工程 4KB BootLoader 参数区整体布局。
   参数区除主参数外还含备份区、升级日志、用户配置和校准数据；
   写升级信息时必须整块读回、修改后再整页回写，不能只写前几个字节。 */
typedef struct __attribute__((packed))
{
    bootloader_port_boot_param_t boot_param;
    bootloader_port_boot_param_t boot_param_reserved;
    uint8_t update_log[1024];
    uint8_t user_config[512];
    uint8_t calib_data[512];
    uint8_t reserved_tail[BOOTLOADER_PORT_PARAM_SIZE - (sizeof(bootloader_port_boot_param_t) * 2U) - 1024U - 512U - 512U];
} bootloader_port_parameter_t;

/* 参数区回写时使用的 RAM 缓冲，避免直接在 Flash 上做读改写。 */
static uint8_t g_bootloader_port_param_buffer[BOOTLOADER_PORT_PARAM_SIZE] = {0};

/* 从小端字节序缓冲区读取 32 位无符号整数，data 为空时返回 0。 */
static uint32_t prv_bootloader_port_read_u32_le(const uint8_t *data)
{
    if(NULL == data)
	{
        return 0U;
    }

    return ((uint32_t)data[0]) | ((uint32_t)data[1] << 8U) | ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

/* 清除内部 Flash 控制器上一次操作留下的完成位和错误位。 */
static void prv_bootloader_port_flash_clear_flags(void)
{
    fmc_flag_clear(FMC_FLAG_END);
    fmc_flag_clear(FMC_FLAG_WPERR);
    fmc_flag_clear(FMC_FLAG_PGSERR);
    fmc_flag_clear(FMC_FLAG_PGMERR);
    fmc_flag_clear(FMC_FLAG_OPERR);
    fmc_flag_clear(FMC_FLAG_RDDERR);
}

/* 按页擦除指定内部 Flash 区间。start_addr 必须按页对齐，length 为 0 时直接返回成功。
   任一页擦除失败立即返回 BOOTLOADER_PORT_STATUS_FLASH_ERROR。 */
static bootloader_port_status_t prv_bootloader_port_flash_erase_pages(uint32_t start_addr,uint32_t length)
{
    uint32_t erase_pages;
    uint32_t page_index;
    fmc_state_enum state;

    if(0U == length)
	{
        return BOOTLOADER_PORT_STATUS_OK;
    }

    erase_pages = (length + BOOTLOADER_PORT_FLASH_PAGE_SIZE - 1U) / BOOTLOADER_PORT_FLASH_PAGE_SIZE;
    for(page_index = 0U; page_index < erase_pages; page_index++)
	{
        /* 每页擦除前先清状态，避免上一轮 Flash 错误残留影响当前判断。 */
        prv_bootloader_port_flash_clear_flags();
        state = fmc_page_erase(start_addr + (page_index * BOOTLOADER_PORT_FLASH_PAGE_SIZE));
        if(FMC_READY != state)
		{
            return BOOTLOADER_PORT_STATUS_FLASH_ERROR;
        }
    }

    return BOOTLOADER_PORT_STATUS_OK;
}

/* 逐字节把一段数据写入内部 Flash。OTA 分包长度不一定按字对齐，
   因此保留逐字节策略，避免为对齐额外引入 RAM 拼包逻辑。
   data 为空且 length 非 0 时返回 BAD_PARAM，任一字节写入失败返回 FLASH_ERROR。 */
static bootloader_port_status_t prv_bootloader_port_flash_write_bytes(uint32_t start_addr,const uint8_t *data,uint32_t length)
{
    uint32_t index;
    fmc_state_enum state;

    if((NULL == data) && (length > 0U))
	{
        return BOOTLOADER_PORT_STATUS_BAD_PARAM;
    }

    for(index = 0U; index < length; index++)
	{
        /*
         * OTA 分包长度不一定按字对齐，因此这里保留逐字节写入策略，
         * 避免为了对齐额外引入 RAM 拼包逻辑。
         */
        state = fmc_byte_program(start_addr + index, data[index]);
        if(FMC_READY != state)
		{
            return BOOTLOADER_PORT_STATUS_FLASH_ERROR;
        }
    }

    return BOOTLOADER_PORT_STATUS_OK;
}

/* 为首次空白参数区填充一组最小可用的默认字段，parameter 为空时直接返回。 */
static void prv_bootloader_port_init_default_parameter(bootloader_port_parameter_t *parameter)
{
    if(NULL == parameter)
	{
        return;
    }

    memset(parameter, 0, sizeof(*parameter));
    parameter->boot_param.magicWord = BOOTLOADER_PORT_MAGIC_WORD;
    parameter->boot_param.version = 0x0001U;
    parameter->boot_param.structSize = sizeof(bootloader_port_boot_param_t);
    parameter->boot_param.updateMode = 0x01U;
    parameter->boot_param.appStartAddr = BOOT_APP_START_ADDRESS;
    /*
     * 参数区的这两个入口字段历史上一直取当前 App 区的向量表值。
     * 本次迁移不改 BootLoader 使用约定，只保持与现有工程行为一致。
     */
    parameter->boot_param.appEntryAddr = *(volatile uint32_t *)(BOOT_APP_START_ADDRESS + 4U);
    parameter->boot_param.appStackAddr = *(volatile uint32_t *)(BOOT_APP_START_ADDRESS + 0U);
    parameter->boot_param.bootVersion = 0x01U;
    parameter->boot_param.bootSize = 64U * 1024U;
    parameter->boot_param.backupAddr = BOOTLOADER_PORT_PARAM_ADDR + sizeof(bootloader_port_boot_param_t);
    parameter->boot_param.backupSize = sizeof(bootloader_port_boot_param_t);
    memcpy(parameter->boot_param.deviceID, "202601301528", 12U);
    memcpy(parameter->boot_param.productModel, "000000000001", 12U);
    memcpy(parameter->boot_param.serialNumber, "100000000000", 12U);
    parameter->boot_param.hwVersion = 1U;
    parameter->boot_param.cpuID = 1U;
    parameter->boot_param.flashSize = 0x400U;
    parameter->boot_param.ramSize = 0x2FU;
    parameter->boot_param.clockFreq = 240000000UL;
    parameter->boot_param.tailMagic = BOOTLOADER_PORT_TAIL_MAGIC;
}

/* 标准 IEEE 802.3 多项式 CRC32，初值 0xFFFFFFFF 最终取反。
   data 为空且 length 非 0 时返回 0；正常情况逐字节处理并返回最终校验值。 */
uint32_t bootloader_port_crc32_calc(const uint8_t *data, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t index;
    uint32_t bit_index;

    if((NULL == data) && (length > 0U))
	{
        return 0U;
    }

    for(index = 0U; index < length; index++)
	{
        crc ^= data[index];
        for(bit_index = 0U; bit_index < 8U; bit_index++)
		{
            if(0U != (crc & 1U))
			{
                crc = (crc >> 1U) ^ 0xEDB88320UL;
            }
			else
			{
                crc >>= 1U;
            }
        }
    }

    return crc ^ 0xFFFFFFFFUL;
}

/* 在已有 CRC32 中间值上继续追加一段数据，用于分块流式计算。
   crc 为未取反的中间值，返回更新后的未取反中间值；data 为空时原样返回 crc。 */
uint32_t bootloader_port_crc32_update(uint32_t crc, const uint8_t *data, uint32_t length)
{
    uint32_t index;
    uint32_t bit_index;

    if((NULL == data) && (length > 0U))
	{
        return crc;
    }

    for(index = 0U; index < length; index++)
	{
        crc ^= data[index];
        for(bit_index = 0U; bit_index < 8U; bit_index++)
		{
            if(0U != (crc & 1U))
			{
                crc = (crc >> 1U) ^ 0xEDB88320UL;
            }
			else
			{
                crc >>= 1U;
            }
        }
    }

    return crc;
}

/* 校验固件镜像向量表是否合法：MSP 必须落在 SRAM 范围内，
   Reset_Handler 最低位须为 1（Thumb 状态）且地址主体必须落在 App 运行区内，
   避免跳向 BootLoader 区或参数区。stack_addr/entry_addr 为可选输出参数。 */
bootloader_port_status_t bootloader_port_validate_firmware_vector(const uint8_t *firmware,uint32_t firmware_size,uint32_t *stack_addr,uint32_t *entry_addr)
{
    uint32_t stack_value;
    uint32_t entry_value;
    uint32_t app_region_end = BOOT_APP_START_ADDRESS + BOOTLOADER_PORT_APP_MAX_SIZE;

    if((NULL == firmware) || (firmware_size < 8U))
	{
        return BOOTLOADER_PORT_STATUS_BAD_PARAM;
    }

    stack_value = prv_bootloader_port_read_u32_le(&firmware[0]);
    entry_value = prv_bootloader_port_read_u32_le(&firmware[4]);

    if(NULL != stack_addr)
	{
        *stack_addr = stack_value;
    }
    if(NULL != entry_addr)
	{
        *entry_addr = entry_value;
    }

    if((stack_value < 0x20000000UL) || (stack_value >= 0x20030000UL))
	{
        return BOOTLOADER_PORT_STATUS_BAD_VECTOR;
    }

    /*
     * Reset_Handler 的最低位必须为 1，表示 Thumb 状态；
     * 地址主体还必须落在当前 App 运行区范围内，避免跳向 BootLoader 或参数区。
     */
    if((0U == (entry_value & 1U)) || ((entry_value & ~1UL) < BOOT_APP_START_ADDRESS) || ((entry_value & ~1UL) >= app_region_end))
	{
        return BOOTLOADER_PORT_STATUS_BAD_VECTOR;
    }

    return BOOTLOADER_PORT_STATUS_OK;
}

/* 擦除内部 Flash 下载缓存区，firmware_size 为 0 或超出最大下载区大小时返回 BAD_PARAM。 */
bootloader_port_status_t bootloader_port_prepare_download_area(uint32_t firmware_size)
{
    bootloader_port_status_t status;

    if((0U == firmware_size) || (firmware_size > BOOTLOADER_PORT_DOWNLOAD_MAX_SIZE))
	{
        return BOOTLOADER_PORT_STATUS_BAD_PARAM;
    }

    fmc_unlock();
    status = prv_bootloader_port_flash_erase_pages(BOOTLOADER_PORT_DOWNLOAD_ADDR, firmware_size);
    fmc_lock();

    return status;
}

/* 把一个 OTA 数据分包写入下载缓存区指定偏移。
   offset+length 超出最大下载区范围时返回 BAD_PARAM。 */
bootloader_port_status_t bootloader_port_write_download_chunk(uint32_t offset,const uint8_t *data, uint32_t length)
{
    bootloader_port_status_t status;

    if((NULL == data) || (0U == length) || ((offset + length) > BOOTLOADER_PORT_DOWNLOAD_MAX_SIZE))
	{
        return BOOTLOADER_PORT_STATUS_BAD_PARAM;
    }

    fmc_unlock();
    status = prv_bootloader_port_flash_write_bytes(BOOTLOADER_PORT_DOWNLOAD_ADDR + offset,data,length);
    fmc_lock();

    return status;
}

/* 从下载缓存区逐字节回读并以相同 CRC32 算法重新计算校验值，供上层与协议中的期望值比对。
   firmware_size 为 0 或超出范围时返回 0。 */
uint32_t bootloader_port_calc_download_crc32(uint32_t firmware_size)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t index;
    uint32_t bit_index;
    uint8_t data_byte;

    if((0U == firmware_size) || (firmware_size > BOOTLOADER_PORT_DOWNLOAD_MAX_SIZE))
	{
        return 0U;
    }

    for(index = 0U; index < firmware_size; index++)
	{
        data_byte = *(volatile uint8_t *)(BOOTLOADER_PORT_DOWNLOAD_ADDR + index);
        crc ^= data_byte;
        for(bit_index = 0U; bit_index < 8U; bit_index++)
		{
            if(0U != (crc & 1U))
			{
                crc = (crc >> 1U) ^ 0xEDB88320UL;
            }
			else
			{
                crc >>= 1U;
            }
        }
    }

    return crc ^ 0xFFFFFFFFUL;
}

/* 把升级控制字段写入 BootLoader 参数区，供 BootLoader 下次复位后搬运固件。
   先把整个 4KB 页读入 RAM，再覆盖 updateFlag/appSize/appCRC32 等字段，
   同时把修改前的主参数备份到 boot_param_reserved 并记录其 CRC32，
   最后擦除整页后完整回写，保持与 Two Stage 工程的参数区布局兼容。 */
bootloader_port_status_t bootloader_port_write_upgrade_info(uint32_t app_version,uint32_t firmware_size,uint32_t firmware_crc32)
{
    uint8_t backup_bytes[sizeof(bootloader_port_boot_param_t)] = {0};
    uint32_t index;
    bootloader_port_status_t status;
    bootloader_port_parameter_t *parameter;

    if((0U == firmware_size) || (firmware_size > BOOTLOADER_PORT_DOWNLOAD_MAX_SIZE))
	{
        return BOOTLOADER_PORT_STATUS_BAD_PARAM;
    }

    for(index = 0U; index < BOOTLOADER_PORT_PARAM_SIZE; index++)
	{
        g_bootloader_port_param_buffer[index] = *(volatile uint8_t *)(BOOTLOADER_PORT_PARAM_ADDR + index);
    }

    parameter = (bootloader_port_parameter_t *)g_bootloader_port_param_buffer;
    if(BOOTLOADER_PORT_MAGIC_WORD != parameter->boot_param.magicWord)
	{
        prv_bootloader_port_init_default_parameter(parameter);
    }

    /*
     * 先把当前主参数复制到备份区，再覆盖升级相关字段，
     * 这样能继续兼容原 Two Stage 工程中的参数区镜像布局。
     */
    parameter->boot_param_reserved = parameter->boot_param;
    memcpy(backup_bytes,&parameter->boot_param_reserved,sizeof(parameter->boot_param_reserved));

    parameter->boot_param.magicWord = BOOTLOADER_PORT_MAGIC_WORD;
    parameter->boot_param.version = 0x0001U;
    parameter->boot_param.structSize = sizeof(bootloader_port_boot_param_t);
    parameter->boot_param.updateFlag = 0x5AU;
    parameter->boot_param.updateMode = 0x01U;
    parameter->boot_param.updateStatus = 0x01U;
    parameter->boot_param.updateProgress = 0U;
    parameter->boot_param.appSize = firmware_size;
    parameter->boot_param.appCRC32 = firmware_crc32;
    parameter->boot_param.appVersion = app_version;
    parameter->boot_param.appStartAddr = BOOT_APP_START_ADDRESS;
    parameter->boot_param.appStackAddr = *(volatile uint32_t *)(BOOT_APP_START_ADDRESS + 0U);
    parameter->boot_param.appEntryAddr = *(volatile uint32_t *)(BOOT_APP_START_ADDRESS + 4U);
    /*
     * 官方旧 App 例程会把"升级前主参数镜像"的 CRC32 记录到 backupCRC32。
     * 这里继续保持同样写法，避免迁移到独立 bootloader_port 模块后丢掉该兼容字段。
     */
    parameter->boot_param.backupCRC32 = bootloader_port_crc32_calc(backup_bytes,sizeof(backup_bytes));
    parameter->boot_param.tailMagic = BOOTLOADER_PORT_TAIL_MAGIC;

    fmc_unlock();
    status = prv_bootloader_port_flash_erase_pages(BOOTLOADER_PORT_PARAM_ADDR,BOOTLOADER_PORT_PARAM_SIZE);
    if(BOOTLOADER_PORT_STATUS_OK == status)
	{
        status = prv_bootloader_port_flash_write_bytes(BOOTLOADER_PORT_PARAM_ADDR,g_bootloader_port_param_buffer,BOOTLOADER_PORT_PARAM_SIZE);
    }
    fmc_lock();

    return status;
}

/* 写入"进入 Bootloader 等待串口升级"的请求标志。
   只写 updateFlag/updateStatus，不写 appSize/appCRC32；
   Bootloader 据此判断本次复位由 0x0501 指令触发，应等待 0x0502 接收 bin，
   普通上电或下载区搬运流程不会误触发此状态。 */
bootloader_port_status_t bootloader_port_request_bootloader_upgrade(void)
{
    uint32_t index;
    bootloader_port_status_t status;
    bootloader_port_parameter_t *parameter;

    for(index = 0U; index < BOOTLOADER_PORT_PARAM_SIZE; index++)
	{
        g_bootloader_port_param_buffer[index] = *(volatile uint8_t *)(BOOTLOADER_PORT_PARAM_ADDR + index);
    }

    parameter = (bootloader_port_parameter_t *)g_bootloader_port_param_buffer;
    if(BOOTLOADER_PORT_MAGIC_WORD != parameter->boot_param.magicWord)
	{
        prv_bootloader_port_init_default_parameter(parameter);
    }

    parameter->boot_param.magicWord = BOOTLOADER_PORT_MAGIC_WORD;
    parameter->boot_param.version = 0x0001U;
    parameter->boot_param.structSize = sizeof(bootloader_port_boot_param_t);
    parameter->boot_param.updateFlag = 0x5AU;
    parameter->boot_param.updateMode = 0x01U;
    parameter->boot_param.updateStatus = BOOTLOADER_PORT_STATUS_WAIT_CONTEST_OTA;
    parameter->boot_param.updateProgress = 0U;
    parameter->boot_param.tailMagic = BOOTLOADER_PORT_TAIL_MAGIC;

    fmc_unlock();
    status = prv_bootloader_port_flash_erase_pages(BOOTLOADER_PORT_PARAM_ADDR,BOOTLOADER_PORT_PARAM_SIZE);
    if(BOOTLOADER_PORT_STATUS_OK == status)
	{
        status = prv_bootloader_port_flash_write_bytes(BOOTLOADER_PORT_PARAM_ADDR,g_bootloader_port_param_buffer,BOOTLOADER_PORT_PARAM_SIZE);
    }
    fmc_lock();

    return status;
}

/* 请求软件复位，让 BootLoader 读取参数区并接手升级流程。 */
void bootloader_port_request_upgrade_reset(void)
{
    __set_FAULTMASK(1);
    NVIC_SystemReset();
}

/* 从参数区 user_config 段读取指定字节数到 RAM 缓冲区。
   Flash 是内存映射的，可直接按结构体字段偏移读取，无需解锁控制器。
   buf 为空或 size 超出 BOOTLOADER_PORT_USER_CONFIG_SIZE（512）时返回 BAD_PARAM。 */
bootloader_port_status_t bootloader_port_read_user_config(uint8_t *buf, uint16_t size)
{
    const bootloader_port_parameter_t *flash_map;
    uint16_t index;

    if((NULL == buf) || (0U == size) || (size > BOOTLOADER_PORT_USER_CONFIG_SIZE)) 
	{
        return BOOTLOADER_PORT_STATUS_BAD_PARAM;
    }

    /*
     * 直接把参数区起始地址强转为结构体指针，利用编译器计算好的字段偏移
     * 定位到 user_config 段，无需手动计算偏移量。
     */
    flash_map = (const bootloader_port_parameter_t *)BOOTLOADER_PORT_PARAM_ADDR;
    for(index = 0U; index < size; index++) 
	{
        buf[index] = flash_map->user_config[index];
    }

    return BOOTLOADER_PORT_STATUS_OK;
}

/* 将 RAM 缓冲区写入参数区 user_config 段，内部执行整页读-改-写。
   先把整个 4KB 参数区读入 g_bootloader_port_param_buffer，只修改 user_config 偏移处，
   再擦除整页后完整回写，保留升级控制字段、设备信息等其他内容不变。 */
bootloader_port_status_t bootloader_port_write_user_config(const uint8_t *buf, uint16_t size)
{
    uint32_t index;
    bootloader_port_status_t status;
    bootloader_port_parameter_t *parameter;

    if((NULL == buf) || (0U == size) || (size > BOOTLOADER_PORT_USER_CONFIG_SIZE)) 
	{
        return BOOTLOADER_PORT_STATUS_BAD_PARAM;
    }

    /* 把整个 4KB 参数页先读到 RAM，避免回写时清除 BootLoader 升级控制字段。 */
    for(index = 0U; index < BOOTLOADER_PORT_PARAM_SIZE; index++) 
	{
        g_bootloader_port_param_buffer[index] =
            *(volatile uint8_t *)(BOOTLOADER_PORT_PARAM_ADDR + index);
    }

    /* 在 RAM 镜像中只覆盖 user_config 段，其余字段保持原值。 */
    parameter = (bootloader_port_parameter_t *)g_bootloader_port_param_buffer;
    for(index = 0U; index < (uint32_t)size; index++) 
	{
        parameter->user_config[index] = buf[index];
    }

    fmc_unlock();
    status = prv_bootloader_port_flash_erase_pages(BOOTLOADER_PORT_PARAM_ADDR,BOOTLOADER_PORT_PARAM_SIZE);
    if(BOOTLOADER_PORT_STATUS_OK == status) 
	{
        status = prv_bootloader_port_flash_write_bytes(BOOTLOADER_PORT_PARAM_ADDR,g_bootloader_port_param_buffer,BOOTLOADER_PORT_PARAM_SIZE);
    }
    fmc_lock();

    return status;
}
