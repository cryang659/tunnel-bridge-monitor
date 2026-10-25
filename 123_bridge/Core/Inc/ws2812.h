/**
 * @file    ws2812.h
 * @brief   WS2812 灯带驱动（数据线 DIN = PB12）
 *
 * 时序敏感：靠高电平持续时间区分 0/1 码，用 DWT 周期计数器掐时间
 * （与 soft_i2c.c / ds18b20.c 同一套方案，不依赖 SysTick），
 * 发送期间关中断，避免 ISR 拉长高电平把位数据带偏。
 */
#ifndef __WS2812_H
#define __WS2812_H

#include <stdint.h>

/* 灯珠数量。桥板 25 颗（长灯带，做「物体沿带移动」演示）；隧板是 8 颗。
 * 改这个数只影响缓冲大小，别的不用动。
 * ⚠️ 必须与实际灯珠数一致：写少了末尾几颗不受控（上电随机色），
 *    写多了会在数据末尾多发几个字节，一般无害但会浪费 30us/颗 的发送时间。 */
#ifndef WS2812_NUM
#define WS2812_NUM        25
#endif

/* 总电流限幅：整条灯带三通道数值之和的上限，按「满白」的百分比算。
 * 25 颗满白 ≈ 25 × 60mA = 1.5A @5V，限到 78% ≈ 1.2A，
 * 给 5V/2A 充电头留出 30% 余量（灯带全白时电源不会被拉塌）。
 * 限幅是**动态**的：只点亮一部分灯珠时，亮的那部分可以到全亮，
 * 只有总和超限才等比压暗，所以进度条/单点跟随这类效果不受影响。 */
#ifndef WS2812_LIMIT_PCT
#define WS2812_LIMIT_PCT  78
#endif

void WS2812_Init(void);

/* 清空待发送缓冲（之后的 SetPixel 生效，Show 才真正发出去） */
void WS2812_Clear(void);

/* 设置第 i 颗灯珠颜色（RGB，各自 0~255）。越界忽略。 */
void WS2812_SetPixel(uint16_t i, uint8_t r, uint8_t g, uint8_t b);

/* 读回第 i 颗的待发颜色。供「在已有底色上再叠一层亮度调制」用
 * （如灯效叠加到跟随结果之上）。越界则输出全 0。 */
void WS2812_GetPixel(uint16_t i, uint8_t *r, uint8_t *g, uint8_t *b);

/* 整条刷同一色（内部就是逐颗 SetPixel） */
void WS2812_Fill(uint8_t r, uint8_t g, uint8_t b);

/* 限幅后发送整帧。注意：内部含约 1ms 的锁存延时（HAL_Delay），
 * 别在关中断的上下文里调用。 */
void WS2812_Show(void);

#endif /* __WS2812_H */
