#include "scheduler.h"
#include <rt_sys.h>

/*
 * 变量作用：
 *   Arm C 库 retarget 使用的标准输入、标准输出和标准错误句柄。
 * 说明：
 *   这些值只在本文件的 _sys_xxx 桩函数内部使用，不映射真实文件系统。
 */
#define APP_STDIN_HANDLE        ((FILEHANDLE)0)
#define APP_STDOUT_HANDLE       ((FILEHANDLE)1)
#define APP_STDERR_HANDLE       ((FILEHANDLE)2)

#if defined(__ARMCC_VERSION)
/*
 * 作用：
 *   告诉 ArmClang C 库当前固件不使用 semihosting。
 * 说明：
 *   如果没有这个符号，C 库的 _sys_open/_sys_write 等默认实现会执行
 *   BKPT 0xAB 请求调试器服务；脱离调试器运行时会进入 HardFault，
 *   表现为 BootLoader 已跳 App 但 App 没有任何日志。
 */
__asm(".global __use_no_semihosting\n");
#endif

/*
 * 函数作用：
 *   判断调试串口 USART0 是否已经完成最小发送条件配置。
 * 主要流程：
 *   1. 检查 USART0 外设时钟是否打开。
 *   2. 检查 USART0 外设和发送器是否使能。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   1：表示 USART0 可以发送字符。
 *   0：表示 USART0 尚未初始化，不能访问发送流程。
 */
static uint8_t app_debug_usart_is_ready(void)
{
    if(0U == (RCU_APB2EN & RCU_APB2EN_USART0EN)) {
        return 0U;
    }

    if(0U == (USART_CTL0(DEBUG_USART) & USART_CTL0_UEN)) {
        return 0U;
    }

    if(0U == (USART_CTL0(DEBUG_USART) & USART_CTL0_TEN)) {
        return 0U;
    }

    return 1U;
}

/*
 * 函数作用：
 *   以阻塞轮询方式向调试串口发送 1 个字符。
 * 主要流程：
 *   1. 若 USART0 还未初始化，直接丢弃字符，避免 C 库启动阶段卡死。
 *   2. 写入 USART 数据寄存器。
 *   3. 等待发送缓冲区空，等待过程带超时保护。
 * 参数说明：
 *   ch：待发送字符，只有低 8 位会被写入 USART0。
 * 返回值说明：
 *   无返回值。
 */
static void app_debug_usart_putc(uint8_t ch)
{
    uint32_t timeout = 1000000UL;

    if(0U == app_debug_usart_is_ready()) {
        return;
    }

    usart_data_transmit(DEBUG_USART, ch);
    while(RESET == usart_flag_get(DEBUG_USART, USART_FLAG_TBE)) {
        if(0U == timeout) {
            break;
        }
        timeout--;
    }
}

/*
 * 函数作用：
 *   判断 Arm C 库传入的文件名是否为标准流名称。
 * 主要流程：
 *   通过与 C 库暴露的 __stdin_name / __stdout_name / __stderr_name 比较，
 *   只接受标准输入、标准输出和标准错误，拒绝普通文件打开请求。
 * 参数说明：
 *   name：C 库请求打开的文件名指针。
 * 返回值说明：
 *   APP_STDIN_HANDLE：表示标准输入。
 *   APP_STDOUT_HANDLE：表示标准输出。
 *   APP_STDERR_HANDLE：表示标准错误。
 *   -1：表示不是本固件支持的标准流。
 */
static FILEHANDLE app_stdio_name_to_handle(const char *name)
{
    if(name == __stdin_name) {
        return APP_STDIN_HANDLE;
    }

    if(name == __stdout_name) {
        return APP_STDOUT_HANDLE;
    }

    if(name == __stderr_name) {
        return APP_STDERR_HANDLE;
    }

    return (FILEHANDLE)-1;
}

/*
 * 函数作用：
 *   Arm C 库文件打开 retarget 桩函数。
 * 主要流程：
 *   只允许 C 库启动阶段打开 stdin/stdout/stderr 三个标准流，
 *   其它文件名一律返回失败，避免误把 semihosting 文件系统当成本地文件系统。
 * 参数说明：
 *   name：C 库请求打开的文件名。
 *   openmode：C 库请求的打开模式，本工程不使用该参数。
 * 返回值说明：
 *   非负值：标准流句柄。
 *   -1：打开失败。
 */
FILEHANDLE _sys_open(const char *name, int openmode)
{
    (void)openmode;
    return app_stdio_name_to_handle(name);
}

/*
 * 函数作用：
 *   Arm C 库文件关闭 retarget 桩函数。
 * 参数说明：
 *   fh：_sys_open 返回的文件句柄。
 * 返回值说明：
 *   0：表示关闭成功；标准流不需要真实关闭动作。
 */
int _sys_close(FILEHANDLE fh)
{
    (void)fh;
    return 0;
}

/*
 * 函数作用：
 *   Arm C 库块写 retarget 桩函数。
 * 主要流程：
 *   对 stdout/stderr，把缓冲区逐字节发送到 USART0；对其它句柄返回失败。
 * 参数说明：
 *   fh：目标文件句柄。
 *   buf：待写入数据缓冲区。
 *   len：待写入字节数。
 *   mode：C 库历史参数，本工程忽略。
 * 返回值说明：
 *   0：表示所有数据都已被本桩函数接收。
 *   len：表示该句柄不支持写入，所有字节都未写入。
 */
int _sys_write(FILEHANDLE fh, const unsigned char *buf, unsigned len, int mode)
{
    unsigned i;

    (void)mode;

    if((fh != APP_STDOUT_HANDLE) && (fh != APP_STDERR_HANDLE)) {
        return (int)len;
    }

    for(i = 0U; i < len; i++) {
        app_debug_usart_putc(buf[i]);
    }

    return 0;
}

/*
 * 函数作用：
 *   Arm C 库块读 retarget 桩函数。
 * 参数说明：
 *   fh：源文件句柄。
 *   buf：接收缓冲区，本工程不写入。
 *   len：期望读取字节数。
 *   mode：C 库历史参数，本工程忽略。
 * 返回值说明：
 *   len：表示没有读到任何字节，避免启动阶段阻塞等待输入。
 */
int _sys_read(FILEHANDLE fh, unsigned char *buf, unsigned len, int mode)
{
    (void)fh;
    (void)buf;
    (void)mode;
    return (int)len;
}

/*
 * 函数作用：
 *   Arm C 库判断文件句柄是否为终端的 retarget 桩函数。
 * 参数说明：
 *   fh：待判断文件句柄。
 * 返回值说明：
 *   1：stdin/stdout/stderr 标准流按终端处理。
 *   0：其它句柄不是终端。
 */
int _sys_istty(FILEHANDLE fh)
{
    if((fh == APP_STDIN_HANDLE) || (fh == APP_STDOUT_HANDLE) || (fh == APP_STDERR_HANDLE)) {
        return 1;
    }

    return 0;
}

/*
 * 函数作用：
 *   Arm C 库文件定位 retarget 桩函数。
 * 参数说明：
 *   fh：文件句柄。
 *   pos：目标位置。
 * 返回值说明：
 *   -1：本固件不支持文件定位。
 */
int _sys_seek(FILEHANDLE fh, long pos)
{
    (void)fh;
    (void)pos;
    return -1;
}

/*
 * 函数作用：
 *   Arm C 库文件刷新 retarget 桩函数。
 * 参数说明：
 *   fh：文件句柄。
 * 返回值说明：
 *   0：表示刷新成功；USART0 发送由底层轮询立即完成。
 */
int _sys_ensure(FILEHANDLE fh)
{
    (void)fh;
    return 0;
}

/*
 * 函数作用：
 *   Arm C 库查询文件长度 retarget 桩函数。
 * 参数说明：
 *   fh：文件句柄。
 * 返回值说明：
 *   -1：本固件不支持普通文件长度查询。
 */
long _sys_flen(FILEHANDLE fh)
{
    (void)fh;
    return -1L;
}

/*
 * 函数作用：
 *   Arm C 库生成临时文件名 retarget 桩函数。
 * 参数说明：
 *   name：输出文件名缓冲区，本工程不写入。
 *   sig：临时文件序号。
 *   maxlen：输出缓冲区最大长度。
 * 返回值说明：
 *   -1：本固件不支持临时文件。
 */
int _sys_tmpnam2(char *name, int sig, unsigned maxlen)
{
    (void)name;
    (void)sig;
    (void)maxlen;
    return -1;
}

/*
 * 函数作用：
 *   Arm C 库命令行参数 retarget 桩函数。
 * 参数说明：
 *   cmd：C 库提供的命令行缓冲区。
 *   len：缓冲区长度。
 * 返回值说明：
 *   返回 cmd，并保证有空间时写入空字符串，表示裸机固件没有命令行参数。
 */
char *_sys_command_string(char *cmd, int len)
{
    if((cmd != NULL) && (len > 0)) {
        cmd[0] = '\0';
    }

    return cmd;
}

/*
 * 函数作用：
 *   Arm C 库紧急单字符输出 retarget 桩函数。
 * 参数说明：
 *   ch：待输出字符。
 * 返回值说明：
 *   无返回值。
 */
void _ttywrch(int ch)
{
    app_debug_usart_putc((uint8_t)ch);
}

/*
 * 函数作用：
 *   Arm C 库退出 retarget 桩函数。
 * 参数说明：
 *   returncode：C 库传入的退出码，本工程不使用。
 * 返回值说明：
 *   无返回值；裸机固件不应返回到宿主环境，因此停在死循环保留现场。
 */
void _sys_exit(int returncode)
{
    (void)returncode;
    while(1) {
    }
}

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
    (void)f;
    app_debug_usart_putc((uint8_t)ch);
    return ch;
}
#endif /* GD_ECLIPSE_GCC */
