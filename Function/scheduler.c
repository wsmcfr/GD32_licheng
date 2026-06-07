#include "scheduler.h"

static uint8_t ntask; /* 有效任务数量 */

/* 一个周期任务 */
typedef struct
{
    void (*fn)(void);
    uint32_t rate;
    uint32_t last;
} task_t;

/* 静态任务表 */
static task_t s_tasks[] =
{
     {led_task,                      20,   0}
    ,{adc_task,                      50,   0}
    ,{gd30ad3344_pt100_task,         200,  0}
    ,{oled_task,                     100,  0}
    ,{uart_task,                     5,    0}
    ,{proto_tick, 100,  0}
};

// 按静态任务表长度初始化有效任务数
static void scheduler_init(void)
{
    ntask = sizeof(s_tasks) / sizeof(task_t);
}

/*深度睡眠唤醒后调用，将所有任务last统一重置为当前tick。*/
void scheduler_reset_runtime(void)
{
    uint8_t i;
    uint32_t now;

    now = timebase_get_ms32();
    for(i = 0; i < ntask; i++) 
	{
        s_tasks[i].last = now;
    }
}


/*系统上电初始化，完成后进入主循环。*/
void system_init(void)
{
	#ifdef __FIRMWARE_VERSION_DEFINE
		uint32_t fw_ver = 0;
	#endif
		boot_app_handoff_init();
		bsp_usart_init();

		params_load();
		alm_init();
		alm_load();

		{
		    uint32_t saved_baud = params_baud();
		    if(saved_baud != RS485_BAUD) 
			{
		        bsp_usart_change_baudrate(saved_baud);
		    }
		}
		rcu_periph_clock_enable(RCU_PMU);
		if(SET == pmu_flag_get(PMU_FLAG_STANDBY)) 
		{
			pmu_flag_clear(PMU_FLAG_RESET_STANDBY);
			pmu_flag_clear(PMU_FLAG_RESET_WAKEUP);
		}

		systick_config();

		delay_ms(200);

	#ifdef __FIRMWARE_VERSION_DEFINE
		fw_ver = gd32f4xx_firmware_version_get();
	#endif

		bsp_led_init();
		bsp_oled_init();

		GD30AD3344_Init();
		gd30ad3344_pt100_app_init();

		bsp_adc_init();
		bsp_dac_init();


		adc_app_set_dac_raw(2048);

		bsp_rtc_init();

		OLED_Init();
		oled_app_reset_cache();

		scheduler_init();

		proto_hb();
}

/* 轮询任务表 */
void scheduler_run(void)
{
    uint32_t now = timebase_get_ms32();
    uint8_t i;

    for (i = 0; i < ntask; i++)
    {
        if ((uint32_t)(now - s_tasks[i].last) >= s_tasks[i].rate)
        {
            s_tasks[i].last = now;
            s_tasks[i].fn();
        }
    }
}
