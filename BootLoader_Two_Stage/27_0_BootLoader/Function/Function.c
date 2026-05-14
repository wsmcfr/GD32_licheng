/************************************************************
 * 版权：2025CIMC Copyright。
 * 文件：Function.c
 * 作者: Jialei Zhao
 * 平台: 2025CIMC IHD-V04
 * 版本: Jialei Zhao     2026/2/4     V0.01    original
************************************************************/


#include "Function.h"
#include <time.h>
#include "rom.h"
#include "usart.h"
#include "bootloader.h"
#include "BootConfig.h"

/************************* 宏定义 *************************/

typedef uint8_t bool;
#define TRUE 1
#define FALSE 0

typedef void (*pFunction)(void);

pFunction jump2app;

/* BootLoader 参数区固定占用 4KB，位于 0x0800C000。 */
#define CONFIG_SIZE                 (1024U * 4U)

/* 参数区魔术字和升级状态值，必须与 BootConfig.h 中的参数区定义保持一致。 */
#define BOOT_PARAM_MAGIC            (0x5AA5C33CUL)
#define BOOT_PARAM_UPDATE_FLAG      (0x5AU)
#define BOOT_PARAM_UPDATING         (0x01U)

/* 当前工程作为 App 时的固定运行地址，必须与 App 工程 IROM/Scatter 地址一致。 */
#define BOOT_APP_START_ADDR         (0x0800D000UL)
#define BOOT_APP_REGION_SIZE        (0x00063000UL)

/* 当前工程的下载缓存区从 App 区末尾开始，占用内部 Flash 最后 64KB，旧 App 先把新固件写到这里，再复位交给 BootLoader 搬运。 */
#define APP_DOWNLOAD_ADDR           (0x08070000UL)
#define APP_DOWNLOAD_MAX_SIZE       (64U * 1024U)

/* GD32F4xx 本工程按 4KB 页粒度擦写 App 区，分块搬运避免占用大 RAM 缓冲区。 */
#define FLASH_PAGE_SIZE             (4096U)
#define BOOT_COPY_CHUNK_SIZE        (1024U)

/* 当前 App 使用主 SRAM 作为 MSP 初值范围，Scatter 中 IRAM1 为 0x20000000~0x2002FFFF。 */
#define BOOT_SRAM_START_ADDR        (0x20000000UL)
#define BOOT_SRAM_END_ADDR          (0x20030000UL)

/************************ 变量定义 ************************/

/*
 * Parameter_t 是“整个 4KB 参数区在 RAM 中的镜像”。
 * BootLoader 读取 0x0800C000 开始的 4KB 数据后，会把它解释成这个总结构。
 * 这样上层代码就可以按字段直接访问升级标志、用户配置和校准信息，
 * 而不需要手动计算每个偏移地址。
 */
typedef struct __attribute__((packed)) Parameter_SUM
{
	BootParam_t BootParam;
	BootParam_t BootParam_Reserved;
	UpdateLog_t UpdateLog;
	UserConfig_t UserConfig;
	CalibData_t CalibData;
}Parameter_t;

/* 当前启动周期内的参数区 RAM 副本。BootLoader 对参数区的所有判断都基于它。 */
Parameter_t my_param_sum = { 0 };

/* config_buf 用来暂存整块 4KB 参数区原始字节流。 */
static uint8_t config_buf[CONFIG_SIZE] = { 0 };

/* app_copy_buf 是搬运 App 时的 1KB 中转缓冲区。 */
static uint8_t app_copy_buf[BOOT_COPY_CHUNK_SIZE] = { 0 };

/************************ 函数定义 ************************/

static void Analysis_ConfigForAddr(void);

static bool Download_Transport(uint32_t DownLoad_Addr);

static bool boot_is_valid_app_size(uint32_t app_size);

static bool boot_is_valid_app_start(uint32_t app_start_addr);

static bool boot_is_valid_app_stack(uint32_t stack_addr);

static bool boot_is_valid_app_entry(uint32_t entry_addr);

static uint32_t crc32_update(uint32_t crc , uint8_t* data , uint32_t len);

uint32_t crc32_calc(uint8_t* data , uint32_t len);

//!软复位
void mcu_software_reset(void);

//!跳转程序
void jump_to_app(void);


/************************************************************
 * Function :       System_Init
 * Comment  :       初始化 BootLoader 运行所需的最小硬件环境。
 *                  当前流程只初始化两类资源：
 *                  1. SysTick：提供 delay_1ms() 所依赖的毫秒节拍；
 *                  2. USART0：提供 BootLoader 日志输出和串口接收能力。
 *                  之所以只初始化这两项，是因为 BootLoader 应尽量保持精简，
 *                  只准备自己真正会用到的底层资源。
 * Parameter:       无参数。
 * Return   :       无返回值。
 * Author   :       Jialei Zhao
 * Date     :       2026-02-4 V0.1 original
************************************************************/

void System_Init(void)
{
	systick_config();

	my_usart_init();
}

/************************************************************
 * Function :       UsrFunction
 * Comment  :       BootLoader 的总调度函数。
 *                  上电后所有业务判断都从这里开始，主流程如下：
 *                  1. 读取参数区 4KB 内容；
 *                  2. 解析升级标志、App 版本、App 地址等字段；
 *                  3. 若参数区表明“已有新固件待搬运”，则把下载区固件复制到正式 App 区；
 *                  4. 搬运成功后清除升级标志并复位，让新 App 在干净环境中启动；
 *                  5. 如果没有升级任务，则直接检查并跳转到当前 App。
 * Parameter:       无参数。
 * Return   :       无返回值。
 * Author   :       Jialei Zhao
 * Date     :       2026-02-4 V0.1 original
************************************************************/
void UsrFunction(void)
{
	printf("BootLoader : start compare config\r\n");

	/* 先把 Flash 参数区完整读入 RAM，后续所有判断都基于这个 RAM 镜像。 */
	Analysis_ConfigForAddr();

	/* 把裸字节流解释成参数区总结构，便于按字段访问升级状态和 App 信息。 */
	memcpy(&my_param_sum , config_buf , sizeof(Parameter_t));

	/*
	 * 这里重新从当前 App 向量表中刷新 appStartAddr / appStackAddr / appEntryAddr。
	 * 这样做的原因是：
	 * 1. 参数区中的入口信息可能是旧值；
	 * 2. 真正可信的入口信息应以 App 区向量表内容为准；
	 * 3. 后续打印日志和跳转判断都需要用到这三个值。
	 */
	my_param_sum.BootParam.appStartAddr = BOOT_APP_START_ADDR;
	my_param_sum.BootParam.appStackAddr = *(__IO uint32_t*)(my_param_sum.BootParam.appStartAddr + 0);
	my_param_sum.BootParam.appEntryAddr = *(__IO uint32_t*)(my_param_sum.BootParam.appStartAddr + 4);

	printf("BootLoader : appStackAddr:0x%08x\r\n" , my_param_sum.BootParam.appStackAddr);
	printf("BootLoader : appEntryAddr:0x%08x\r\n" , my_param_sum.BootParam.appEntryAddr);
	printf("BootLoader : appStartAddr:0x%08x\r\n" , my_param_sum.BootParam.appStartAddr);

	printf("BootLoader : appVersion:0x%08x\r\n" , my_param_sum.BootParam.appVersion);
	printf("BootLoader : updateStatus:0x%02x\r\n" , my_param_sum.BootParam.updateStatus);
	printf("BootLoader : updateFlag:0x%02x\r\n" , my_param_sum.BootParam.updateFlag);
	printf("BootLoader : magicWord:0x%08x\r\n" , my_param_sum.BootParam.magicWord);

	/* 保留 1 秒日志观察窗口，方便上电时通过串口终端看到当前状态。 */
	delay_1ms(1000);


	if (my_param_sum.BootParam.magicWord != BOOT_PARAM_MAGIC)
	{
		/*
		 * 参数区魔术字错误，说明参数区可能未初始化、被擦空或内容损坏。
		 * 这里不直接判死刑，而是仍然尝试跳转现有 App，因为“参数区坏了”不等于“App 一定坏了”。
		 */
		printf("BootLoader : param magic is false\r\n");
		goto BootJump;
	}

	/*
	 * updateFlag + updateStatus 同时满足时，表示旧 App 已经把新固件放进下载缓存区，
	 * 当前这次上电的任务就是把下载区的新固件搬到正式 App 区。
	 */
	if (my_param_sum.BootParam.updateStatus == BOOT_PARAM_UPDATING && my_param_sum.BootParam.updateFlag == BOOT_PARAM_UPDATE_FLAG)
	{
		printf("BootLoader : app is updating, need to copy param to app addr\r\n");

		bool Download_Transport_Result = Download_Transport(APP_DOWNLOAD_ADDR);

		/*
		 * 搬运完成后重新读取一遍参数区。
		 * 这样可以避免后续仅依赖旧的 RAM 副本，并为重新回写统计信息做准备。
		 */
		for (uint16_t i = 0; i < CONFIG_SIZE; i++)
		{
			config_buf[i] = internal_flash_read_Char(BOOT_CONFIG_ADDR + i);
		}
		memcpy(&my_param_sum , config_buf , sizeof(Parameter_t));

		if (Download_Transport_Result == TRUE)
		{
			/* 升级成功后重新刷新 App 向量表信息，确保参数区写回的是新 App 的真实入口。 */
			my_param_sum.BootParam.appStartAddr = BOOT_APP_START_ADDR;
			my_param_sum.BootParam.appStackAddr = *(__IO uint32_t*)(my_param_sum.BootParam.appStartAddr + 0);
			my_param_sum.BootParam.appEntryAddr = *(__IO uint32_t*)(my_param_sum.BootParam.appStartAddr + 4);

			/* 清空升级标志，防止下一次上电又重复搬运同一份固件。 */
			my_param_sum.BootParam.updateStatus = 0x00;
			my_param_sum.BootParam.updateFlag = 0x00;

			/* 升级成功次数加一，供后续诊断和日志使用。 */
			my_param_sum.BootParam.updateCount++;

			printf("BootLoader : app update success\r\n");
		}
		else
		{
			/* 搬运失败时记录失败统计，方便后面排查“反复重启却进不了新 App”的问题。 */
			printf("BootLoader : app update fail\r\n");
			my_param_sum.BootParam.resetCount++;
			my_param_sum.BootParam.bootFailCount++;
		}

		/*
		 * 当前实现按整块 4KB 参数区重写，而不是只改几个字节。
		 * 原因是 Flash 不能像 RAM 那样直接覆盖写，先读整块到 RAM、修改字段、
		 * 再整体擦除回写，是最直观也最安全的做法。
		 */
		memcpy(config_buf , &my_param_sum , sizeof(Parameter_t));
		internal_flash_erase(BOOT_CONFIG_ADDR);
		internal_flash_write_str_Char(BOOT_CONFIG_ADDR , config_buf , CONFIG_SIZE);

		/*
		 * 这里故意不直接 jump_to_app()，而是执行一次软件复位。
		 * 这样新 App 会在更接近“真实上电”的干净环境里启动，
		 * 避免继承 BootLoader 的部分外设或中断状态。
		 */
		mcu_software_reset();
	}
	else
	{
	BootJump:
		/* 正常路径：没有待升级任务，就直接检查并跳转到当前 App。 */
		jump_to_app();
	}


	/* 理论上不会走到这里，保留死循环作为防御性兜底。 */
	while (1)
	{

	}
}
/************************************************************
 * Function :       Analysis_ConfigForAddr
 * Comment  :       从 BootLoader 参数区读取完整 4KB 配置内容到 RAM 缓冲区。
 *                  读取后由调用者再按 Parameter_t 结构解析升级标志、App 大小、
 *                  CRC 和 App 地址等字段。
 * Parameter:       null
 * Return   :       null
 * Author   :       Jialei Zhao
 * Date     :       2026-02-4 V0.1 original
************************************************************/
static void Analysis_ConfigForAddr(void)
{
	for (uint16_t i = 0; i < CONFIG_SIZE; i++)
	{
		/* 参数区总大小只有 4KB，按字节读入实现简单直观，便于后续统一解析。 */
		config_buf[i] = internal_flash_read_Char(BOOT_CONFIG_ADDR + i);
	}
}

/************************************************************
 * Function :       boot_is_valid_app_size
 * Comment  :       检查参数区记录的 App 固件长度是否在 BootLoader 可处理范围内。
 *                  这里同时限制下载缓存区和 App 运行区，避免搬运时越界覆盖参数区、
 *                  下载缓存区或 Flash 尾部空间。
 * Parameter:       app_size 当前待搬运 App 的字节数，来源于参数区 BootParam.appSize。
 * Return   :       TRUE 表示长度合法；FALSE 表示长度为 0 或超过允许范围。
 * 修改者  :       OpenAI
 * 日期    :       2026-05-12 V0.2 适配当前 App
 ************************************************************/
static bool boot_is_valid_app_size(uint32_t app_size)
{
	if (app_size == 0U)
	{
		printf("BootLoader : app size is zero\r\n");
		return FALSE;
	}

	if (app_size > APP_DOWNLOAD_MAX_SIZE)
	{
		printf("BootLoader : app size over download buffer, size:%d max:%d\r\n" , app_size , APP_DOWNLOAD_MAX_SIZE);
		return FALSE;
	}

	if (app_size > BOOT_APP_REGION_SIZE)
	{
		printf("BootLoader : app size over app region, size:%d max:%d\r\n" , app_size , BOOT_APP_REGION_SIZE);
		return FALSE;
	}

	return TRUE;
}

/************************************************************
 * Function :       boot_is_valid_app_start
 * Comment  :       检查参数区记录的 App 起始地址是否等于当前工程约定的 App 区地址。
 *                  当前 BootLoader 只服务于 0x0800D000 起始的 App，拒绝其它地址可以
 *                  防止错误参数把固件写到 BootLoader 区或参数区。
 * Parameter:       app_start_addr 参数区 BootParam.appStartAddr 字段。
 * Return   :       TRUE 表示地址匹配；FALSE 表示地址不是当前 App 起始地址。
 * 修改者  :       OpenAI
 * 日期    :       2026-05-12 V0.2 适配当前 App
 ************************************************************/
static bool boot_is_valid_app_start(uint32_t app_start_addr)
{
	if (app_start_addr != BOOT_APP_START_ADDR)
	{
		printf("BootLoader : app start addr invalid, addr:0x%08x expect:0x%08x\r\n" , app_start_addr , BOOT_APP_START_ADDR);
		return FALSE;
	}

	return TRUE;
}

/************************************************************
 * Function :       boot_is_valid_app_stack
 * Comment  :       检查 App 向量表第 0 项 MSP 初值是否落在主 SRAM 范围内，并且满足
 *                  Cortex-M 栈指针 8 字节对齐要求。该检查用于避免把空 Flash、
 *                  错误文件或损坏镜像当成 App 跳转。
 * Parameter:       stack_addr App 向量表第 0 项，即复位后 MSP 初值。
 * Return   :       TRUE 表示 MSP 合法；FALSE 表示 MSP 不在 SRAM 范围或未对齐。
 * 修改者  :       OpenAI
 * 日期    :       2026-05-12 V0.2 适配当前 App
 ************************************************************/
static bool boot_is_valid_app_stack(uint32_t stack_addr)
{
	if (stack_addr < BOOT_SRAM_START_ADDR || stack_addr > BOOT_SRAM_END_ADDR)
	{
		printf("BootLoader : app stack addr invalid, stack:0x%08x\r\n" , stack_addr);
		return FALSE;
	}

	if ((stack_addr & 0x7U) != 0U)
	{
		printf("BootLoader : app stack addr not aligned, stack:0x%08x\r\n" , stack_addr);
		return FALSE;
	}

	return TRUE;
}

/************************************************************
 * Function :       boot_is_valid_app_entry
 * Comment  :       检查 App 向量表第 1 项复位入口是否落在 App Flash 区内，并且最低位为 1。
 *                  Cortex-M 使用 Thumb 指令集，函数入口地址最低位应为 1；去掉最低位后的
 *                  实际地址必须位于当前 App 区。
 * Parameter:       entry_addr App 向量表第 1 项，即 Reset_Handler 入口地址。
 * Return   :       TRUE 表示入口地址合法；FALSE 表示入口地址越界或不是 Thumb 入口。
 * 修改者  :       OpenAI
 * 日期    :       2026-05-12 V0.2 适配当前 App
 ************************************************************/
static bool boot_is_valid_app_entry(uint32_t entry_addr)
{
	uint32_t entry_base = entry_addr & 0xFFFFFFFEUL;

	if ((entry_addr & 0x1U) == 0U)
	{
		printf("BootLoader : app entry is not thumb addr, entry:0x%08x\r\n" , entry_addr);
		return FALSE;
	}

	if (entry_base < BOOT_APP_START_ADDR || entry_base >= (BOOT_APP_START_ADDR + BOOT_APP_REGION_SIZE))
	{
		printf("BootLoader : app entry addr invalid, entry:0x%08x\r\n" , entry_addr);
		return FALSE;
	}

	return TRUE;
}
/************************************************************
 * Function :       Download_Transport
 * Comment  :       将下载缓存区中的新 App 固件搬运到正式 App 区。
 *                  主要流程：
 *                  1. 校验参数区中的 App 大小和目标地址，防止越界写 Flash。
 *                  2. 按 App 实际大小计算需要擦除的 4KB 页数。
 *                  3. 分块从下载缓存区读取数据，再写入 App 区，避免使用大 RAM 缓冲区。
 *                  4. 从写入后的 App 区重新读取数据计算 CRC32，确认 Flash 写入结果正确。
 * Parameter:       DownLoad_Addr 下载缓存区起始地址，当前工程固定为 0x08070000。
 * Return   :       TRUE 表示搬运和 CRC 校验成功；FALSE 表示参数非法、写入越界或 CRC 不一致。
 * Author   :       Jialei Zhao
 * Date     :       2026-02-4 V0.1 original
 * 修改者  :       OpenAI
 * 日期    :       2026-05-12 V0.2 适配当前 App
************************************************************/
static bool Download_Transport(uint32_t DownLoad_Addr)
{
	uint32_t app_size = my_param_sum.BootParam.appSize;
	uint32_t app_start_addr = my_param_sum.BootParam.appStartAddr;
	uint32_t erase_pages = 0U;
	uint32_t copied_size = 0U;
	uint32_t app_crc32 = 0xFFFFFFFFUL;

	printf("BootLoader : appSize:%d\r\n" , app_size);

	if (DownLoad_Addr != APP_DOWNLOAD_ADDR)
	{
		/* 当前工程只允许从固定下载区搬运，防止错误参数把任意 Flash 区当成固件源。 */
		printf("BootLoader : download addr invalid, addr:0x%08x expect:0x%08x\r\n" , DownLoad_Addr , APP_DOWNLOAD_ADDR);
		return FALSE;
	}

	if (boot_is_valid_app_size(app_size) == FALSE)
	{
		return FALSE;
	}

	if (boot_is_valid_app_start(app_start_addr) == FALSE)
	{
		return FALSE;
	}

	/*
	 * 按实际固件大小擦除 App 区，不能再固定擦除 3 页。
	 * 当前 App 已超过 12KB，固定 3 页会导致后续 Flash 写入到未擦除区域。
	 */
	erase_pages = (app_size + FLASH_PAGE_SIZE - 1U) / FLASH_PAGE_SIZE;
	printf("BootLoader : erase app pages:%d\r\n" , erase_pages);
	for (uint32_t i = 0U; i < erase_pages; i++)
	{
		/* 必须先擦再写，否则 Flash 中未擦区域会导致写入失败或保留脏数据。 */
		internal_flash_erase(app_start_addr + i * FLASH_PAGE_SIZE);
	}

	/*
	 * 分块搬运，避免把整个 App 读入 RAM。
	 * 这样当前 App 后续变大时，只要不超过下载缓存区和 App 区，BootLoader 仍能处理。
	 */
	while (copied_size < app_size)
	{
		uint32_t chunk_size = app_size - copied_size;

		if (chunk_size > BOOT_COPY_CHUNK_SIZE)
		{
			chunk_size = BOOT_COPY_CHUNK_SIZE;
		}

		for (uint32_t i = 0U; i < chunk_size; i++)
		{
			/* 先从下载区读到 RAM 中转缓冲，避免直接跨区域边读边写带来的控制复杂度。 */
			app_copy_buf[i] = internal_flash_read_Char(DownLoad_Addr + copied_size + i);
		}

		/* 把当前这一块写到正式 App 区对应偏移。 */
		internal_flash_write_str_Char(app_start_addr + copied_size , app_copy_buf , chunk_size);
		copied_size += chunk_size;
	}

	/*
	 * 从正式 App 区重新读取并计算 CRC，而不是只对下载缓存区计算。
	 * 这样可以发现擦除不足、Flash 写入失败、地址写错等搬运阶段问题。
	 */
	copied_size = 0U;
	while (copied_size < app_size)
	{
		uint32_t chunk_size = app_size - copied_size;

		if (chunk_size > BOOT_COPY_CHUNK_SIZE)
		{
			chunk_size = BOOT_COPY_CHUNK_SIZE;
		}

		for (uint32_t i = 0U; i < chunk_size; i++)
		{
			/* 注意这里读的是“正式 App 区”，而不是下载区。 */
			app_copy_buf[i] = internal_flash_read_Char(app_start_addr + copied_size + i);
		}

		app_crc32 = crc32_update(app_crc32 , app_copy_buf , chunk_size);
		copied_size += chunk_size;
	}
	app_crc32 ^= 0xFFFFFFFFUL;

	printf("BootLoader : appCRC32:0x%08x , app_crc32:0x%08x\r\n" , my_param_sum.BootParam.appCRC32 , app_crc32);
	if (app_crc32 == my_param_sum.BootParam.appCRC32)
	{
		printf("app crc32 check pass\r\n");
		return TRUE;
	}
	else
	{
		printf("app crc32 check fail\r\n");
		return FALSE;
	}
}
/************************************************************
 * Function :       crc32_calc
 * Comment  :       对一段连续内存计算标准 CRC32 校验值。
 *                  该函数保留给参数区或小块数据使用；BootLoader 搬运 App 时使用
 *                  crc32_update() 分块累计，避免为了 CRC 再占用整包 RAM 缓冲区。
 * Parameter:       data 数据指针，指向需要参与 CRC32 计算的内存首地址。
 * Parameter:       len 数据长度，单位为字节。
 * Return   :       返回计算完成后的 CRC32 值。
 * Author   :       Jialei Zhao
 * Date     :       2026-02-4 V0.1 original
************************************************************/
uint32_t crc32_calc(uint8_t* data , uint32_t len)
{
	uint32_t crc = 0xFFFFFFFFUL;

	/* 先用统一的增量接口计算，再在末尾做标准 CRC32 收尾异或。 */
	crc = crc32_update(crc , data , len);

	return crc ^ 0xFFFFFFFFUL;
}

/************************************************************
 * Function :       crc32_update
 * Comment  :       在已有 CRC32 中间值基础上继续累计一段数据。
 *                  传入初始值 0xFFFFFFFF，按块重复调用，所有数据处理完后再异或
 *                  0xFFFFFFFF，即可得到与 crc32_calc() 相同的结果。
 * Parameter:       crc 当前 CRC32 中间值，首次调用应传 0xFFFFFFFF。
 * Parameter:       data 当前数据块指针。
 * Parameter:       len 当前数据块长度，单位为字节。
 * Return   :       返回更新后的 CRC32 中间值，调用者可继续传入下一块数据。
 * 修改者  :       OpenAI
 * 日期    :       2026-05-12 V0.2 适配当前 App
 ************************************************************/
static uint32_t crc32_update(uint32_t crc , uint8_t* data , uint32_t len)
{
	uint32_t i , j;

	for (i = 0U; i < len; i++)
	{
		/* 逐字节纳入 CRC；与上位机工具保持一致时，BootLoader 才能正确比较固件校验值。 */
		crc ^= data[i];
		for (j = 0U; j < 8U; j++)
		{
			if (crc & 1)
				crc = (crc >> 1) ^ 0xEDB88320;
			else
				crc >>= 1;
		}
	}

	return crc;
}
/************************************************************
 * Function :       mcu_software_reset
 * Comment  :       触发一次 MCU 软件复位。
 *                  升级成功后不直接跳 App，而是通过复位重新走一遍标准启动流程，
 *                  可以最大程度减少 BootLoader 运行现场对新 App 的影响。
 * Parameter:       无参数。
 * Return   :       无返回值。
 * Author   :       Jialei Zhao
 * Date     :       2026-02-4 V0.1 original
************************************************************/
void mcu_software_reset(void)
{
	/* 先屏蔽异常响应，再触发系统复位，减少复位前被其它异常打断的概率。 */
	__set_FAULTMASK(1);
	NVIC_SystemReset();
}
/************************************************************
 * Function :       iap_load_app
 * Comment  :       从指定 App 起始地址加载并跳转到 App。
 *                  跳转前会校验 MSP 和 Reset_Handler，关闭 SysTick，清除 NVIC 使能和挂起位，
 *                  设置 VTOR 和 MSP，最后跳转到 App 的复位入口。
 * Parameter:       appxaddr App 向量表起始地址，当前工程应为 0x0800D000。
 * Return   :       无返回值。若 App 合法会直接跳走；若非法或跳转失败则停在错误循环。
 * Author   :       Jialei Zhao
 * Date     :       2026-02-4 V0.1 original
 * 修改者  :       OpenAI
 * 日期    :       2026-05-12 V0.2 适配当前 App
 ************************************************************/
void iap_load_app(uint32_t appxaddr)
{
	uint32_t app_stack_addr = *(__IO uint32_t*)appxaddr;
	uint32_t app_entry_addr = *(__IO uint32_t*)(appxaddr + 4U);

	if (boot_is_valid_app_start(appxaddr) == FALSE)
	{
		/* 起始地址不合法时直接停机，避免跳到错误区域造成不可预期后果。 */
		while (1);
	}

	if (boot_is_valid_app_stack(app_stack_addr) == FALSE || boot_is_valid_app_entry(app_entry_addr) == FALSE)
	{
		printf("BootLoader : no valid app image\r\n");
		while (1);
	}

	printf("BootLoader : jump app vtor:0x%08x msp:0x%08x entry:0x%08x\r\n" , appxaddr , app_stack_addr , app_entry_addr);

	/* 关闭所有中断，避免跳转过程中仍执行 BootLoader 的外设 ISR。 */
	__disable_irq();

	/* 关闭 SysTick，避免 App 刚切换向量表时继承 BootLoader 的节拍中断。 */
	SysTick->CTRL = 0;
	SysTick->LOAD = 0;
	SysTick->VAL = 0;

	/*
	 * SysTick 挂起位不在 NVIC->ICPR 外设中断数组里。
	 * 这里单独清掉 PENDST，避免 App 一恢复全局中断就处理 BootLoader 遗留的节拍异常。
	 */
	SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;

	/* 清除所有 NVIC 中断使能和挂起位，给 App 一个干净的中断现场。 */
	for (uint32_t i = 0U; i < 8U; i++)
	{
		NVIC->ICER[i] = 0xFFFFFFFFUL;
		NVIC->ICPR[i] = 0xFFFFFFFFUL;
	}

	/* 等待 NVIC/SysTick/VTOR 相关写入完成后再设置栈并跳转。 */
	__DSB();
	__ISB();

	/* 设置新的中断向量表。App 侧仍会再次设置 VTOR，用于兼容直接调试 App 的场景。 */
	SCB->VTOR = appxaddr;

	/*
	 * 先把入口函数指针写入全局变量，再切换 MSP。
	 * 切换 MSP 之后不能再依赖当前函数的局部栈变量，否则编译器在低优化等级下
	 * 可能从“新的 App 栈”去取 BootLoader 旧栈里的局部变量，导致跳转地址异常。
	 */
	jump2app = (pFunction)app_entry_addr;

	/*
	 * 确保进入 App 时使用 MSP 作为线程栈，保持和硬复位进入 Reset_Handler 的现场一致。
	 * 当前 BootLoader 本身没有使用 PSP，但这里显式复位 CONTROL 可避免后续扩展引入隐患。
	 */
	__set_CONTROL(0U);
	__ISB();

	/* 设置新的栈指针。普通函数跳转不会自动切换 MSP，必须手动加载 App 向量表第 0 项。 */
	__set_MSP(app_stack_addr);

	/* 跳转到应用程序。入口地址来自 App 向量表第 1 项。 */
	jump2app();

	/* 如果执行到这里说明跳转失败。 */
	printf("jump to app fail\r\n");
	while (1);
}

/************************************************************
 * Function :       jump_to_app
 * Comment  :       根据参数区和 App 向量表信息决定是否跳转到当前 App。
 *                  该函数只允许跳转到固定 App 区，避免错误参数导致跳入 BootLoader、
 *                  参数区或下载缓存区。
 * Parameter:       null
 * Return   :       无返回值。合法时跳转 App；非法时停在错误循环。
 * Author   :       Jialei Zhao
 * Date     :       2026-02-4 V0.1 original
 * 修改者  :       OpenAI
 * 日期    :       2026-05-12 V0.2 适配当前 App
 ************************************************************/
void jump_to_app(void)
{
	uint32_t app_start_addr = BOOT_APP_START_ADDR;
	uint32_t app_stack_addr = *(__IO uint32_t*)(app_start_addr + 0U);
	uint32_t app_entry_addr = *(__IO uint32_t*)(app_start_addr + 4U);

	/* 把当前 App 的真实向量表信息同步到 RAM 参数结构，便于日志和后续统一使用。 */
	my_param_sum.BootParam.appStartAddr = app_start_addr;
	my_param_sum.BootParam.appStackAddr = app_stack_addr;
	my_param_sum.BootParam.appEntryAddr = app_entry_addr;

	if (boot_is_valid_app_stack(app_stack_addr) == TRUE && boot_is_valid_app_entry(app_entry_addr) == TRUE)
	{
		/* 只有当前 App 镜像看起来合法，才允许真正切换执行流。 */
		iap_load_app(app_start_addr);
	}
	else
	{
		printf("BootLoader : app image invalid, stop\r\n");
		while (1);
	}

}

/****************************End*****************************/
