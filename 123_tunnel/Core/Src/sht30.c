/**
 * @file    sht30.c
 * @brief   SHT30 温湿度驱动实现
 *
 * 命令 0x24 0x00：高重复性测量（时钟拉伸禁用），发命令后延时 20ms 再读 6 字节。
 * 数据格式：温度高字节 / 温度低字节 / 温度 CRC8 / 湿度高 / 湿度低 / 湿度 CRC8。
 */
#include "sht30.h"
#include "soft_i2c.h"
#include "main.h"

/* CRC-8（多项式 0x31，初值 0xFF） */
static uint8_t sht30_crc8(const uint8_t *data, int len)
{
    uint8_t crc = 0xFF;
    for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

void SHT30_Init(void)
{
    /* SHT30 无需初始化寄存器，上电即可用 */
}

/* 读数缓存：同一片在 1 秒内只真正测量一次。
 * 温度、湿度是本工程里两个独立的上报项，会各自调用一次 SHT30_Read，
 * 没有缓存时同一片每个采集周期要被测量两次（2×20ms I2C 阻塞）。
 * 本板有两片，缓存把每周期的 4 次读数降到 2 次。 */
#define SHT30_CACHE_MS   1000U

typedef struct {
    float    temp;
    float    humi;
    uint32_t tick;
    uint8_t  valid;
} Sht30Cache;

static Sht30Cache s_cache[2];   /* [0]=ADDR_1, [1]=ADDR_2 */

static Sht30Cache *cache_of(uint8_t addr)
{
    return &s_cache[addr == SHT30_ADDR_2 ? 1 : 0];
}

int SHT30_Read(uint8_t addr, float *temp, float *humi)
{
    Sht30Cache *c = cache_of(addr);
    uint32_t now = HAL_GetTick();

    if (c->valid && (now - c->tick) < SHT30_CACHE_MS) {
        *temp = c->temp;
        *humi = c->humi;
        return 0;                    /* 命中缓存，不再占用 I2C */
    }

    uint8_t cmd[2] = {0x24, 0x00};   /* 高重复性，时钟拉伸禁用 */
    if (SoftI2C_Write(addr, cmd, 2) != 0) {
        return -1;
    }
    HAL_Delay(20);                   /* 等待测量完成 */

    uint8_t buf[6];
    if (SoftI2C_Read(addr, buf, 6) != 0) {
        return -1;
    }
    /* CRC 校验 */
    if (sht30_crc8(buf, 2) != buf[2] || sht30_crc8(&buf[3], 2) != buf[5]) {
        return -1;
    }

    uint16_t raw_t = (uint16_t)((buf[0] << 8) | buf[1]);
    uint16_t raw_h = (uint16_t)((buf[3] << 8) | buf[4]);
    *temp = -45.0f + 175.0f * raw_t / 65535.0f;
    *humi = 100.0f * raw_h / 65535.0f;

    c->temp  = *temp;
    c->humi  = *humi;
    c->tick  = HAL_GetTick();        /* 从测量完成起算缓存有效期 */
    c->valid = 1;
    return 0;
}
