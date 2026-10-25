/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f1xx_hal.h"
#include "edge_alarm.h"   /* AlarmLevel（执行器模式驱动用） */

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */
/* 执行器工作模式（顺序即菜单里右推切换的顺序） */
typedef enum {
    ACT_MODE_AUTO = 0,   /* 自动：跟随告警 */
    ACT_MODE_ON   = 1,   /* 一直打开 */
    ACT_MODE_OFF  = 2,   /* 关闭 */
    ACT_MODE_COUNT
} ActuatorMode;

/* 灯带颜色（顺序即菜单里右推切换的顺序；AUTO 跟随告警级别） */
typedef enum {
    LED_COLOR_AUTO = 0,  /* 自动：正常绿 / 预警黄 / 告警红 */
    LED_COLOR_RED,
    LED_COLOR_ORANGE,
    LED_COLOR_YELLOW,
    LED_COLOR_GREEN,
    LED_COLOR_CYAN,
    LED_COLOR_BLUE,
    LED_COLOR_PURPLE,
    LED_COLOR_WHITE,
    LED_COLOR_RAINBOW,   /* 彩虹：色相绕灯带铺开并流动（配合跟随效果最好看） */
    LED_COLOR_COUNT
} LedColor;

/* 灯带灯效（顺序即菜单里右推切换的顺序）。
 * 灯效只负责**亮度调制**，与「颜色」「跟随」都正交，可任意叠加：
 *   呼吸 + 单点跟随 = 光点呼吸着跟人走；流水 + 彩虹 = 彩虹底上跑亮珠。 */
typedef enum {
    LED_FX_STEADY = 0,   /* 常亮：不调制 */
    LED_FX_BREATH,       /* 呼吸：整条明暗渐变 */
    LED_FX_FLOW,         /* 流水：一颗亮珠带拖尾绕圈跑 */
    LED_FX_COUNT
} LedEffect;

/* 灯带跟随效果（顺序即菜单里右推切换的顺序）。
 * 前三种把「超声波测到的物体位置」映射到灯带上，最后一种是按距离整体变色。
 * 与「灯效」正交：跟随决定**哪些颗亮**，灯效决定**怎么明暗变化**。 */
typedef enum {
    LED_FOLLOW_OFF = 0,  /* 关闭跟随：整条按颜色常亮（退化成告警指示） */
    LED_FOLLOW_DOT,      /* 单点跟随：物体在哪，哪几颗亮 */
    LED_FOLLOW_PROGRESS, /* 进度条：物体之前的灯全亮 */
    LED_FOLLOW_TAIL,     /* 彗星拖尾：物体位置最亮，往后渐暗拖一条尾 */
    LED_FOLLOW_FAR_COLOR,/* 距离变色：远=蓝紫 → 近=红，整条同色 */
    LED_FOLLOW_COUNT
} LedFollow;
/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */
/* ============ 执行器（蜂鸣器 / 继电器）驱动 ============
 * 三种模式可在板上菜单「执行器」页手动切换：
 *   AUTO 自动（跟随告警）/ ON 一直打开 / OFF 关闭
 * 实现见 main.c，引脚见下方 Private defines */
void Actuator_Init(void);
void Actuator_Update(AlarmLevel level, uint32_t now);
ActuatorMode Actuator_GetBuzzerMode(void);
ActuatorMode Actuator_GetRelayMode(void);
void Actuator_CycleBuzzerMode(void);
void Actuator_CycleRelayMode(void);

/* ============ WS2812 灯带 ============
 * 开关 / 颜色 / 灯效 / 跟随 四项可在板上菜单「执行器」页切换，均断电保存。
 * 颜色、灯效、跟随三者正交：颜色铺底色 → 跟随决定哪些颗亮 → 灯效调制亮度。
 * 驱动本体见 ws2812.c/h；跟随映射与渲染见 main.c 的 led_frame_render。
 * 超声波：RCWL-1670（Trig=PB15 / Echo=PB13），驱动见 ultrasonic.c/h */
uint8_t   Led_GetOn(void);
LedColor  Led_GetColor(void);
LedEffect Led_GetEffect(void);
LedFollow Led_GetFollow(void);
void      Led_Toggle(void);
void      Led_CycleColor(void);
void      Led_CycleEffect(void);
void      Led_CycleFollow(void);

void WS2812_Init(void);
void WS2812_Update(AlarmLevel level, uint32_t now);
/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
