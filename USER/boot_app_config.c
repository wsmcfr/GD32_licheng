#include "boot_app_config.h"

/*
 * 函数作用：
 *   将异常和外设中断入口切换到 App 自己的向量表。
 * 主要流程：
 *   1. 写入 SCB->VTOR，使中断向量表基址指向 BOOT_APP_START_ADDRESS。
 *   2. 执行数据同步和指令同步屏障，确保后续中断使用新的向量表。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void boot_app_vector_table_init(void)
{
    /*
     * BootLoader 跳转到 App 后，必须让 VTOR 指向 App 的向量表。
     * 否则后续 SysTick、USART、DMA 等中断仍可能按 BootLoader 的向量表取入口。
     */
    SCB->VTOR = BOOT_APP_START_ADDRESS;

    /*
     * 屏障指令用于确保 VTOR 写入已经对内核生效，再继续执行后续初始化。
     * 这对刚切换向量表后马上打开中断的启动流程更稳妥。
     */
    __DSB();
    __ISB();
}

/*
 * 函数作用：
 *   完成 BootLoader 跳转到 App 后的最小运行环境接管。
 * 主要流程：
 *   1. 先把 VTOR 切换到当前 App 的向量表，保证异常和外设中断入口正确。
 *   2. 清理 NVIC 和 SysTick 中仍可能残留的中断挂起位，避免一开中断就误进旧现场。
 *   3. 重新打开全局中断，因为官方 BootLoader 在跳转前会调用 __disable_irq()。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void boot_app_handoff_init(void)
{
    boot_app_vector_table_init();

    /*
     * 官方 BootLoader 跳转前已经清过一次 NVIC，这里再次清理是为了兼容
     * 直接调试 App、异常复位后重进 App 等场景，确保后续 SysTick/USART/DMA
     * 中断从干净状态开始。
     */
    for(uint32_t i = 0U; i < 8U; i++) {
        NVIC->ICPR[i] = 0xFFFFFFFFUL;
    }

    /*
     * SysTick 属于内核异常，不在 NVIC->ICPR 外设中断数组里。
     * 单独清除 PENDST，避免 BootLoader 或调试现场残留的 SysTick 挂起位
     * 在 App 刚打开全局中断时立即触发。
     */
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;

    /*
     * BootLoader 为避免跳转过程中被中断打断，会在 iap_load_app() 里关闭全局中断。
     * PRIMASK 不会因为普通函数跳转自动恢复，因此 App 必须在完成 VTOR 切换后重新开中断。
     */
    __enable_irq();
    __DSB();
    __ISB();
}
