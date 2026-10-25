/**
 * @file    sht30.h
 * @brief   SHT30 温湿度传感器驱动接口（I2C）
 */
#ifndef __SHT30_H
#define __SHT30_H

#include <stdint.h>   /* uint8_t（增量编译不报，Rebuild 时才暴露） */

/* SHT30 I2C 地址（由模块 ADDR 引脚决定，两片挂同一条软 I2C 总线 PB10/PB11） */
#define SHT30_ADDR_1   0x44   /* #1 ADDR 接地 */
#define SHT30_ADDR_2   0x45   /* #2 ADDR 接 VCC */

/**
 * @brief 初始化（无额外寄存器，空实现占位）
 */
void SHT30_Init(void);

/**
 * @brief 读一次温湿度（高重复性测量，约 20ms）
 * @param addr 器件地址（SHT30_ADDR_1 / SHT30_ADDR_2）
 * @param temp 温度，单位 ℃
 * @param humi 湿度，单位 %
 * @return 0 成功，-1 失败
 *
 * 注：同一片在 1 秒内被重复读取时直接返回上次缓存值。
 *     温度与湿度是两个独立的上报项、会各调一次本函数，
 *     缓存可避免同一片在一个采集周期内被测两次（省一半 I2C 阻塞时间）。
 */
int SHT30_Read(uint8_t addr, float *temp, float *humi);

#endif /* __SHT30_H */
