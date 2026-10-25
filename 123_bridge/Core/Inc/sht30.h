/**
 * @file    sht30.h
 * @brief   SHT30 温湿度传感器驱动接口（I2C）
 */
#ifndef __SHT30_H
#define __SHT30_H

/* SHT30 I2C 地址（ADDR 接地 = 0x44，接 VCC = 0x45） */
#define SHT30_ADDR   0x44

/**
 * @brief 初始化（无额外寄存器，空实现占位）
 */
void SHT30_Init(void);

/**
 * @brief 读一次温湿度（高重复性测量，约 15ms）
 * @param temp 温度 ℃
 * @param humi 湿度 %
 * @return 0 成功，-1 失败
 */
int SHT30_Read(float *temp, float *humi);

#endif /* __SHT30_H */
