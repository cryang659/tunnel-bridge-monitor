/**
 * @file    key.h
 * @brief   摇杆导航按键（Y 轴上下 / X 轴左=返回 / SW 按下=确认）
 */
#ifndef __KEY_H
#define __KEY_H

#include "stm32f1xx_hal.h"

/* 摇杆引脚（模拟量走 ADC，SW 为数字按键）：
 *   VRy（上下）接 PA0（ADC_IN0）
 *   VRx（左右）接 PA1（ADC_IN1）
 *   SW（按下） 接 PB14（上拉输入，按下接 GND）
 */
#define JOY_SW_PORT     GPIOB
#define JOY_SW_PIN      GPIO_PIN_14
#define JOY_VRY_CH      ADC_CHANNEL_0   /* PA0 上下 */
#define JOY_VRX_CH      ADC_CHANNEL_1   /* PA1 左右 */

/* 按键事件（单击，按下沿触发一次） */
typedef enum {
    KEY_NONE = 0,
    KEY_UP,      /* 上移 / 上翻一页 */
    KEY_DOWN,    /* 下移 / 下翻一页 */
    KEY_OK,      /* 确认 / 进入 */
    KEY_BACK,    /* 返回上一级 */
} KeyEvent;

void Key_Init(void);          /* 初始化摇杆 SW GPIO（上拉输入） */
KeyEvent Key_Scan(void);      /* 非阻塞扫描，返回本次按键事件 */

#endif /* __KEY_H */
