/**
 * @file    menu.h
 * @brief   128x128 LCD 中文菜单（4 键导航：上/下/确认/返回）
 *
 * 主菜单 5 项：实时数据 / 阈值设置 / 告警记录 / 网络状态 / 系统信息
 */
#ifndef __MENU_H
#define __MENU_H

#include "sensor_types.h"
#include "edge_alarm.h"

/* 菜单运行上下文：由主循环周期性刷新，菜单仅读取显示 */
typedef struct {
    SensorData data[SENSOR_TYPE_COUNT];   /* 最近一次采集数据 */
    int        count;                     /* 有效数据条数   */
    AlarmLevel alarm[SENSOR_TYPE_COUNT];  /* 各传感器告警级别 */
    int        net_ok;                    /* 网络连接状态 1/0 */
} MenuCtx;

void Menu_Init(void);                    /* 初始化（含按键）并显示主菜单 */
void Menu_Process(const MenuCtx *ctx);   /* 每次主循环调用：扫描按键 + 刷新 */

#endif /* __MENU_H */
