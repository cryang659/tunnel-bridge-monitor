/**
 * @file    data_report.h
 * @brief   数据上报：传感器数据打包为 JSON 并发布
 */
#ifndef __DATA_REPORT_H
#define __DATA_REPORT_H

#include "sensor_types.h"
#include "gps.h"

/* 设备 ID（与接口规范 device_id 一致，多节点时改为 NODE_002 等） */
#define DEVICE_ID "NODE_002"

/**
 * @brief 将批量传感器数据打包为 JSON（接口规范「批量上报」格式，含经纬度）
 * @return 0 成功
 */
int DataReport_BuildJSON(const SensorData *data, int count, const GPSData *gps, char *out, int max_len);

/**
 * @brief 上报数据：打包 + MQTT 发布到 shm/{device_id}/data
 * @return 0 成功
 */
int DataReport_Publish(const SensorData *data, int count, const GPSData *gps);

/**
 * @brief 发送心跳到 shm/{device_id}/heartbeat
 * @return 0 成功
 */
int DataReport_Heartbeat(void);

/**
 * @brief 上报阈值变更到 shm/{device_id}/threshold，服务器据此更新该传感器判断阈值
 * @param type_str 传感器类型字符串（SENSOR_TYPE_STR，如 "water_level"）
 * @param value    新阈值
 * @return 0 成功
 */
int DataReport_PublishThreshold(const char *type_str, float value);

/* ---- 服务器时间对时（主菜单底部时钟，见 data_report.c） ---- */
void TimeSync_Reset(void);                              /* 启动复位时间基准 */
void TimeSync_Parse(const uint8_t *buf, uint16_t len);  /* 解析服务器下发的时间 */
void TimeSync_GetHMS(char *out);                        /* 取当前 HH:MM:SS */
void TimeSync_GetDate(char *out);                       /* 取当前 YYYY-MM-DD（未对时为空） */

#endif /* __DATA_REPORT_H */
