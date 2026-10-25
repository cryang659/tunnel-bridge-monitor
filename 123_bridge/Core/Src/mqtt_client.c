/**
 * @file    mqtt_client.c
 * @brief   轻量 MQTT 3.1.1 客户端实现（报文手动构造，基于 TCP 透传）
 *
 * 实现 CONNECT / PUBLISH / SUBSCRIBE 报文，QoS 0/1。
 * 报文格式参照 MQTT 3.1.1 规范，与服务器（amqtt/mosquitto）互通。
 */
#include "mqtt_client.h"
#include "lte_ct02.h"
#include <string.h>
#include <stdio.h>

/* ============ 变长字节（剩余长度）编码 ============ */
static int encode_remaining_length(uint32_t len, uint8_t *buf)
{
    int n = 0;
    do {
        uint8_t byte = len % 128;
        len /= 128;
        if (len > 0) {
            byte |= 0x80;
        }
        buf[n++] = byte;
    } while (len > 0);
    return n;
}

/* ============ CONNACK 解析 ============ */
/* CONNACK 报文：固定头 0x20，剩余长度 0x02，可变头 [0x00, return_code]。
 * return_code: 0x00=已接受 0x01=协议版本不支持 0x02=client_id 非法
 *              0x03=服务器不可用 0x04=用户名或密码错误 0x05=未授权。
 * 4G 模块已用 AT+REHEAD=0 关闭数据报头，宽松匹配以容忍残留。 */
static int mqtt_parse_connack(const uint8_t *buf, int len)
{
    for (int i = 0; i + 3 < len; i++) {
        if (buf[i] == 0x20 && buf[i + 1] == 0x02) {
            return (buf[i + 3] == 0x00) ? 0 : -1;
        }
    }
    return -1;   /* 未收到有效 CONNACK */
}

/* ============ CONNECT 报文 ============ */
int MQTT_Connect(MQTT_Client *cli)
{
    uint8_t packet[256] = {0};
    int pos = 0;

    /* 连接标志：bit1 clean session=1；用户名/密码非空才置对应标志位 */
    uint8_t connect_flags = 0x02;
    uint16_t u_len = strlen(cli->username);
    uint16_t p_len = strlen(cli->password);
    if (u_len > 0) connect_flags |= 0x80;   /* bit7: 有 username */
    if (p_len > 0) connect_flags |= 0x40;   /* bit6: 有 password */

    /* 可变头：协议名 "MQTT" */
    packet[pos++] = 0x00; packet[pos++] = 0x04;
    packet[pos++] = 'M'; packet[pos++] = 'Q'; packet[pos++] = 'T'; packet[pos++] = 'T';
    /* 协议级 4（MQTT 3.1.1） */
    packet[pos++] = 0x04;
    packet[pos++] = connect_flags;
    /* keepalive */
    packet[pos++] = (uint8_t)(cli->keepalive >> 8);
    packet[pos++] = (uint8_t)(cli->keepalive & 0xFF);

    /* 载荷：client_id */
    uint16_t id_len = strlen(cli->client_id);
    packet[pos++] = (uint8_t)(id_len >> 8);
    packet[pos++] = (uint8_t)(id_len & 0xFF);
    memcpy(&packet[pos], cli->client_id, id_len); pos += id_len;
    /* 载荷：username（标志位置位才发送） */
    if (u_len > 0) {
        packet[pos++] = (uint8_t)(u_len >> 8);
        packet[pos++] = (uint8_t)(u_len & 0xFF);
        memcpy(&packet[pos], cli->username, u_len); pos += u_len;
    }
    /* 载荷：password（标志位置位才发送） */
    if (p_len > 0) {
        packet[pos++] = (uint8_t)(p_len >> 8);
        packet[pos++] = (uint8_t)(p_len & 0xFF);
        memcpy(&packet[pos], cli->password, p_len); pos += p_len;
    }

    /* 固定头 + 剩余长度 + 报文 */
    uint8_t out[256];
    int o = 0;
    out[o++] = 0x10;                            /* CONNECT 报文类型 */
    o += encode_remaining_length(pos, &out[o]);
    memcpy(&out[o], packet, pos); o += pos;

    if (LTE_Send(out, o) != 0) {
        return -1;
    }

    /* 等待并解析 CONNACK，校验返回码 */
    uint8_t resp[64];
    int len = LTE_Recv(resp, sizeof(resp), 3000);
    return mqtt_parse_connack(resp, len);
}

/* ============ PUBLISH 报文 ============ */
int MQTT_Publish(const char *topic, const char *payload, uint16_t len, uint8_t qos)
{
    uint8_t packet[800];   /* JSON 载荷最大 768 字节，需足够大防栈溢出 */
    int pos = 0;

    /* 可变头：topic */
    uint16_t t_len = strlen(topic);
    packet[pos++] = (uint8_t)(t_len >> 8);
    packet[pos++] = (uint8_t)(t_len & 0xFF);
    memcpy(&packet[pos], topic, t_len); pos += t_len;
    /* 报文标识（仅 QoS > 0） */
    if (qos > 0) {
        packet[pos++] = 0x00;
        packet[pos++] = 0x01;
    }
    /* 载荷 */
    memcpy(&packet[pos], payload, len); pos += len;

    /* 固定头：PUBLISH | DUP/QoS/RETAIN */
    uint8_t out[820];
    int o = 0;
    out[o++] = 0x30 | (qos << 1);               /* PUBLISH 类型 + QoS 标志 */
    o += encode_remaining_length(pos, &out[o]);
    memcpy(&out[o], packet, pos); o += pos;

    return LTE_Send(out, o);
}

/* ============ SUBSCRIBE 报文 ============ */
int MQTT_Subscribe(const char *topic, uint8_t qos)
{
    uint8_t packet[256];
    int pos = 0;

    /* 报文标识 */
    packet[pos++] = 0x00; packet[pos++] = 0x01;
    /* topic filter */
    uint16_t t_len = strlen(topic);
    packet[pos++] = (uint8_t)(t_len >> 8);
    packet[pos++] = (uint8_t)(t_len & 0xFF);
    memcpy(&packet[pos], topic, t_len); pos += t_len;
    /* 请求 QoS */
    packet[pos++] = qos;

    uint8_t out[256];
    int o = 0;
    out[o++] = 0x82;                            /* SUBSCRIBE 报文类型 */
    o += encode_remaining_length(pos, &out[o]);
    memcpy(&out[o], packet, pos); o += pos;

    return LTE_Send(out, o);
}
