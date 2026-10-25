/**
 * @file    sensor.h
 * @brief   传感器抽象层：统一初始化与读取接口
 *
 * 设计思路：上层（数据上报、边缘告警）只依赖本抽象层，
 * 不关心具体传感器是 ADC 还是 I2C，新增/替换传感器只需改 sensor_drivers.c。
 */
#ifndef __SENSOR_H
#define __SENSOR_H

#include "sensor_types.h"

/**
 * @brief 初始化全部传感器
 */
void Sensor_InitAll(void);

/**
 * @brief 读取单个传感器
 * @param type  传感器类型
 * @param out   输出数据
 * @return 0 成功，非 0 失败
 */
int Sensor_Read(SensorType type, SensorData *out);

/**
 * @brief 读取全部传感器（批量）
 * @param out        输出数组
 * @param max_count  数组容量
 * @return 实际读取到的传感器个数
 */
int Sensor_ReadAll(SensorData *out, int max_count);

#endif /* __SENSOR_H */
