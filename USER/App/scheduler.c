#include "scheduler.h"

/*
 * 变量作用：
 *   记录 scheduler_task[] 中的有效任务数量。
 * 说明：
 *   该值在 scheduler_init() 中按静态任务表长度计算，避免手工维护数量导致越界或漏调度。
 */
static uint8_t task_num;

/*
 * 结构体作用：
 *   描述一个调度任务的入口函数、周期和上次执行时间。
 * 成员说明：
 *   task_func：任务入口函数指针，必须是不带参数且无返回值的周期任务。
 *   rate_ms：任务执行周期，单位为毫秒。
 *   last_run：上次执行时间戳，单位为毫秒，由 scheduler_run() 更新。
 */
typedef struct {
    void (*task_func)(void);
    uint32_t rate_ms;
    uint32_t last_run;
} task_t;

/*
 * 变量作用：
 *   静态调度任务表，集中登记所有需要周期运行的应用层任务。
 * 说明：
 *   新增周期任务时只在这里登记，避免调度入口散落在各个功能模块中。
 */
static task_t scheduler_task[] =
{
     {led_task,  1,    0}
    ,{adc_task,  100,  0}
    ,{oled_task, 10,   0}
    ,{btn_task,  5,    0}
    ,{uart_task, 5,    0}
    ,{rtc_task,  500,  0}
};

/*
 * 函数作用：
 *   根据静态任务表初始化调度任务数量。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void scheduler_init(void)
{
    task_num = sizeof(scheduler_task) / sizeof(task_t);
}


/*
 * 函数作用：
 *   完成系统上电后的基础外设、组件和应用任务初始化。
 * 主要流程：
 *   1. 初始化 SysTick、周期计数器和基础板级外设。
 *   2. 按依赖顺序初始化存储、串口、ADC/DAC、RTC、OLED 和按键应用层。
 *   3. 执行 SPI Flash 与可选 SD/FatFs 冒烟测试。
 *   4. 初始化调度器任务数量，进入主循环前完成任务表准备。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void system_init(void)
{
	#ifdef __FIRMWARE_VERSION_DEFINE
		uint32_t fw_ver = 0;
	#endif
		/*
		 * 当前工程作为 BootLoader App 运行，链接地址不再是 0x08000000。
		 * 先接管 BootLoader 跳转现场，确保 VTOR 指向 App 向量表，并恢复
		 * BootLoader 跳转前关闭的全局中断，否则 SysTick/USART/DMA 中断不会触发。
		 *
		 * 这一步必须放在绝大多数外设初始化之前，因为后续很多初始化都会依赖：
		 * 1. SysTick 正常进入 App 自己的中断服务函数；
		 * 2. USART / DMA / RTC 等外设中断已经改用 App 的向量表；
		 * 3. 全局中断状态已经从 BootLoader 关闭态恢复。
		 */
		boot_app_handoff_init();

		systick_config();
		init_cycle_counter(false);
		/* 上电后保留短延时，给下载器和调试器重新连接 SWIO 留出窗口。 */
		delay_ms(200);

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
/*
 * 函数作用：
 *   按毫秒周期轮询任务表，执行已经到期的周期任务。
 * 主要流程：
 *   1. 遍历 scheduler_task[] 中的每个任务。
 *   2. 使用 get_system_ms() 读取当前时间。
 *   3. 当前时间达到任务周期后，更新 last_run 并调用任务函数。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
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
