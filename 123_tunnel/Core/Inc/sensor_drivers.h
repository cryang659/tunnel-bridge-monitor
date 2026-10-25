/**
 * @file    sensor_drivers.h
 * @brief   各传感器底层驱动接口（具体实现见 sensor_drivers.c）
 */
#ifndef __SENSOR_DRIVERS_H
#define __SENSOR_DRIVERS_H

#include "sensor_types.h"
#include "k210.h"        /* CrackConf_Read / VehicleCount_Read / PersonCount_Read */
#include <stdint.h>

/* 各传感器驱动初始化 */
void Vibration_Init(void);
void Stress_Init(void);
void Temperature_Init(void);
void Humidity_Init(void);
void WaterLevel_Init(void);
void Seepage_Init(void);
void Seepage2_Init(void);
void Displacement_Init(void);
void Load_Init(void);
void Convergence_Init(void);
/* 2026-09-17 新增 */
void Temperature2_Init(void);
void Humidity2_Init(void);
void WaterLevel2_Init(void);
void DomePressure_Init(void);

/* 各传感器驱动读取，返回 0 成功 */
int Vibration_Read(float *value);
int Stress_Read(float *value);
int Temperature_Read(float *value);
int Humidity_Read(float *value);
int WaterLevel_Read(float *value);
int Seepage_Read(float *value);
int Seepage2_Read(float *value);
int Displacement_Read(float *value);
int Load_Read(float *value);
int Convergence_Read(float *value);
/* 2026-09-17 新增 */
int Temperature2_Read(float *value);
int Humidity2_Read(float *value);
int WaterLevel2_Read(float *value);
int DomePressure_Read(float *value);
/* 2026-09-26 新增：K210 视觉（接口实现见 k210.h / k210.c，
 * 车辆/人员为预留，恒返回 -1） */
int CrackConf_Read(float *value);
int VehicleCount_Read(float *value);
int PersonCount_Read(float *value);

/* 通用 ADC 采样（供传感器读取 + 摇杆按键复用），返回 12 位原始值 */
uint16_t adc_read_channel(uint32_t channel);

#endif /* __SENSOR_DRIVERS_H */
