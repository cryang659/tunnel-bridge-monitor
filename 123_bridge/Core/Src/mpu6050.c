/**
 * @file    mpu6050.c
 * @brief   MPU-6050 驱动实现（仅加速度计）
 *
 * 寄存器：PWR_MGMT_1(0x6B) 唤醒；ACCEL_CONFIG(0x1C) 量程；ACCEL_XOUT_H(0x3B) 数据。
 * ±2g 量程下 1g = 16384 LSB。
 */
#include "mpu6050.h"
#include "soft_i2c.h"

void MPU6050_Init(uint8_t addr)
{
    uint8_t d = 0x00;
    SoftI2C_WriteReg(addr, 0x6B, &d, 1);   /* 退出休眠 */
    d = 0x00;
    SoftI2C_WriteReg(addr, 0x1C, &d, 1);   /* 加速度量程 ±2g */
}

int MPU6050_ReadAccel(uint8_t addr, float *ax, float *ay, float *az)
{
    uint8_t buf[6];
    if (SoftI2C_ReadReg(addr, 0x3B, buf, 6) != 0) {
        return -1;
    }
    int16_t rx = (int16_t)((buf[0] << 8) | buf[1]);
    int16_t ry = (int16_t)((buf[2] << 8) | buf[3]);
    int16_t rz = (int16_t)((buf[4] << 8) | buf[5]);
    *ax = rx / 16384.0f;
    *ay = ry / 16384.0f;
    *az = rz / 16384.0f;
    return 0;
}
