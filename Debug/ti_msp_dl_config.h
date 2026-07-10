/*
 * Copyright (c) 2023, Texas Instruments Incorporated - http://www.ti.com
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * *  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * *  Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 *  ============ ti_msp_dl_config.h =============
 *  Configured MSPM0 DriverLib module declarations
 *
 *  DO NOT EDIT - This file is generated for the MSPM0G350X
 *  by the SysConfig tool.
 */
#ifndef ti_msp_dl_config_h
#define ti_msp_dl_config_h

#define CONFIG_MSPM0G350X
#define CONFIG_MSPM0G3507

#if defined(__ti_version__) || defined(__TI_COMPILER_VERSION__)
#define SYSCONFIG_WEAK __attribute__((weak))
#elif defined(__IAR_SYSTEMS_ICC__)
#define SYSCONFIG_WEAK __weak
#elif defined(__GNUC__)
#define SYSCONFIG_WEAK __attribute__((weak))
#endif

#include <ti/devices/msp/msp.h>
#include <ti/driverlib/driverlib.h>
#include <ti/driverlib/m0p/dl_core.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 *  ======== SYSCFG_DL_init ========
 *  Perform all required MSP DL initialization
 *
 *  This function should be called once at a point before any use of
 *  MSP DL.
 */


/* clang-format off */

#define POWER_STARTUP_DELAY                                                (16)


#define GPIO_HFXT_PORT                                                     GPIOA
#define GPIO_HFXIN_PIN                                             DL_GPIO_PIN_5
#define GPIO_HFXIN_IOMUX                                         (IOMUX_PINCM10)
#define GPIO_HFXOUT_PIN                                            DL_GPIO_PIN_6
#define GPIO_HFXOUT_IOMUX                                        (IOMUX_PINCM11)
#define CPUCLK_FREQ                                                     80000000
/* Defines for SYSPLL_ERR_01 Workaround */
/* Represent 1.000 as 1000 */
#define FLOAT_TO_INT_SCALE                                               (1000U)
#define FCC_EXPECTED_RATIO                                                  2000
#define FCC_UPPER_BOUND                       (FCC_EXPECTED_RATIO * (1 + 0.003))
#define FCC_LOWER_BOUND                       (FCC_EXPECTED_RATIO * (1 - 0.003))

bool SYSCFG_DL_SYSCTL_SYSPLL_init(void);


/* Defines for PWMA */
#define PWMA_INST                                                          TIMG0
#define PWMA_INST_IRQHandler                                    TIMG0_IRQHandler
#define PWMA_INST_INT_IRQN                                      (TIMG0_INT_IRQn)
#define PWMA_INST_CLK_FREQ                                              40000000
/* GPIO defines for channel 0 */
#define GPIO_PWMA_C0_PORT                                                  GPIOA
#define GPIO_PWMA_C0_PIN                                          DL_GPIO_PIN_12
#define GPIO_PWMA_C0_IOMUX                                       (IOMUX_PINCM34)
#define GPIO_PWMA_C0_IOMUX_FUNC                      IOMUX_PINCM34_PF_TIMG0_CCP0
#define GPIO_PWMA_C0_IDX                                     DL_TIMER_CC_0_INDEX
/* GPIO defines for channel 1 */
#define GPIO_PWMA_C1_PORT                                                  GPIOA
#define GPIO_PWMA_C1_PIN                                          DL_GPIO_PIN_13
#define GPIO_PWMA_C1_IOMUX                                       (IOMUX_PINCM35)
#define GPIO_PWMA_C1_IOMUX_FUNC                      IOMUX_PINCM35_PF_TIMG0_CCP1
#define GPIO_PWMA_C1_IDX                                     DL_TIMER_CC_1_INDEX



/* Defines for COMPARE_0 */
#define COMPARE_0_INST                                                   (TIMA1)
#define COMPARE_0_INST_IRQHandler                               TIMA1_IRQHandler
#define COMPARE_0_INST_INT_IRQN                                 (TIMA1_INT_IRQn)
/* GPIO defines for channel 0 */
#define GPIO_COMPARE_0_C0_PORT                                             GPIOA
#define GPIO_COMPARE_0_C0_PIN                                     DL_GPIO_PIN_17
#define GPIO_COMPARE_0_C0_IOMUX                                  (IOMUX_PINCM39)
#define GPIO_COMPARE_0_C0_IOMUX_FUNC                 IOMUX_PINCM39_PF_TIMA1_CCP0

/* Defines for COMPARE_2 */
#define COMPARE_2_INST                                                   (TIMA0)
#define COMPARE_2_INST_IRQHandler                               TIMA0_IRQHandler
#define COMPARE_2_INST_INT_IRQN                                 (TIMA0_INT_IRQn)
/* GPIO defines for channel 0 */
#define GPIO_COMPARE_2_C0_PORT                                             GPIOA
#define GPIO_COMPARE_2_C0_PIN                                     DL_GPIO_PIN_21
#define GPIO_COMPARE_2_C0_IOMUX                                  (IOMUX_PINCM46)
#define GPIO_COMPARE_2_C0_IOMUX_FUNC                 IOMUX_PINCM46_PF_TIMA0_CCP0




/* Defines for TIMER_0 */
#define TIMER_0_INST                                                    (TIMG12)
#define TIMER_0_INST_IRQHandler                                TIMG12_IRQHandler
#define TIMER_0_INST_INT_IRQN                                  (TIMG12_INT_IRQn)
#define TIMER_0_INST_LOAD_VALUE                                        (799999U)



/* Defines for UART_0 */
#define UART_0_INST                                                        UART0
#define UART_0_INST_FREQUENCY                                           40000000
#define UART_0_INST_IRQHandler                                  UART0_IRQHandler
#define UART_0_INST_INT_IRQN                                      UART0_INT_IRQn
#define GPIO_UART_0_RX_PORT                                                GPIOA
#define GPIO_UART_0_TX_PORT                                                GPIOA
#define GPIO_UART_0_RX_PIN                                        DL_GPIO_PIN_11
#define GPIO_UART_0_TX_PIN                                        DL_GPIO_PIN_10
#define GPIO_UART_0_IOMUX_RX                                     (IOMUX_PINCM22)
#define GPIO_UART_0_IOMUX_TX                                     (IOMUX_PINCM21)
#define GPIO_UART_0_IOMUX_RX_FUNC                      IOMUX_PINCM22_PF_UART0_RX
#define GPIO_UART_0_IOMUX_TX_FUNC                      IOMUX_PINCM21_PF_UART0_TX
#define UART_0_BAUD_RATE                                                (115200)
#define UART_0_IBRD_40_MHZ_115200_BAUD                                      (21)
#define UART_0_FBRD_40_MHZ_115200_BAUD                                      (45)





/* Port definition for Pin Group ShowStatus */
#define ShowStatus_PORT                                                  (GPIOA)

/* Defines for LED: GPIOA.14 with pinCMx 36 on package pin 29 */
#define ShowStatus_LED_PIN                                      (DL_GPIO_PIN_14)
#define ShowStatus_LED_IOMUX                                     (IOMUX_PINCM36)
/* Defines for AIN1: GPIOA.8 with pinCMx 19 on package pin 16 */
#define DC_Motor_AIN1_PORT                                               (GPIOA)
#define DC_Motor_AIN1_PIN                                        (DL_GPIO_PIN_8)
#define DC_Motor_AIN1_IOMUX                                      (IOMUX_PINCM19)
/* Defines for AIN2: GPIOA.9 with pinCMx 20 on package pin 17 */
#define DC_Motor_AIN2_PORT                                               (GPIOA)
#define DC_Motor_AIN2_PIN                                        (DL_GPIO_PIN_9)
#define DC_Motor_AIN2_IOMUX                                      (IOMUX_PINCM20)
/* Defines for STBY: GPIOB.24 with pinCMx 52 on package pin 42 */
#define DC_Motor_STBY_PORT                                               (GPIOB)
#define DC_Motor_STBY_PIN                                       (DL_GPIO_PIN_24)
#define DC_Motor_STBY_IOMUX                                      (IOMUX_PINCM52)
/* Defines for BIN1: GPIOB.2 with pinCMx 15 on package pin 14 */
#define DC_Motor_BIN1_PORT                                               (GPIOB)
#define DC_Motor_BIN1_PIN                                        (DL_GPIO_PIN_2)
#define DC_Motor_BIN1_IOMUX                                      (IOMUX_PINCM15)
/* Defines for BIN2: GPIOB.3 with pinCMx 16 on package pin 15 */
#define DC_Motor_BIN2_PORT                                               (GPIOB)
#define DC_Motor_BIN2_PIN                                        (DL_GPIO_PIN_3)
#define DC_Motor_BIN2_IOMUX                                      (IOMUX_PINCM16)
/* Defines for PIN_0: GPIOB.16 with pinCMx 33 on package pin 26 */
#define GPIO_SENSOR_PIN_0_PORT                                           (GPIOB)
#define GPIO_SENSOR_PIN_0_PIN                                   (DL_GPIO_PIN_16)
#define GPIO_SENSOR_PIN_0_IOMUX                                  (IOMUX_PINCM33)
/* Defines for PIN_1: GPIOA.25 with pinCMx 55 on package pin 45 */
#define GPIO_SENSOR_PIN_1_PORT                                           (GPIOA)
#define GPIO_SENSOR_PIN_1_PIN                                   (DL_GPIO_PIN_25)
#define GPIO_SENSOR_PIN_1_IOMUX                                  (IOMUX_PINCM55)
/* Defines for PIN_2: GPIOA.23 with pinCMx 53 on package pin 43 */
#define GPIO_SENSOR_PIN_2_PORT                                           (GPIOA)
#define GPIO_SENSOR_PIN_2_PIN                                   (DL_GPIO_PIN_23)
#define GPIO_SENSOR_PIN_2_IOMUX                                  (IOMUX_PINCM53)
/* Defines for PIN_3: GPIOB.9 with pinCMx 26 on package pin 23 */
#define GPIO_SENSOR_PIN_3_PORT                                           (GPIOB)
#define GPIO_SENSOR_PIN_3_PIN                                    (DL_GPIO_PIN_9)
#define GPIO_SENSOR_PIN_3_IOMUX                                  (IOMUX_PINCM26)
/* Defines for PIN_4: GPIOB.7 with pinCMx 24 on package pin 21 */
#define GPIO_SENSOR_PIN_4_PORT                                           (GPIOB)
#define GPIO_SENSOR_PIN_4_PIN                                    (DL_GPIO_PIN_7)
#define GPIO_SENSOR_PIN_4_IOMUX                                  (IOMUX_PINCM24)
/* Defines for PIN_5: GPIOA.22 with pinCMx 47 on package pin 40 */
#define GPIO_SENSOR_PIN_5_PORT                                           (GPIOA)
#define GPIO_SENSOR_PIN_5_PIN                                   (DL_GPIO_PIN_22)
#define GPIO_SENSOR_PIN_5_IOMUX                                  (IOMUX_PINCM47)
/* Defines for PIN_6: GPIOB.19 with pinCMx 45 on package pin 38 */
#define GPIO_SENSOR_PIN_6_PORT                                           (GPIOB)
#define GPIO_SENSOR_PIN_6_PIN                                   (DL_GPIO_PIN_19)
#define GPIO_SENSOR_PIN_6_IOMUX                                  (IOMUX_PINCM45)
/* Defines for PIN_7: GPIOB.17 with pinCMx 43 on package pin 36 */
#define GPIO_SENSOR_PIN_7_PORT                                           (GPIOB)
#define GPIO_SENSOR_PIN_7_PIN                                   (DL_GPIO_PIN_17)
#define GPIO_SENSOR_PIN_7_IOMUX                                  (IOMUX_PINCM43)


/* clang-format on */

void SYSCFG_DL_init(void);
void SYSCFG_DL_initPower(void);
void SYSCFG_DL_GPIO_init(void);
void SYSCFG_DL_SYSCTL_init(void);

bool SYSCFG_DL_SYSCTL_SYSPLL_init(void);
void SYSCFG_DL_PWMA_init(void);
void SYSCFG_DL_COMPARE_0_init(void);
void SYSCFG_DL_COMPARE_2_init(void);
void SYSCFG_DL_TIMER_0_init(void);
void SYSCFG_DL_UART_0_init(void);


bool SYSCFG_DL_saveConfiguration(void);
bool SYSCFG_DL_restoreConfiguration(void);

#ifdef __cplusplus
}
#endif

#endif /* ti_msp_dl_config_h */
