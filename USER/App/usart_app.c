#include "usart_app.h"

__IO uint8_t rx_flag = 0;
__IO uint16_t uart_dma_length = 0;
uint8_t uart_dma_buffer[UART_APP_DMA_BUFFER_SIZE] = {0};

/*
 * 变量作用：
 *   USART0 命令处理专用快照缓冲区和文件回显缓冲区。
 * 说明：
 *   先从 ISR 共享缓冲区复制命令帧，再在任务层执行 LittleFS 文件与目录操作，
 *   避免处理过程中共享缓冲区被下一次串口中断改写。
 */
static uint8_t uart_command_buffer[UART_APP_DMA_BUFFER_SIZE] = {0};
static uint8_t uart_file_buffer[UART_APP_DMA_BUFFER_SIZE] = {0};

/*
 * 宏作用：
 *   定义 UART 文件系统壳层当前工作目录缓冲区长度。
 * 说明：
 *   该值需要同时覆盖 LittleFS 绝对路径和串口命令层的拼接空间，因此与命令帧缓冲独立。
 */
#define UART_LFS_PATH_BUFFER_SIZE      256U

/*
 * 变量作用：
 *   保存当前串口文件系统壳层的工作目录。
 * 说明：
 *   上电默认位于根目录 `/`；`cd` 成功后会更新这里，后续相对路径命令都基于该目录解析。
 */
static char g_uart_current_dir[UART_LFS_PATH_BUFFER_SIZE] = "/";

/*
 * 变量作用：
 *   固定帮助文本，供 `help` 和未知命令回包使用。
 * 说明：
 *   当前协议统一走 USART0 文本命令壳层，因此帮助文本需要同时展示 LittleFS 与 RTC 命令集合。
 */
static const char g_uart_help_text[] =
    "LFS SHELL:\r\n"
    "help\r\n"
    "gettime\r\n"
    "settime <yyyy-mm-dd> <hh:mm:ss>\r\n"
    "pwd\r\n"
    "ls [path]\r\n"
    "cd <path>\r\n"
    "cat <file>\r\n"
    "write [-a] <file> <text>\r\n"
    "mkdir <dir>\r\n"
    "touch <file>\r\n"
    "rm <path>\r\n"
    "stat [path]\r\n"
    "df\r\n";

/*
 * 宏作用：
 *   定义 my_printf() 的格式化缓冲区长度。
 * 说明：
 *   单行日志主要用于命令状态和错误提示，不承载大块文件正文。
 */
#define UART_APP_PRINTF_BUFFER_SIZE    512U

/*
 * 结构体作用：
 *   描述一次命令解析后得到的“命令名 + 剩余参数字符串”。
 * 成员说明：
 *   command：命令关键字起始地址，指向命令缓冲区内部。
 *   args：参数字符串起始地址；无参数时指向空串。
 */
typedef struct
{
    char *command;
    char *args;
} uart_shell_command_t;

/*
 * 结构体作用：
 *   描述需要拆成两个参数的命令解析结果。
 * 成员说明：
 *   arg1：第一个参数，例如路径。
 *   arg2：第二个参数，例如写入文本。
 */
typedef struct
{
    char *arg1;
    char *arg2;
} uart_shell_two_args_t;

/*
 * 结构体作用：
 *   描述 `write` 命令解析后的写入模式、目标路径和正文内容。
 * 成员说明：
 *   append_mode：1 表示追加写，0 表示覆盖写。
 *   path：目标文件路径字符串，指向命令缓冲区内部。
 *   text：待写入正文字符串，指向命令缓冲区内部。
 */
typedef struct
{
    uint8_t append_mode;
    char *path;
    char *text;
} uart_shell_write_args_t;

/*
 * 枚举作用：
 *   描述 `settime` 参数解析的结果类型，便于串口层回显更明确的错误原因。
 * 枚举说明：
 *   UART_RTC_PARSE_OK：参数解析成功。
 *   UART_RTC_PARSE_MISSING_ARGS：缺少完整时间参数。
 *   UART_RTC_PARSE_BAD_FORMAT：参数格式不符合 `yyyy-mm-dd hh:mm:ss`。
 *   UART_RTC_PARSE_OUT_OF_RANGE：年月日时分秒数值超出允许范围。
 */
typedef enum
{
    UART_RTC_PARSE_OK = 0,
    UART_RTC_PARSE_MISSING_ARGS,
    UART_RTC_PARSE_BAD_FORMAT,
    UART_RTC_PARSE_OUT_OF_RANGE,
} uart_rtc_parse_result_t;

/*
 * 函数作用：
 *   将中断上下文产生的一帧数据原子地转存到任务层私有缓冲区。
 * 主要流程：
 *   1. 关总中断，避免复制过程中 ISR 再次改写共享缓冲区。
 *   2. 按有效长度将共享缓冲区复制到任务层私有缓冲区。
 *   3. 清除共享长度和完成标志，然后恢复中断。
 * 参数说明：
 *   ready_flag：表示对应共享缓冲区当前是否已经有一帧待处理数据。
 *   shared_length：表示共享缓冲区当前有效长度。
 *   shared_buffer：ISR 使用的共享缓冲区。
 *   task_buffer：任务层私有缓冲区。
 *   task_buf_size：任务层私有缓冲区大小，单位为字节。
 * 返回值说明：
 *   返回成功取出的有效字节数；0 表示当前没有新帧或参数无效。
 */
static uint16_t prv_uart_take_frame(__IO uint8_t *ready_flag,
                                    __IO uint16_t *shared_length,
                                    uint8_t *shared_buffer,
                                    uint8_t *task_buffer,
                                    uint16_t task_buf_size)
{
    uint16_t valid_length = 0U;

    if((NULL == ready_flag) || (NULL == shared_length) ||
       (NULL == shared_buffer) || (NULL == task_buffer) ||
       (0U == task_buf_size)){
        return 0U;
    }

    __disable_irq();
    if(0U != *ready_flag){
        valid_length = *shared_length;
        /*
         * 命令层按 C 字符串解析，因此这里强制预留 1 字节 '\0'，避免整帧满载时越界扫描。
         */
        if(valid_length >= task_buf_size){
            valid_length = task_buf_size - 1U;
        }
        if(valid_length > 0U){
            memcpy(task_buffer, shared_buffer, valid_length);
        }
        task_buffer[valid_length] = '\0';
        *shared_length = 0U;
        *ready_flag = 0U;
    }
    __enable_irq();

    return valid_length;
}

/*
 * 函数作用：
 *   通过阻塞发送的方式向指定串口输出格式化字符串。
 * 主要流程：
 *   1. 用 vsnprintf 将可变参数格式化到固定长度缓冲区。
 *   2. 根据格式化结果计算实际发送长度。
 *   3. 通过 Driver 层通用发送接口发送到目标串口。
 * 参数说明：
 *   usart_periph：目标 USART 外设编号。
 *   format：printf 风格格式字符串，后续可变参数必须与格式占位符匹配。
 * 返回值说明：
 *   正数或 0：vsnprintf 返回的完整格式化长度，可能大于实际发送长度。
 *   负数：格式化失败，函数不发送数据并原样返回该错误值。
 */
int my_printf(uint32_t usart_periph, const char *format, ...)
{
    char buffer[UART_APP_PRINTF_BUFFER_SIZE];
    va_list arg;
    int length;
    uint16_t send_length;

    va_start(arg, format);
    length = vsnprintf(buffer, sizeof(buffer), format, arg);
    va_end(arg);

    if(length <= 0){
        return length;
    }

    if(length >= (int)sizeof(buffer)){
        send_length = (uint16_t)(sizeof(buffer) - 1U);
    }else{
        send_length = (uint16_t)length;
    }

    (void)bsp_usart_send_buffer(usart_periph, (const uint8_t *)buffer, send_length);
    return length;
}

/*
 * 函数作用：
 *   去掉串口帧末尾的回车、换行和已有字符串结尾，得到纯命令内容。
 * 参数说明：
 *   buffer：待裁剪的命令缓冲区，必须非空。
 *   length：当前命令有效长度，单位为字节。
 * 返回值说明：
 *   返回裁剪后的有效命令长度；0 表示裁剪后为空命令或参数无效。
 */
static uint16_t prv_uart_trim_command(uint8_t *buffer, uint16_t length)
{
    if((NULL == buffer) || (0U == length)){
        return 0U;
    }

    while(length > 0U){
        if(('\r' != buffer[length - 1U]) &&
           ('\n' != buffer[length - 1U]) &&
           ('\0' != buffer[length - 1U])){
            break;
        }
        length--;
    }

    buffer[length] = '\0';
    return length;
}

/*
 * 函数作用：
 *   去掉字符串前导空格和制表符，便于统一解析参数。
 * 参数说明：
 *   text：待处理字符串起始地址，可为空。
 * 返回值说明：
 *   非空：返回跳过前导空白后的新起始地址。
 *   空：当输入为空指针时返回空指针。
 */
static char *prv_uart_skip_spaces(char *text)
{
    if(NULL == text){
        return NULL;
    }

    while((' ' == *text) || ('\t' == *text)){
        text++;
    }

    return text;
}

/*
 * 函数作用：
 *   就地裁掉字符串尾部空格和制表符。
 * 参数说明：
 *   text：待裁剪字符串，必须非空。
 * 返回值说明：
 *   无返回值。
 */
static void prv_uart_rstrip_spaces(char *text)
{
    size_t length;

    if(NULL == text){
        return;
    }

    length = strlen(text);
    while(length > 0U){
        if((' ' != text[length - 1U]) && ('\t' != text[length - 1U])){
            break;
        }
        text[length - 1U] = '\0';
        length--;
    }
}

/*
 * 函数作用：
 *   把一帧命令拆成“命令关键字 + 参数串”。
 * 主要流程：
 *   1. 跳过帧首空白。
 *   2. 以第一个空白分隔命令关键字和参数串。
 *   3. 对参数串再做一次前导空白裁剪。
 * 参数说明：
 *   command_text：可修改的命令字符串缓冲区，必须非空。
 *   parsed：解析结果输出结构体指针，必须非空。
 * 返回值说明：
 *   true：表示成功得到命令关键字。
 *   false：表示空命令或参数无效。
 */
static bool prv_uart_parse_command(char *command_text, uart_shell_command_t *parsed)
{
    char *cursor;

    if((NULL == command_text) || (NULL == parsed)){
        return false;
    }

    command_text = prv_uart_skip_spaces(command_text);
    if('\0' == *command_text){
        return false;
    }

    parsed->command = command_text;
    parsed->args = command_text + strlen(command_text);

    cursor = command_text;
    while('\0' != *cursor){
        if((' ' == *cursor) || ('\t' == *cursor)){
            *cursor = '\0';
            parsed->args = prv_uart_skip_spaces(cursor + 1);
            return true;
        }
        cursor++;
    }

    parsed->args = cursor;
    return true;
}

/*
 * 函数作用：
 *   把参数字符串拆成两个部分，用于 `write <path> <text>` 这类命令。
 * 主要流程：
 *   1. 跳过参数串前导空白。
 *   2. 找到第一个空白作为分隔点，将路径和正文断开。
 *   3. 对正文保留原始内部空格，只去掉前导空白。
 * 参数说明：
 *   args：待拆分参数字符串，必须非空。
 *   result：两参数输出结构体指针，必须非空。
 * 返回值说明：
 *   true：表示成功拿到两个参数。
 *   false：表示参数不足。
 */
static bool prv_uart_split_two_args(char *args, uart_shell_two_args_t *result)
{
    char *cursor;

    if((NULL == args) || (NULL == result)){
        return false;
    }

    args = prv_uart_skip_spaces(args);
    if('\0' == *args){
        return false;
    }

    cursor = args;
    while(('\0' != *cursor) && (' ' != *cursor) && ('\t' != *cursor)){
        cursor++;
    }

    if('\0' == *cursor){
        return false;
    }

    *cursor = '\0';
    result->arg1 = args;
    result->arg2 = prv_uart_skip_spaces(cursor + 1);
    if(('\0' == *result->arg1) || ('\0' == *result->arg2)){
        return false;
    }

    return true;
}

/*
 * 函数作用：
 *   解析 `write` 命令参数，支持默认覆盖写和 `-a` 追加写两种模式。
 * 主要流程：
 *   1. 先复用两参数拆分逻辑，得到“首段参数 + 剩余正文”。
 *   2. 若首段参数为 `-a`，继续再拆一次，取出真实路径和正文。
 *   3. 产出统一的写入模式、路径和文本，供后续文件写接口直接使用。
 * 参数说明：
 *   args：`write` 命令参数字符串，必须非空。
 *   result：解析结果输出结构体指针，必须非空。
 * 返回值说明：
 *   true：表示成功解析出写入模式、路径和正文。
 *   false：表示参数格式不正确。
 */
static bool prv_uart_parse_write_args(char *args, uart_shell_write_args_t *result)
{
    uart_shell_two_args_t parsed_args;

    if((NULL == args) || (NULL == result)){
        return false;
    }

    if(!prv_uart_split_two_args(args, &parsed_args)){
        return false;
    }

    if(0 == strcmp(parsed_args.arg1, "-a")){
        if(!prv_uart_split_two_args(parsed_args.arg2, &parsed_args)){
            return false;
        }

        result->append_mode = 1U;
        result->path = parsed_args.arg1;
        result->text = parsed_args.arg2;
        return true;
    }

    result->append_mode = 0U;
    result->path = parsed_args.arg1;
    result->text = parsed_args.arg2;
    return true;
}

/*
 * 函数作用：
 *   判断指定年份是否为公历闰年，供串口层输入范围校验使用。
 * 参数说明：
 *   year：完整年份，例如 2026。
 * 返回值说明：
 *   true：表示闰年。
 *   false：表示平年。
 */
static bool prv_uart_is_leap_year(uint16_t year)
{
    if((0U == (year % 400U)) || ((0U == (year % 4U)) && (0U != (year % 100U)))){
        return true;
    }

    return false;
}

/*
 * 函数作用：
 *   获取指定年月对应月份的最大天数，供 `settime` 参数校验使用。
 * 参数说明：
 *   year：完整年份，例如 2026。
 *   month：十进制月份，范围预期为 1~12。
 * 返回值说明：
 *   28~31：表示该月允许的最大日期。
 *   0：表示月份不在有效范围内。
 */
static uint8_t prv_uart_get_days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t days_in_month[] = {31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U};

    if((month < 1U) || (month > 12U)){
        return 0U;
    }

    if((2U == month) && prv_uart_is_leap_year(year)){
        return 29U;
    }

    return days_in_month[month - 1U];
}

/*
 * 函数作用：
 *   解析 `settime` 命令的日期时间参数，并在串口层先做一轮用户输入校验。
 * 主要流程：
 *   1. 检查参数是否为空，给出“缺参”错误。
 *   2. 按 `yyyy-mm-dd hh:mm:ss` 格式解析年月日时分秒。
 *   3. 先在命令层校验数值范围，这样可以向操作者返回更明确的错误提示。
 * 参数说明：
 *   args：`settime` 命令参数字符串，允许前导空白，但必须包含完整时间文本。
 *   result：解析成功后的十进制时间结构体输出指针，必须非空。
 * 返回值说明：
 *   UART_RTC_PARSE_OK：解析成功。
 *   UART_RTC_PARSE_MISSING_ARGS：参数为空。
 *   UART_RTC_PARSE_BAD_FORMAT：格式不是 `yyyy-mm-dd hh:mm:ss`。
 *   UART_RTC_PARSE_OUT_OF_RANGE：存在非法年月日时分秒值。
 */
static uart_rtc_parse_result_t prv_uart_parse_datetime_args(char *args, bsp_rtc_datetime_t *result)
{
    unsigned int year;
    unsigned int month;
    unsigned int date;
    unsigned int hour;
    unsigned int minute;
    unsigned int second;
    unsigned int matched;
    char extra_char;
    uint8_t max_day;

    if(NULL == result){
        return UART_RTC_PARSE_BAD_FORMAT;
    }

    args = prv_uart_skip_spaces(args);
    if((NULL == args) || ('\0' == *args)){
        return UART_RTC_PARSE_MISSING_ARGS;
    }
    /* 接受串口终端在命令尾部附带的多余空格，避免人工输入时被误判为格式错误。 */
    prv_uart_rstrip_spaces(args);

    matched = (unsigned int)sscanf(args,
                                   "%u-%u-%u %u:%u:%u %c",
                                   &year,
                                   &month,
                                   &date,
                                   &hour,
                                   &minute,
                                   &second,
                                   &extra_char);
    if(6U != matched){
        return UART_RTC_PARSE_BAD_FORMAT;
    }

    if((year < 2000U) || (year > 2099U) ||
       (month < 1U) || (month > 12U) ||
       (hour > 23U) || (minute > 59U) || (second > 59U)){
        return UART_RTC_PARSE_OUT_OF_RANGE;
    }

    max_day = prv_uart_get_days_in_month((uint16_t)year, (uint8_t)month);
    if((0U == max_day) || (date < 1U) || (date > (unsigned int)max_day)){
        return UART_RTC_PARSE_OUT_OF_RANGE;
    }

    result->year = (uint16_t)year;
    result->month = (uint8_t)month;
    result->date = (uint8_t)date;
    result->hour = (uint8_t)hour;
    result->minute = (uint8_t)minute;
    result->second = (uint8_t)second;
    result->day_of_week = 0U;
    return UART_RTC_PARSE_OK;
}

/*
 * 函数作用：
 *   向调试串口发送一段以 '\0' 结尾的纯文本。
 * 参数说明：
 *   text：待发送的文本字符串指针，允许为空；为空时直接返回。
 * 返回值说明：
 *   无返回值。
 */
static void prv_uart_send_text(const char *text)
{
    uint16_t send_length;

    if(NULL == text){
        return;
    }

    send_length = (uint16_t)strlen(text);
    if(send_length > 0U){
        (void)bsp_usart_send_buffer(DEBUG_USART, (const uint8_t *)text, send_length);
    }
}

/*
 * 函数作用：
 *   向调试串口发送一段原始字节流。
 * 参数说明：
 *   data：待发送数据起始地址，必须非空。
 *   length：待发送字节数，单位为字节。
 * 返回值说明：
 *   无返回值。
 */
static void prv_uart_send_bytes(const uint8_t *data, uint16_t length)
{
    if((NULL == data) || (0U == length)){
        return;
    }

    (void)bsp_usart_send_buffer(DEBUG_USART, data, length);
}

/*
 * 函数作用：
 *   发送当前调试口支持的命令帮助信息。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
static void prv_uart_send_help(void)
{
    prv_uart_send_text(g_uart_help_text);
}

/*
 * 函数作用：
 *   按统一格式输出一组十进制 RTC 日期时间，避免 `gettime` 和 `settime` 各自维护一套格式串。
 * 参数说明：
 *   prefix：输出前缀，例如 `RTC: ` 或 `RTC: SET OK `，必须非空。
 *   datetime：待输出时间结构体指针，必须非空。
 * 返回值说明：
 *   无返回值。
 */
static void prv_uart_send_rtc_datetime(const char *prefix, const bsp_rtc_datetime_t *datetime)
{
    if((NULL == prefix) || (NULL == datetime)){
        return;
    }

    my_printf(DEBUG_USART,
              "%s%04u-%02u-%02u %02u:%02u:%02u\r\n",
              prefix,
              (unsigned int)datetime->year,
              (unsigned int)datetime->month,
              (unsigned int)datetime->date,
              (unsigned int)datetime->hour,
              (unsigned int)datetime->minute,
              (unsigned int)datetime->second);
}

/*
 * 函数作用：
 *   按统一格式输出 LittleFS 命令失败信息。
 * 参数说明：
 *   action：失败动作描述，例如 "cat"、"ls"、"mkdir"。
 *   err：对应的 LittleFS 错误码或本模块错误码。
 * 返回值说明：
 *   无返回值。
 */
static void prv_uart_report_lfs_error(const char *action, int err)
{
    my_printf(DEBUG_USART,
              "LFS: %s failed (%d)\r\n",
              (NULL != action) ? action : "unknown",
              err);
}

/*
 * 函数作用：
 *   把用户传入的相对路径或绝对路径解析成规范绝对路径。
 * 主要流程：
 *   1. 绝对路径直接复制。
 *   2. 相对路径基于当前工作目录拼接。
 *   3. 处理 `.`、`..` 和重复 `/`，最终归一化成 LittleFS 绝对路径。
 * 参数说明：
 *   input_path：用户输入路径，必须非空。
 *   output_path：输出绝对路径缓冲区，必须非空。
 *   output_size：输出缓冲区大小，单位为字节。
 * 返回值说明：
 *   true：表示解析成功。
 *   false：表示路径为空、超长或归一化失败。
 */
static bool prv_uart_resolve_path(const char *input_path, char *output_path, uint16_t output_size)
{
    char working[UART_LFS_PATH_BUFFER_SIZE];
    char normalized[UART_LFS_PATH_BUFFER_SIZE];
    char *src;
    char *segment_start;
    char *next_slash;
    size_t normalized_len;

    if((NULL == input_path) || (NULL == output_path) || (output_size < 2U)){
        return false;
    }

    while((' ' == *input_path) || ('\t' == *input_path)){
        input_path++;
    }

    if('\0' == *input_path){
        return false;
    }

    if('/' == input_path[0]){
        if(strlen(input_path) >= sizeof(working)){
            return false;
        }
        (void)strcpy(working, input_path);
    }else{
        if(0 == strcmp(g_uart_current_dir, "/")){
            if(snprintf(working, sizeof(working), "/%s", input_path) >= (int)sizeof(working)){
                return false;
            }
        }else{
            if(snprintf(working, sizeof(working), "%s/%s", g_uart_current_dir, input_path) >= (int)sizeof(working)){
                return false;
            }
        }
    }

    normalized[0] = '/';
    normalized[1] = '\0';
    normalized_len = 1U;
    src = working;

    while('\0' != *src){
        while('/' == *src){
            src++;
        }

        if('\0' == *src){
            break;
        }

        segment_start = src;
        next_slash = src;
        while(('\0' != *next_slash) && ('/' != *next_slash)){
            next_slash++;
        }

        if((1 == (next_slash - segment_start)) && ('.' == segment_start[0])){
            src = next_slash;
            continue;
        }

        if((2 == (next_slash - segment_start)) &&
           ('.' == segment_start[0]) &&
           ('.' == segment_start[1])){
            if(normalized_len > 1U){
                normalized_len--;
                while((normalized_len > 1U) && ('/' != normalized[normalized_len - 1U])){
                    normalized_len--;
                }
                normalized[normalized_len] = '\0';
            }
            src = next_slash;
            continue;
        }

        if(normalized_len > 1U){
            if((normalized_len + 1U) >= sizeof(normalized)){
                return false;
            }
            normalized[normalized_len++] = '/';
        }

        if((normalized_len + (size_t)(next_slash - segment_start)) >= sizeof(normalized)){
            return false;
        }
        memcpy(&normalized[normalized_len],
               segment_start,
               (size_t)(next_slash - segment_start));
        normalized_len += (size_t)(next_slash - segment_start);
        normalized[normalized_len] = '\0';
        src = next_slash;
    }

    if(normalized_len >= output_size){
        return false;
    }

    (void)strcpy(output_path, normalized);
    return true;
}

/*
 * 函数作用：
 *   输出当前工作目录。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
static void prv_uart_handle_pwd(void)
{
    my_printf(DEBUG_USART, "%s\r\n", g_uart_current_dir);
}

/*
 * 函数作用：
 *   输出当前文件系统总容量、已用容量和当前工作目录。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
static void prv_uart_handle_df(void)
{
    lfs_storage_info_t info;
    int err;

    err = lfs_storage_get_info(NULL, &info);
    if(LFS_ERR_OK != err){
        prv_uart_report_lfs_error("df", err);
        return;
    }

    my_printf(DEBUG_USART,
              "LFS: DF total=%lu used=%lu free=%lu block=%lu blocks=%lu used_blocks=%lu cwd=%s\r\n",
              (unsigned long)info.total_bytes,
              (unsigned long)info.used_bytes,
              (unsigned long)(info.total_bytes - info.used_bytes),
              (unsigned long)info.block_size,
              (unsigned long)info.block_count,
              (unsigned long)info.used_blocks,
              g_uart_current_dir);
}

/*
 * 函数作用：
 *   读取当前 RTC 的完整年月日时分秒，并按串口协议格式回显给操作者。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
static void prv_uart_handle_gettime(void)
{
    bsp_rtc_datetime_t datetime;

    if(0 != bsp_rtc_get_datetime(&datetime)){
        my_printf(DEBUG_USART, "RTC: read failed\r\n");
        return;
    }

    prv_uart_send_rtc_datetime("RTC: ", &datetime);
}

/*
 * 函数作用：
 *   解析并设置 RTC 的完整年月日时分秒，成功后立即读回确认最终生效值。
 * 主要流程：
 *   1. 在串口命令层解析 `yyyy-mm-dd hh:mm:ss` 文本，并区分缺参、格式错、范围错。
 *   2. 调用驱动层接口写 RTC，避免命令层直接碰底层寄存器格式。
 *   3. 成功后读回一遍时间并按统一格式输出，便于操作者确认最终结果。
 * 参数说明：
 *   args：`settime` 命令参数字符串，必须包含完整年月日时分秒。
 * 返回值说明：
 *   无返回值。
 */
static void prv_uart_handle_settime(char *args)
{
    bsp_rtc_datetime_t datetime;
    uart_rtc_parse_result_t parse_result;

    parse_result = prv_uart_parse_datetime_args(args, &datetime);
    if(UART_RTC_PARSE_MISSING_ARGS == parse_result){
        my_printf(DEBUG_USART, "RTC: settime usage: settime yyyy-mm-dd hh:mm:ss\r\n");
        return;
    }

    if(UART_RTC_PARSE_BAD_FORMAT == parse_result){
        my_printf(DEBUG_USART, "RTC: bad format, use yyyy-mm-dd hh:mm:ss\r\n");
        return;
    }

    if(UART_RTC_PARSE_OUT_OF_RANGE == parse_result){
        my_printf(DEBUG_USART, "RTC: out of range\r\n");
        return;
    }

    if(0 != bsp_rtc_set_datetime(&datetime)){
        my_printf(DEBUG_USART, "RTC: set failed\r\n");
        return;
    }

    if(0 != bsp_rtc_get_datetime(&datetime)){
        my_printf(DEBUG_USART, "RTC: set ok but readback failed\r\n");
        return;
    }

    prv_uart_send_rtc_datetime("RTC: SET OK ", &datetime);
}

/*
 * 函数作用：
 *   输出指定路径或当前目录的类型和大小信息。
 * 参数说明：
 *   args：可选路径参数；为空时默认查询当前工作目录。
 * 返回值说明：
 *   无返回值。
 */
static void prv_uart_handle_stat(char *args)
{
    char resolved_path[UART_LFS_PATH_BUFFER_SIZE];
    lfs_storage_path_info_t path_info;
    char *target_path;
    int err;

    target_path = prv_uart_skip_spaces(args);
    if((NULL == target_path) || ('\0' == *target_path)){
        target_path = g_uart_current_dir;
    }

    prv_uart_rstrip_spaces(target_path);
    if(!prv_uart_resolve_path(target_path, resolved_path, sizeof(resolved_path))){
        my_printf(DEBUG_USART, "LFS: stat bad path\r\n");
        return;
    }

    err = lfs_storage_get_path_info(resolved_path, &path_info);
    if(LFS_ERR_OK != err){
        prv_uart_report_lfs_error("stat", err);
        return;
    }

    if(0U == path_info.exists){
        my_printf(DEBUG_USART, "LFS: stat no entry %s\r\n", resolved_path);
        return;
    }

    my_printf(DEBUG_USART,
              "LFS: STAT path=%s type=%s size=%lu\r\n",
              resolved_path,
              (LFS_TYPE_DIR == path_info.type) ? "dir" : "file",
              (unsigned long)path_info.size);
}

/*
 * 函数作用：
 *   `ls` 目录遍历回调，把单个目录项格式化输出到串口。
 * 参数说明：
 *   entry：当前目录项信息指针，必须非空。
 *   context：预留上下文，本实现未使用。
 * 返回值说明：
 *   无返回值。
 */
static void prv_uart_ls_entry_callback(const lfs_storage_dir_entry_t *entry, void *context)
{
    (void)context;

    if(NULL == entry){
        return;
    }

    my_printf(DEBUG_USART,
              "%s\t%lu\t%s\r\n",
              (LFS_TYPE_DIR == entry->info.type) ? "dir" : "file",
              (unsigned long)entry->display_size,
              entry->info.name);
}

/*
 * 函数作用：
 *   列出指定目录或当前目录下的文件和子目录。
 * 参数说明：
 *   args：可选目录参数；为空时默认列当前目录。
 * 返回值说明：
 *   无返回值。
 */
static void prv_uart_handle_ls(char *args)
{
    char resolved_path[UART_LFS_PATH_BUFFER_SIZE];
    lfs_storage_path_info_t path_info;
    char *target_path;
    uint32_t entry_count;
    int err;

    target_path = prv_uart_skip_spaces(args);
    if((NULL == target_path) || ('\0' == *target_path)){
        target_path = g_uart_current_dir;
    }

    prv_uart_rstrip_spaces(target_path);
    if(!prv_uart_resolve_path(target_path, resolved_path, sizeof(resolved_path))){
        my_printf(DEBUG_USART, "LFS: ls bad path\r\n");
        return;
    }

    err = lfs_storage_get_path_info(resolved_path, &path_info);
    if(LFS_ERR_OK != err){
        prv_uart_report_lfs_error("ls", err);
        return;
    }

    if(0U == path_info.exists){
        my_printf(DEBUG_USART, "LFS: ls no entry %s\r\n", resolved_path);
        return;
    }

    if(LFS_TYPE_DIR != path_info.type){
        my_printf(DEBUG_USART, "LFS: ls not dir %s\r\n", resolved_path);
        return;
    }

    my_printf(DEBUG_USART, "LFS: LS %s\r\n", resolved_path);
    err = lfs_storage_list_dir(resolved_path,
                               prv_uart_ls_entry_callback,
                               NULL,
                               &entry_count);
    if(LFS_ERR_OK != err){
        prv_uart_report_lfs_error("ls", err);
        return;
    }

    my_printf(DEBUG_USART, "LFS: LS done count=%lu\r\n", (unsigned long)entry_count);
}

/*
 * 函数作用：
 *   切换当前工作目录。
 * 参数说明：
 *   args：目录参数字符串，必须包含目标路径。
 * 返回值说明：
 *   无返回值。
 */
static void prv_uart_handle_cd(char *args)
{
    char resolved_path[UART_LFS_PATH_BUFFER_SIZE];
    lfs_storage_path_info_t path_info;
    char *target_path;
    int err;

    target_path = prv_uart_skip_spaces(args);
    if((NULL == target_path) || ('\0' == *target_path)){
        my_printf(DEBUG_USART, "LFS: cd missing path\r\n");
        return;
    }

    prv_uart_rstrip_spaces(target_path);
    if(!prv_uart_resolve_path(target_path, resolved_path, sizeof(resolved_path))){
        my_printf(DEBUG_USART, "LFS: cd bad path\r\n");
        return;
    }

    err = lfs_storage_get_path_info(resolved_path, &path_info);
    if(LFS_ERR_OK != err){
        prv_uart_report_lfs_error("cd", err);
        return;
    }

    if(0U == path_info.exists){
        my_printf(DEBUG_USART, "LFS: cd no dir %s\r\n", resolved_path);
        return;
    }

    if(LFS_TYPE_DIR != path_info.type){
        my_printf(DEBUG_USART, "LFS: cd not dir %s\r\n", resolved_path);
        return;
    }

    (void)strcpy(g_uart_current_dir, resolved_path);
    my_printf(DEBUG_USART, "LFS: CWD %s\r\n", g_uart_current_dir);
}

/*
 * 函数作用：
 *   读取指定文件并把内容回显到串口。
 * 参数说明：
 *   args：文件路径参数字符串，必须包含目标文件。
 * 返回值说明：
 *   无返回值。
 */
static void prv_uart_handle_cat(char *args)
{
    char resolved_path[UART_LFS_PATH_BUFFER_SIZE];
    uint32_t read_length;
    char *target_path;
    int err;

    target_path = prv_uart_skip_spaces(args);
    if((NULL == target_path) || ('\0' == *target_path)){
        my_printf(DEBUG_USART, "LFS: cat missing path\r\n");
        return;
    }

    prv_uart_rstrip_spaces(target_path);
    if(!prv_uart_resolve_path(target_path, resolved_path, sizeof(resolved_path))){
        my_printf(DEBUG_USART, "LFS: cat bad path\r\n");
        return;
    }

    err = lfs_storage_read_file(resolved_path,
                                uart_file_buffer,
                                sizeof(uart_file_buffer),
                                &read_length);
    if(LFS_ERR_NOENT == err){
        my_printf(DEBUG_USART, "LFS: cat no file %s\r\n", resolved_path);
        return;
    }

    if(LFS_ERR_FBIG == err){
        my_printf(DEBUG_USART, "LFS: cat file too large for uart buffer\r\n");
        return;
    }

    if(LFS_ERR_ISDIR == err){
        my_printf(DEBUG_USART, "LFS: cat is dir %s\r\n", resolved_path);
        return;
    }

    if(LFS_ERR_OK != err){
        prv_uart_report_lfs_error("cat", err);
        return;
    }

    my_printf(DEBUG_USART,
              "LFS: CAT OK len=%lu path=%s\r\n",
              (unsigned long)read_length,
              resolved_path);
    if(read_length > 0U){
        prv_uart_send_bytes(uart_file_buffer, (uint16_t)read_length);
    }
    prv_uart_send_text("\r\n");
}

/*
 * 函数作用：
 *   向指定文件执行覆盖写或追加写，并立即读回校验。
 * 参数说明：
 *   args：参数字符串，格式必须为 `[-a] <path> <text>`；不带 `-a` 时覆盖写，带 `-a` 时追加写。
 * 返回值说明：
 *   无返回值。
 */
static void prv_uart_handle_write(char *args)
{
    uart_shell_write_args_t parsed_args;
    char resolved_path[UART_LFS_PATH_BUFFER_SIZE];
    uint32_t read_length;
    uint32_t expected_length;
    uint32_t original_length;
    lfs_storage_path_info_t path_info;
    int err;

    if(!prv_uart_parse_write_args(args, &parsed_args)){
        my_printf(DEBUG_USART, "LFS: write usage: write [-a] <file> <text>\r\n");
        return;
    }

    if(!prv_uart_resolve_path(parsed_args.path, resolved_path, sizeof(resolved_path))){
        my_printf(DEBUG_USART, "LFS: write bad path\r\n");
        return;
    }

    expected_length = (uint32_t)strlen(parsed_args.text);
    original_length = 0U;

    if(0U != parsed_args.append_mode){
        err = lfs_storage_get_path_info(resolved_path, &path_info);
        if(LFS_ERR_NOENT == err){
            original_length = 0U;
        }else if(LFS_ERR_OK == err){
            if((0U == path_info.exists) || (LFS_TYPE_REG != path_info.type)){
                my_printf(DEBUG_USART, "LFS: write is dir %s\r\n", resolved_path);
                return;
            }

            original_length = path_info.size;
        }else if(LFS_ERR_ISDIR == err){
            my_printf(DEBUG_USART, "LFS: write is dir %s\r\n", resolved_path);
            return;
        }else{
            prv_uart_report_lfs_error("write", err);
            return;
        }

        err = lfs_storage_append_file(resolved_path,
                                      (const uint8_t *)parsed_args.text,
                                      expected_length);
    }else{
        err = lfs_storage_write_file(resolved_path,
                                     (const uint8_t *)parsed_args.text,
                                     expected_length);
    }

    if(LFS_ERR_NOENT == err){
        my_printf(DEBUG_USART, "LFS: write parent missing %s\r\n", resolved_path);
        return;
    }

    if(LFS_ERR_ISDIR == err){
        my_printf(DEBUG_USART, "LFS: write is dir %s\r\n", resolved_path);
        return;
    }

    if(LFS_ERR_OK != err){
        prv_uart_report_lfs_error("write", err);
        return;
    }

    err = lfs_storage_read_file(resolved_path,
                                uart_file_buffer,
                                sizeof(uart_file_buffer),
                                &read_length);
    if(LFS_ERR_OK != err){
        my_printf(DEBUG_USART, "LFS: write verify failed (%d)\r\n", err);
        return;
    }

    if(0U != parsed_args.append_mode){
        if(read_length != (original_length + expected_length)){
            my_printf(DEBUG_USART, "LFS: write verify mismatch\r\n");
            return;
        }

        if(expected_length > 0U){
            if(read_length < expected_length){
                my_printf(DEBUG_USART, "LFS: write verify mismatch\r\n");
                return;
            }

            if(0 != memcmp(&uart_file_buffer[read_length - expected_length],
                           parsed_args.text,
                           expected_length)){
                my_printf(DEBUG_USART, "LFS: write verify mismatch\r\n");
                return;
            }
        }
    }else{
        if((read_length != expected_length) ||
           (0 != memcmp(uart_file_buffer, parsed_args.text, read_length))){
            my_printf(DEBUG_USART, "LFS: write verify mismatch\r\n");
            return;
        }
    }

    my_printf(DEBUG_USART,
              "LFS: WRITE OK mode=%s len=%lu path=%s\r\n",
              (0U != parsed_args.append_mode) ? "append" : "overwrite",
              (unsigned long)read_length,
              resolved_path);
    if(read_length > 0U){
        prv_uart_send_bytes(uart_file_buffer, (uint16_t)read_length);
    }
    prv_uart_send_text("\r\n");
}

/*
 * 函数作用：
 *   创建目录。
 * 参数说明：
 *   args：目录路径参数字符串，必须包含目标目录。
 * 返回值说明：
 *   无返回值。
 */
static void prv_uart_handle_mkdir(char *args)
{
    char resolved_path[UART_LFS_PATH_BUFFER_SIZE];
    char *target_path;
    int err;

    target_path = prv_uart_skip_spaces(args);
    if((NULL == target_path) || ('\0' == *target_path)){
        my_printf(DEBUG_USART, "LFS: mkdir missing path\r\n");
        return;
    }

    prv_uart_rstrip_spaces(target_path);
    if(!prv_uart_resolve_path(target_path, resolved_path, sizeof(resolved_path))){
        my_printf(DEBUG_USART, "LFS: mkdir bad path\r\n");
        return;
    }

    err = lfs_storage_mkdir(resolved_path);
    if(LFS_ERR_EXIST == err){
        my_printf(DEBUG_USART, "LFS: mkdir exists %s\r\n", resolved_path);
        return;
    }

    if(LFS_ERR_NOENT == err){
        my_printf(DEBUG_USART, "LFS: mkdir parent missing %s\r\n", resolved_path);
        return;
    }

    if(LFS_ERR_OK != err){
        prv_uart_report_lfs_error("mkdir", err);
        return;
    }

    my_printf(DEBUG_USART, "LFS: MKDIR OK path=%s\r\n", resolved_path);
}

/*
 * 函数作用：
 *   创建空文件；若文件已存在则保持原样返回。
 * 参数说明：
 *   args：文件路径参数字符串，必须包含目标文件。
 * 返回值说明：
 *   无返回值。
 */
static void prv_uart_handle_touch(char *args)
{
    char resolved_path[UART_LFS_PATH_BUFFER_SIZE];
    char *target_path;
    int err;

    target_path = prv_uart_skip_spaces(args);
    if((NULL == target_path) || ('\0' == *target_path)){
        my_printf(DEBUG_USART, "LFS: touch missing path\r\n");
        return;
    }

    prv_uart_rstrip_spaces(target_path);
    if(!prv_uart_resolve_path(target_path, resolved_path, sizeof(resolved_path))){
        my_printf(DEBUG_USART, "LFS: touch bad path\r\n");
        return;
    }

    err = lfs_storage_touch_file(resolved_path);
    if(LFS_ERR_NOENT == err){
        my_printf(DEBUG_USART, "LFS: touch parent missing %s\r\n", resolved_path);
        return;
    }

    if(LFS_ERR_ISDIR == err){
        my_printf(DEBUG_USART, "LFS: touch is dir %s\r\n", resolved_path);
        return;
    }

    if(LFS_ERR_OK != err){
        prv_uart_report_lfs_error("touch", err);
        return;
    }

    my_printf(DEBUG_USART, "LFS: TOUCH OK path=%s\r\n", resolved_path);
}

/*
 * 函数作用：
 *   删除指定文件或目录；目录会递归删除全部子项。
 * 参数说明：
 *   args：目标路径参数字符串，必须包含待删除路径。
 * 返回值说明：
 *   无返回值。
 */
static void prv_uart_handle_rm(char *args)
{
    char resolved_path[UART_LFS_PATH_BUFFER_SIZE];
    char *target_path;
    int err;

    target_path = prv_uart_skip_spaces(args);
    if((NULL == target_path) || ('\0' == *target_path)){
        my_printf(DEBUG_USART, "LFS: rm missing path\r\n");
        return;
    }

    prv_uart_rstrip_spaces(target_path);
    if(!prv_uart_resolve_path(target_path, resolved_path, sizeof(resolved_path))){
        my_printf(DEBUG_USART, "LFS: rm bad path\r\n");
        return;
    }

    if(0 == strcmp(resolved_path, "/")){
        my_printf(DEBUG_USART, "LFS: rm reject root /\r\n");
        return;
    }

    err = lfs_storage_remove_path(resolved_path);
    if(LFS_ERR_NOENT == err){
        my_printf(DEBUG_USART, "LFS: rm no entry %s\r\n", resolved_path);
        return;
    }

    if(LFS_ERR_INVAL == err){
        my_printf(DEBUG_USART, "LFS: rm bad path\r\n");
        return;
    }

    if(LFS_ERR_OK != err){
        prv_uart_report_lfs_error("rm", err);
        return;
    }

    my_printf(DEBUG_USART, "LFS: RM OK path=%s\r\n", resolved_path);
}

/*
 * 函数作用：
 *   对一帧 USART0 文本命令做解析，并分发到对应的 LittleFS 或 RTC 操作。
 * 主要流程：
 *   1. 去掉命令尾部回车换行，得到纯命令字符串。
 *   2. 识别 `help/gettime/settime/pwd/ls/cd/cat/write/mkdir/touch/rm/stat/df` 等命令。
 *   3. 对未知命令返回错误提示并附带帮助文本。
 * 参数说明：
 *   command_buffer：待解析命令缓冲区，必须非空。
 *   command_length：当前缓冲区有效长度，单位为字节。
 * 返回值说明：
 *   无返回值。
 */
static void prv_uart_process_command(uint8_t *command_buffer, uint16_t command_length)
{
    uart_shell_command_t parsed;

    if((NULL == command_buffer) || (0U == command_length)){
        my_printf(DEBUG_USART, "LFS: empty command\r\n");
        return;
    }

    command_length = prv_uart_trim_command(command_buffer, command_length);
    if(0U == command_length){
        my_printf(DEBUG_USART, "LFS: empty command\r\n");
        return;
    }

    if(!prv_uart_parse_command((char *)command_buffer, &parsed)){
        my_printf(DEBUG_USART, "LFS: empty command\r\n");
        return;
    }

    if(0 == strcmp(parsed.command, "help")){
        prv_uart_send_help();
        return;
    }

    if(0 == strcmp(parsed.command, "gettime")){
        prv_uart_handle_gettime();
        return;
    }

    if(0 == strcmp(parsed.command, "settime")){
        prv_uart_handle_settime(parsed.args);
        return;
    }

    if(0 == strcmp(parsed.command, "pwd")){
        prv_uart_handle_pwd();
        return;
    }

    if(0 == strcmp(parsed.command, "df")){
        prv_uart_handle_df();
        return;
    }

    if(0 == strcmp(parsed.command, "ls")){
        prv_uart_handle_ls(parsed.args);
        return;
    }

    if(0 == strcmp(parsed.command, "cd")){
        prv_uart_handle_cd(parsed.args);
        return;
    }

    if(0 == strcmp(parsed.command, "cat")){
        prv_uart_handle_cat(parsed.args);
        return;
    }

    if(0 == strcmp(parsed.command, "write")){
        prv_uart_handle_write(parsed.args);
        return;
    }

    if(0 == strcmp(parsed.command, "mkdir")){
        prv_uart_handle_mkdir(parsed.args);
        return;
    }

    if(0 == strcmp(parsed.command, "touch")){
        prv_uart_handle_touch(parsed.args);
        return;
    }

    if(0 == strcmp(parsed.command, "rm")){
        prv_uart_handle_rm(parsed.args);
        return;
    }

    if(0 == strcmp(parsed.command, "stat")){
        prv_uart_handle_stat(parsed.args);
        return;
    }

    my_printf(DEBUG_USART, "LFS: unknown command\r\n");
    prv_uart_send_help();
}

/*
 * 函数作用：
 *   周期性处理 USART0 上收到的文本调试命令。
 * 主要流程：
 *   1. 从 USART0 共享缓冲区原子取出一帧命令到任务层私有缓冲区。
 *   2. 在任务上下文完成文件系统命令解析、RTC 校时命令处理和串口回显。
 *   3. 不再把 USART0 数据转发到 RS485，保持串口0专用于调试命令壳层。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void uart_task(void)
{
    uint16_t rx_length;

    rx_length = prv_uart_take_frame(&rx_flag,
                                    &uart_dma_length,
                                    uart_dma_buffer,
                                    uart_command_buffer,
                                    (uint16_t)sizeof(uart_command_buffer));
    if(rx_length > 0U){
        prv_uart_process_command(uart_command_buffer, rx_length);
    }
}
