/**
 * @file    data_report.c
 * @brief   数据上报实现：手动拼接 JSON（嵌入式环境不引入 JSON 库）
 */
#include "data_report.h"
#include "mqtt_client.h"
#include <stdio.h>
#include <string.h>
#include "main.h"

/* 生成时间戳（暂无 RTC，用上电运行时长模拟，保留 ISO8601 格式外壳） */
static void get_timestamp(char *buf, int max_len)
{
    uint32_t sec = HAL_GetTick() / 1000;
    uint32_t h = (sec / 3600) % 24;
    uint32_t m = (sec / 60) % 60;
    uint32_t s = sec % 60;
    snprintf(buf, max_len, "2026-01-01T%02u:%02u:%02u",
             (unsigned)h, (unsigned)m, (unsigned)s);
}

int DataReport_BuildJSON(const SensorData *data, int count, const GPSData *gps, char *out, int max_len)
{
    char ts[32];
    get_timestamp(ts, sizeof(ts));

    int pos = snprintf(out, max_len,
                       "{\"device_id\":\"%s\",\"timestamp\":\"%s\",\"lat\":%.6f,\"lng\":%.6f,\"data\":[",
                       DEVICE_ID, ts,
                       gps ? (double)gps->lat : 0.0,
                       gps ? (double)gps->lon : 0.0);
    for (int i = 0; i < count; i++) {
        pos += snprintf(out + pos, max_len - pos,
                        "%s{\"type\":\"%s\",\"value\":%.3f,\"unit\":\"%s\"}",
                        i > 0 ? "," : "",
                        SENSOR_TYPE_STR[data[i].type],
                        data[i].value, data[i].unit);
        if (pos >= max_len) {
            return -1;                 /* 缓冲不足 */
        }
    }
    snprintf(out + pos, max_len - pos, "]}");
    return 0;
}

int DataReport_Publish(const SensorData *data, int count, const GPSData *gps)
{
    char json[768];
    if (DataReport_BuildJSON(data, count, gps, json, sizeof(json)) != 0) {
        return -1;
    }
    char topic[64];
    snprintf(topic, sizeof(topic), TOPIC_DATA, DEVICE_ID);
    return MQTT_Publish(topic, json, (uint16_t)strlen(json), 0);
}

int DataReport_Heartbeat(void)
{
    char ts[32];
    get_timestamp(ts, sizeof(ts));
    char json[128];
    snprintf(json, sizeof(json),
             "{\"device_id\":\"%s\",\"timestamp\":\"%s\",\"type\":\"heartbeat\",\"status\":\"online\"}",
             DEVICE_ID, ts);
    char topic[64];
    snprintf(topic, sizeof(topic), TOPIC_HEARTBEAT, DEVICE_ID);
    return MQTT_Publish(topic, json, (uint16_t)strlen(json), 0);
}

int DataReport_PublishThreshold(const char *type_str, float value)
{
    char json[128];
    char topic[64];
    snprintf(json, sizeof(json),
             "{\"device_id\":\"%s\",\"type\":\"%s\",\"value\":%.2f}",
             DEVICE_ID, type_str, value);
    snprintf(topic, sizeof(topic), TOPIC_THRESHOLD, DEVICE_ID);
    return MQTT_Publish(topic, json, (uint16_t)strlen(json), 0);
}

/* ============ 服务器时间对时（LCD 主菜单底部显示真实时间） ============
 * 后端收到心跳后会向 shm/{dev}/time 下发 {"ts":"YYYY-MM-DD HH:MM:SS"}，
 * 固件读透传数据、解析 ts 校准当日秒基准，之后靠晶振(HAL_GetTick)续走。
 * 未对时前从 00:00:00 计时兜底。 */
static uint32_t s_base_tick = 0;   /* 收到对时那一刻的 HAL_GetTick */
static uint32_t s_base_sec = 0;    /* 对时得到的当日秒 0~86399 */
static uint16_t s_year = 0, s_mon = 0, s_day = 0;   /* 对时得到的日期 */
static uint8_t  s_synced = 0;      /* 是否已收到服务器对时 */

/* 复位时间基准（启动时调用） */
void TimeSync_Reset(void)
{
    s_base_tick = HAL_GetTick();
    s_base_sec = 0;
    s_synced = 0;
}

/* 从读到的服务器数据里找 "ts":"YYYY-MM-DD HH:MM:SS" 并校准时钟 */
void TimeSync_Parse(const uint8_t *buf, uint16_t len)
{
    static const char key[] = "\"ts\":\"";
    for (uint16_t st = 0; st + (uint16_t)(sizeof(key) - 1) <= len; st++) {
        if (memcmp(&buf[st], key, sizeof(key) - 1) == 0) {
            int p = (int)st + (int)(sizeof(key) - 1);
            if (p + 19 > (int)len) {
                return;
            }
            unsigned y, mo, dd, h, m, s;
            /* 时间串 "YYYY-MM-DD HH:MM:SS"，直接从开头解析 */
            if (sscanf((const char *)&buf[p], "%u-%u-%u %u:%u:%u",
                       &y, &mo, &dd, &h, &m, &s) == 6 &&
                y >= 2000 && y <= 2100 && mo >= 1 && mo <= 12 &&
                dd >= 1 && dd <= 31 && h < 24 && m < 60 && s < 60) {
                s_year = (uint16_t)y;
                s_mon  = (uint16_t)mo;
                s_day  = (uint16_t)dd;
                s_synced = 1;
                s_base_sec = h * 3600u + m * 60u + s;
                s_base_tick = HAL_GetTick();
            }
            return;
        }
    }
}

/* 取当前 "HH:MM:SS"（out 至少 9 字节） */
void TimeSync_GetHMS(char *out)
{
    uint32_t now_sec = s_base_sec + (HAL_GetTick() - s_base_tick) / 1000;
    now_sec %= 86400;
    snprintf(out, 9, "%02lu:%02lu:%02lu",
             (unsigned long)(now_sec / 3600),
             (unsigned long)((now_sec % 3600) / 60),
             (unsigned long)(now_sec % 60));
}

/* 取当前日期 "YYYY-MM-DD"（未对时输出空串），out 至少 12 字节 */
void TimeSync_GetDate(char *out)
{
    if (!s_synced) {
        out[0] = '\0';
        return;
    }
    snprintf(out, 12, "%04u-%02u-%02u",
             (unsigned)s_year, (unsigned)s_mon, (unsigned)s_day);
}
