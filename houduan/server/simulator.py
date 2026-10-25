# -*- coding: utf-8 -*-
"""模拟监测节点：模拟隧道/桥梁传感器数据上报，用于无真实硬件时跑通全链路"""
import json
import random
import time
from datetime import datetime

import paho.mqtt.client as mqtt

from config import MQTT_BROKER, MQTT_PORT, MQTT_USERNAME, MQTT_PASSWORD

DEVICE_ID = "NODE_001"

# 各传感器类型：(类型, 基准值, 波动幅度, 单位)
SENSORS = [
    ("vibration", 0.02, 0.05, "g"),
    ("stress", 120.0, 80.0, "με"),
    ("temperature", 26.0, 8.0, "℃"),
    ("humidity", 65.0, 15.0, "%"),
    ("seepage_1", 40.0, 20.0, "%"),
    ("seepage_2", 40.0, 20.0, "%"),
    ("displacement", 1.0, 2.0, "mm"),
    ("load", 15.0, 20.0, "t"),
    ("deck_temp_1", 28.0, 5.0, "℃"),
    ("deck_temp_2", 28.0, 5.0, "℃"),
    ("pier_temp_1", 27.0, 5.0, "℃"),
    ("pier_temp_2", 27.0, 5.0, "℃"),
    ("convergence", 30.0, 10.0, "mm"),
    # 2026-09-17 隧道板新增：温湿度/水位各拆两路 + 穹顶压力
    ("temperature_1", 25.0, 6.0, "℃"),
    ("temperature_2", 24.0, 6.0, "℃"),
    ("humidity_1", 60.0, 15.0, "%"),
    ("humidity_2", 62.0, 15.0, "%"),
    ("water_level_1", 0.3, 0.2, "m"),
    ("water_level_2", 0.3, 0.2, "m"),
    ("dome_pressure", 20.0, 15.0, "kPa"),
    # 2026-09-26 隧道板新增：K210 视觉（裂缝为实际实现，车辆/人员预留）
    ("crack", 0.35, 0.30, ""),
    ("vehicle", 12.0, 8.0, "辆"),
    ("person", 3.0, 2.0, "人"),
    # 2026-09-25 桥板新增：RCWL-1670 超声波测距
    ("position", 60.0, 30.0, "cm"),
]


def gen_payload():
    now = datetime.now().isoformat(timespec="seconds")
    data = []
    for type_, base, amp, unit in SENSORS:
        value = round(max(0.0, base + random.uniform(-amp, amp)), 3)
        data.append({"type": type_, "value": value, "unit": unit})
    # 模拟固定经纬度（深圳），与真实固件上报格式一致
    return {"device_id": DEVICE_ID, "timestamp": now,
            "lat": 22.61, "lng": 113.83, "data": data}


def main():
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)
    client.connect(MQTT_BROKER, MQTT_PORT, 60)
    client.loop_start()
    print(f"[模拟节点] {DEVICE_ID} 开始上报，broker={MQTT_BROKER}:{MQTT_PORT}")
    try:
        while True:
            payload = gen_payload()
            topic = f"shm/{DEVICE_ID}/data"
            client.publish(topic, json.dumps(payload, ensure_ascii=False))
            print(f"[上报] {payload['timestamp']} 批量 {len(payload['data'])} 项")
            time.sleep(5)  # 每 5 秒上报一次，便于观察
    except KeyboardInterrupt:
        print("\n[模拟节点] 已停止")
        client.loop_stop()


if __name__ == "__main__":
    main()
