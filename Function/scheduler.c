#include "scheduler.h"

static uint8_t ntask; /* 有效任务数量，由scheduler_init()按任务表长度计算 */

/* 描述一个周期任务：入口函数、周期（ms）、上次执行时刻（ms） */
typedef struct
{
    void (*fn)(void);
    uint32_t rate;
    uint32_t last;
} task_t;

/* 静态任务表，新增周期任务在此登记 */
static task_t s_tasks[] =
{
     {led_task,                      20,   0}
    ,{adc_task,                      50,   0}
    ,{gd30ad3344_pt100_task,         200,  0}
    ,{oled_task,                     100,  0}
    ,{uart_task,                     5,    0}
    ,{rtc_task,                      1000, 0}  /* 每秒更新RTC共享缓存 */
    ,{proto_tick, 100,  0}  /* 自动上报，内部自管理间隔 */
};

// 按静态任务表长度初始化有效任务数
void scheduler_init(void)
{
    ntask = sizeof(s_tasks) / sizeof(task_t);
}

/*
 * 深度睡眠唤醒后调用，将所有任务last统一重置为当前tick。
 * 避免唤醒后外设重建期间各任务集中到期引发冲突。
 */
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


/*
 * 系统上电初始化，完成后进入主循环。
 * 初始化顺序：BootLoader现场接管 → USART/参数/告警 → LED/OLED/ADC/DAC/RTC/PT100 → 调度器 → 开机心跳。
 */
void system_init(void)
{
	#ifdef __FIRMWARE_VERSION_DEFINE
		uint32_t fw_ver = 0;
	#endif
		/*
		 * 当前工程作为BootLoader App运行，链接地址不是0x08000000。
		 * 先接管跳转现场：VTOR指向App向量表，恢复BootLoader关闭的全局中断，
		 * 否则SysTick/USART/DMA中断不会触发。
		 */
		boot_app_handoff_init();
		bsp_usart_init();

		params_load();
		alm_init();
		alm_load();

		/* Flash保存的波特率与出厂默认不同时，原地切换USART1波特率 */
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

		delay_ms(200); /* 给调试器重连SWIO留出窗口 */

	#ifdef __FIRMWARE_VERSION_DEFINE
		fw_ver = gd32f4xx_firmware_version_get();
	#endif /* __FIRMWARE_VERSION_DEFINE */

		bsp_led_init();
		bsp_oled_init();

		bsp_gd30ad3344_init();
		gd30ad3344_pt100_app_init();

		bsp_adc_init();
		bsp_dac_init();

		/* DAC初始值2048（约1.65V），确保CH1上电后有可测量信号 */
		adc_app_set_dac_raw(2048);

		bsp_rtc_init();

		OLED_Init();
		oled_app_reset_cache();

		scheduler_init();

		/* 上电后主动发送心跳帧，通知上位机本机在线 */
		proto_hb();
}

/* 轮询任务表，执行已到期的周期任务，32位tick回绕安全 */
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
