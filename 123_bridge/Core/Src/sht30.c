/**
 * @file    sht30.c
 * @brief   SHT30 温湿度驱动实现
 *
 * 命令 0x24 0x00：高重复性测量（时钟拉伸禁用），发命令后延时 20ms 再读 6 字节。
 * 数据格式：温度高字节 / 温度低字节 / 温度 CRC8 / 湿度高 / 湿度低 / 湿度 CRC8。
 */
#include "sht30.h"
#include "soft_i2c.h"
#include "main.h"

/* CRC-8（多项式 0x31，初值 0xFF） */
static uint8_t sht30_crc8(const uint8_t *data, int len)
{
    uint8_t crc = 0xFF;
    for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

void SHT30_Init(void)
{
    /* SHT30 无需初始化寄存器，上电即可用 */
}

int SHT30_Read(float *temp, float *humi)
{
    uint8_t cmd[2] = {0x24, 0x00};   /* 高重复性，时钟拉伸禁用 */
    if (SoftI2C_Write(SHT30_ADDR, cmd, 2) != 0) {
        return -1;
    }
    HAL_Delay(20);                   /* 等待测量完成 */

    uint8_t buf[6];
    if (SoftI2C_Read(SHT30_ADDR, buf, 6) != 0) {
        return -1;
    }
    /* CRC 校验 */
    if (sht30_crc8(buf, 2) != buf[2] || sht30_crc8(&buf[3], 2) != buf[5]) {
        return -1;
    }

    uint16_t raw_t = (uint16_t)((buf[0] << 8) | buf[1]);
    uint16_t raw_h = (uint16_t)((buf[3] << 8) | buf[4]);
    *temp = -45.0f + 175.0f * raw_t / 65535.0f;
    *humi = 100.0f * raw_h / 65535.0f;
    return 0;
}
