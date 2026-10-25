/**
 * @file    mpu6050.h
 * @brief   MPU-6050 六轴传感器驱动接口（I2C，仅用加速度计）
 */
#ifndef __MPU6050_H
#define __MPU6050_H

#include <stdint.h>

/* MPU-6050 I2C 地址（AD0 接地 = 0x68，接 VCC = 0x69） */
#define MPU6050_ADDR_0   0x68
#define MPU6050_ADDR_1   0x69

/**
 * @brief 初始化：唤醒 + 配置加速度量程 ±2g
 * @param addr 设备地址
 */
void MPU6050_Init(uint8_t addr);

/**
 * @brief 读三轴加速度（单位 g）
 * @return 0 成功
 */
int MPU6050_ReadAccel(uint8_t addr, float *ax, float *ay, float *az);

#endif /* __MPU6050_H */
