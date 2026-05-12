#include "scheduler.h"

/*
 * 函数作用：
 *   固件主入口，完成系统初始化后持续运行周期调度器。
 * 主要流程：
 *   1. 调用 system_init() 初始化时钟、外设、组件和任务表。
 *   2. 在无限循环中调用 scheduler_run() 执行到期任务。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   正常情况下不返回；返回值仅满足 C 语言 main 函数签名要求。
 */
int main(void)
{
	system_init();
    while(1) {
        scheduler_run();
    }
}

#ifdef GD_ECLIPSE_GCC
/*
 * 函数作用：
 *   在 Eclipse GCC 环境下重定向 C 库 printf 的单字符输出到调试串口。
 * 参数说明：
 *   ch：待输出的单个字符，低 8 位会写入串口数据寄存器。
 * 返回值说明：
 *   返回已经写入的字符值。
 */
int __io_putchar(int ch)
{
    usart_data_transmit(EVAL_COM0, (uint8_t)ch);
    /* 等待发送缓冲区空，保证下一个字符不会覆盖当前字符。 */
    while(RESET == usart_flag_get(EVAL_COM0, USART_FLAG_TBE));
    return ch;
}
#else
/*
 * 函数作用：
 *   重定向 C 库 printf/fputc 的单字符输出到 USART0 调试串口。
 * 参数说明：
 *   ch：待输出的单个字符，低 8 位会写入串口数据寄存器。
 *   f：C 标准库传入的文件流指针，本工程不区分具体流。
 * 返回值说明：
 *   返回已经写入的字符值。
 */
int fputc(int ch, FILE *f)
{
    usart_data_transmit(USART0, (uint8_t)ch);
    /* 轮询 TBE，确保 printf 连续输出时字符顺序稳定。 */
    while(RESET == usart_flag_get(USART0, USART_FLAG_TBE));
    return ch;
}
#endif /* GD_ECLIPSE_GCC */
