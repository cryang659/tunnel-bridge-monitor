/**
 * @file    mqtt_client.h
 * @brief   轻量 MQTT 客户端（基于 TCP 透传，实现 CONNECT/PUBLISH/SUBSCRIBE）
 * @note    MQTT 3.1.1，QoS 0/1，报文手动构造，无需第三方库
 */
#ifndef __MQTT_CLIENT_H
#define __MQTT_CLIENT_H

#include <stdint.h>

/* 上报 Topic 模板：shm/{device_id}/data 等，{device_id} 替换为实际设备号 */
#define TOPIC_DATA      "shm/%s/data"
#define TOPIC_HEARTBEAT "shm/%s/heartbeat"
#define TOPIC_ALARM     "shm/%s/alarm"
#define TOPIC_THRESHOLD "shm/%s/threshold"   /* 板上阈值变更上报，服务器据此更新判断阈值 */

/* MQTT 认证凭据（EMQX Cloud 用户名密码认证，需与后端 config.py 一致） */
#define MQTT_USERNAME    "YOUR_MQTT_USERNAME"
#define MQTT_PASSWORD    "YOUR_MQTT_PASSWORD"

/* 客户端配置 */
typedef struct {
    char client_id[32];   /* 客户端 ID（即 device_id） */
    char username[32];    /* 用户名（可为空）            */
    char password[32];    /* 密码（可为空）              */
    uint16_t keepalive;   /* 心跳周期，秒                */
} MQTT_Client;

/**
 * @brief 建立 MQTT 连接（发送 CONNECT 报文并等待 CONNACK）
 * @return 0 成功
 */
int MQTT_Connect(MQTT_Client *cli);

/**
 * @brief 发布消息（PUBLISH）
 * @param topic    主题字符串
 * @param payload  载荷
 * @param len      载荷长度
 * @param qos      QoS 等级（0 或 1）
 * @return 0 成功
 */
int MQTT_Publish(const char *topic, const char *payload, uint16_t len, uint8_t qos);

/**
 * @brief 订阅主题（SUBSCRIBE）
 * @return 0 成功
 */
int MQTT_Subscribe(const char *topic, uint8_t qos);

#endif /* __MQTT_CLIENT_H */
