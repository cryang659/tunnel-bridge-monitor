# -*- coding: utf-8 -*-
"""MQTT 订阅与数据处理"""
import json
from datetime import datetime

import paho.mqtt.client as mqtt

from config import (
    MQTT_BROKER, MQTT_PORT, MQTT_KEEPALIVE,
    MQTT_USERNAME, MQTT_PASSWORD,
    TOPIC_DATA, TOPIC_HEARTBEAT, TOPIC_ALARM, TOPIC_THRESHOLD,
)
from database import (
    insert_data, insert_alarm, upsert_device, get_thresholds, set_thresholds,
)


def _check_threshold(type_, value):
    """返回 (status, level)：越限时 status=warning/alarm，否则 normal"""
    th = get_thresholds().get(type_)
    if th is None:
        return "normal", None
    if value >= th:
        return "alarm", "alarm"
    if value >= th * 0.8:
        return "warning", "warning"
    return "normal", None


def _process_item(device_id, item, timestamp):
    type_ = item.get("type")
    value = item.get("value")
    unit = item.get("unit", "")
    status, level = _check_threshold(type_, value)
    insert_data(device_id, type_, value, unit, status, timestamp)
    if level:
        th = get_thresholds().get(type_)
        insert_alarm(device_id, type_, value, th, level, timestamp)
        print(f"[告警] {device_id} {type_}={value}{unit} 越限（阈值 {th}）")
    else:
        print(f"[数据] {device_id} {type_}={value}{unit} [{status}]")


def _on_connect(client, userdata, flags, reason_code, properties):
    if not reason_code.is_failure:
        print(f"[MQTT] 已连接 broker {MQTT_BROKER}:{MQTT_PORT}")
        client.subscribe(TOPIC_DATA)
        client.subscribe(TOPIC_HEARTBEAT)
        client.subscribe(TOPIC_ALARM)
        client.subscribe(TOPIC_THRESHOLD)
        print(f"[MQTT] 已订阅 {TOPIC_DATA} / {TOPIC_HEARTBEAT} / {TOPIC_ALARM} / {TOPIC_THRESHOLD}")
    else:
        print(f"[MQTT] 连接失败 reason_code={reason_code}")


def _on_message(client, userdata, msg):
    try:
        payload = json.loads(msg.payload.decode("utf-8"))
    except Exception as e:
        print(f"[MQTT] 报文解析失败 topic={msg.topic}: {e}")
        return

    device_id = payload.get("device_id", "unknown")
    # 设备无 RTC，上报的是模拟时间戳（2026-01-01+上电时长），改用后端真实接收时间
    timestamp = datetime.now().isoformat(timespec="seconds")
    lat = payload.get("lat")
    lng = payload.get("lng")

    # 经纬度有效（非 0 非 None）才更新设备位置，避免未定位时覆盖旧坐标
    has_fix = bool(lat and lng)

    if msg.topic.endswith("/data"):
        if has_fix:
            upsert_device(device_id, timestamp, lat, lng)
        else:
            upsert_device(device_id, timestamp)
        if "data" in payload:          # 批量上报
            for item in payload["data"]:
                _process_item(device_id, item, timestamp)
        else:                          # 单条上报
            _process_item(device_id, payload, timestamp)
    elif msg.topic.endswith("/heartbeat"):
        upsert_device(device_id, timestamp)
        # 回发当前时间，供板上 LCD 对时（板订阅 shm/{dev}/time）
        ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        client.publish(f"shm/{device_id}/time", '{"ts":"%s"}' % ts)
        print(f"[心跳] {device_id} 在线，已回发时间 {ts}")
    elif msg.topic.endswith("/alarm"):
        insert_alarm(
            device_id, payload.get("type"), payload.get("value"),
            payload.get("threshold"), payload.get("level"), timestamp,
        )
        print(f"[告警] {device_id} 上报告警")
    elif msg.topic.endswith("/threshold"):
        # 板上摇杆改阈值后同步：更新服务器判断阈值（写内存 + DB）
        t = payload.get("type")
        v = payload.get("value")
        if t and v is not None:
            try:
                set_thresholds({str(t): float(v)})
                print(f"[阈值] {device_id} {t} -> {v} (服务器已更新)")
            except Exception as e:
                print(f"[阈值] 更新失败 {t}: {e}")
        else:
            print(f"[阈值] 报文缺 type/value: {payload}")


def start_mqtt():
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    client.on_connect = _on_connect
    client.on_message = _on_message
    client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)
    client.connect(MQTT_BROKER, MQTT_PORT, MQTT_KEEPALIVE)
    client.loop_forever()
