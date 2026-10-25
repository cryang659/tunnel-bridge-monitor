/**
 * @file    ds18b20.c
 * @brief   DS18B20 单总线驱动实现
 *
 * 引脚（桥板空闲 ADC 口）：#0=PA4、#1=PA6（桥面），#2=PB0、#3=PB1（桥墩）。
 * 开漏输出（GPIO_MODE_OUTPUT_OD）+ 外部 4.7k 上拉：输出高 = 释放总线，
 * 读时输出高后直接读 IDR，无需切换方向。时序用 DWT 计数器做微秒延时。
 *
 * 状态机：START 对 4 个并发发转换命令(0x44) → WAIT 800ms → READ 逐个读暂存器(0xBE)。
 */
#include "ds18b20.h"
#include "main.h"

/* 引脚表（桥面 PA4/PA6，桥墩 PB0/PB1） */
typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
} DsPin;

static const DsPin s_pins[DS18B20_NUM] = {
    { GPIOA, GPIO_PIN_4 },   /* 桥面 #0 */
    { GPIOA, GPIO_PIN_6 },   /* 桥面 #1 */
    { GPIOB, GPIO_PIN_0 },   /* 桥墩 #2 */
    { GPIOB, GPIO_PIN_1 },   /* 桥墩 #3 */
};

static float   s_temp[DS18B20_NUM];    /* 最近一次温度（℃） */
static uint8_t s_valid[DS18B20_NUM];   /* 是否有效 */

/* 状态机 */
typedef enum { DS_START, DS_WAIT, DS_READ } DsState;
static DsState  s_state = DS_START;
static uint32_t s_deadline = 0;

#define DS_CONVERT_MS   800    /* 12 位分辨率转换时间，留余量 */

/* 微秒延时（DWT 计数器，与 soft_i2c.c 一致） */
static void ds_delay_us(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * (SystemCoreClock / 1000000U);
    while ((DWT->CYCCNT - start) < ticks) {
    }
}

/* ---- 单总线原语 ---- */
/* 复位：拉低 480us → 释放 → 检测存在脉冲；返回 0=存在器件 */
static uint8_t ds_reset(DsPin p)
{
    HAL_GPIO_WritePin(p.port, p.pin, GPIO_PIN_RESET);
    ds_delay_us(480);
    HAL_GPIO_WritePin(p.port, p.pin, GPIO_PIN_SET);   /* 释放 */
    ds_delay_us(70);
    uint8_t present = (HAL_GPIO_ReadPin(p.port, p.pin) == GPIO_PIN_RESET);
    ds_delay_us(410);   /* 等存在脉冲结束 */
    return present;
}

static void ds_write_bit(DsPin p, uint8_t bit)
{
    if (bit) {
        HAL_GPIO_WritePin(p.port, p.pin, GPIO_PIN_RESET);
        ds_delay_us(2);
        HAL_GPIO_WritePin(p.port, p.pin, GPIO_PIN_SET);   /* 写 1：短低后释放 */
        ds_delay_us(60);
    } else {
        HAL_GPIO_WritePin(p.port, p.pin, GPIO_PIN_RESET);
        ds_delay_us(65);                                   /* 写 0：长低 */
        HAL_GPIO_WritePin(p.port, p.pin, GPIO_PIN_SET);
        ds_delay_us(5);
    }
}

static uint8_t ds_read_bit(DsPin p)
{
    HAL_GPIO_WritePin(p.port, p.pin, GPIO_PIN_RESET);
    ds_delay_us(2);
    HAL_GPIO_WritePin(p.port, p.pin, GPIO_PIN_SET);   /* 释放，等 DS18B20 输出 */
    ds_delay_us(8);
    uint8_t bit = HAL_GPIO_ReadPin(p.port, p.pin);
    ds_delay_us(50);                                   /* 完成位周期 */
    return bit;
}

static void ds_write_byte(DsPin p, uint8_t byte)
{
    for (int i = 0; i < 8; i++) {
        ds_write_bit(p, byte & 0x01);
        byte >>= 1;
    }
}

static uint8_t ds_read_byte(DsPin p)
{
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++) {
        byte |= (uint8_t)(ds_read_bit(p) << i);
    }
    return byte;
}

/* 读一次温度（需先已发转换命令并等待完成） */
static int ds_read_temp(DsPin p, float *temp)
{
    if (!ds_reset(p)) {
        return -1;
    }
    ds_write_byte(p, 0xCC);   /* 跳过 ROM */
    ds_write_byte(p, 0xBE);   /* 读暂存器 */
    uint8_t lo = ds_read_byte(p);
    uint8_t hi = ds_read_byte(p);
    int16_t raw = (int16_t)((hi << 8) | lo);
    if (raw == 0x0550) {       /* 85.0℃ = 上电默认值，视为未转换/未接 */
        return -1;
    }
    *temp = (float)raw / 16.0f;
    return 0;
}

/* 对 4 个并发发转换命令 */
static void ds_start_convert(void)
{
    for (int i = 0; i < DS18B20_NUM; i++) {
        if (ds_reset(s_pins[i])) {
            ds_write_byte(s_pins[i], 0xCC);   /* 跳过 ROM */
            ds_write_byte(s_pins[i], 0x44);   /* 启动温度转换 */
        }
    }
}

void DS18B20_Init(void)
{
    /* 使能 DWT 计数器（微秒延时） */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Mode  = GPIO_MODE_OUTPUT_OD;   /* 开漏：靠外部 4.7k 上拉拉高 */
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    for (int i = 0; i < DS18B20_NUM; i++) {
        gpio.Pin = s_pins[i].pin;
        HAL_GPIO_Init(s_pins[i].port, &gpio);
        HAL_GPIO_WritePin(s_pins[i].port, s_pins[i].pin, GPIO_PIN_SET);
    }

    s_state = DS_START;
}

void DS18B20_Tick(void)
{
    switch (s_state) {
    case DS_START:
        ds_start_convert();
        s_deadline = HAL_GetTick() + DS_CONVERT_MS;
        s_state = DS_WAIT;
        break;

    case DS_WAIT:
        if ((int32_t)(HAL_GetTick() - s_deadline) >= 0) {
            s_state = DS_READ;
        }
        break;

    case DS_READ:
        for (int i = 0; i < DS18B20_NUM; i++) {
            float t;
            s_valid[i] = (ds_read_temp(s_pins[i], &t) == 0);
            if (s_valid[i]) { s_temp[i] = t; }
        }
        s_state = DS_START;   /* 进入下一轮转换 */
        break;
    }
}

int DS18B20_GetTemp(int idx, float *temp)
{
    if (idx < 0 || idx >= DS18B20_NUM || !s_valid[idx]) {
        return -1;
    }
    *temp = s_temp[idx];
    return 0;
}
