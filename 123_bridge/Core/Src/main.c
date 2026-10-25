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
#include "lte_ct02.h"
#include "gps.h"
#include "data_report.h"
#include "edge_alarm.h"
#include "sensor_drivers.h"  /* Cable_Tick（索力非阻塞采样） */
#include "ds18b20.h"         /* DS18B20_Tick（固体温度非阻塞采样） */
#include "ultrasonic.h"      /* Ultrasonic_Tick（超声波非阻塞测距） */
#include "ws2812.h"          /* WS2812_SetPixel/Fill/Show（灯带跟随） */
#include "st7735.h"
#include "menu.h"
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
/* sw-420 震动开关输入（数字输出 DO） */
#define SW420_PORT            GPIOC
#define SW420_1_PIN           GPIO_PIN_4
#define SW420_2_PIN           GPIO_PIN_5

/* sw-420 触发方向：这类模块的 DO 极性厂家做法不一（LED 接 VCC-DO 或 DO-GND 不同）。
 * 实测确认：用万用表量 DO 对 GND——
 *   静止 ≈0V、震动 ≈3.3V → 改为 0（低电平触发）
 *   静止 ≈3.3V、震动 ≈0V → 保持 1（高电平触发）
 * 症状对照：若静止时蜂鸣器一直响，多半是方向反了，把这个宏反过来即可。 */
#define SW420_ACTIVE_HIGH     1

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

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */
static void report_alarm(const SensorData *d, AlarmLevel level);
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

/* sw-420 是否触发：任意一块检测到震动即为真（两块装在两个位置，扩大覆盖）。
 * 触发极性由 SW420_ACTIVE_HIGH 决定，见文件顶部说明。
 * 注意：模块未接时引脚靠内部上拉为高电平，判定为「无震动」，不会误报。 */
static int sw420_triggered(void)
{
#if SW420_ACTIVE_HIGH
    return (HAL_GPIO_ReadPin(SW420_PORT, SW420_1_PIN) == GPIO_PIN_SET) ||
           (HAL_GPIO_ReadPin(SW420_PORT, SW420_2_PIN) == GPIO_PIN_SET);
#else
    return (HAL_GPIO_ReadPin(SW420_PORT, SW420_1_PIN) == GPIO_PIN_RESET) ||
           (HAL_GPIO_ReadPin(SW420_PORT, SW420_2_PIN) == GPIO_PIN_RESET);
#endif
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

/* 灯带状态（灯带本体见下方 WS2812 段；状态与执行器同页保存故放这里）。
 * 置位 s_led_first 表示「下一次 Update 立刻出帧、不等帧周期」，
 * Led_* 在本段就要写它，而渲染代码在文件更后面，故声明放在这一组里。 */
static uint8_t      s_led_on      = 1;
static LedColor     s_led_color   = LED_COLOR_AUTO;
static LedEffect    s_led_effect  = LED_FX_STEADY;
static LedFollow    s_led_follow  = LED_FOLLOW_OFF;
static uint8_t      s_led_first   = 1;

/* 执行器模式的 Flash 持久化：存倒数第二页，与阈值页（末页 0x0803F800）互不干扰，
 * 这样改模式不会擦掉阈值、改阈值也不会擦掉模式 */
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
    /* p[3]~p[6] 是后加的字段（颜色/跟随/开关/灯效）。旧固件只写过 3 个字，
     * 这几处是擦除态 0xFFFFFFFF，越界检查会拦下 → 保持默认值，无需版本迁移。
     * p[6]（灯效）是新加在**末尾**的，不影响前面已存的索引。 */
    if (p[3] < LED_COLOR_COUNT)  { s_led_color  = (LedColor)p[3];  }
    if (p[4] < LED_FOLLOW_COUNT) { s_led_follow = (LedFollow)p[4]; }
    if (p[5] <= 1)               { s_led_on     = (uint8_t)p[5];   }
    if (p[6] < LED_FX_COUNT)     { s_led_effect = (LedEffect)p[6]; }
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
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, (uint32_t)s_led_follow);
        addr += 4;
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, (uint32_t)s_led_on);
        addr += 4;
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, (uint32_t)s_led_effect);
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

uint8_t   Led_GetOn(void)     { return s_led_on;      }
LedColor  Led_GetColor(void)  { return s_led_color;   }
LedEffect Led_GetEffect(void) { return s_led_effect;  }
LedFollow Led_GetFollow(void) { return s_led_follow;  }

/* 切换开关/颜色/跟随效果后置位 s_led_first，让下一次 Update 立刻出帧、不等帧周期，
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
    s_led_first  = 1;
    Actuator_SaveFlash();
}

void Led_CycleFollow(void)
{
    s_led_follow = (LedFollow)((s_led_follow + 1) % LED_FOLLOW_COUNT);
    s_led_first  = 1;
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

/* ============ WS2812 灯带：物体位置跟随 ============
 * 超声波测到物体距离 → 映射到灯带上的位置 → 按「跟随效果」点亮灯珠。
 * 帧率与去重在这里把控（同隧板做法），主循环只管每轮调一次 Update。
 *   · 静态效果（单点/进度条，且颜色固定）走慢帧，省 SPI
 *   · 会动的（拖尾、自动色、彩虹、距离渐变）走快帧
 */
#define LED_FRAME_MS        50
/* ⭐ 快帧取 8ms 而不是 12ms：主循环 HAL_Delay(10) 已占 10ms，
 * 12ms 的帧间隔会落到「隔一轮才出一帧」的节奏上（实际 ~20ms/帧 → 50fps），
 * 取 8ms 才能稳定地**每轮都出帧**，跟上主循环的 10ms。 */
#define LED_FRAME_FAST_MS   8

/* 流水灯每颗的停留时间（ms）：head = now / LED_FLOW_STEP_MS。
 * 原来写 90ms —— 25 颗绕一圈要 2.25 秒，且每颗位置停留 7.5 帧，
 * 视觉上是一格一格地蹦。降到 35ms：每颗约 3.5 帧、绕一圈约 0.9 秒，
 * 连续感明显变好；再小就跟不上帧率了（会出现同一颗停留 1 帧的抖动）。 */
#define LED_FLOW_STEP_MS    35
/* 流水拖尾长度（颗）。亮珠本身最亮，往后线性渐暗到 0。
 * 注意与「跟随」里的 LED_TAIL_SPAN 是两回事，别混用。 */
#define LED_FLOW_SPAN       6

/* 一轮呼吸的时长（整条明暗渐变，用三角波，两端各留一拍便于观察） */
#define LED_BREATH_MS       2000
#define LED_N               WS2812_NUM       /* 灯珠数，见 ws2812.h */

/* 测距映射到灯带位置的量程（cm）：把 [MIN, MAX] 线性铺到 25 颗灯珠上。
 *
 * ⭐ 这个范围**必须覆盖实际演示的距离**，否则会被钳死：
 *    超出 MAX 的值全被钳到最后一颗 → 进度条整条常亮不变、拖尾只剩一个点、
 *    距离变色恒定一个颜色（只有单点跟随看着「像正常」，因为它也只是停在末端）。
 *    曾把 MAX 设成 200cm，而对墙测超过 2 米，就踩了这个坑。
 *
 * 当前按「贴着手推，10~100cm」的演示设定，两头各留一点余量，
 * 使 10~100cm 这段用满 25 颗（约 4.0cm/颗）。换演示距离时改这两个数即可。 */
#define LED_RANGE_MIN_CM    5.0f
#define LED_RANGE_MAX_CM    105.0f
/* 单点跟随的点亮颗数（居中），拖尾的总长（含头） */
#define LED_DOT_SPAN        2
#define LED_TAIL_SPAN       8

static uint32_t s_led_last = 0;

/* 预设色表，顺序与 LedColor 枚举一致。
 * AUTO 用告警色、RAINBOW 按色相算，这两项不查表。 */
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

/* 色相 → RGB（0..255 对应 0..360°）。六段线性插值，无浮点无除法。 */
static void hue_to_rgb(uint8_t hue, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (hue < 85)       { *r = (uint8_t)(255 - hue * 3);    *g = (uint8_t)(hue * 3);       *b = 0; }
    else if (hue < 170) { uint8_t t = (uint8_t)(hue - 85);  *r = 0;                        *g = (uint8_t)(255 - t * 3); *b = (uint8_t)(t * 3); }
    else                { uint8_t t = (uint8_t)(hue - 170); *r = (uint8_t)(t * 3);          *g = 0;                        *b = (uint8_t)(255 - t * 3); }
}

/* 按「颜色」设置 + 告警级别，算出某颗灯珠的底色 */
static void led_base_color(uint16_t i, AlarmLevel level, float cm, uint8_t *r, uint8_t *g, uint8_t *b)
{
    switch (s_led_color) {
    case LED_COLOR_AUTO:
        switch (level) {
        case ALARM_LEVEL_ALARM:   *r = 255; *g = 0;   *b = 0;   break;  /* 红 */
        case ALARM_LEVEL_WARNING: *r = 255; *g = 160; *b = 0;   break;  /* 黄 */
        default:                  *r = 0;   *g = 255; *b = 0;   break;  /* 绿 */
        }
        break;

    case LED_COLOR_RAINBOW:
        /* 色相沿灯带铺一圈，再随时间整体旋转 */
        hue_to_rgb((uint8_t)((HAL_GetTick() / 12) + i * (256 / LED_N)), r, g, b);
        break;

    default: {
        const uint8_t *c = LED_PRESET[s_led_color];
        *r = c[0]; *g = c[1]; *b = c[2];
        break;
    }
    }
}

/* 距离 → 灯带位置（0 ~ LED_N-1 的**浮点**位置）。量程外分别钳到两端。
 *
 * 为什么给浮点而不是取整：拖尾/单点的亮度按「离光点多远」连续衰减，
 * 保留小数才能让光点落在两颗灯珠之间（各亮一半），移动才平滑；
 * 取整的话，距离变化不足一颗的宽度时 pos 不动，光点会一格一格地蹦。 */
static void led_pos_of(float cm, float *pos)
{
    if (cm <= LED_RANGE_MIN_CM) { *pos = 0.0f; return; }
    if (cm >= LED_RANGE_MAX_CM) { *pos = (float)(LED_N - 1); return; }
    float f = (cm - LED_RANGE_MIN_CM) / (LED_RANGE_MAX_CM - LED_RANGE_MIN_CM);
    *pos = f * (float)(LED_N - 1);
    if (*pos < 0.0f) { *pos = 0.0f; }
    if (*pos > (float)(LED_N - 1)) { *pos = (float)(LED_N - 1); }
}

/* 「距离越近颜色越炫」：远=蓝紫(冷) → 近=红(热)，色相 170→0 线性过渡 */
static void led_dist_hue(float cm, uint8_t *r, uint8_t *g, uint8_t *b)
{
    float f = (cm - LED_RANGE_MIN_CM) / (LED_RANGE_MAX_CM - LED_RANGE_MIN_CM);
    if (f < 0.0f) { f = 0.0f; }
    if (f > 1.0f) { f = 1.0f; }
    hue_to_rgb((uint8_t)(f * 170.0f), r, g, b);   /* f=0 → 0(红) ; f=1 → 170(蓝) */
}

/* 渲染一帧到灯带，返回 1 表示内容有变化、需要发送 */
static int led_frame_render(AlarmLevel level, uint32_t now)
{
    float cm = 0.0f;
    int   follow = (int)s_led_follow;

    /* 测不到距离（模块未接 / 超 4m 量程）时，**按最远处处理**：
     * led_pos_of 会把超出量程的值钳到最后一颗，于是
     *   单点跟随 → 光点亮在末端、进度条 → 全亮、拖尾 → 跑到末端
     * 这样超声波没接也能看到跟随效果，便于演示和排查。
     * 只有「跟随=关」才不做位置映射（那本来就不看距离）。 */
    int   has_pos = (Ultrasonic_Read(&cm) == 0);
    float cm_use  = has_pos ? cm : (LED_RANGE_MAX_CM + 1.0f);
    float posf    = 0.0f;     /* 距离映射到的**浮点**灯位，位置型效果用（见 led_pos_of） */

    /* 会动的组合走快帧：呼吸/流水在变亮度、拖尾在跑、自动色随告警变、
     * 彩虹在流动、距离渐变随物体动 */
    int animated = (s_led_effect != LED_FX_STEADY) ||
                   (follow == LED_FOLLOW_TAIL) ||
                   (s_led_color == LED_COLOR_AUTO) ||
                   (s_led_color == LED_COLOR_RAINBOW) ||
                   (follow == LED_FOLLOW_FAR_COLOR);
    uint32_t frame_ms = animated ? LED_FRAME_FAST_MS : LED_FRAME_MS;
    if (!s_led_first && (now - s_led_last) < frame_ms) {
        return 0;
    }
    s_led_last  = now;
    s_led_first = 0;

    WS2812_Clear();

    /* 总开关关闭 → 全灭（颜色/跟随/灯效都不必算） */
    if (!s_led_on) {
        /* 已 Clear，直接发 */
    } else {
        /* ---- 第一遍：颜色 + 跟随 → 每颗灯的底色 ---- */
        if (follow == LED_FOLLOW_FAR_COLOR) {
            /* 距离变色：整条同色，随距离冷暖渐变 */
            uint8_t r, g, b;
            led_dist_hue(cm_use, &r, &g, &b);
            WS2812_Fill(r, g, b);
        } else if (follow == LED_FOLLOW_OFF) {
            /* 「跟随=关」不做位置映射 → 整条按颜色铺底 */
            for (uint16_t i = 0; i < LED_N; i++) {
                uint8_t r, g, b;
                led_base_color(i, level, cm_use, &r, &g, &b);
                WS2812_SetPixel(i, r, g, b);
            }
        } else {
            /* 三种位置型效果：单点跟随 / 进度条 / 拖尾。
             * cm_use 在测不到距离时是「超量程」的值，led_pos_of 会钳到最后一颗，
             * 于是效果退到最远端 —— 超声波没接也能看到跟随在动。 */
            led_pos_of(cm_use, &posf);
            for (uint16_t i = 0; i < LED_N; i++) {
                uint8_t r, g, b;
                led_base_color(i, level, cm_use, &r, &g, &b);

                if (follow == LED_FOLLOW_PROGRESS) {
                    /* 进度条：物体之前的灯全亮，之后的灭 */
                    if ((float)i > posf) { r = g = b = 0; }
                } else if (follow == LED_FOLLOW_DOT) {
                    /* 单点跟随：只有物体位置附近 LED_DOT_SPAN 颗亮，其余留一点点底光。
                     * 用浮点距离，光点跨在两颗之间时两颗各亮一些，过渡自然。 */
                    float dd = (float)i - posf;
                    if (dd < 0.0f) { dd = -dd; }
                    if (dd >= (float)LED_DOT_SPAN) {
                        r = (uint8_t)((uint32_t)r * 6 / 255);   /* ≈2% 底光，便于看清整条带 */
                        g = (uint8_t)((uint32_t)g * 6 / 255);
                        b = (uint8_t)((uint32_t)b * 6 / 255);
                    }
                } else { /* LED_FOLLOW_TAIL：彗星拖尾 */
                    /* d = 该灯珠落后头部多少颗：
                     *   d < 0  → 在头部前方，全灭
                     *   d = 0  → 头部，最亮
                     *   d 递增 → 拖尾线性渐暗，到 LED_TAIL_SPAN 处正好为 0
                     * ⚠️ 衰减要用浮点算：写成 d * (255 / LED_TAIL_SPAN) 的话
                     *  255/8 是整数除法 = 31，d=7 时 k=38，永远到不了 0 ——
                     *  尾巴会「突然截断」而不是平滑消失。 */
                    float dd = (float)i - posf;
                    if (dd < 0.0f || dd >= (float)LED_TAIL_SPAN) {
                        r = g = b = 0;
                    } else {
                        uint8_t k = (uint8_t)(255.0f *
                                    (1.0f - dd / (float)LED_TAIL_SPAN));
                        r = (uint8_t)((uint32_t)r * k / 255);
                        g = (uint8_t)((uint32_t)g * k / 255);
                        b = (uint8_t)((uint32_t)b * k / 255);
                    }
                }
                WS2812_SetPixel(i, r, g, b);
            }
        }

        /* ---- 第二遍：灯效调制亮度 ----
         * 与颜色/跟随正交，所以「呼吸 + 单点跟随」「流水 + 彩虹」这类组合都能work。
         * 关灯时已在上面全 0，调不调都是灭，不必特判。 */
        if (s_led_effect == LED_FX_BREATH) {
            /* 三角波：0 → 全亮 → 0，一个 LED_BREATH_MS 周期。
             * 谷底留 8% 底色，纯灭看起来像接触不良。 */
            uint32_t ph   = now % LED_BREATH_MS;
            uint32_t half = LED_BREATH_MS / 2;
            uint32_t num  = ((ph < half) ? ph : (LED_BREATH_MS - ph)) + half / 12;
            uint32_t den  = half + half / 12;
            for (uint16_t i = 0; i < LED_N; i++) {
                uint8_t r, g, b;
                WS2812_GetPixel(i, &r, &g, &b);
                WS2812_SetPixel(i,
                                (uint8_t)((uint32_t)r * num / den),
                                (uint8_t)((uint32_t)g * num / den),
                                (uint8_t)((uint32_t)b * num / den));
            }
        } else if (s_led_effect == LED_FX_FLOW) {
            /* 流水：一颗亮珠带递减拖尾，绕整条灯带循环跑。
             * 只调制亮度，所以底下铺的彩虹色照样透出来。
             * 位置用**带小数的步进**再取整，而不是整数取模：
             * 直接 (now/35)%N 的话，位置是整数台阶，灯珠一颗一颗地蹦；
             * 这里让 head 按浮点推进、每颗的亮度按「离亮珠多远」**连续**衰减，
             * 于是光点能落在两颗之间（各亮一半），移动就顺了。 */
            /* now 从 0 单调递增，不会为负，直接对 LED_N 取模即得 [0, LED_N) 的浮点位置 */
            float head_f = (float)(now % (LED_FLOW_STEP_MS * LED_N))
                         / (float)LED_FLOW_STEP_MS;
            for (uint16_t i = 0; i < LED_N; i++) {
                /* 到亮珠的环绕距离，取最近方向（前后都能靠近） */
                float d = (float)i - head_f;
                if (d < -((float)LED_N / 2.0f)) { d += (float)LED_N; }
                if (d >  ((float)LED_N / 2.0f)) { d -= (float)LED_N; }
                if (d < 0.0f) { d = -d; }

                /* 衰减：LED_FLOW_SPAN 颗之内线性渐暗，之外全灭 */
                uint8_t k;
                if (d >= (float)LED_FLOW_SPAN) {
                    k = 0;
                } else {
                    k = (uint8_t)(255.0f * (1.0f - d / (float)LED_FLOW_SPAN));
                }
                uint8_t r, g, b;
                WS2812_GetPixel(i, &r, &g, &b);
                WS2812_SetPixel(i,
                                (uint8_t)((uint32_t)r * k / 255),
                                (uint8_t)((uint32_t)g * k / 255),
                                (uint8_t)((uint32_t)b * k / 255));
            }
        }
        /* LED_FX_STEADY：不调制，保持第一遍的结果 */
    }

    return 1;
}

/* 主循环每轮调用：按当前设置 + 测距结果推进一帧 */
void WS2812_Update(AlarmLevel level, uint32_t now)
{
    if (led_frame_render(level, now)) {
        WS2812_Show();
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

  /* WS2812 灯带初始化（DIN=PB12，30 颗；上电先全灭） */
  WS2812_Init();

  /* LCD 初始化 */
  ST7735_Init();

  /* LCD 中文菜单初始化（含 4 键） */
  Menu_Init();

  /* 时间基准复位（等服务器下发校准） */
  TimeSync_Reset();

  /* 4G 联网改为后台状态机（主循环轮询），失败自动重试，不阻塞采集/告警 */
  net_ok = 0;
  LTE_TaskInit();

  /* 后台联网状态机变量：0=联网中 2=已完成 3=失败等待重试 */
  int lte_phase = 0;
  uint32_t lte_retry_deadline = 0;

  /* GPS 最近定位 + 查询周期计时 */
  GPSData gps_last;
  memset(&gps_last, 0, sizeof(gps_last));
  uint32_t last_gps = 0;

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

    /* 4G 后台联网状态机：TCP 就绪后一次性建 MQTT + 开 GPS */
    if (lte_phase == 0) {
      int r = LTE_TaskPoll();
      if (r == 0) {
        MQTT_Client cli;
        memset(&cli, 0, sizeof(cli));
        strncpy(cli.client_id, DEVICE_ID, sizeof(cli.client_id) - 1);
        strncpy(cli.username, MQTT_USERNAME, sizeof(cli.username) - 1);
        strncpy(cli.password, MQTT_PASSWORD, sizeof(cli.password) - 1);
        cli.keepalive = 60;
        net_ok = (MQTT_Connect(&cli) == 0) ? 1 : 0;
        if (net_ok) {
          /* 订阅自己的时间主题，接收服务器对时 */
          char sub[48];
          snprintf(sub, sizeof(sub), "shm/%s/time", DEVICE_ID);
          MQTT_Subscribe(sub, 0);
        }
        GPS_Init();
        lte_phase = 2;
      } else if (r == 1) {
        lte_phase = 3;
        lte_retry_deadline = now + 30000;   /* 失败 30 秒后重试 */
      }
    } else if (lte_phase == 3) {
      if ((int32_t)(now - lte_retry_deadline) >= 0) {
        LTE_TaskInit();
        lte_phase = 0;
      }
    }

    /* 索力采样推进（非阻塞，10ms 主循环轮询） */
    Cable_Tick();

    /* DS18B20 固体温度采样推进（非阻塞） */
    DS18B20_Tick();

    /* 超声波测距推进（非阻塞，100ms 一次；供实时数据页与灯带跟随用） */
    Ultrasonic_Tick();

    /* 周期采集 → 边缘告警 → 数据上报（结果同步到菜单上下文） */
    if (now - last_report >= REPORT_INTERVAL_MS) {
      last_report = now;

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

      /* sw-420 震动开关：检测到震动按报警处理（两蜂鸣器响 + 上报） */
      if (sw420_triggered()) {
        g_alarm_level = ALARM_LEVEL_ALARM;
        if (net_ok) {
          SensorData v = { SENSOR_VIBRATION, 1.0f, "g" };
          report_alarm(&v, ALARM_LEVEL_ALARM);
        }
      }

      if (net_ok) {
        DataReport_Publish(menu_ctx.data, menu_ctx.count, &gps_last);
      }
    }

    /* 周期查询 GPS 定位（联网成功后才查，与 MQTT 串行共用 USART2） */
    if (net_ok && now - last_gps >= GPS_INTERVAL_MS) {
      last_gps = now;
      GPSData gps;
      if (GPS_GetFix(&gps) == 0 && gps.fix == 1) {
        gps_last = gps;
      }
    }

    /* 周期心跳 */
    if (now - last_heartbeat >= HEARTBEAT_INTERVAL_MS) {
      last_heartbeat = now;
      if (net_ok) {
        DataReport_Heartbeat();
        /* 心跳发送完毕后的空闲窗读一次服务器回发的对时（4G 命令式串口，
         * 只在此时读，避免与 AT 会话抢串口导致连不上网） */
        uint8_t rb[192];
        HAL_Delay(150);
        int rn = LTE_Recv(rb, sizeof(rb), 400);
        if (rn > 0) {
          TimeSync_Parse(rb, (uint16_t)rn);
        }
      }
    }

    /* 执行器驱动：蜂鸣器分级 + 继电器（预警滴滴滴需每轮刷新，独立于 2 秒采集周期） */
    Actuator_Update(g_alarm_level, now);

    /* WS2812 灯带：按超声波测到的物体位置做跟随（帧率内部把控） */
    WS2812_Update(g_alarm_level, now);

    /* 菜单：按键导航 + 实时数据页定时刷新 */
    Menu_Process(&menu_ctx);

    HAL_Delay(10);   /* 10ms 主循环：索力采样（Cable_Tick）+ 摇杆更灵敏 */
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
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL8;
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

  /* sw-420 震动开关：上拉输入，震动时输出低电平 */
  GPIO_InitStruct.Pin = SW420_1_PIN | SW420_2_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(SW420_PORT, &GPIO_InitStruct);

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
