/* Licence
* Company: MCUSTUDIO
* Auther: Ahypnis.
* Version: V0.10
* Time: 2025/06/05
* Note:
*/
#ifndef MCU_CMIC_GD32F470VET6_H
#define MCU_CMIC_GD32F470VET6_H

#include "gd32f4xx.h"
#include "gd32f4xx_sdio.h"
#include "gd32f4xx_dma.h"
#include "systick.h"

#include "ebtn.h"
#include "oled.h"
#include "gd25qxx.h"
#include "gd30ad3344.h"
#include "sdio_sdcard.h"
#include "ff.h"
#include "diskio.h"

#include "sd_app.h"
#include "led_app.h"
#include "adc_app.h"
#include "oled_app.h"
#include "usart_app.h"
#include "rtc_app.h"
#include "btn_app.h"
#include "scheduler.h"

#include "perf_counter.h"

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************************************************************/
/* V2 pin map */
/***************************************************************************************************************/
/* SDIO */
#define SD_CD_PORT           GPIOE
#define SD_CD_PIN            GPIO_PIN_2
#define SD_CMD_PORT          GPIOD
#define SD_CMD_PIN           GPIO_PIN_2
#define SD_CLK_PORT          GPIOC
#define SD_CLK_PIN           GPIO_PIN_12
#define SD_DAT3_PORT         GPIOC
#define SD_DAT3_PIN          GPIO_PIN_11
#define SD_DAT2_PORT         GPIOC
#define SD_DAT2_PIN          GPIO_PIN_10
#define SD_DAT1_PORT         GPIOC
#define SD_DAT1_PIN          GPIO_PIN_9
#define SD_DAT0_PORT         GPIOC
#define SD_DAT0_PIN          GPIO_PIN_8

/***************************************************************************************************************/
/* ADC */
#define ADC_IN10_PORT        GPIOC
#define ADC_IN10_PIN         GPIO_PIN_0
#define ADC_IN12_PORT        GPIOC
#define ADC_IN12_PIN         GPIO_PIN_2

/***************************************************************************************************************/
/* ETH */
#define ETH_MDC_PORT         GPIOC
#define ETH_MDC_PIN          GPIO_PIN_1
#define ETH_REF_CLK_PORT     GPIOA
#define ETH_REF_CLK_PIN      GPIO_PIN_1
#define ETH_MDIO_PORT        GPIOA
#define ETH_MDIO_PIN         GPIO_PIN_2
#define ETH_CRS_DV_PORT      GPIOA
#define ETH_CRS_DV_PIN       GPIO_PIN_7
#define ETH_RXD0_PORT        GPIOC
#define ETH_RXD0_PIN         GPIO_PIN_4
#define ETH_RXD1_PORT        GPIOC
#define ETH_RXD1_PIN         GPIO_PIN_5
#define ETH_TX_EN_PORT       GPIOB
#define ETH_TX_EN_PIN        GPIO_PIN_11
#define ETH_TXD0_PORT        GPIOB
#define ETH_TXD0_PIN         GPIO_PIN_12
#define ETH_TXD1_PORT        GPIOB
#define ETH_TXD1_PIN         GPIO_PIN_13

/***************************************************************************************************************/
/* Control */
#define WK_UP_PORT           GPIOA
#define WK_UP_PIN            GPIO_PIN_0
#define W_RST_PORT           GPIOA
#define W_RST_PIN            GPIO_PIN_3
#define PHY_RST_PORT         GPIOD
#define PHY_RST_PIN          GPIO_PIN_3

/***************************************************************************************************************/
/* DAC */
#define DAC0_OUT_PORT        GPIOA
#define DAC0_OUT_PIN         GPIO_PIN_4
#define DAC1_OUT_PORT        GPIOA
#define DAC1_OUT_PIN         GPIO_PIN_5

/***************************************************************************************************************/
/* CS/GPIO */
#define RS485_CS_PORT        GPIOE
#define RS485_CS_PIN         GPIO_PIN_8
#define W_SPI_CS_PORT        GPIOE
#define W_SPI_CS_PIN         GPIO_PIN_9
#define S_SPI_CS_PORT        GPIOE
#define S_SPI_CS_PIN         GPIO_PIN_10

/***************************************************************************************************************/
/* SPI3 */
#define SPI3_SCK_PORT        GPIOE
#define SPI3_SCK_PIN         GPIO_PIN_12
#define SPI3_MISO_PORT       GPIOE
#define SPI3_MISO_PIN        GPIO_PIN_13
#define SPI3_MOSI_PORT       GPIOE
#define SPI3_MOSI_PIN        GPIO_PIN_14

/***************************************************************************************************************/
/* OLED (I2C0) */
#define OLED_DAT_PORT        GPIOB
#define OLED_DAT_PIN_V2      GPIO_PIN_9
#define OLED_CLK_PORT_V2     GPIOB
#define OLED_CLK_PIN_V2      GPIO_PIN_8

/***************************************************************************************************************/
/* SPI FLASH (SPI0) */
#define SPI_FLASH_SCK_PORT   GPIOB
#define SPI_FLASH_SCK_PIN    GPIO_PIN_3
#define SPI_FLASH_MISO_PORT  GPIOB
#define SPI_FLASH_MISO_PIN   GPIO_PIN_4
#define SPI_FLASH_MOSI_PORT  GPIOB
#define SPI_FLASH_MOSI_PIN   GPIO_PIN_5
#define SPI_FLASH_CS_PORT    GPIOA
#define SPI_FLASH_CS_PIN     GPIO_PIN_15

/***************************************************************************************************************/
/* USART */
#define USART1_RX_PORT       GPIOD
#define USART1_RX_PIN        GPIO_PIN_6
#define USART1_TX_PORT       GPIOD
#define USART1_TX_PIN        GPIO_PIN_5
#define USART0_RX_PORT       GPIOA
#define USART0_RX_PIN        GPIO_PIN_10
#define USART0_TX_PORT       GPIOA
#define USART0_TX_PIN        GPIO_PIN_9
#define USART5_RX_PORT       GPIOC
#define USART5_RX_PIN        GPIO_PIN_7
#define USART5_TX_PORT       GPIOC
#define USART5_TX_PIN        GPIO_PIN_6
#define USART2_RX_PORT       GPIOD
#define USART2_RX_PIN        GPIO_PIN_9
#define USART2_TX_PORT       GPIOD
#define USART2_TX_PIN        GPIO_PIN_8

/***************************************************************************************************************/
/* V2 AF map (checked against GD32F470 datasheet peripheral mapping) */
#define AF_OLED_I2C0         GPIO_AF_4
#define AF_SPI_FLASH         GPIO_AF_5
#define AF_SPI3              GPIO_AF_5
#define AF_USART0            GPIO_AF_7
#define AF_USART1            GPIO_AF_7
#define AF_USART2            GPIO_AF_7
#define AF_USART5            GPIO_AF_8
#define AF_ETH               GPIO_AF_11
#define AF_SDIO              GPIO_AF_12

/***************************************************************************************************************/

/* LED IO map
 * LED1 -> PD10
 * LED2 -> PD11
 * LED3 -> PD12
 * LED4 -> PD13
 * LED5 -> PD14
 * LED6 -> PD15
 */
#define LED_PORT        GPIOD
#define LED_CLK_PORT    RCU_GPIOD

#define LED1_PIN        GPIO_PIN_10    /* PD10 */
#define LED2_PIN        GPIO_PIN_11    /* PD11 */
#define LED3_PIN        GPIO_PIN_12    /* PD12 */
#define LED4_PIN        GPIO_PIN_13    /* PD13 */
#define LED5_PIN        GPIO_PIN_14    /* PD14 */
#define LED6_PIN        GPIO_PIN_15    /* PD15 */

/* LED active level config: 1 = active-high, 0 = active-low */
#define LED_ACTIVE_HIGH 1

#if LED_ACTIVE_HIGH
#define LED_WRITE(pin, on) do { if(on) GPIO_BOP(LED_PORT) = (pin); else GPIO_BC(LED_PORT) = (pin); } while(0)
#else
#define LED_WRITE(pin, on) do { if(on) GPIO_BC(LED_PORT) = (pin); else GPIO_BOP(LED_PORT) = (pin); } while(0)
#endif

#define LED1_SET(x)     do { LED_WRITE(LED1_PIN, (x)); } while(0)
#define LED2_SET(x)     do { LED_WRITE(LED2_PIN, (x)); } while(0)
#define LED3_SET(x)     do { LED_WRITE(LED3_PIN, (x)); } while(0)
#define LED4_SET(x)     do { LED_WRITE(LED4_PIN, (x)); } while(0)
#define LED5_SET(x)     do { LED_WRITE(LED5_PIN, (x)); } while(0)
#define LED6_SET(x)     do { LED_WRITE(LED6_PIN, (x)); } while(0)

#define LED1_TOGGLE     do { GPIO_TG(LED_PORT) = LED1_PIN; } while(0)
#define LED2_TOGGLE     do { GPIO_TG(LED_PORT) = LED2_PIN; } while(0)
#define LED3_TOGGLE     do { GPIO_TG(LED_PORT) = LED3_PIN; } while(0)
#define LED4_TOGGLE     do { GPIO_TG(LED_PORT) = LED4_PIN; } while(0)
#define LED5_TOGGLE     do { GPIO_TG(LED_PORT) = LED5_PIN; } while(0)
#define LED6_TOGGLE     do { GPIO_TG(LED_PORT) = LED6_PIN; } while(0)

#define LED1_ON         do { LED_WRITE(LED1_PIN, 1); } while(0)
#define LED2_ON         do { LED_WRITE(LED2_PIN, 1); } while(0)
#define LED3_ON         do { LED_WRITE(LED3_PIN, 1); } while(0)
#define LED4_ON         do { LED_WRITE(LED4_PIN, 1); } while(0)
#define LED5_ON         do { LED_WRITE(LED5_PIN, 1); } while(0)
#define LED6_ON         do { LED_WRITE(LED6_PIN, 1); } while(0)

#define LED1_OFF        do { LED_WRITE(LED1_PIN, 0); } while(0)
#define LED2_OFF        do { LED_WRITE(LED2_PIN, 0); } while(0)
#define LED3_OFF        do { LED_WRITE(LED3_PIN, 0); } while(0)
#define LED4_OFF        do { LED_WRITE(LED4_PIN, 0); } while(0)
#define LED5_OFF        do { LED_WRITE(LED5_PIN, 0); } while(0)
#define LED6_OFF        do { LED_WRITE(LED6_PIN, 0); } while(0)


// FUNCTION
void bsp_led_init(void);

/***************************************************************************************************************/
/* KEY IO map
 * KEY1 -> PE15
 * KEY2 -> PE6
 * KEY3 -> PE11
 * KEY4 -> PE4
 * KEY5 -> PE7
 * KEY6 -> PB0
 * KEYW -> PA0, wakeup key
 */
#define KEYE_PORT        GPIOE
#define KEYB_PORT        GPIOB
#define KEYA_PORT        GPIOA
#define KEYE_CLK_PORT    RCU_GPIOE
#define KEYB_CLK_PORT    RCU_GPIOB
#define KEYA_CLK_PORT    RCU_GPIOA

#define KEY1_PIN        GPIO_PIN_15    /* PE15 */
#define KEY2_PIN        GPIO_PIN_6     /* PE6  */
#define KEY3_PIN        GPIO_PIN_11    /* PE11 */
#define KEY4_PIN        GPIO_PIN_4     /* PE4  */
#define KEY5_PIN        GPIO_PIN_7     /* PE7  */
#define KEY6_PIN        GPIO_PIN_0     /* PB0  */
#define KEYW_PIN        GPIO_PIN_0     /* PA0  */

#define KEY1_READ       gpio_input_bit_get(KEYE_PORT, KEY1_PIN)    /* PE15 */
#define KEY2_READ       gpio_input_bit_get(KEYE_PORT, KEY2_PIN)    /* PE6  */
#define KEY3_READ       gpio_input_bit_get(KEYE_PORT, KEY3_PIN)    /* PE11 */
#define KEY4_READ       gpio_input_bit_get(KEYE_PORT, KEY4_PIN)    /* PE4  */
#define KEY5_READ       gpio_input_bit_get(KEYE_PORT, KEY5_PIN)    /* PE7  */
#define KEY6_READ       gpio_input_bit_get(KEYB_PORT, KEY6_PIN)    /* PB0  */
#define KEYW_READ       gpio_input_bit_get(KEYA_PORT, KEYW_PIN)    /* PA0  */

// FUNCTION
void bsp_btn_init(void);
void bsp_wkup_key_exti_init(void);
void bsp_enter_deepsleep(void);

/***************************************************************************************************************/

/* OLED */
#define I2C0_OWN_ADDRESS7      0x72
#define I2C0_SLAVE_ADDRESS7    0x82
#define I2C0_DATA_ADDRESS      (uint32_t)&I2C_DATA(I2C0)

#define OLED_PORT        OLED_DAT_PORT
#define OLED_CLK_PORT    RCU_GPIOB
#define OLED_DAT_PIN     OLED_DAT_PIN_V2
#define OLED_CLK_PIN     OLED_CLK_PIN_V2

// FUNCTION
void bsp_oled_init(void);

/***************************************************************************************************************/

/* gd25qxx */

#define SPI_PORT              SPI_FLASH_SCK_PORT
#define SPI_CLK_PORT          RCU_GPIOB
#define SPI_CS_PORT           SPI_FLASH_CS_PORT
#define SPI_CS_CLK_PORT       RCU_GPIOA

#define SPI_NSS               SPI_FLASH_CS_PIN
#define SPI_SCK               SPI_FLASH_SCK_PIN
#define SPI_MISO              SPI_FLASH_MISO_PIN
#define SPI_MOSI              SPI_FLASH_MOSI_PIN

// FUNCTION
void bsp_gd25qxx_init(void);

/***************************************************************************************************************/

/* gd30ad3344 */

#define SPI3_PORT              SPI3_SCK_PORT
#define SPI3_CLK_PORT          RCU_GPIOE

#define SPI3_NSS               S_SPI_CS_PIN
#define SPI3_SCK               SPI3_SCK_PIN
#define SPI3_MISO              SPI3_MISO_PIN
#define SPI3_MOSI              SPI3_MOSI_PIN

// FUNCTION
void bsp_gd30ad3344_init(void);

/***************************************************************************************************************/

/* USART */
#define DEBUG_USART               (USART0)
#define USART0_RDATA_ADDRESS      ((uint32_t)&USART_DATA(USART0))
#define USART1_RDATA_ADDRESS      ((uint32_t)&USART_DATA(USART1))
#define USART2_RDATA_ADDRESS      ((uint32_t)&USART_DATA(USART2))
#define USART5_RDATA_ADDRESS      ((uint32_t)&USART_DATA(USART5))

#define USART0_RX_DMA_PERIPH      DMA1
#define USART0_RX_DMA_CHANNEL     DMA_CH5
#define USART0_RX_DMA_SUBPERI     DMA_SUBPERI4

#define USART1_RX_DMA_PERIPH      DMA0
#define USART1_RX_DMA_CHANNEL     DMA_CH5
#define USART1_RX_DMA_SUBPERI     DMA_SUBPERI4

#define USART2_RX_DMA_PERIPH      DMA0
#define USART2_RX_DMA_CHANNEL     DMA_CH1
#define USART2_RX_DMA_SUBPERI     DMA_SUBPERI4

#define USART5_RX_DMA_PERIPH      DMA1
#define USART5_RX_DMA_CHANNEL     DMA_CH1
#define USART5_RX_DMA_SUBPERI     DMA_SUBPERI5

#define USART0_PORT               USART0_TX_PORT
#define USART0_CLK_PORT           RCU_GPIOA

#define USART0_TX                 USART0_TX_PIN
#define USART0_RX                 USART0_RX_PIN

// FUNCTION
void bsp_usart0_init(void);
void bsp_usart_init(void);
void bsp_usart1_init(void);
void bsp_usart2_init(void);
void bsp_usart5_init(void);
void bsp_usart_all_init(void);

/***************************************************************************************************************/

/* ADC */
#define ADC1_PORT       ADC_IN10_PORT
#define ADC1_CLK_PORT   RCU_GPIOC

#define ADC1_PIN        ADC_IN10_PIN
#define ADC_VREF_PIN    ADC_IN12_PIN

// FUNCTION
void bsp_adc_init(void);

/***************************************************************************************************************/

/* DAC */
#define CONVERT_NUM                     (1)
#define DAC0_R12DH_ADDRESS              (0x40007408)  /* 12??????DAC??????????? */

#define DAC1_PORT       DAC0_OUT_PORT
#define DAC1_CLK_PORT   RCU_GPIOA

#define DAC1_PIN        DAC0_OUT_PIN

// FUNCTION
void bsp_dac_init(void);

/***************************************************************************************************************/

/* RTC */
#define RTC_CLOCK_SOURCE_LXTAL
#define BKP_VALUE    0x32F0

// FUNCTION
int bsp_rtc_init(void);

/***************************************************************************************************************/

#ifdef __cplusplus
  }
#endif

#endif /* MCU_CMIC_GD32F470VET6_H */


