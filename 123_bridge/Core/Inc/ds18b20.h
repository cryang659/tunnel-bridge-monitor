/**
 * @file    ds18b20.h
 * @brief   DS18B20 单总线温度传感器驱动接口（桥板：桥面/桥墩固体温度）
 *
 * 4 个 DS18B20 各挂一条独立 GPIO（跳过 ROM 匹配，直接用 0xCC），
 * 开漏输出 + 外部 4.7k 上拉。非阻塞状态机读取（转换 750ms 期间不阻塞主循环）。
 */
#ifndef __DS18B20_H
#define __DS18B20_H

#include <stdint.h>

/* DS18B20 数量（桥面 2 个 + 桥墩 2 个） */
#define DS18B20_NUM   4

/**
 * @brief 初始化：使能 DWT 延时 + 配置 4 个 GPIO 为开漏输出
 */
void DS18B20_Init(void);

/**
 * @brief 主循环每轮调用：推进「发转换 → 等 750ms → 读温度」状态机（非阻塞）
 */
void DS18B20_Tick(void);

/**
 * @brief 读某个 DS18B20 的最近一次温度（℃，从缓存读，不触发总线时序）
 * @param idx  0..3（0/1=桥面，2/3=桥墩）
 * @return 0 成功，非 0 失败（未读到有效温度）
 */
int DS18B20_GetTemp(int idx, float *temp);

#endif /* __DS18B20_H */
