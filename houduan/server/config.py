# -*- coding: utf-8 -*-
"""全局配置"""
import sys

# Windows 控制台默认 GBK，统一转 UTF-8 避免中文 print 报错
try:
    sys.stdout.reconfigure(encoding="utf-8")
except Exception:
    pass

# MQTT Broker 配置（EMQX Cloud 专有版，托管在阿里云）
MQTT_BROKER = "your-broker.example.com"
MQTT_PORT = 1883                      # MQTT over TCP（明文，设备端也用这个）
MQTT_KEEPALIVE = 60
MQTT_USERNAME = "YOUR_MQTT_USERNAME"
MQTT_PASSWORD = "YOUR_MQTT_PASSWORD"

# 同一实例的其它端口（备查，本工程后端未使用）
#   8883  MQTT over TLS/SSL
#   8083  WebSocket
#   8084  WebSocket over TLS/SSL
#   8443  REST API（https://your-broker.example.com:8443/api/v5）

# 订阅 Topic（+ 通配设备 ID）
TOPIC_DATA = "shm/+/data"
TOPIC_HEARTBEAT = "shm/+/heartbeat"
TOPIC_ALARM = "shm/+/alarm"
TOPIC_THRESHOLD = "shm/+/threshold"   # 板上摇杆改阈值后同步，更新服务器判断阈值

# SQLite 数据库文件
DB_PATH = "monitor.db"

# 告警阈值（默认值，与接口规范 v1.0 一致）
THRESHOLDS = {
    "vibration": 0.10,      # 桥面振动加速度 g（MPU-6050 #2）
    "cable_force": 500.0,   # 索力 kN（频率法反推，阈值待按实际索力标定）
    "stress": 300.0,        # 应力/应变 με（预留，待补购）
    "temperature": 50.0,    # 环境温度 ℃（SHT30，桥板 NODE_001）
    "humidity": 90.0,       # 环境湿度 %（SHT30，桥板 NODE_001）
    "temperature_1": 50.0,  # 温度1 ℃（SHT30#1 ADDR接GND，隧道板 0x44）
    "temperature_2": 50.0,  # 温度2 ℃（SHT30#2 ADDR接VCC，隧道板 0x45）
    "humidity_1": 90.0,     # 湿度1 %（SHT30#1，隧道板）
    "humidity_2": 90.0,     # 湿度2 %（SHT30#2，隧道板）
    "water_level_1": 0.8,   # 水位1 m（隧道板 PA4）
    "water_level_2": 0.8,   # 水位2 m（隧道板 PA5）
    "dome_pressure": 50.0,  # 穹顶压力 kPa（隧道板 PA6 薄膜压力，占位待标定）
    "seepage_1": 80.0,      # 渗水1 %（MH-RD 雨滴 PB0）
    "seepage_2": 80.0,      # 渗水2 %（MH-RD 雨滴 PB1）
    "displacement": 10.0,   # 位移/挠度 mm（VL53L1X 穹顶）
    "load": 40.0,           # 荷载/压力 t（压力模块，后续到）
    "deck_temp_1": 60.0,    # 桥面温度1 ℃（DS18B20）
    "deck_temp_2": 60.0,    # 桥面温度2 ℃（DS18B20）
    "pier_temp_1": 60.0,    # 桥墩温度1 ℃（DS18B20）
    "pier_temp_2": 60.0,    # 桥墩温度2 ℃（DS18B20）
    "convergence": 50.0,    # 净空收敛 mm（VL53L1X 两侧壁距之和）
    # 2026-09-26 隧道板新增：K210 视觉模块（裂缝为实际实现，车辆/人员预留）
    "crack": 0.70,          # 裂缝置信度 0~1 无量纲（K210 视觉，≥0.7 判为有裂缝）
    "vehicle": 10.0,        # 车辆计数 辆（K210 端尚未实现，预留）
    "person": 5.0,          # 人员计数 人（K210 端尚未实现，预留）
    # 2026-09-25 桥板新增：RCWL-1670 超声波测距（既驱动 WS2812 灯带跟随，也上报）
    "position": 100.0,      # 物体位置 cm（RCWL-1670，量程 2~400cm；阈值待按演示场地标定）
}

# 运维：设备在线判定（超过该秒数未收到心跳即判离线；心跳 KeepAlive 60s，取 2 倍余量）
ONLINE_TIMEOUT_SECONDS = 120

# REST 服务
API_HOST = "0.0.0.0"
API_PORT = 8000

# JWT 登录认证
JWT_SECRET = "CHANGE_ME_IN_PRODUCTION"  # 生产环境请更换
JWT_ALGORITHM = "HS256"
JWT_EXPIRE_HOURS = 24

# 初始用户（首次启动自动写入数据库，可用 username/password 登录）
DEFAULT_USERS = [
    {"username": "admin", "password": "change-me", "role": "admin"},
]
