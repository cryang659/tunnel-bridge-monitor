/**
 * @file    gps.h
 * @brief   GNSS 定位模块接口（DX-CT02-A 集成，AT 指令查询）
 *
 * 模块内置 GNSS，通过主串口（USART2）用 AT 指令查询定位信息，
 * 无需解析 NMEA 语句，坐标输出 WGS-84。
 */
#ifndef __GPS_H
#define __GPS_H

#include <stdint.h>

typedef struct {
    int   fix;     /* 定位状态：1=已定位，0=未定位 */
    float lon;     /* 经度（度，WGS-84） */
    float lat;     /* 纬度（度，WGS-84） */
    float alt;     /* 高度（米） */
} GPSData;

/**
 * @brief 开启 GPS 定位（AT+MGPSC=1）
 * @return 0 成功
 */
int GPS_Init(void);

/**
 * @brief 查询定位信息（AT+GPSST），解析经纬度
 * @param out 输出定位数据
 * @return 0 成功解析（定位与否看 out->fix）
 */
int GPS_GetFix(GPSData *out);

#endif /* __GPS_H */
