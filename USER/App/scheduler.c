/* Licence
* Company: MCUSTUDIO
* Auther: Ahypnis.
* Version: V0.10
* Time: 2025/06/05
* Note:
*/
#include "scheduler.h"

/* Number of scheduler tasks in scheduler_task[]. */
static uint8_t task_num;

/*
 * 结构体作用：
 *   描述一个调度任务的入口函数、周期和上次执行时间。
 */
typedef struct {
    void (*task_func)(void);
    uint32_t rate_ms;
    uint32_t last_run;
} task_t;

/* Static task table: function pointer, run period in ms, and last run time. */
static task_t scheduler_task[] =
{
     {led_task,  1,    0}
    ,{adc_task,  100,  0}
    ,{oled_task, 10,   0}
    ,{btn_task,  5,    0}
    ,{uart_task, 5,    0}
    ,{rtc_task,  500,  0}
};

/**
 * @brief Initialize task count from the static task table.
 */
void scheduler_init(void)
{
    task_num = sizeof(scheduler_task) / sizeof(task_t);
}



void system_init(void)
{
	#ifdef __FIRMWARE_VERSION_DEFINE
		uint32_t fw_ver = 0;
	#endif
		systick_config();
		init_cycle_counter(false);
		delay_ms(200); // Wait download if SWIO be set to GPIO

	#ifdef __FIRMWARE_VERSION_DEFINE
		fw_ver = gd32f4xx_firmware_version_get();
	#endif /* __FIRMWARE_VERSION_DEFINE */

		bsp_led_init();
		bsp_btn_init();
		bsp_oled_init();
		bsp_gd25qxx_init();
		bsp_usart_init();

		my_printf(DEBUG_USART, "BOOT: start\r\n");

		my_printf(DEBUG_USART, "BOOT: gd30 init...\r\n");
		bsp_gd30ad3344_init();
		my_printf(DEBUG_USART, "BOOT: gd30 done\r\n");

		my_printf(DEBUG_USART, "BOOT: adc init...\r\n");
		bsp_adc_init();
		my_printf(DEBUG_USART, "BOOT: adc done\r\n");

		my_printf(DEBUG_USART, "BOOT: dac init...\r\n");
		bsp_dac_init();
		my_printf(DEBUG_USART, "BOOT: dac done\r\n");

		my_printf(DEBUG_USART, "BOOT: rtc init...\r\n");
		bsp_rtc_init();
		my_printf(DEBUG_USART, "BOOT: rtc done\r\n");

		sd_fatfs_init();
		app_btn_init();

		my_printf(DEBUG_USART, "BOOT: oled init...\r\n");
		OLED_Init();
		my_printf(DEBUG_USART, "BOOT: oled done\r\n");

		test_spi_flash();
	#if SD_FATFS_DEMO_ENABLE
		sd_fatfs_test();
	#else
		my_printf(DEBUG_USART, "BOOT: sd_fatfs_test skipped (SD_FATFS_DEMO_ENABLE=0)\r\n");
	#endif

		scheduler_init();
}
/**
 * @brief Run due tasks according to their millisecond period.
 */
void scheduler_run(void)
{
    for (uint8_t i = 0; i < task_num; i++)
    {
        uint32_t now_time = get_system_ms();
        if (now_time >= scheduler_task[i].rate_ms + scheduler_task[i].last_run)
        {
            scheduler_task[i].last_run = now_time;
            scheduler_task[i].task_func();
        }
    }
}
