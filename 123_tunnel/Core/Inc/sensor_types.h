/**
 * @file    sensor_types.h
 * @brief   传感器类型定义（与接口规范 v1.0 的 type 编码一一对应）
 */
#ifndef __SENSOR_TYPES_H
#define __SENSOR_TYPES_H

/* 传感器类型枚举，顺序与接口规范 type 编码一致
 * 注：枚举名保持不变以兼容既有代码，上报的类型串见 sensor.c 的 SENSOR_TYPE_STR */
typedef enum {
    SENSOR_VIBRATION = 0,   /* 振动加速度  g      */
    SENSOR_STRESS,          /* 应力/应变   με     */
    SENSOR_TEMPERATURE,     /* 温度1(SHT30#1) ℃ */
    SENSOR_HUMIDITY,        /* 湿度1(SHT30#1) % */
    SENSOR_WATER_LEVEL,     /* 水位1(PA4)  m      */
    SENSOR_SEEPAGE,         /* 渗水1(PB0)  含水率 % */
    SENSOR_DISPLACEMENT,    /* 位移        mm     */
    SENSOR_LOAD,            /* 车辆荷载    t      */
    SENSOR_CONVERGENCE,     /* 净空收敛    mm     */
    SENSOR_SEEPAGE_2,       /* 渗水2(PB1)  含水率 % */
    /* ===== 2026-09-17 新增（追加在末尾，不打乱既有索引）===== */
    SENSOR_TEMPERATURE_2,   /* 温度2(SHT30#2, 0x45) ℃ */
    SENSOR_HUMIDITY_2,      /* 湿度2(SHT30#2, 0x45) % */
    SENSOR_WATER_LEVEL_2,   /* 水位2(PA5)  m      */
    SENSOR_DOME_PRESSURE,   /* 穹顶压力(PA6,薄膜) kPa */
    /* ===== 2026-09-26 新增：K210 视觉模块（软件串口 PB13/PB15）=====
     * 裂缝为实际实现（K210 端跑识别）；车辆/人员为预留——
     * 物模型与菜单名称先建好，K210 端实现后无需改固件即可出数。 */
    SENSOR_CRACK,           /* 裂缝置信度  0~1 无量纲（K210 视觉） */
    SENSOR_VEHICLE,         /* 车辆计数    辆（预留） */
    SENSOR_PERSON,          /* 人员计数    人（预留） */
    SENSOR_TYPE_COUNT       /* 传感器类型总数     */
} SensorType;

/* 单条传感器数据 */
typedef struct {
    SensorType type;        /* 传感器类型   */
    float      value;       /* 数值         */
    char       unit[16];    /* 单位         */
} SensorData;

/* 类型编码字符串（用于 JSON 上报，与接口规范一致） */
extern const char *SENSOR_TYPE_STR[SENSOR_TYPE_COUNT];
/* 单位字符串 */
extern const char *SENSOR_UNIT_STR[SENSOR_TYPE_COUNT];

#endif /* __SENSOR_TYPES_H */
