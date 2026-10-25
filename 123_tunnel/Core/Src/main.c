/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "sensor.h"
#include "mqtt_client.h"
#include "wifi_esp8266.h"
#include "gps.h"
#include "data_report.h"
#include "edge_alarm.h"
#include "st7735.h"
#include "menu.h"
#include "k210.h"
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define REPORT_INTERVAL_MS    2000U    /* 数据上报周期：2 秒  */
#define HEARTBEAT_INTERVAL_MS 30000U   /* 心跳周期：30 秒     */
#define GPS_INTERVAL_MS       60000U   /* GPS 定位查询周期：60 秒 */
/* 执行器（蜂鸣器 / 继电器）：蜂鸣器有源低电平响；继电器为 CW-025 模块 */
#define ACT_PORT              GPIOC
#define BUZZER1_PIN           GPIO_PIN_0
#define BUZZER2_PIN           GPIO_PIN_1
#define RELAY1_PIN            GPIO_PIN_2
#define RELAY2_PIN            GPIO_PIN_3

/* 继电器触发极性：CW-025 有「高电平触发」和「低电平触发」两种版本。
 * 确认方法：上电后把模块 IN 脚直接碰 3.3V，听到「咔」一声吸合即高电平触发。
 * 实测是哪种就改这个宏（1=高电平触发，0=低电平触发），其余代码不用动。 */
#define RELAY_ACTIVE_HIGH     1
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

I2C_HandleTypeDef hi2c1;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
/* USART1：接 K210 视觉模块（PA9=TX / PA10=RX，115200）。
 * 放在 USER CODE 区而非让 CubeMX 生成——因为 .ioc 里没配 USART1，
 * 这样即使以后用 CubeMX 重新生成代码，本段也会被原样保留。 */
UART_HandleTypeDef huart1;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */
static void report_alarm(const SensorData *d, AlarmLevel level);
static void MX_USART1_UART_Init(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* 网络连接状态：1=4G+MQTT 已连，0=未连（未连时跳过上报，但仍实时采集显示） */
static int net_ok = 0;

/* 边缘告警发布：判定为预警/告警时，推送到 shm/{device_id}/alarm */
static void report_alarm(const SensorData *d, AlarmLevel level)
{
    char json[128];
    char topic[64];
    snprintf(json, sizeof(json),
             "{\"device_id\":\"%s\",\"type\":\"%s\",\"level\":%d,\"value\":%.3f,\"unit\":\"%s\"}",
             DEVICE_ID, SENSOR_TYPE_STR[d->type], (int)level, d->value, d->unit);
    snprintf(topic, sizeof(topic), TOPIC_ALARM, DEVICE_ID);
    MQTT_Publish(topic, json, (uint16_t)strlen(json), 0);
}

/* 当前最高告警级别（采集循环更新，主循环每轮据此驱动执行器） */
static AlarmLevel g_alarm_level = ALARM_LEVEL_NORMAL;

/* ============ 执行器（蜂鸣器 / 继电器） ============ */
#if RELAY_ACTIVE_HIGH
#define RELAY_SET(on)  HAL_GPIO_WritePin(ACT_PORT, RELAY1_PIN | RELAY2_PIN, \
                                         (on) ? GPIO_PIN_SET : GPIO_PIN_RESET)
#else
#define RELAY_SET(on)  HAL_GPIO_WritePin(ACT_PORT, RELAY1_PIN | RELAY2_PIN, \
                                         (on) ? GPIO_PIN_RESET : GPIO_PIN_SET)
#endif

/* 蜂鸣器低电平响 */
#define BEEP_SET(on)   HAL_GPIO_WritePin(ACT_PORT, BUZZER1_PIN | BUZZER2_PIN, \
                                         (on) ? GPIO_PIN_RESET : GPIO_PIN_SET)
#define BEEP1_SET(on)  HAL_GPIO_WritePin(ACT_PORT, BUZZER1_PIN, \
                                         (on) ? GPIO_PIN_RESET : GPIO_PIN_SET)
#define BEEP2_SET(on)  HAL_GPIO_WritePin(ACT_PORT, BUZZER2_PIN, \
                                         (on) ? GPIO_PIN_RESET : GPIO_PIN_SET)

static ActuatorMode s_buzzer_mode = ACT_MODE_AUTO;
static ActuatorMode s_relay_mode  = ACT_MODE_AUTO;
/* 灯带 开关 / 颜色 / 灯效（灯带本体在下方 WS2812 段，状态与执行器同页保存故放这里） */
static uint8_t      s_led_on      = 1;
static LedColor     s_led_color   = LED_COLOR_AUTO;
static LedEffect    s_led_effect  = LED_FX_STEADY;
/* 置位表示「下一次 Update 立刻出帧、不等帧周期」。Led_Cycle* 在本段（执行器段）
 * 就要写它，而灯带渲染代码在文件更后面，故声明必须放在这里、不能放到 WS2812 段。 */
static uint8_t      s_led_first   = 1;

/* 执行器模式 + 灯带颜色/灯效的 Flash 持久化：存倒数第二页，与阈值页
 * （末页 0x0803F800）互不干扰，这样改模式不会擦掉阈值、改阈值也不会擦掉模式 */
#define ACT_FLASH_ADDR   0x0803F000UL
#define ACT_FLASH_MAGIC  0x5A5A5A5AUL

void Actuator_LoadFlash(void)
{
    const uint32_t *p = (const uint32_t *)ACT_FLASH_ADDR;
    if (p[0] != ACT_FLASH_MAGIC) {
        return;   /* 无有效数据，保持默认（自动） */
    }
    if (p[1] < ACT_MODE_COUNT) { s_buzzer_mode = (ActuatorMode)p[1]; }
    if (p[2] < ACT_MODE_COUNT) { s_relay_mode  = (ActuatorMode)p[2]; }
    /* p[3] 是「颜色」，p[5] 才是「开关」——中间隔着 p[4]「灯效」。
     * 早先 LED_COLOR_OFF 曾作为颜色枚举的最后一项存在过，那个版本若存过盘，
     * p[3] 会留下 out-of-range 的旧值，被下面的检查拦掉后回落到 LED_COLOR_AUTO。
     * 旧固件（只写过 3 个字）这两处则是擦除态 0xFFFFFFFF，同样被拦下 → 保持默认。 */
    if (p[3] < LED_COLOR_COUNT) { s_led_color  = (LedColor)p[3]; }
    if (p[4] < LED_FX_COUNT)    { s_led_effect = (LedEffect)p[4]; }
    if (p[5] <= 1)              { s_led_on     = (uint8_t)p[5]; }
}

/* 把当前模式写入 Flash（切换模式后调用，断电不丢） */
void Actuator_SaveFlash(void)
{
    HAL_FLASH_Unlock();

    FLASH_EraseInitTypeDef erase = {0};
    erase.TypeErase   = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = ACT_FLASH_ADDR;
    erase.NbPages     = 1;
    uint32_t page_err = 0;
    if (HAL_FLASHEx_Erase(&erase, &page_err) == HAL_OK) {
        uint32_t addr = ACT_FLASH_ADDR;
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, ACT_FLASH_MAGIC);
        addr += 4;
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, (uint32_t)s_buzzer_mode);
        addr += 4;
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, (uint32_t)s_relay_mode);
        addr += 4;
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, (uint32_t)s_led_color);
        addr += 4;
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, (uint32_t)s_led_effect);
        addr += 4;
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, (uint32_t)s_led_on);
    }

    HAL_FLASH_Lock();
}

void Actuator_Init(void)
{
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* PC0~PC3 推挽输出（蜂鸣器 ×2 + 继电器 ×2） */
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = BUZZER1_PIN | BUZZER2_PIN | RELAY1_PIN | RELAY2_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(ACT_PORT, &gpio);

    s_buzzer_mode = ACT_MODE_AUTO;
    s_relay_mode  = ACT_MODE_AUTO;
    BEEP_SET(0);      /* 上电不响 */
    RELAY_SET(0);     /* 上电不吸合 */

    Actuator_LoadFlash();   /* 用上次保存的模式覆盖默认值（无有效数据则保持自动） */
}

ActuatorMode Actuator_GetBuzzerMode(void) { return s_buzzer_mode; }
ActuatorMode Actuator_GetRelayMode(void)  { return s_relay_mode; }

void Actuator_CycleBuzzerMode(void)
{
    s_buzzer_mode = (ActuatorMode)((s_buzzer_mode + 1) % ACT_MODE_COUNT);
    Actuator_SaveFlash();
}

void Actuator_CycleRelayMode(void)
{
    s_relay_mode = (ActuatorMode)((s_relay_mode + 1) % ACT_MODE_COUNT);
    Actuator_SaveFlash();
}

uint8_t   Led_GetOn(void)     { return s_led_on;     }
LedColor  Led_GetColor(void)  { return s_led_color;  }
LedEffect Led_GetEffect(void) { return s_led_effect; }

/* 切换开关/颜色/灯效后置位 s_led_first，让下一次 Update 立刻出帧、不等帧周期，
 * 这样按一下摇杆灯就变，不会有「按了没反应」的观感延迟。 */
void Led_Toggle(void)
{
    s_led_on    = s_led_on ? 0 : 1;
    s_led_first = 1;
    Actuator_SaveFlash();
}

void Led_CycleColor(void)
{
    s_led_color = (LedColor)((s_led_color + 1) % LED_COLOR_COUNT);
    s_led_first = 1;
    Actuator_SaveFlash();
}

void Led_CycleEffect(void)
{
    s_led_effect = (LedEffect)((s_led_effect + 1) % LED_FX_COUNT);
    s_led_first = 1;
    Actuator_SaveFlash();
}

/* 主循环每轮调用：按当前模式驱动蜂鸣器与继电器 */
void Actuator_Update(AlarmLevel level, uint32_t now)
{
    /* ---- 蜂鸣器（低电平响） ---- */
    switch (s_buzzer_mode) {
    case ACT_MODE_ON:
        BEEP_SET(1);
        break;

    case ACT_MODE_OFF:
        BEEP_SET(0);
        break;

    case ACT_MODE_AUTO:
    default:
        if (level == ALARM_LEVEL_ALARM) {
            BEEP_SET(1);                    /* 报警：两个常响 */
        } else if (level == ALARM_LEVEL_WARNING) {
            BEEP1_SET(((now / 200U) % 2U) == 0U);   /* 预警：蜂鸣器 1 间歇滴滴滴 */
            BEEP2_SET(0);
        } else {
            BEEP_SET(0);                    /* 正常：不响 */
        }
        break;
    }

    /* ---- 继电器：自动模式下告警即吸合 ---- */
    switch (s_relay_mode) {
    case ACT_MODE_ON:
        RELAY_SET(1);
        break;
    case ACT_MODE_OFF:
        RELAY_SET(0);
        break;
    case ACT_MODE_AUTO:
    default:
        RELAY_SET(level >= ALARM_LEVEL_WARNING);
        break;
    }
}

/* ============ WS2812 灯带（8 颗，DIN=PB12） ============
 * 颜色：自动（跟随告警级别）或 8 种预设色；灯效：常亮 / 呼吸 / 流水 / 彩虹。
 * 两者都可在菜单「执行器」页切换并断电保存。
 * WS2812 靠高电平持续时间区分 0/1 码：0 码约 0.4us、1 码约 0.8us，位周期约 1.25us。
 * 用 DWT 周期计数器掐时间（与 soft_i2c.c 同一套方案，不依赖 SysTick），
 * 发送期间关中断，避免 ISR 拉长高电平把位数据带偏。 */
#define WS2812_LEDS        8
#define WS2812_PORT        GPIOB
#define WS2812_PIN         GPIO_PIN_12

#define WS_PIN_H()         (WS2812_PORT->BSRR = WS2812_PIN)
#define WS_PIN_L()         (WS2812_PORT->BRR  = WS2812_PIN)

/* 帧周期：正常 50ms（20fps，配合主循环节奏不追帧）；
 * 流水/彩虹 /4 提速到 12.5ms（80fps），跑动才跟手不卡顿。 */
#define LED_FRAME_MS        50
#define LED_FRAME_FAST_MS   12
/* 一轮呼吸的时长（整条一起明暗渐变，用三角波，两端各停一拍便于观察） */
#define LED_BREATH_MS       2000

static uint32_t s_t0h, s_t1h, s_tper;   /* 三个时间点折算成 DWT 周期数 */

/* 合成后待发送的一帧灯珠数据。非 static 是刻意为之：
 * ARMGCC 下 static 数组会被放进 .bss 由 startup 清零，而 Keil/ARMCC 会把它
 * 优化成「每次进作用域再赋值」的代码，等于每帧多跑 24 次赋值；放全局则
 * 只在反复使用后才可能被提到循环外，开销可忽略。（顺带也免去栈占用。） */
uint8_t g_led_fb[WS2812_LEDS * 3];

static uint32_t s_led_last;             /* 上次刷灯的 tick，控制帧率 */
static uint8_t  s_led_shadow[WS2812_LEDS * 3];  /* 上一帧已发出去的内容，用于去重 */

/* 按当前主频把纳秒折算成 DWT 周期数。
 * 用 ns 而非 us 是为了取规格中点：WS2812B 要求
 *   T0H 220~380ns、T1H 580~1600ns、位周期 1050~1400ns，
 * 取 T0H=300 / T1H=800 / TPER=1100 居中，两边都有余量。
 * （早先按 400/800/1250 取值，T0H 算出来 390ns 已超出 380ns 上限，
 *   0 码被灯珠误判成 1 码 → 整条数据错位、灯珠乱亮。） */
static uint32_t ws_ticks_ns(uint32_t ns)
{
    return (uint32_t)(((uint64_t)ns * SystemCoreClock) / 1000000000ULL);
}

static void ws2812_send_byte(uint8_t b)
{
    for (int i = 7; i >= 0; i--) {
        uint32_t t0 = DWT->CYCCNT;
        WS_PIN_H();
        uint32_t hi = (b & (1u << i)) ? s_t1h : s_t0h;
        while ((DWT->CYCCNT - t0) < hi) { }
        WS_PIN_L();
        while ((DWT->CYCCNT - t0) < s_tper) { }
    }
}

/* 发送一整帧 g_led_fb（WS2812 数据顺序是 GRB 不是 RGB）。
 * 8 颗 × 3 字节 × 1.1us ≈ 26us，关中断时长极短，不会影响 SysTick 计时。 */
static void ws2812_show(void)
{
    __disable_irq();
    for (int i = 0; i < WS2812_LEDS; i++) {
        ws2812_send_byte(g_led_fb[i * 3 + 1]);   /* G */
        ws2812_send_byte(g_led_fb[i * 3 + 0]);   /* R */
        ws2812_send_byte(g_led_fb[i * 3 + 2]);   /* B */
    }
    __enable_irq();

    /* 锁存：拉低 >50us。必须放在开中断之后——HAL_Delay 靠 SysTick 中断计时，
     * 中断关着调它会永远等不到 tick。 */
    HAL_Delay(1);
}

/* ---- 帧缓冲写入 ---- */

static void fb_set(unsigned i, uint8_t r, uint8_t g, uint8_t b)
{
    g_led_fb[i * 3 + 0] = r;
    g_led_fb[i * 3 + 1] = g;
    g_led_fb[i * 3 + 2] = b;
}

/* ---- 颜色工具 ---- */

/* 预设色表，顺序与 LedColor 枚举一致。
 * AUTO 用告警色、RAINBOW 按色相算、OFF 全灭，这三项都不查表。 */
static const uint8_t LED_PRESET[LED_COLOR_COUNT][3] = {
    {   0,   0,   0 },   /* AUTO 占位 */
    { 255,   0,   0 },   /* 红 */
    { 255,  80,   0 },   /* 橙 */
    { 255, 200,   0 },   /* 黄 */
    {   0, 255,   0 },   /* 绿 */
    {   0, 255, 180 },   /* 青 */
    {   0,  60, 255 },   /* 蓝 */
    { 160,   0, 255 },   /* 紫 */
    { 255, 255, 255 },   /* 白 */
    {   0,   0,   0 },   /* RAINBOW 占位 */
};

/* 当前生效的颜色：AUTO 走告警级别，其余查预设表 */
static void led_pick_color(AlarmLevel level, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (s_led_color == LED_COLOR_AUTO) {
        switch (level) {
        case ALARM_LEVEL_ALARM:   *r = 255; *g = 0;   *b = 0;   break;  /* 红 */
        case ALARM_LEVEL_WARNING: *r = 255; *g = 160; *b = 0;   break;  /* 黄 */
        default:                  *r = 0;   *g = 255; *b = 0;   break;  /* 绿 */
        }
    } else {
        const uint8_t *c = LED_PRESET[s_led_color];
        *r = c[0]; *g = c[1]; *b = c[2];
    }
}

/* 色相 → RGB（0..255 对应 0..360°）。只做彩虹灯效用，
 * 六段线性插值，无浮点无除法（各段跨度都是 42 或 43）。 */
static void hue_to_rgb(uint8_t hue, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (hue < 85)       { *r = (uint8_t)(255 - hue * 3);       *g = (uint8_t)(hue * 3);       *b = 0;   }
    else if (hue < 170) { uint8_t t = (uint8_t)(hue - 85);     *r = 0;                        *g = (uint8_t)(255 - t * 3); *b = (uint8_t)(t * 3); }
    else                { uint8_t t = (uint8_t)(hue - 170);    *r = (uint8_t)(t * 3);          *g = 0;                        *b = (uint8_t)(255 - t * 3); }
}

/* 渲染一帧到 g_led_fb，返回 1 表示内容有变化、需要发送。
 * 正交模型：先由**颜色**铺出每颗灯的底色，再由**灯效**调制亮度。
 * now 由主循环传入，避免这里再取一次 tick 与调用方不同步。 */
static int led_frame_render(AlarmLevel level, uint32_t now)
{
    uint8_t base_r[WS2812_LEDS], base_g[WS2812_LEDS], base_b[WS2812_LEDS];
    uint8_t r, g, b;

    /* 只有会动的组合才需要高频出帧：彩虹本身在流动，呼吸/流水在变亮度 */
    int animated = (s_led_effect != LED_FX_STEADY) ||
                   (s_led_color  == LED_COLOR_RAINBOW);
    uint32_t frame_ms = animated ? LED_FRAME_FAST_MS : LED_FRAME_MS;
    if (!s_led_first && (now - s_led_last) < frame_ms) {
        return 0;                       /* 还没到出帧时间，省下这次 SPI 发送 */
    }
    s_led_last  = now;
    s_led_first = 0;

    /* ---- 第一步：颜色 → 每颗灯的底色（总开关关掉时直接全 0，颜色/灯效都不必算） ---- */
    if (!s_led_on) {
        for (unsigned i = 0; i < WS2812_LEDS; i++) {
            base_r[i] = base_g[i] = base_b[i] = 0;
        }
    } else if (s_led_color == LED_COLOR_RAINBOW) {
        /* 色相沿灯带铺一圈（256/8 = 32/颗），再随时间整体旋转：
         * 每颗的色相变化速度一致，整条看起来是「彩虹在流动」而不是各转各的。 */
        uint8_t rot = (uint8_t)(now / 12);
        for (unsigned i = 0; i < WS2812_LEDS; i++) {
            hue_to_rgb((uint8_t)(rot + i * (256 / WS2812_LEDS)),
                       &base_r[i], &base_g[i], &base_b[i]);
        }
    } else {
        led_pick_color(level, &r, &g, &b);
        for (unsigned i = 0; i < WS2812_LEDS; i++) {
            base_r[i] = r; base_g[i] = g; base_b[i] = b;
        }
    }

    /* ---- 第二步：灯效调制亮度（关灯时底色已全 0，调不调都是灭，不必特判） ---- */
    switch (s_led_effect) {
    case LED_FX_BREATH: {
        /* 三角波：0 → 全亮 → 0，一个 LED_BREATH_MS 周期。
         * 前半段 num 直接从 0 升到 den、后半段对称下降，不需要取绝对值。
         * 谷底留 8% 底色，纯灭看起来像接触不良。 */
        uint32_t ph   = now % LED_BREATH_MS;
        uint32_t half = LED_BREATH_MS / 2;
        uint32_t num  = ((ph < half) ? ph : (LED_BREATH_MS - ph)) + half / 12;
        uint32_t den  = half + half / 12;
        for (unsigned i = 0; i < WS2812_LEDS; i++) {
            fb_set(i, (uint8_t)((uint32_t)base_r[i] * num / den),
                      (uint8_t)((uint32_t)base_g[i] * num / den),
                      (uint8_t)((uint32_t)base_b[i] * num / den));
        }
        break;
    }

    case LED_FX_FLOW: {
        /* 亮珠带两条递减拖尾，绕灯带循环跑 */
        unsigned head = (now / 90) % WS2812_LEDS;
        for (unsigned i = 0; i < WS2812_LEDS; i++) {
            unsigned d = (i + WS2812_LEDS - head) % WS2812_LEDS;
            uint8_t k = (d == 0) ? 255 : (d == 1 ? 90 : (d == 2 ? 30 : 0));
            fb_set(i, (uint8_t)((uint32_t)base_r[i] * k / 255),
                      (uint8_t)((uint32_t)base_g[i] * k / 255),
                      (uint8_t)((uint32_t)base_b[i] * k / 255));
        }
        break;
    }

    case LED_FX_STEADY:
    default:
        for (unsigned i = 0; i < WS2812_LEDS; i++) {
            fb_set(i, base_r[i], base_g[i], base_b[i]);
        }
        break;
    }

    /* 与上一帧逐字节相同就不发：常亮/关闭时省掉每帧一次 26us 发送 + 1ms 锁存延时 */
    if (memcmp(g_led_fb, s_led_shadow, sizeof(g_led_fb)) == 0) {
        return 0;
    }
    memcpy(s_led_shadow, g_led_fb, sizeof(g_led_fb));
    return 1;
}

void WS2812_Init(void)
{
    /* DWT 周期计数器（soft_i2c.c 里也开过一次，这里再开保证本模块独立可用） */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    /* 下面三个常数依赖 SystemCoreClock 已是最终主频（64MHz）。
     * HAL_RCC_ClockConfig() 内部会更新该变量，只要本函数在 SystemClock_Config()
     * 之后调用即可（当前在 main() 初始化段，满足）。若日后调整初始化顺序需留意。 */
    s_t0h  = ws_ticks_ns(300);    /* 0.30us（规格 220~380） */
    s_t1h  = ws_ticks_ns(800);    /* 0.80us（规格 580~1600） */
    s_tper = ws_ticks_ns(1100);   /* 1.10us（规格 1050~1400） */

    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin   = WS2812_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(WS2812_PORT, &gpio);
    WS_PIN_L();                        /* 空闲保持低电平 */

    memset(g_led_fb, 0, sizeof(g_led_fb));
    memset(s_led_shadow, 0xFF, sizeof(s_led_shadow));   /* 与全灭帧不同 → 首次必发 */
    ws2812_show();                     /* 上电先全灭，避免上电瞬间残留随机色 */

    s_led_last  = HAL_GetTick();
    s_led_first = 1;                   /* 第一次 Update 立刻出帧（含数据） */
}

/* 主循环每轮调用：按当前颜色/灯效推进一帧。
 * 帧率与「内容有没有变」都在 led_frame_render 里判掉——
 * 常亮/呼吸下颜色没变就不再发 SPI，发一帧 8 颗约 26us。 */
void WS2812_Update(AlarmLevel level, uint32_t now)
{
    if (led_frame_render(level, now)) {
        ws2812_show();
    }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_ADC1_Init();
  MX_I2C1_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  /* 从 Flash 加载阈值（有有效数据则覆盖默认值） */
  EdgeAlarm_LoadFlash();

  /* 传感器初始化 */
  Sensor_InitAll();

  /* 执行器初始化（蜂鸣器 PC0/PC1 + 继电器 PC2/PC3，初始均关闭） */
  Actuator_Init();

  /* WS2812 灯带初始化（PB12，上电先全灭） */
  WS2812_Init();

  /* K210 视觉模块链路初始化（USART1，PA9=TX/PA10=RX，见 k210.c） */
  MX_USART1_UART_Init();
  K210_Init();

  /* LCD 初始化 */
  ST7735_Init();

  /* LCD 中文菜单初始化（含 4 键） */
  Menu_Init();

  /* 时间基准复位（等服务器下发校准） */
  TimeSync_Reset();

  /* 4G 网络注册 + TCP + MQTT 连接：失败不阻塞，继续采集显示，只是不上报 */
  net_ok = 1;
  if (WiFi_Init() != 0) {
    net_ok = 0;
  }
  if (net_ok && WiFi_ConnectServer() != 0) {
    net_ok = 0;
  }
  if (net_ok) {
    /* 建立 MQTT 连接（EMQX Cloud 用户名密码认证，凭据定义在 mqtt_client.h） */
    MQTT_Client cli;
    memset(&cli, 0, sizeof(cli));
    strncpy(cli.client_id, DEVICE_ID, sizeof(cli.client_id) - 1);
    strncpy(cli.username, MQTT_USERNAME, sizeof(cli.username) - 1);
    strncpy(cli.password, MQTT_PASSWORD, sizeof(cli.password) - 1);
    cli.keepalive = 60;
    if (MQTT_Connect(&cli) != 0) {
      net_ok = 0;
    } else {
      /* 订阅自己的时间主题，接收服务器下发的对时时间 */
      char sub[48];
      snprintf(sub, sizeof(sub), "shm/%s/time", DEVICE_ID);
      MQTT_Subscribe(sub, 0);
    }
  }

  /* 隧板无 GNSS：固定坐标（待现场标定），fix=1 表示已定位 */
  GPSData gps_last;
  memset(&gps_last, 0, sizeof(gps_last));
  gps_last.fix = 1;
  gps_last.lat = 0.0f;   /* TODO: 隧道节点纬度 */
  gps_last.lon = 0.0f;   /* TODO: 隧道节点经度 */

  /* 周期计时起点 */
  uint32_t last_report = 0;
  uint32_t last_heartbeat = 0;

  /* 菜单上下文：主循环周期刷新数据，菜单只读展示 */
  MenuCtx menu_ctx;
  memset(&menu_ctx, 0, sizeof(menu_ctx));
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    uint32_t now = HAL_GetTick();

    /* 周期采集 → 边缘告警 → 数据上报（结果同步到菜单上下文） */
    if (now - last_report >= REPORT_INTERVAL_MS) {
      last_report = now;

      /* 先问 K210 要一次识别结果（阻塞约几 ms，K210 无应答时最多
       * K210_REPLY_TIMEOUT_MS），随后 Sensor_ReadAll 才能读到裂缝值。
       * 放在采集之前，保证读到的是本轮最新结果。 */
      K210_Poll();

      menu_ctx.count = Sensor_ReadAll(menu_ctx.data, SENSOR_TYPE_COUNT);
      menu_ctx.net_ok = net_ok;

      /* 边缘告警判定：确定最高告警级别（执行器由主循环 Actuator_Update 驱动） */
      g_alarm_level = ALARM_LEVEL_NORMAL;
      for (int i = 0; i < menu_ctx.count; i++) {
        menu_ctx.alarm[i] = EdgeAlarm_CheckOne(&menu_ctx.data[i]);
        if (menu_ctx.alarm[i] > g_alarm_level) {
          g_alarm_level = menu_ctx.alarm[i];
        }
        if (menu_ctx.alarm[i] >= ALARM_LEVEL_WARNING && net_ok) {
          report_alarm(&menu_ctx.data[i], menu_ctx.alarm[i]);
        }
      }

      if (net_ok) {
        DataReport_Publish(menu_ctx.data, menu_ctx.count, &gps_last);
      }
    }

    /* 周期心跳 */
    if (now - last_heartbeat >= HEARTBEAT_INTERVAL_MS) {
      last_heartbeat = now;
      if (net_ok) {
        DataReport_Heartbeat();
        /* 心跳后短暂等待，读服务器回发的对时时间 */
        uint8_t rb[192];
        HAL_Delay(150);
        int rn = WiFi_Recv(rb, sizeof(rb), 400);
        if (rn > 0) {
          TimeSync_Parse(rb, (uint16_t)rn);
        }
      }
    }

    /* 蜂鸣器分级驱动（预警滴滴滴需每轮刷新，独立于 2 秒采集周期） */
    /* 执行器驱动：蜂鸣器分级 + 继电器（预警滴滴滴需每轮刷新，独立于 2 秒采集周期） */
    Actuator_Update(g_alarm_level, now);

    /* WS2812 灯带：按当前颜色/灯效推进一帧（帧率内部把控） */
    WS2812_Update(g_alarm_level, now);

    /* 菜单：按键导航 + 实时数据页定时刷新 */
    Menu_Process(&menu_ctx);

    HAL_Delay(50);
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL16;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
  HAL_RCC_MCOConfig(RCC_MCO, RCC_MCO1SOURCE_SYSCLK, RCC_MCODIV_1);
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_0;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_1CYCLE_5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief USART1 Initialization Function（接 K210 视觉模块）
  * @note  PA9 = USART1_TX / PA10 = USART1_RX，均为默认映射、无需重映射，
  *        所以 HAL_UART_Init() 会自动配好 GPIO（见 HAL_UART_MspInit）。
  *        参数与接 ESP8266 的 USART2 一致：115200 8N1。
  *        整个函数写在 USER CODE 区，CubeMX 重新生成时不会被清掉。
  */
static void MX_USART1_UART_Init(void)
{
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();

  /*Configure GPIO pin : PA8 */
  GPIO_InitStruct.Pin = GPIO_PIN_8;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* 蜂鸣器（PC0/PC1）+ 继电器（PC2/PC3）的 GPIO 由 Actuator_Init() 配置 */

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
