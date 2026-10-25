/**
 * @file    wifi_esp8266.c
 * @brief   ESP8266 WiFi 模块驱动实现（AT 指令 + TCP 透传）
 * @note    ESP8266 接 USART2（PA2=TX, PA3=RX，CubeMX 配置）
 */
#include "wifi_esp8266.h"
#include "main.h"
#include <string.h>
#include <stdio.h>

extern UART_HandleTypeDef huart2;   /* CubeMX 生成 */
#define ESP_UART huart2

/* 发送 AT 指令并等待期望响应 */
static int at_command(const char *cmd, const char *expect, uint32_t timeout_ms)
{
    uint8_t buf[256] = {0};
    uint16_t len = 0;

    /* 发送命令 + 回车换行 */
    HAL_UART_Transmit(&ESP_UART, (uint8_t *)cmd, strlen(cmd), 1000);
    HAL_UART_Transmit(&ESP_UART, (uint8_t *)"\r\n", 2, 1000);

    /* 读取响应 */
    uint32_t start = HAL_GetTick();
    while (HAL_GetTick() - start < timeout_ms) {
        uint8_t ch;
        if (HAL_UART_Receive(&ESP_UART, &ch, 1, 10) == HAL_OK) {
            if (len < sizeof(buf) - 1) {
                buf[len++] = ch;
            }
        }
    }
    buf[len] = '\0';
    if (expect == NULL) {
        return 0;
    }
    return (strstr((char *)buf, expect) != NULL) ? 0 : -1;
}

int WiFi_Init(void)
{
    /* 复位模块 */
    at_command("AT+RST", "ready", 3000);
    HAL_Delay(1000);

    /* 关闭回显，设置 Station 模式 */
    at_command("ATE0", "OK", 1000);
    if (at_command("AT+CWMODE=1", "OK", 1000) != 0) {
        return -1;
    }

    /* 连接 WiFi：AT+CWJAP="ssid","pwd" */
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", WIFI_SSID, WIFI_PASSWORD);
    if (at_command(cmd, "OK", 8000) != 0) {
        return -1;
    }
    return 0;
}

int WiFi_ConnectServer(void)
{
    char cmd[128];
    /* 单连接模式 */
    at_command("AT+CIPMUX=0", "OK", 1000);
    /* 建立 TCP 连接 */
    snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",%d", SERVER_IP, SERVER_PORT);
    if (at_command(cmd, "OK", 5000) != 0) {
        return -1;
    }
    return 0;
}

int WiFi_Send(const uint8_t *buf, uint16_t len)
{
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%d", len);
    if (at_command(cmd, ">", 2000) != 0) {
        return -1;
    }
    /* 发送实际数据 */
    if (HAL_UART_Transmit(&ESP_UART, (uint8_t *)buf, len, 3000) != HAL_OK) {
        return -1;
    }
    return 0;
}

int WiFi_Recv(uint8_t *buf, uint16_t max_len, uint32_t timeout)
{
    uint16_t len = 0;
    uint32_t start = HAL_GetTick();
    while (HAL_GetTick() - start < timeout) {
        uint8_t ch;
        if (HAL_UART_Receive(&ESP_UART, &ch, 1, 10) == HAL_OK) {
            if (len < max_len - 1) {
                buf[len++] = ch;
            }
        }
    }
    buf[len] = '\0';
    return len;
}
