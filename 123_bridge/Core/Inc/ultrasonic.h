/**
 * @file    ultrasonic.h
 * @brief   RCWL-1670 超声波测距驱动（Trig=PB15、Echo=PB13）
 *
 * ⚠️ 接线（RCWL-1670 与 HC-SR04 不同，勿照抄 HC-SR04 的接法）：
 *   HY2.0 四脚：1=5V(供电 5V)  2=TX(Echo)  3=RX(Trig)  4=GND
 *   供电必须 5V——3.3V 下模块常报 "Distance Measurement Timed Out"。
 *
 * ⚠️ Echo 电平必须先量：5V 供电时模块 TX 高电平很可能是 5V，
 *   接之前用万用表量「模块 TX 对 GND」的高电平电压：
 *     实测 ≈3.3V  → 可直接接 PB13，把 ULTRASONIC_ECHO_DIVIDER 设为 0
 *     实测 ≈5V    → **必须 1k+2k 分压**再接，宏保持 1（默认）
 *   直接接 5V 会打坏 STM32 引脚。
 *
 * 时序与 HC-SR04 兼容：Trig 给 >10us 高脉冲 → Echo 输出高电平脉宽，
 * 距离 = 脉宽(us) / 58（cm），或 /5.8（mm）。
 *
 * 采用非阻塞状态机（同 ds18b20.c 的做法）：Tick 由主循环 10ms 轮询，
 * Read 返回缓存值，不会因等待回波阻塞主循环（4G 串口/摇杆都要靠主循环）。
 */
#ifndef __ULTRASONIC_H
#define __ULTRASONIC_H

#include <stdint.h>

/* Echo 是否经过分压（1=经 1k+2k 分压，0=直连 3.3V 电平模块） */
#ifndef ULTRASONIC_ECHO_DIVIDER
#define ULTRASONIC_ECHO_DIVIDER   1
#endif

void Ultrasonic_Init(void);

/* 主循环每 10ms 调一次，推进状态机 */
void Ultrasonic_Tick(void);

/* 读最近一次测距结果（cm），返回 0 成功、-1 表示无有效数据（未接/超量程/超时） */
int Ultrasonic_Read(float *cm);

#endif /* __ULTRASONIC_H */
