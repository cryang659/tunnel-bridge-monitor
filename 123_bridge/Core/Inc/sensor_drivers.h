/**
 * @file    sensor_drivers.h
 * @brief   各传感器底层驱动接口（具体实现见 sensor_drivers.c）
 */
#ifndef __SENSOR_DRIVERS_H
#define __SENSOR_DRIVERS_H

#include "sensor_types.h"
#include <stdint.h>

/* 各传感器驱动初始化 */
void Vibration_Init(void);
void Stress_Init(void);
void Temperature_Init(void);
void Humidity_Init(void);
void WaterLevel_Init(void);
void Seepage_Init(void);
void Displacement_Init(void);
void Load_Init(void);
void CableForce_Init(void);
void DeckTemp_Init(void);
void PierTemp_Init(void);
void DeckTemp2_Init(void);
void PierTemp2_Init(void);
void Position_Init(void);

/* 各传感器驱动读取，返回 0 成功 */
int Vibration_Read(float *value);
int Stress_Read(float *value);
int Temperature_Read(float *value);
int Humidity_Read(float *value);
int WaterLevel_Read(float *value);
int Seepage_Read(float *value);
int Displacement_Read(float *value);
int Load_Read(float *value);
int CableForce_Read(float *value);
int DeckTemp_Read(float *value);
int PierTemp_Read(float *value);
int DeckTemp2_Read(float *value);
int PierTemp2_Read(float *value);
int Position_Read(float *value);

/* 通用 ADC 采样（供传感器读取 + 摇杆按键复用），返回 12 位原始值 */
void Cable_Tick(void);

uint16_t adc_read_channel(uint32_t channel);

#endif /* __SENSOR_DRIVERS_H */
