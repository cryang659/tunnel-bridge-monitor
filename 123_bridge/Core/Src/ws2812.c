/**
 * @file    ws2812.c
 * @brief   WS2812 灯带驱动实现（DIN = PB12）
 *
 * WS2812B 时序规格：T0H 220~380ns、T1H 580~1600ns、位周期 1050~1400ns。
 * 取 T0H=300 / T1H=800 / TPER=1100 居中，两边都有余量。
 * （按 400/800/1250 取值时 T0H 算出来 390ns 已超 380 上限，
 *   0 码会被灯珠误判成 1 码 → 整条数据错位、灯珠乱亮。）
 */
#include "ws2812.h"
#include "main.h"

#define WS_PORT   GPIOB
#define WS_PIN    GPIO_PIN_12

#define WS_H()    (WS_PORT->BSRR = WS_PIN)
#define WS_L()    (WS_PORT->BRR  = WS_PIN)

/* 待发送缓冲：RGB 顺序存放，发送时才换成 WS2812 要求的 GRB */
static uint8_t s_buf[WS2812_NUM * 3];

static uint32_t s_t0h, s_t1h, s_tper;   /* 三个时间点折算成 DWT 周期数 */

/* 按当前主频把纳秒折算成 DWT 周期数 */
static uint32_t ws_ticks_ns(uint32_t ns)
{
    return (uint32_t)(((uint64_t)ns * SystemCoreClock) / 1000000000ULL);
}

static void ws_send_byte(uint8_t b)
{
    for (int i = 7; i >= 0; i--) {
        uint32_t t0 = DWT->CYCCNT;
        WS_H();
        uint32_t hi = (b & (1u << i)) ? s_t1h : s_t0h;
        while ((DWT->CYCCNT - t0) < hi) { }
        WS_L();
        while ((DWT->CYCCNT - t0) < s_tper) { }
    }
}

void WS2812_Init(void)
{
    /* DWT 周期计数器（soft_i2c.c / ds18b20.c 里也开过，重复开无副作用） */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    /* 下面三个常数依赖 SystemCoreClock 已是最终主频（64MHz）。
     * HAL_RCC_ClockConfig() 内部会更新该变量，只要本函数在 SystemClock_Config()
     * 之后调用即可（当前在 main() 初始化段，满足）。 */
    s_t0h  = ws_ticks_ns(300);
    s_t1h  = ws_ticks_ns(800);
    s_tper = ws_ticks_ns(1100);

    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin   = WS_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(WS_PORT, &gpio);
    WS_L();                             /* 空闲保持低电平 */

    WS2812_Clear();
    WS2812_Show();                      /* 上电先全灭，避免残留随机色 */
}

void WS2812_Clear(void)
{
    for (uint16_t i = 0; i < WS2812_NUM * 3; i++) {
        s_buf[i] = 0;
    }
}

void WS2812_SetPixel(uint16_t i, uint8_t r, uint8_t g, uint8_t b)
{
    if (i >= WS2812_NUM) { return; }
    s_buf[i * 3 + 0] = r;
    s_buf[i * 3 + 1] = g;
    s_buf[i * 3 + 2] = b;
}

void WS2812_GetPixel(uint16_t i, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (i >= WS2812_NUM) {
        *r = *g = *b = 0;
        return;
    }
    *r = s_buf[i * 3 + 0];
    *g = s_buf[i * 3 + 1];
    *b = s_buf[i * 3 + 2];
}

void WS2812_Fill(uint8_t r, uint8_t g, uint8_t b)
{
    for (uint16_t i = 0; i < WS2812_NUM; i++) {
        WS2812_SetPixel(i, r, g, b);
    }
}

void WS2812_Show(void)
{
    /* --- 总电流限幅：三通道之和超限就整帧等比压暗 --- */
    uint32_t sum = 0;
    for (uint16_t i = 0; i < WS2812_NUM * 3; i++) {
        sum += s_buf[i];
    }
    const uint32_t limit = (uint32_t)WS2812_NUM * 3 * 255 * WS2812_LIMIT_PCT / 100;
    uint32_t num = 1, den = 1;
    if (sum > limit) {
        num = limit;
        den = sum;
    }

    /* --- 发送 ---
     * ⚠️ 关中断时长要点算过：每颗 3 字节 × 8 位 × 1.10us = 26.4us/颗。
     *   8 颗 → 211us，25 颗 → **660us**，31 颗 → 818us，45 颗 → 1188us。
     *   25 颗的 660us 已占 SysTick 周期(1000us)的 66%，而超声波 EXTI 还要
     *   在这期间进中断记时间戳 —— 一次屏蔽 660us 会明显干扰它们。
     *
     * 故按 WS_TX_CHUNK 颗一段分段发送：每段之间开一次中断，
     * 让挂起的中断能在缝隙里得到响应。分段不影响灯带数据——
     * WS2812 是靠「拉低 >50us」才锁存，段与段之间只隔几十 ns（几次循环），
     * 远不到锁存条件，数据会连续地流下去。
     */
    #define WS_TX_CHUNK  8
    uint16_t i = 0;
    while (i < WS2812_NUM) {
        uint16_t end = i + WS_TX_CHUNK;
        if (end > WS2812_NUM) { end = WS2812_NUM; }

        __disable_irq();
        for (; i < end; i++) {
            uint8_t r = s_buf[i * 3 + 0];
            uint8_t g = s_buf[i * 3 + 1];
            uint8_t b = s_buf[i * 3 + 2];
            if (den != 1) {
                r = (uint8_t)((uint32_t)r * num / den);
                g = (uint8_t)((uint32_t)g * num / den);
                b = (uint8_t)((uint32_t)b * num / den);
            }
            ws_send_byte(g);   /* WS2812 数据顺序是 GRB 不是 RGB */
            ws_send_byte(r);
            ws_send_byte(b);
        }
        __enable_irq();        /* 每段结束放行一次中断 */
    }

    /* 锁存：拉低 >50us。必须放在开中断之后——HAL_Delay 靠 SysTick 中断计时，
     * 中断关着调它会永远等不到 tick。 */
    HAL_Delay(1);
}
