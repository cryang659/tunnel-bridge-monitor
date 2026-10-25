/**
 * @file    lte_ct02.h
 * @brief   DX-CT02-A 4G+GNSS 模块驱动接口（AT 指令 + TCP 非透传）
 *
 * 说明：模块串口接 USART2（PA2=TX, PA3=RX），默认 115200/8/n/1。
 *       上电后自动注册网络（提示 "Online Reg OK!"），本驱动等待注册完成后
 *       建立 TCP 连接，供上层 MQTT 透传收发。MQTT 报文仍由 mqtt_client.c 构造。
 */
#ifndef __LTE_CT02_H
#define __LTE_CT02_H

#include <stdint.h>

/* EMQX Cloud 专有版地址（域名，贵阳）与 MQTT 端口 */
#define SERVER_IP     "your-broker.example.com"
#define SERVER_PORT   1883

/* APN：国内三大运营商物联网卡通常免配，留空即可；特殊卡按运营商填 */
#define LTE_APN       ""

/**
 * @brief 初始化：等待模块就绪 + 网络注册成功
 * @return 0 成功（已注册网络）
 */
int LTE_Init(void);

/**
 * @brief 建立到 MQTT 服务器的 TCP 连接（先域名解析再连接）
 * @return 0 成功
 */
int LTE_ConnectServer(void);

/**
 * @brief 发送 TCP 数据（AT+CIPSEND 非透传模式）
 * @return 0 成功
 */
int LTE_Send(const uint8_t *buf, uint16_t len);

/**
 * @brief 接收 TCP 数据（阻塞超时）
 * @return 实际接收字节数，0 表示超时
 */
int LTE_Recv(uint8_t *buf, uint16_t max_len, uint32_t timeout);

#endif /* __LTE_CT02_H */
