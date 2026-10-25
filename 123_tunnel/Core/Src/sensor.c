/**
 * @file    sensor.c
 * @brief   传感器抽象层实现：初始化与统一读取分发
 */
#include "sensor.h"
#include "sensor_drivers.h"
#include "k210.h"          /* 裂缝/车辆/人员来自 K210 视觉模块 */

/* 类型编码字符串，与接口规范 v1.0 type 编码一致
 * 注：温湿度/水位在本板拆成 1/2 两路独立上报（桥板仍用 temperature/humidity） */
const char *SENSOR_TYPE_STR[SENSOR_TYPE_COUNT] = {
    "vibration", "stress", "temperature_1", "humidity_1", "water_level_1",
    "seepage_1", "displacement", "load", "convergence", "seepage_2",
    "temperature_2", "humidity_2", "water_level_2", "dome_pressure",
    "crack", "vehicle", "person",
};

/* 单位字符串 */
const char *SENSOR_UNIT_STR[SENSOR_TYPE_COUNT] = {
    "g", "με", "℃", "%", "m", "%", "mm", "t", "mm", "%",
    "℃", "%", "m", "kPa",
    "", "辆", "人",
};

/* 读取函数指针表：索引对应 SensorType */
typedef int (*read_fn)(float *);
static const read_fn READ_FN[SENSOR_TYPE_COUNT] = {
    Vibration_Read, Stress_Read, Temperature_Read, Humidity_Read,
    WaterLevel_Read, Seepage_Read, Displacement_Read, Load_Read,
    Convergence_Read, Seepage2_Read,
    Temperature2_Read, Humidity2_Read, WaterLevel2_Read, DomePressure_Read,
    CrackConf_Read, VehicleCount_Read, PersonCount_Read,
};

void Sensor_InitAll(void)
{
    Vibration_Init();
    Stress_Init();
    Temperature_Init();
    Humidity_Init();
    WaterLevel_Init();
    Seepage_Init();
    Displacement_Init();
    Load_Init();
    Convergence_Init();
    Seepage2_Init();
    Temperature2_Init();
    Humidity2_Init();
    WaterLevel2_Init();
    DomePressure_Init();
}

int Sensor_Read(SensorType type, SensorData *out)
{
    if (type >= SENSOR_TYPE_COUNT || out == 0) {
        return -1;
    }
    out->type = type;
    if (READ_FN[type](&out->value) != 0) {
        return -1;                       /* 读取失败 */
    }
    /* 拷贝单位字符串 */
    for (int i = 0; i < sizeof(out->unit) - 1; i++) {
        out->unit[i] = SENSOR_UNIT_STR[type][i];
        if (SENSOR_UNIT_STR[type][i] == '\0') break;
    }
    out->unit[sizeof(out->unit) - 1] = '\0';
    return 0;
}

int Sensor_ReadAll(SensorData *out, int max_count)
{
    int n = 0;
    for (int t = 0; t < SENSOR_TYPE_COUNT && n < max_count; t++) {
        if (Sensor_Read((SensorType)t, &out[n]) == 0) {
            n++;
        }
    }
    return n;
}
