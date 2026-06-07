#include "bootloader_port.h"

/* updateStatus=0x02 表示 App 已收到 0x0501*/
#define BOOTLOADER_PORT_STATUS_WAIT_CONTEST_OTA    0x02U

/* BootLoader 主参数区 256 字节布局。 */
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

typedef struct __attribute__((packed))
{
    bootloader_port_boot_param_t boot_param;
    bootloader_port_boot_param_t boot_param_reserved;
    uint8_t update_log[1024];
    uint8_t user_config[512];
    uint8_t calib_data[512];
    uint8_t reserved_tail[BOOTLOADER_PORT_PARAM_SIZE - (sizeof(bootloader_port_boot_param_t) * 2) - 1024 - 512 - 512];
} bootloader_port_parameter_t;

/* 参数区回写时使用的 RAM 缓冲，避免直接在 Flash 上做读改写。 */
static uint8_t g_bootloader_port_param_buffer[BOOTLOADER_PORT_PARAM_SIZE] = {0};

/* 清除FMC残留标志。 */
static void prv_bootloader_port_flash_clear_flags(void)
{
    fmc_flag_clear(FMC_FLAG_END);
    fmc_flag_clear(FMC_FLAG_WPERR);
    fmc_flag_clear(FMC_FLAG_PGSERR);
    fmc_flag_clear(FMC_FLAG_PGMERR);
    fmc_flag_clear(FMC_FLAG_OPERR);
    fmc_flag_clear(FMC_FLAG_RDDERR);
}

/* 按页擦除Flash区间。 */
static bootloader_port_status_t prv_bootloader_port_flash_erase_pages(uint32_t start_addr,uint32_t length)
{
    uint32_t erase_pages;
    uint32_t page_index;
    fmc_state_enum state;

    if(0 == length)
	{
        return BOOTLOADER_PORT_STATUS_OK;
    }

    erase_pages = (length + BOOTLOADER_PORT_FLASH_PAGE_SIZE - 1) / BOOTLOADER_PORT_FLASH_PAGE_SIZE;
    for(page_index = 0; page_index < erase_pages; page_index++)
	{
        /* 先清状态，避免旧错误影响当前页。 */
        prv_bootloader_port_flash_clear_flags();
        state = fmc_page_erase(start_addr + (page_index * BOOTLOADER_PORT_FLASH_PAGE_SIZE));
        if(FMC_READY != state)
		{
            return BOOTLOADER_PORT_STATUS_FLASH_ERROR;
        }
    }

    return BOOTLOADER_PORT_STATUS_OK;
}

/* 写Flash字节流，长度不要求对齐。 */
static bootloader_port_status_t prv_bootloader_port_flash_write_bytes(uint32_t start_addr,const uint8_t *data,uint32_t length)
{
    uint32_t index;
    fmc_state_enum state;

    if((!data) && (length > 0))
	{
        return BOOTLOADER_PORT_STATUS_BAD_PARAM;
    }

    for(index = 0; index < length; index++)
	{
        state = fmc_byte_program(start_addr + index, data[index]);
        if(FMC_READY != state)
		{
            return BOOTLOADER_PORT_STATUS_FLASH_ERROR;
        }
    }

    return BOOTLOADER_PORT_STATUS_OK;
}

/* 初始化空白Boot参数页。 */
static void prv_bootloader_port_init_default_parameter(bootloader_port_parameter_t *parameter)
{
    if(!parameter)
	{
        return;
    }

    memset(parameter, 0, sizeof(*parameter));
    parameter->boot_param.magicWord = BOOTLOADER_PORT_MAGIC_WORD;
    parameter->boot_param.version = 0x0001U;
    parameter->boot_param.structSize = sizeof(bootloader_port_boot_param_t);
    parameter->boot_param.updateMode = 0x01U;
    parameter->boot_param.appStartAddr = BOOT_APP_START_ADDRESS;

    parameter->boot_param.appEntryAddr = *(volatile uint32_t *)(BOOT_APP_START_ADDRESS + 4);
    parameter->boot_param.appStackAddr = *(volatile uint32_t *)(BOOT_APP_START_ADDRESS + 0);
    parameter->boot_param.bootVersion = 0x01U;
    parameter->boot_param.bootSize = 64 * 1024;
    parameter->boot_param.backupAddr = BOOTLOADER_PORT_PARAM_ADDR + sizeof(bootloader_port_boot_param_t);
    parameter->boot_param.backupSize = sizeof(bootloader_port_boot_param_t);
    memcpy(parameter->boot_param.deviceID, "202601301528", 12);
    memcpy(parameter->boot_param.productModel, "000000000001", 12);
    memcpy(parameter->boot_param.serialNumber, "100000000000", 12);
    parameter->boot_param.hwVersion = 1;
    parameter->boot_param.cpuID = 1;
    parameter->boot_param.flashSize = 0x400U;
    parameter->boot_param.ramSize = 0x2FU;
    parameter->boot_param.clockFreq = 240000000UL;
    parameter->boot_param.tailMagic = BOOTLOADER_PORT_TAIL_MAGIC;
}

/* CRC32，参数保存和告警保存共用。 */
uint32_t bootloader_port_crc32_calc(const uint8_t *data, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t index;
    uint32_t bit_index;

    if((!data) && (length > 0))
	{
        return 0;
    }

    for(index = 0; index < length; index++)
	{
        crc ^= data[index];
        for(bit_index = 0; bit_index < 8; bit_index++)
		{
            if(0 != (crc & 1))
			{
                crc = (crc >> 1) ^ 0xEDB88320UL;
            }
			else
			{
                crc >>= 1;
            }
        }
    }

    return crc ^ 0xFFFFFFFFUL;
}

/* 写0x0501升级请求，Bootloader随后接收0x0502/0x0503。 */
bootloader_port_status_t bootloader_port_request_bootloader_upgrade(void)
{
    uint32_t index;
    bootloader_port_status_t status;
    bootloader_port_parameter_t *parameter;

    for(index = 0; index < BOOTLOADER_PORT_PARAM_SIZE; index++)
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
    parameter->boot_param.updateProgress = 0;
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

/* 复位进入Bootloader。 */
void bootloader_port_request_upgrade_reset(void)
{
    __set_FAULTMASK(1);
    NVIC_SystemReset();
}

/* 读取Boot参数页中的user_config。 */
bootloader_port_status_t bootloader_port_read_user_config(uint8_t *buf, uint16_t size)
{
    const bootloader_port_parameter_t *flash_map;
    uint16_t index;

    if((!buf) || (0 == size) || (size > BOOTLOADER_PORT_USER_CONFIG_SIZE)) 
	{
        return BOOTLOADER_PORT_STATUS_BAD_PARAM;
    }

    flash_map = (const bootloader_port_parameter_t *)BOOTLOADER_PORT_PARAM_ADDR;
    for(index = 0; index < size; index++) 
	{
        buf[index] = flash_map->user_config[index];
    }

    return BOOTLOADER_PORT_STATUS_OK;
}

/* 回写user_config，保留Bootloader控制字段。 */
bootloader_port_status_t bootloader_port_write_user_config(const uint8_t *buf, uint16_t size)
{
    uint32_t index;
    bootloader_port_status_t status;
    bootloader_port_parameter_t *parameter;

    if((!buf) || (0 == size) || (size > BOOTLOADER_PORT_USER_CONFIG_SIZE)) 
	{
        return BOOTLOADER_PORT_STATUS_BAD_PARAM;
    }

    for(index = 0; index < BOOTLOADER_PORT_PARAM_SIZE; index++) 
	{
        g_bootloader_port_param_buffer[index] =
            *(volatile uint8_t *)(BOOTLOADER_PORT_PARAM_ADDR + index);
    }

    parameter = (bootloader_port_parameter_t *)g_bootloader_port_param_buffer;
    for(index = 0; index < (uint32_t)size; index++) 
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
