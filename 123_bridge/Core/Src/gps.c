/**
 * @file    gps.c
 * @brief   GNSS 定位模块实现（AT 指令）
 *
 * 指令（见 docs/DX-CT02-A应用指导.pdf）：
 *   AT+MGPSC=1  开启 GPS → +GPS: start up success.
 *   AT+GPSST    查询定位 → +GPSST: <fix>,<cn>,<lon>,<alt>,<lat>; <sat...>
 *      fix=1 已定位；lon=经度（前）、lat=纬度（后）、alt=高度
 */
#include "gps.h"
#include "main.h"
#include <string.h>
#include <stdio.h>

extern UART_HandleTypeDef huart2;      /* USART2 与 4G 模块共用 */
#define GPS_UART huart2

/* 读串口数据 */
static int uart_read(uint8_t *buf, uint16_t max_len, uint32_t timeout_ms)
{
    uint16_t len = 0;
    uint32_t start = HAL_GetTick();
    while (HAL_GetTick() - start < timeout_ms) {
        uint8_t ch;
        if (HAL_UART_Receive(&GPS_UART, &ch, 1, 10) == HAL_OK) {
            if (len < max_len - 1) {
                buf[len++] = ch;
            }
        }
    }
    buf[len] = '\0';
    return len;
}

/* 发 AT 命令并读取响应 */
static int at_cmd(const char *cmd, uint8_t *resp, uint16_t resp_len, uint32_t timeout_ms)
{
    HAL_UART_Transmit(&GPS_UART, (uint8_t *)cmd, strlen(cmd), 1000);
    HAL_UART_Transmit(&GPS_UART, (uint8_t *)"\r\n", 2, 1000);
    return uart_read(resp, resp_len, timeout_ms);
}

int GPS_Init(void)
{
    uint8_t buf[128] = {0};
    at_cmd("AT+MGPSC=1", buf, sizeof(buf) - 1, 3000);       /* 开 GPS 定位 */
    at_cmd("AT+MGPSGET=ALL,0", buf, sizeof(buf) - 1, 3000); /* 关闭 NMEA 主动输出，避免冲 MQTT 串口 */
    if (strstr((char *)buf, "OK") != NULL || strstr((char *)buf, "success") != NULL) {
        return 0;
    }
    return -1;
}

int GPS_GetFix(GPSData *out)
{
    uint8_t buf[256] = {0};
    at_cmd("AT+GPSST", buf, sizeof(buf) - 1, 3000);

    char *p = strstr((char *)buf, "+GPSST:");
    if (p == NULL) {
        return -1;
    }

    int fix = 0, cn = 0;
    float lon = 0.0f, alt = 0.0f, lat = 0.0f;
    /* 响应形如 "+GPSST: 1, 1, 113.83, 23.33, 22.61; ..." */
    if (sscanf(p, "+GPSST:%d,%d,%f,%f,%f", &fix, &cn, &lon, &alt, &lat) != 5) {
        return -1;
    }

    out->fix = fix;
    out->lon = lon;
    out->lat = lat;
    out->alt = alt;
    return 0;
}
