/**
 * @file    soft_i2c.c
 * @brief   软件模拟 I2C（bit-bang）实现
 *
 * 时序：100kHz 量级，用 DWT 计数器做微秒延时。
 * SCL 推挽输出；SDA 发送时推挽输出、接收 ACK 时切上拉输入。
 */
#include "soft_i2c.h"
#include "main.h"

#define SCL_H()  HAL_GPIO_WritePin(SOFT_I2C_SCL_PORT, SOFT_I2C_SCL_PIN, GPIO_PIN_SET)
#define SCL_L()  HAL_GPIO_WritePin(SOFT_I2C_SCL_PORT, SOFT_I2C_SCL_PIN, GPIO_PIN_RESET)
#define SDA_H()  HAL_GPIO_WritePin(SOFT_I2C_SDA_PORT, SOFT_I2C_SDA_PIN, GPIO_PIN_SET)
#define SDA_L()  HAL_GPIO_WritePin(SOFT_I2C_SDA_PORT, SOFT_I2C_SDA_PIN, GPIO_PIN_RESET)
#define SDA_RD() HAL_GPIO_ReadPin(SOFT_I2C_SDA_PORT, SOFT_I2C_SDA_PIN)

/* 微秒延时（DWT 计数器） */
static void i2c_delay_us(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * (SystemCoreClock / 1000000U);
    while ((DWT->CYCCNT - start) < ticks) {
    }
}

/* 切换 SDA 方向 */
static void sda_out(void)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin  = SOFT_I2C_SDA_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(SOFT_I2C_SDA_PORT, &gpio);
}

static void sda_in(void)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin  = SOFT_I2C_SDA_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(SOFT_I2C_SDA_PORT, &gpio);
}

void SoftI2C_Init(void)
{
    /* 使能 DWT 计数器（微秒延时） */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin  = SOFT_I2C_SCL_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(SOFT_I2C_SCL_PORT, &gpio);
    sda_out();
    SCL_H();
    SDA_H();
}

/* ---- 总线原语 ---- */
static void i2c_start(void)
{
    SDA_H(); i2c_delay_us(5);
    SCL_H(); i2c_delay_us(5);
    SDA_L(); i2c_delay_us(5);
    SCL_L(); i2c_delay_us(5);
}

static void i2c_stop(void)
{
    SDA_L(); i2c_delay_us(5);
    SCL_H(); i2c_delay_us(5);
    SDA_H(); i2c_delay_us(5);
}

/* 发一字节，返回 0=收到 ACK，1=NACK */
static uint8_t i2c_write_byte(uint8_t byte)
{
    for (int i = 0; i < 8; i++) {
        if (byte & 0x80) SDA_H(); else SDA_L();
        i2c_delay_us(2);
        SCL_H(); i2c_delay_us(5);
        SCL_L(); i2c_delay_us(2);
        byte <<= 1;
    }
    sda_in();
    SCL_H(); i2c_delay_us(5);
    uint8_t ack = SDA_RD();
    SCL_L(); i2c_delay_us(2);
    sda_out();
    return ack;
}

/* 收一字节，ack=0 发 ACK、ack=1 发 NACK */
static uint8_t i2c_read_byte(uint8_t ack)
{
    uint8_t byte = 0;
    sda_in();
    for (int i = 0; i < 8; i++) {
        SCL_H(); i2c_delay_us(5);
        byte <<= 1;
        if (SDA_RD()) byte |= 1;
        SCL_L(); i2c_delay_us(2);
    }
    sda_out();
    if (ack) SDA_H(); else SDA_L();
    i2c_delay_us(2);
    SCL_H(); i2c_delay_us(5);
    SCL_L(); i2c_delay_us(2);
    SDA_H();
    return byte;
}

/* ---- 读写接口 ---- */
int SoftI2C_WriteReg(uint8_t dev_addr, uint8_t reg_addr, const uint8_t *data, uint16_t len)
{
    i2c_start();
    if (i2c_write_byte((uint8_t)(dev_addr << 1))) { i2c_stop(); return -1; }
    i2c_write_byte(reg_addr);
    for (uint16_t i = 0; i < len; i++) {
        i2c_write_byte(data[i]);
    }
    i2c_stop();
    return 0;
}

int SoftI2C_ReadReg(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint16_t len)
{
    i2c_start();
    if (i2c_write_byte((uint8_t)(dev_addr << 1))) { i2c_stop(); return -1; }
    i2c_write_byte(reg_addr);
    i2c_start();
    i2c_write_byte((uint8_t)((dev_addr << 1) | 1));
    for (uint16_t i = 0; i < len; i++) {
        data[i] = i2c_read_byte((uint8_t)(i < len - 1 ? 0 : 1));
    }
    i2c_stop();
    return 0;
}

int SoftI2C_WriteReg16(uint8_t dev_addr, uint16_t reg_addr, const uint8_t *data, uint16_t len)
{
    i2c_start();
    if (i2c_write_byte((uint8_t)(dev_addr << 1))) { i2c_stop(); return -1; }
    i2c_write_byte((uint8_t)(reg_addr >> 8));
    i2c_write_byte((uint8_t)(reg_addr & 0xFF));
    for (uint16_t i = 0; i < len; i++) {
        i2c_write_byte(data[i]);
    }
    i2c_stop();
    return 0;
}

int SoftI2C_ReadReg16(uint8_t dev_addr, uint16_t reg_addr, uint8_t *data, uint16_t len)
{
    i2c_start();
    if (i2c_write_byte((uint8_t)(dev_addr << 1))) { i2c_stop(); return -1; }
    i2c_write_byte((uint8_t)(reg_addr >> 8));
    i2c_write_byte((uint8_t)(reg_addr & 0xFF));
    i2c_start();
    i2c_write_byte((uint8_t)((dev_addr << 1) | 1));
    for (uint16_t i = 0; i < len; i++) {
        data[i] = i2c_read_byte((uint8_t)(i < len - 1 ? 0 : 1));
    }
    i2c_stop();
    return 0;
}

int SoftI2C_Write(uint8_t dev_addr, const uint8_t *data, uint16_t len)
{
    i2c_start();
    if (i2c_write_byte((uint8_t)(dev_addr << 1))) { i2c_stop(); return -1; }
    for (uint16_t i = 0; i < len; i++) {
        i2c_write_byte(data[i]);
    }
    i2c_stop();
    return 0;
}

int SoftI2C_Read(uint8_t dev_addr, uint8_t *data, uint16_t len)
{
    i2c_start();
    if (i2c_write_byte((uint8_t)((dev_addr << 1) | 1))) { i2c_stop(); return -1; }
    for (uint16_t i = 0; i < len; i++) {
        data[i] = i2c_read_byte((uint8_t)(i < len - 1 ? 0 : 1));
    }
    i2c_stop();
    return 0;
}
