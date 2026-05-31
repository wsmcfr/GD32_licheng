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
     {led_task,  20,    0}
    ,{adc_task,  50,  0}
    ,{gd30ad3344_pt100_task, 200, 0}
    ,{oled_task, 100,   0}
    ,{btn_task,  5,    0}
    ,{uart_task, 5,    0}
    ,{uart_ota_task, 5, 0}
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
 *   将调度器所有任务的 last_run 统一重置为当前 timebase tick。
 * 主要流程：
 *   1. 读取一次当前 32 位毫秒 tick 作为统一基线。
 *   2. 遍历静态任务表，把每个任务的 last_run 都设置成该基线。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 * 说明：
 *   深度睡眠唤醒后外设会集中重建，如果沿用睡前 last_run，OLED/UART/RTC/ADC
 *   等任务可能在恢复后的第一轮同时到期。重基线可以让任务从唤醒时刻重新按周期展开。
 */
void scheduler_reset_runtime(void)
{
    uint8_t i;
    uint32_t now_time;

    now_time = timebase_get_ms32();
    for(i = 0U; i < task_num; i++) {
        scheduler_task[i].last_run = now_time;
    }
}


/*
 * 函数作用：
 *   完成系统上电后的基础外设、组件和应用任务初始化。
 * 主要流程：
 *   1. 初始化本地 SysTick timebase 和基础板级外设。
 *   2. 按依赖顺序初始化存储、串口、ADC/DAC、RTC、OLED 和按键应用层。
 *   3. 执行 SPI Flash 冒烟测试。
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
		/*
		 * 调试串口必须尽早初始化。这样 BootLoader 跳到 App 后，哪怕后续 SPI Flash、
		 * OLED 或 SMARTFS 初始化卡住，也能从 USART0 日志判断已经进入 App。
		 */
		bsp_usart_init();
		my_printf(DEBUG_USART, "BOOT: handoff start\r\n");
		rcu_periph_clock_enable(RCU_PMU);
		if(SET == pmu_flag_get(PMU_FLAG_STANDBY)) {
			/*
			 * Standby 唤醒会按复位流程重新启动，无法从睡前调用栈返回。
			 * 这里在调试串口可用后尽早输出来源标记，方便区分普通上电复位和
			 * KEYW/PMU WKUP 触发的 Standby 唤醒复位。
			 */
			my_printf(DEBUG_USART, "BOOT: wake from standby\r\n");
			pmu_flag_clear(PMU_FLAG_RESET_STANDBY);
			pmu_flag_clear(PMU_FLAG_RESET_WAKEUP);
		}

		systick_config();
		my_printf(DEBUG_USART, "BOOT: systick/timebase done\r\n");

		/* 上电后保留短延时，给下载器和调试器重新连接 SWIO 留出窗口。 */
		delay_ms(200);
		my_printf(DEBUG_USART, "BOOT: delay done\r\n");

	#ifdef __FIRMWARE_VERSION_DEFINE
		fw_ver = gd32f4xx_firmware_version_get();
	#endif /* __FIRMWARE_VERSION_DEFINE */

		bsp_led_init();
		my_printf(DEBUG_USART, "BOOT: led done\r\n");
		bsp_btn_init();
		my_printf(DEBUG_USART, "BOOT: btn done\r\n");
		bsp_oled_init();
		my_printf(DEBUG_USART, "BOOT: oled bus done\r\n");
		bsp_gd25qxx_init();
		my_printf(DEBUG_USART, "BOOT: gd25qxx bus done\r\n");
		uart_ota_reset_runtime();

		my_printf(DEBUG_USART, "BOOT: start\r\n");

		my_printf(DEBUG_USART, "BOOT: gd30 init...\r\n");
		bsp_gd30ad3344_init();
		gd30ad3344_pt100_app_init();
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

		app_btn_init();

		my_printf(DEBUG_USART, "BOOT: oled init...\r\n");
		OLED_Init();
		oled_app_reset_cache();
		my_printf(DEBUG_USART, "BOOT: oled done\r\n");

#if SMART_STORAGE_BOOT_SELF_TEST_ENABLE
		/*
		 * SMARTFS 自检使用整片 GD25Q16 文件系统管理区。
		 * 用户已取消末尾 4KB 裸测保留区，因此这里不会再为 test_spi_flash() 留专用扇区。
		 */
		if (SMART_STORAGE_ERR_OK != smart_storage_self_test()) {
			my_printf(DEBUG_USART, "BOOT: smart_storage_self_test failed\r\n");
		}
#else
		my_printf(DEBUG_USART, "BOOT: smart_storage_self_test skipped (SMART_STORAGE_BOOT_SELF_TEST_ENABLE=0)\r\n");
#endif

#if SPI_FLASH_RAW_TEST_ENABLE
		test_spi_flash();
#else
		my_printf(DEBUG_USART, "BOOT: test_spi_flash skipped (SPI_FLASH_RAW_TEST_ENABLE=0)\r\n");
#endif

		scheduler_init();
		/*
		 * 只有在调度器完成初始化后才向 RS485 口输出 ready。
		 * 上位机看到该探测串后可能立即裸发 Project_ota.bin，因此此时必须保证
		 * 主循环中的 uart_ota_task() 已经具备消费 DMA 队列的条件。
		 */
		uart_ota_emit_startup_probe();
}
/*
 * 函数作用：
 *   按毫秒周期轮询任务表，执行已经到期的周期任务。
 * 主要流程：
 *   1. 单次读取当前 32 位毫秒 tick。
 *   2. 遍历 scheduler_task[] 中的每个任务。
 *   3. 使用 unsigned 差值判断周期是否到期，兼容 tick 回绕。
 *   4. 当前时间达到任务周期后，更新 last_run 并调用任务函数。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void scheduler_run(void)
{
    uint32_t now_time = timebase_get_ms32();
    uint8_t i;

    for (i = 0U; i < task_num; i++)
    {
        /*
         * 32 位毫秒 tick 约 49.7 天回绕一次。
         * 使用 unsigned 差值判断，不依赖 now_time 绝对大于 last_run，
         * 因此即使跨过回绕点，周期任务仍能按预期触发。
         */
        if ((uint32_t)(now_time - scheduler_task[i].last_run) >= scheduler_task[i].rate_ms)
        {
            scheduler_task[i].last_run = now_time;
            scheduler_task[i].task_func();
        }
    }
}
