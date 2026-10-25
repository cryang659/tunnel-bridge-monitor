/**
 * @file    wifi_esp8266.h
 * @brief   ESP8266 WiFi 模块驱动接口（AT 指令）
 * @note    ESP8266 通过 USART2 与 STM32 相连，透传模式下实现 TCP 数据收发
 */
#ifndef __WIFI_ESP8266_H
#define __WIFI_ESP8266_H

#include <stdint.h>

/* WiFi 与服务器参数（按实际网络环境修改） */
#define WIFI_SSID        "YOUR_WIFI_SSID"
#define WIFI_PASSWORD    "YOUR_WIFI_PASSWORD"
#define SERVER_IP        "your-broker.example.com"   /* EMQX Cloud 专有版（阿里云） */
#define SERVER_PORT      1883              /* MQTT 端口       */

/**
 * @brief 初始化 ESP8266：复位、设置 Station 模式、连接 WiFi
 * @return 0 成功
 */
int WiFi_Init(void);

/**
 * @brief 建立到服务器的 TCP 连接（透传模式）
 * @return 0 成功
 */
int WiFi_ConnectServer(void);

/**
 * @brief 发送数据（透传）
 * @return 0 成功
 */
int WiFi_Send(const uint8_t *buf, uint16_t len);

/**
 * @brief 接收数据（阻塞超时）
 * @param buf      接收缓冲
 * @param max_len  缓冲容量
 * @param timeout  超时 ms
 * @return 实际接收字节数，0 表示超时
 */
int WiFi_Recv(uint8_t *buf, uint16_t max_len, uint32_t timeout);

#endif /* __WIFI_ESP8266_H */
