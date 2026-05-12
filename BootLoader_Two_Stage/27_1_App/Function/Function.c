/************************************************************
 * 版权：2025CIMC Copyright。
 * 文件：Function.c
 * 作者: Jialei Zhao
 * 平台: 2025CIMC IHD-V04
 * 版本: Jialei Zhao     2026/2/4     V0.01    original
************************************************************/


/************************* 头文件 *************************/

#include "Function.h"
#include "LED.h"
#include "rom.h"
#include "usart.h"
#include "BootConfig.h"


/************************* 宏定义 *************************/
#define DOWNLOAD_ADDR 0x8073000

#define CONFIG_SIZE 1024*4

#define PARAM_ADDR 0x800C000

/************************ 变量定义 ************************/

typedef struct __attribute__((packed)) Parameter_SUM
{
	BootParam_t BootParam;
	BootParam_t BootParam_Reserved;
	UpdateLog_t UpdateLog;
	UserConfig_t UserConfig;
	CalibData_t CalibData;

}Parameter_t;

Parameter_t my_param_sum = { 0 };

uint8_t config_buf[CONFIG_SIZE] = { 0 };

//!串口
extern uint8_t usart0_tmp_buf[10 * 1024];
extern uint16_t usart0_tmp_buf_len;
extern uint8_t usart0_recv_flag;


uint32_t config_crc32 = 0;


/************************ 函数定义 ************************/

uint32_t crc32_calc(uint8_t* data , uint32_t len);

void mcu_software_reset(void);

/************************************************************
 * Function :       System_Init
 * Comment  :       用于初始化MCU
 * Parameter:       null
 * Return   :       null
 * Author   :       Jialei Zhao
 * Date     :       2026-02-04 V0.01 original
************************************************************/

void System_Init(void)
{
	/* 关键修改1: 先关闭所有中断 */
	__disable_irq();

	/* 关键修改2: 重定位中断向量表到App起始地址 */
	SCB->VTOR = 0x0800D000;   // App 起始地址

	/* 关键修改3: 清除所有可能残留的中断挂起标志 */
	for (uint32_t i = 0; i < 8; i++)
	{
		NVIC->ICPR[i] = 0xFFFFFFFF;
	}

	/* 确保操作完成 */
	__DSB();
	__ISB();

	/* 时钟配置 */
	systick_config();

	/* LED初始化 */
	LED_Init();

	/* 串口初始化 */
	my_usart_init();

	/* 关键修改4: 在所有外设初始化完成后才使能中断 */
	__enable_irq();
}

/************************************************************
 * Function :       UsrFunction
 * Comment  :       用户程序功能: LED1闪烁
 * Parameter:       null
 * Return   :       null
 * Author   :       Jialei Zhao
 * Date     :       2026-02-04 V0.01 original
************************************************************/
void UsrFunction(void)
{

	bootloader_config_init(&my_param_sum.BootParam , &my_param_sum.UpdateLog , &my_param_sum.UserConfig , &my_param_sum.CalibData);

	while (1)
	{

		if (usart0_recv_flag)
		{
			/* 关键修改5: 立即关闭中断，防止重复触发 */
			// __disable_irq();
			usart0_recv_flag = 0;

			uint32_t magic_value = 0;
			uint32_t tmp = usart0_tmp_buf[0];
			usart0_tmp_buf[0] = usart0_tmp_buf[1];
			usart0_tmp_buf[1] = tmp;

			tmp = usart0_tmp_buf[2];
			usart0_tmp_buf[2] = usart0_tmp_buf[3];
			usart0_tmp_buf[3] = tmp;

			//!0 2交换  1 3 交换
			tmp = usart0_tmp_buf[0];
			usart0_tmp_buf[0] = usart0_tmp_buf[2];
			usart0_tmp_buf[2] = tmp;

			tmp = usart0_tmp_buf[1];
			usart0_tmp_buf[1] = usart0_tmp_buf[3];
			usart0_tmp_buf[3] = tmp;

			tmp = usart0_tmp_buf[4];
			usart0_tmp_buf[4] = usart0_tmp_buf[5];
			usart0_tmp_buf[5] = tmp;

			tmp = usart0_tmp_buf[6];
			usart0_tmp_buf[6] = usart0_tmp_buf[7];
			usart0_tmp_buf[7] = tmp;

			tmp = usart0_tmp_buf[4];
			usart0_tmp_buf[4] = usart0_tmp_buf[6];
			usart0_tmp_buf[6] = tmp;

			tmp = usart0_tmp_buf[5];
			usart0_tmp_buf[5] = usart0_tmp_buf[7];
			usart0_tmp_buf[7] = tmp;

			memcpy(&magic_value , usart0_tmp_buf + 0 , 4);
			uint32_t appVersion = 0;
			memcpy(&appVersion , usart0_tmp_buf + 4 , 4);
			if (magic_value != 0x5AA5C33C)
			{
				printf("magic_value : 0x%08X\r\n" , magic_value);
				printf("update magic_value error!\r\n");
				printf("please check magic_value!\r\n");
				usart0_tmp_buf_len = 0;
				usart_interrupt_enable(USART0 , USART_INT_IDLE);
				usart_interrupt_enable(USART0 , USART_INT_RBNE);
				continue;
			}
			// my_param_sum.BootParam.appVersion = appVersion;
			printf("magic_value : 0x%08X\r\n" , magic_value);
			printf("appVersion : 0x%08X\r\n" , appVersion);

			usart0_tmp_buf_len -= 8;

			config_crc32 = crc32_calc(usart0_tmp_buf + 8 , usart0_tmp_buf_len);

			printf("Received %d bytes, CRC32: 0x%08X\r\n" , usart0_tmp_buf_len , config_crc32);

			/* Flash擦除操作 */
			for (uint8_t i = 0; i < 13; i++)
			{
				internal_flash_erase(DOWNLOAD_ADDR + i * 4 * 1024);
			}

			/* Flash写入操作 */
			internal_flash_write_str_Char(DOWNLOAD_ADDR , usart0_tmp_buf + 8 , usart0_tmp_buf_len);

			//!读出原来参数然后在次基础上进行修改
			for (uint16_t i = 0; i < CONFIG_SIZE; i++)
			{
				config_buf[i] = internal_flash_read_Char(PARAM_ADDR + i);
			}

			memcpy(&my_param_sum , config_buf , sizeof(Parameter_t));

			/* 更新参数 */
			my_param_sum.BootParam_Reserved = my_param_sum.BootParam;

			uint8_t tmp_str[256] = { 0 };

			memcpy(tmp_str , &my_param_sum.BootParam_Reserved , sizeof(BootParam_t));

			my_param_sum.BootParam.backupCRC32 = crc32_calc(tmp_str , sizeof(BootParam_t));
			my_param_sum.BootParam.appStartAddr = 0x800D000;
			my_param_sum.BootParam.appStackAddr = *(__IO uint32_t*)(my_param_sum.BootParam.appStartAddr + 0);	//栈顶地址
			my_param_sum.BootParam.appEntryAddr = *(__IO uint32_t*)(my_param_sum.BootParam.appStartAddr + 4);	// 应用程序入口地址在应用程序起始地址后4字节(需要手动指定中断向量表的位置)
			my_param_sum.BootParam.magicWord = 0x5AA5C33C;
			my_param_sum.BootParam.appVersion = appVersion;
			// printf("zzz my_param_sum.BootParam.appVersion : 0x%08X\r\n" , my_param_sum.BootParam.appVersion);
			delay_1ms(1000);


			my_param_sum.BootParam.updateFlag = 0x5A;
			my_param_sum.BootParam.updateStatus = 0x01;
			my_param_sum.BootParam.appSize = usart0_tmp_buf_len;
			my_param_sum.BootParam.appCRC32 = config_crc32;

			/* 写入参数到Flash */
			internal_flash_erase(PARAM_ADDR);
			memcpy(config_buf , &my_param_sum , sizeof(Parameter_t));
			internal_flash_write_str_Char(PARAM_ADDR , config_buf , sizeof(Parameter_t));

			usart0_tmp_buf_len = 0;

			// LED2_ON();
			/* 进行软复位 */
			mcu_software_reset();
		}

		printf("Hello World!\r\n");
		LED1_ON();
		delay_1ms(500);
		LED1_OFF();
		delay_1ms(500);
	}
}

/************************************************************
 * Function :       crc32_calc
 * Comment  :       用于计算crc32校验值
 * Parameter:       null
 * Return   :       null
 * Author   :       Jialei Zhao
 * Date     :       2026-02-04 V0.01 original
************************************************************/
uint32_t crc32_calc(uint8_t* data , uint32_t len)
{
	uint32_t crc = 0xFFFFFFFF;
	uint32_t i , j;

	for (i = 0; i < len; i++)
	{
		crc ^= data[i];
		for (j = 0; j < 8; j++)
		{
			if (crc & 1)
				crc = (crc >> 1) ^ 0xEDB88320;
			else
				crc >>= 1;
		}
	}

	return crc ^ 0xFFFFFFFF;
}
/************************************************************
 * Function :       mcu_software_reset
 * Comment  :       用于进行软复位
 * Parameter:       null
 * Return   :       null
 * Author   :       Jialei Zhao
 * Date     :       2026-02-04 V0.01 original
************************************************************/
void mcu_software_reset(void)
{
	/* set FAULTMASK */
	__set_FAULTMASK(1);
	NVIC_SystemReset();
}
/****************************End*****************************/


