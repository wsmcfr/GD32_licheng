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
 *   对 stdout/stderr 直接丢弃缓冲区；对其它句柄返回失败。
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
    (void)mode;
    (void)buf;

    if((fh != APP_STDOUT_HANDLE) && (fh != APP_STDERR_HANDLE)) {
        return (int)len;
    }

    /*
     * 正式版不绑定任何调试串口。stdout/stderr 数据在这里直接丢弃，
     * 既满足 C 库 retarget 合约，又避免向 USART1/RS485 写入非赛题协议文本。
     */
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
 *   0：表示刷新成功；正式版不绑定调试串口，输出在 retarget 层已丢弃。
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
    (void)ch;
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
