/************************************************************
 * 版权：2025CIMC Copyright。
 * 文件：usart.c
 * 作者: Jialei Zhao
 * 平台: 2025CIMC IHD-V04
 * 版本: Jialei Zhao     2026/2/4     V0.01    original
************************************************************/

/************************* 头文件 *************************/
#include "usart.h"
/************************ 全局变量定义 ************************/
uint8_t usart0_tmp_buf[10 * 1024] = { 0 };
uint16_t usart0_tmp_buf_len = 0;

uint8_t usart0_recv_flag = 0;

/************************************************************
 * Function :       my_usart_init
 * Comment  :       用于初始化USART0端口
 * Parameter:       null
 * Return   :       null
 * Author   :       Jialei Zhao
 * Date     :       2026-02-04 V0.01 original
************************************************************/
void my_usart_init(void)
{
	//!开启接收的中断，主要用来接收不定长数据
	nvic_priority_group_set(NVIC_PRIGROUP_PRE4_SUB0);
	nvic_irq_enable(USART0_IRQn , 3 , 0);

	/* enable GPIO clock */
	rcu_periph_clock_enable(RCU_GPIOA);

	/* enable USART clock */
	rcu_periph_clock_enable(RCU_USART0);

	/* configure the USART0 TX pin and USART0 RX pin */
	gpio_af_set(GPIOA , GPIO_AF_7 , GPIO_PIN_9);
	gpio_af_set(GPIOA , GPIO_AF_7 , GPIO_PIN_10);

	/* configure USART0 TX as alternate function push-pull */
	gpio_mode_set(GPIOA , GPIO_MODE_AF , GPIO_PUPD_PULLUP , GPIO_PIN_9);
	gpio_output_options_set(GPIOA , GPIO_OTYPE_PP , GPIO_OSPEED_50MHZ , GPIO_PIN_9);

	/* configure USART0 RX as alternate function push-pull */
	gpio_mode_set(GPIOA , GPIO_MODE_AF , GPIO_PUPD_PULLUP , GPIO_PIN_10);
	gpio_output_options_set(GPIOA , GPIO_OTYPE_PP , GPIO_OSPEED_50MHZ , GPIO_PIN_10);

	/* USART configure */
	usart_deinit(USART0);
	usart_baudrate_set(USART0 , USART_DEFAULT_BAUDRATE);
	usart_receive_config(USART0 , USART_RECEIVE_ENABLE);
	usart_transmit_config(USART0 , USART_TRANSMIT_ENABLE);
	usart_enable(USART0);

	usart_interrupt_enable(USART0 , USART_INT_RBNE);
	usart_interrupt_enable(USART0 , USART_INT_IDLE);

}
/************************************************************
 * Function :       fputc
 * Comment  :       用于向USART0发送字符
 * Parameter:       ch 要发送的字符
 * Return   :       发送的字符
 * Author   :       Jialei Zhao
 * Date     :       2026-02-04 V0.01 original
************************************************************/
int fputc(int ch , FILE* f)
{
	usart_data_transmit(USART0 , (uint8_t)ch);
	while (RESET == usart_flag_get(USART0 , USART_FLAG_TBE));
	return ch;
}
/************************************************************
 * Function :       USART0_IRQHandler
 * Comment  :       用于处理USART0中断
 * Parameter:       null
 * Return   :       null
 * Author   :       Jialei Zhao
 * Date     :       2026-02-04 V0.01 original
************************************************************/
void USART0_IRQHandler(void)
{
	/* 关键修改: 添加缓冲区溢出保护 */
	if (RESET != usart_flag_get(USART0 , USART_FLAG_RBNE))
	{
		if (usart0_tmp_buf_len < sizeof(usart0_tmp_buf))
		{
			usart0_tmp_buf[usart0_tmp_buf_len++] = usart_data_receive(USART0);
		}
		else
		{
			/* 缓冲区满，丢弃数据并读取寄存器清除标志 */
			(void)usart_data_receive(USART0);
		}
	}

	/* 接收完成 */
	if (RESET != usart_flag_get(USART0 , USART_FLAG_IDLE) && usart0_tmp_buf_len > 0)
	{
		/* 清除中断标记位 */
		(void)usart_data_receive(USART0);

		/* 关键修改: 只有在未处理时才设置标志 */
		if (usart0_recv_flag == 0)
		{
			usart0_recv_flag = 1;

			/* 关键修改: 接收完成后禁用IDLE中断，防止重复触发 */
			usart_interrupt_disable(USART0 , USART_INT_IDLE);
			usart_interrupt_disable(USART0 , USART_INT_RBNE);
		}
	}
}

/****************************End*****************************/
