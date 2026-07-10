#ifndef GRAYSCALE_H_
#define GRAYSCALE_H_

#include "ti_msp_dl_config.h"
#include <stdint.h>

/**
 * @brief  初始化灰度传感器模块
 * @note   引脚硬件初始化由 SysConfig 生成的 SYSCFG_DL_init() 完成
 */
void Grayscale_Init(void);

/**
 * @brief  读取 8 路灰度传感器电平状态
 * @return uint8_t 打包状态: Bit0=Pin0 ... Bit7=Pin7 (1:高电平 0:低电平)
 */
uint8_t Grayscale_Read(void);

/**
 * @brief  以二进制明文 (8 个 '1'/'0' + CRLF) 打印传感器状态
 * @param  value 打包的 8 路传感器状态
 * @note   经通用 UART 库 (g_uart0) 发送; 使用前需 UART_Init()
 */
void Grayscale_PrintBinary8(uint8_t value);

#endif /* GRAYSCALE_H_ */
