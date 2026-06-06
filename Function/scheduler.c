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
     {led_task,                      20,   0}
    ,{adc_task,                      50,   0}
    ,{gd30ad3344_pt100_task,         200,  0}
    ,{oled_task,                     100,  0}
    ,{uart_task,                     5,    0}
    ,{rtc_task,                      1000, 0}  /* 每秒更新 RTC 共享缓存 */
    ,{cimc_protocol_auto_report_tick, 100,  0}  /* 自动上报心跳，内部自管理间隔 */
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
 *   2. 按依赖顺序初始化 USART1/RS485、ADC/DAC、RTC、OLED 和 PT100 外部 ADC。
 *   3. 初始化调度器任务数量，进入主循环前完成任务表准备。
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
		/* 正式版只初始化 USART1/RS485，起始波特率固定 19200，后续根据 Flash 参数切换。 */
		bsp_usart_init();

		/*
		 * 从 Flash user_config 区加载持久化参数（设备 ID、波特率、变比、阈值等）。
		 * 必须在任何需要参数的模块初始化前调用，尤其是 USART 波特率切换和心跳发送。
		 */
		cimc_params_load();
		cimc_alarm_init();
		cimc_alarm_load();  /* 从 Flash 恢复历史告警记录，必须在 init 之后调用 */

		/*
		 * 若 Flash 中保存的波特率不是出厂默认值（19200），则原地切换 USART1 波特率。
		 * 典型场景：上位机通过 0x01A2 切到 115200 并触发重启后，下次上电在此处恢复。
		 */
		{
		    uint32_t saved_baud = cimc_params_get_baud_rate();
		    if(saved_baud != CIMC_RS485_BAUDRATE) {
		        bsp_usart_change_baudrate(saved_baud);
		    }
		}
		rcu_periph_clock_enable(RCU_PMU);
		if(SET == pmu_flag_get(PMU_FLAG_STANDBY)) {
			/*
			 * 旧按键 Standby 演示已从正式任务中删除。保留 PMU 标志清理，
			 * 避免曾经进入待机后的复位标志影响后续启动判断。
			 */
			pmu_flag_clear(PMU_FLAG_RESET_STANDBY);
			pmu_flag_clear(PMU_FLAG_RESET_WAKEUP);
		}

		systick_config();

		/* 上电后保留短延时，给下载器和调试器重新连接 SWIO 留出窗口。 */
		delay_ms(200);

	#ifdef __FIRMWARE_VERSION_DEFINE
		fw_ver = gd32f4xx_firmware_version_get();
	#endif /* __FIRMWARE_VERSION_DEFINE */

		bsp_led_init();
		bsp_oled_init();

		bsp_gd30ad3344_init();
		gd30ad3344_pt100_app_init();

		bsp_adc_init();

		bsp_dac_init();

		bsp_rtc_init();

		OLED_Init();
		oled_app_reset_cache();

		scheduler_init();

		/*
		 * 上电/重启后主动发送心跳帧（帧类型 0x05，命令字 0x8888）。
		 * 自动测评 A-02 发送重启命令后，A-03 要求在 15s 内收到心跳且 ID 一致。
		 * 必须在 scheduler_init() 后调用，确保此时 USART1/RS485 已完全就绪。
		 */
		cimc_protocol_send_heartbeat();
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
