/**
 * @file    soft_i2c.h
 * @brief   软件模拟 I2C（bit-bang）驱动接口
 *
 * 用 GPIO 模拟 I2C 时序，避开 STM32F103 硬件 I2C 的 errata。
 * SCL=PB10, SDA=PB11（原 DHT11/渗水引脚，现空出）。
 */
#ifndef __SOFT_I2C_H
#define __SOFT_I2C_H

#include <stdint.h>

/* 软件 I2C 引脚 */
#define SOFT_I2C_SCL_PORT    GPIOB
#define SOFT_I2C_SCL_PIN     GPIO_PIN_10
#define SOFT_I2C_SDA_PORT    GPIOB
#define SOFT_I2C_SDA_PIN     GPIO_PIN_11

/**
 * @brief 初始化：使能 DWT 延时 + 配置 SCL/SDA 引脚
 */
void SoftI2C_Init(void);

/**
 * @brief 写 8 位寄存器地址 + 数据（SHT30/MPU-6050 用）
 * @param dev_addr 7 位设备地址
 * @return 0 成功，-1 NACK
 */
int SoftI2C_WriteReg(uint8_t dev_addr, uint8_t reg_addr, const uint8_t *data, uint16_t len);

/**
 * @brief 读 8 位寄存器地址 + 数据
 */
int SoftI2C_ReadReg(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint16_t len);

/**
 * @brief 写 16 位寄存器地址 + 数据（VL53L1X 用）
 */
int SoftI2C_WriteReg16(uint8_t dev_addr, uint16_t reg_addr, const uint8_t *data, uint16_t len);

/**
 * @brief 读 16 位寄存器地址 + 数据（VL53L1X 用）
 */
int SoftI2C_ReadReg16(uint8_t dev_addr, uint16_t reg_addr, uint8_t *data, uint16_t len);

/**
 * @brief 直接写数据（无寄存器地址，SHT30 发命令用）
 */
int SoftI2C_Write(uint8_t dev_addr, const uint8_t *data, uint16_t len);

/**
 * @brief 直接读数据（无寄存器地址，SHT30 读测量结果用）
 */
int SoftI2C_Read(uint8_t dev_addr, uint8_t *data, uint16_t len);

#endif /* __SOFT_I2C_H */
