/************************************************************
 * 版权：2025CIMC Copyright。
 * 文件：BootConfig.h
 * 作者: Jialei Zhao
 * 平台: 2025CIMC IHD-V04
 * 版本: Jialei Zhao     2026/2/4     V0.01    original
************************************************************/
#ifndef __BOOTCONFIG_H__
#define __BOOTCONFIG_H__
/************************* 头文件 *************************/
#include "HeaderFiles.h"
/************************* 宏定义 *************************/

/*
 * BootLoader 参数区固定放在 0x0800C000，大小 4KB。
 * 这 4KB 不是只给一个结构体使用，而是被拆成：
 * 1. 主参数区；
 * 2. 预留参数区；
 * 3. 升级日志区；
 * 4. 用户配置区；
 * 5. 校准数据区。
 *
 * BootLoader 和 App 必须对这块区域的布局完全达成一致，否则 BootLoader 读到的
 * 升级标志、App 长度、CRC32 等信息就会错位。
 */
#define BOOT_CONFIG_ADDR 0x0800C000
/************************ 变量定义 ************************/

/**
 * @brief BootLoader 主参数区结构体
 * @note  存储在 0x0800C000，大小 256 字节
 *
 * 这是 BootLoader 启动时最先关心的结构体。
 * 其中真正驱动升级流程的关键字段主要有：
 * 1. magicWord：判断参数区是否已被正确初始化；
 * 2. updateFlag / updateStatus：判断是否存在待搬运的新 App；
 * 3. appSize / appCRC32 / appVersion：描述待升级 App；
 * 4. appStartAddr / appEntryAddr / appStackAddr：描述正式 App 的运行入口；
 * 5. resetCount / bootFailCount / updateCount：记录运行和升级统计信息。
 *
 * 主参数区范围：0x0800C000 - 0x0800C0FF。
 */
typedef struct __attribute__((packed))
{
	// ============ [0-15] 基础标识 (16字节) ============
	uint32_t magicWord;         // [0-3]   魔术字: 						        0x5AA5C33C
	uint16_t version;           // [4-5]   参数版本: 					        0x0001
	uint16_t structSize;        // [6-7]   结构体大小: 					        256
	uint32_t buildDate;         // [8-11]  参数创建日期(BCD) 			        
	uint32_t reserved0;         // [12-15] 保留 						

	// ============ [16-31] 升级控制 (16字节) ============
	uint8_t  updateFlag;        // [16]    升级标志: 0x5A=升级, 0x00=正常 ★	    0x00
	uint8_t  updateMode;        // [17]    升级模式: 0x01 = 串口			    0x01	
	uint8_t  updateStatus;      // [18]    升级状态 							0x01 -- 正在升级 0x00 -- 升级完成
	uint8_t  updateProgress;    // [19]    升级进度: 						
	uint32_t updateCount;       // [20-23] 升级次数累计						
	uint32_t lastUpdateTime;    // [24-27] 最后升级时间戳					
	uint32_t reserved1;         // [28-31] 保留								

	// ============ [32-63] App固件信息 (32字节) ============
	uint32_t appSize;           // [32-35] App大小(字节)						
	uint32_t appCRC32;          // [36-39] App CRC32校验值 ★						
	uint32_t appVersion;        // [40-43] App版本(主.次.补丁.构建)			
	uint32_t appBuildDate;      // [44-47] App编译日期					
	uint32_t appStartAddr;      // [48-51] App起始地址: 				        0x0800D000
	uint32_t appEntryAddr;      // [52-55] App入口地址					        0x0800D000
	uint32_t appStackAddr;      // [56-59] App栈地址(MSP)				        
	uint32_t reserved2;         // [60-63] 保留							

	// ============ [64-79] Bootloader信息 (16字节) ============
	uint32_t bootVersion;       // [64-67] Bootloader版本   			0x01
	uint32_t bootCRC32;         // [68-71] Bootloader CRC32				
	uint32_t bootSize;          // [72-75] Bootloader大小				4096
	uint32_t reserved3;         // [76-79] 保留 						

	// ============ [80-111] 系统状态 (32字节) ============
	uint32_t runTimestamp;      // [80-83]  最后运行时间				
	uint32_t resetCount;        // [84-87]  总复位次数					
	uint16_t lastResetReason;   // [88-89]  最后复位原因				
	uint16_t bootFailCount;     // [90-91]  启动失败次数 ★				
	uint32_t totalRuntime;      // [92-95]  总运行时间(秒)				
	uint32_t wdtResetCount;     // [96-99]  看门狗复位次数				
	uint32_t hardFaultCount;    // [100-103] HardFault次数				
	uint32_t lastErrorCode;     // [104-107] 最后错误码						
	uint32_t reserved4;         // [108-111] 保留 						

	// ============ [112-143] 备份固件信息 (32字节) ============
	uint32_t backupFlag;        // [112-115] 备份标志: 					
	uint32_t backupAddr;        // [116-119] 备份地址				0x0800C100
	uint32_t backupSize;        // [120-123] 备份大小	 			256
	uint32_t backupCRC32;       // [124-127] 备份CRC32  			
	uint32_t backupVersion;     // [128-131] 备份版本号				
	uint32_t backupDate;        // [132-135] 备份时间  				
	uint32_t reserved5[2];      // [136-143] 保留 					

	// ============ [144-159] 安全相关 (16字节 - 保留) ============
	uint32_t securityFlag;      // [144-147] 安全标志 				
	uint32_t encryptKey;        // [148-151] 加密密钥索引 				
	uint32_t authCode;          // [152-155] 认证码 				
	uint32_t reserved6;         // [156-159] 保留 						

	// ============ [160-207] 设备信息 (48字节) ============
	uint8_t  deviceID[16];      // [160-175] 设备唯一ID			"202601301528"
	uint8_t  productModel[16];  // [176-191] 产品型号			"000000000001"
	uint8_t  serialNumber[16];  // [192-207] 序列号				"100000000000"

	// ============ [208-239] 硬件配置 (32字节) ============
	uint32_t hwVersion;         // [208-211] 硬件版本	  			1
	uint32_t cpuID;             // [212-215] CPU ID					1
	uint16_t flashSize;         // [216-217] Flash容量(KB) 			0x400
	uint16_t ramSize;           // [218-219] RAM容量(KB) 			0x2F
	uint32_t clockFreq;         // [220-223] 时钟频率(Hz) 			240000000
	uint32_t reserved7[4];      // [224-239] 保留						

	// ============ [240-255] 校验与结束 (16字节) ============
	uint32_t reserved8[2];      // [240-247] 保留 					
	uint32_t paramCRC32;        // [248-251] 整个参数区CRC32 ★ 			
	uint32_t tailMagic;         // [252-255] 尾部魔术字:			0xA5A5C3C3

} BootParam_t;  // 总大小: 256字节


/**
 * @brief 升级日志区结构体
 * @note  当前定义是整块 1024 字节，不是单独 32 字节日志项。
 *
 * 这块区域原本预留给升级记录扩展使用。当前 BootLoader 主流程并没有读写它，
 * 但它已经占用了参数区中的固定地址空间，因此 App 与后续工具也必须保持同样布局。
 *
 * 地址范围：0x0800C200 - 0x0800C5FF。
 */
typedef struct __attribute__((packed))
{
	uint32_t timestamp;         // 升级时间戳 							
	uint32_t oldVersion;        // 旧版本 								
	uint32_t newVersion;        // 新版本 								
	uint32_t newSize;           // 新固件大小 							
	uint32_t newCRC32;          // 新固件CRC 							
	uint8_t  status;            // 状态: 0x00=成功, 0xFF=失败 			
	uint8_t  mode;              // 模式: 0x01=串口, 0x02=CAN			 1
	uint16_t duration;          // 耗时(秒) 							
	uint32_t errorCode;         // 错误码(失败时) 						
	uint8_t  expand[996];		// 保留 	
} UpdateLog_t;  //1024字节

/**
 * @brief 用户配置参数（512字节）
 *
 * 这块区域用于保存与具体业务相关、但又希望跨升级保留的用户配置。
 * 当前 BootLoader 主流程不会读取这里的字段，但升级时会整体保留并回写整块参数区。
 *
 * 用户配置区：0x0800C600 - 0x0800C7FF。
 */
typedef struct __attribute__((packed))
{
	// [0-31] 通信配置
	uint32_t uart_baudrate;     // 串口波特率 					115200
	uint8_t  uart_parity;       // 校验位 						0
	uint8_t  uart_stopbit;      // 停止位 						1
	uint16_t reserved_uart; 	//								0
	uint32_t can_baudrat;      // CAN波特率 - 保留 				0
	uint32_t eth_ip;            // 以太网IP - 保留 					0
	uint32_t eth_mask;          // 子网掩码 - 保留 					0
	uint32_t eth_gateway;       // 网关 - 保留 						0
	uint32_t reserved_comm[2];

	// [32-63] 功能开关
	uint32_t feature_flags;     // 功能标志位 - 保留 				0
	uint32_t debug_level;       // 调试级别 - 保留 					0
	uint32_t watchdog_timeout;  // 看门狗超时(ms) - 保留 			0
	uint32_t reserved_feat[5];

	// [64-127] 定时器配置
	uint32_t timer_intervals[16]; // 16个定时器周期 				0

	// [128-255] IO配置
	uint8_t  gpio_config[128];   // GPIO配置表 - 保留  				0

	// [256-383] 自定义参数
	uint8_t  user_data[128];     // 用户自定义数据 - 保留 				0

	// [384-511] 校验与保留
	uint8_t  reserved[124];		// 0
	uint32_t configCRC32;        // 配置区CRC32 					0	
} UserConfig_t;  // 512字节


/**
 * @brief 出厂校准数据（512字节）  - 整个区域都做保留
 *
 * 这块区域用于保存生产阶段写入的校准数据。
 * 当前 BootLoader 主流程不解释其内容，但为了保证升级后设备行为稳定，
 * 整个参数区都要被原样保留。
 *
 * 校准数据区：0x0800C800 - 0x0800C9FF。
 */
typedef struct __attribute__((packed))
{
	// [0-15] 标识
	uint32_t calib_magic;       // 校准魔术字: 					0xCAC0FFEE
	uint32_t calib_date;        // 校准日期						0
	uint32_t calib_version;     // 校准版本						0
	uint32_t reserved0;

	// [16-47] ADC校准
	uint16_t adc_offset[16];    // ADC偏移校准 					0

	// [48-79] DAC校准 
	uint16_t dac_gain[16];      // DAC增益校准 					0

	// [80-143] 温度校准
	int16_t  temp_curve[32];    // 温度曲线校准点 					0

	// [144-207] 电压校准
	float    voltage_k[16];     // 电压斜率 					0

	// [208-271] 电流校准
	float    current_k[16];     // 电流斜率 					0

	// [272-507] 保留
	uint8_t  reserved[236]; 	//								0

	// [508-511] 校验
	uint32_t calibCRC32;        // 校准数据CRC32 				0

} CalibData_t;  // 512字节

/************************ 函数定义 ************************/

/*
 * 函数作用：
 *   为参数区里的各个结构体填充一套“出厂默认值”。
 *   它更像是参数区初始化助手，适合首次烧录、产测或恢复默认参数时调用。
 *   当前 BootLoader 启动主流程并不会自动调用它。
 * 参数说明：
 *   param：输出的主参数区结构体指针，函数会向其中写入默认 BootParam_t。
 *   mylog：输出的升级日志区结构体指针，函数会写入默认日志区内容。
 *   userconfig：输出的用户配置结构体指针，函数会写入默认串口等配置。
 *   calibdata：输出的校准数据结构体指针，函数会写入默认校准区内容。
 * 返回值说明：
 *   无返回值。
 */
void bootloader_config_init(BootParam_t* param , UpdateLog_t* mylog , UserConfig_t* userconfig , CalibData_t* calibdata);


#endif // !__BOOTCONFIG_H__

/****************************End*****************************/
