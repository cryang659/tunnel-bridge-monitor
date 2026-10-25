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
 * @brief 启动非阻塞的 4G 初始化 + TCP 连接状态机（立即返回）
 *        流程：上电等待 → AT 就绪 → 关回显/报头 → 网络注册 → 域名解析 → TCP。
 */
void LTE_TaskInit(void);

/**
 * @brief 主循环轮询（非阻塞，每次调用执行一小步）
 * @return -1 进行中；0 TCP 已就绪（可建 MQTT）；1 失败（可稍后 LTE_TaskInit 重试）
 */
int LTE_TaskPoll(void);

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

/**
 * @brief 返回当前 4G 状态机状态名（ASCII，供 LCD 调试显示）
 *        如 "POWERUP"/"AT"/"CEREG"/"DNS"/"TCP"/"DONE"/"FAIL"
 */
const char *LTE_GetStateStr(void);

#endif /* __LTE_CT02_H */
