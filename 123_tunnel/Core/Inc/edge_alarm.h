/**
 * @file    edge_alarm.h
 * @brief   边缘阈值告警：本地越限判定
 */
#ifndef __EDGE_ALARM_H
#define __EDGE_ALARM_H

#include "sensor_types.h"

/* 告警级别 */
typedef enum {
    ALARM_LEVEL_NORMAL = 0,   /* 正常   */
    ALARM_LEVEL_WARNING,      /* 预警   */
    ALARM_LEVEL_ALARM,        /* 告警   */
} AlarmLevel;

/**
 * @brief 判定单个传感器数据的告警级别（阈值与服务器 config.py 一致）
 * @return 告警级别
 */
AlarmLevel EdgeAlarm_CheckOne(const SensorData *d);

/**
 * @brief 获取某传感器的告警阈值（供菜单阈值页显示）
 * @return 阈值，非法类型返回 0
 */
float EdgeAlarm_GetThreshold(SensorType type);

/**
 * @brief 设置某传感器的告警阈值（仅改内存，落盘需再调 SaveFlash）
 */
void EdgeAlarm_SetThreshold(SensorType type, float value);

/**
 * @brief 从内部 Flash 末页加载阈值（启动时调用，有有效数据则覆盖默认值）
 */
void EdgeAlarm_LoadFlash(void);

/**
 * @brief 把当前阈值写入内部 Flash 末页（断电不丢）
 */
void EdgeAlarm_SaveFlash(void);

#endif /* __EDGE_ALARM_H */
