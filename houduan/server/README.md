# 隧道/桥梁监测系统 · 服务器 + MQTT 骨架

对应接口规范 v1.0 的数据层实现。技术栈：Python + FastAPI + MQTT(paho) + SQLite。

## 目录结构

```
server/
├── main.py           # FastAPI 入口，启动时拉起 MQTT 订阅线程
├── mqtt_handler.py   # MQTT 订阅 + 解析 + 入库 + 阈值告警
├── database.py       # SQLite 建表与增查
├── config.py         # 配置（EMQX Cloud 地址/认证、Topic、告警阈值、端口）
├── auth.py           # JWT 认证（密码哈希 + token 签发/验证）
├── simulator.py      # 模拟监测节点（无真实硬件时跑通全链路）
└── requirements.txt  # 依赖
```

## 启动步骤

### 0. 安装依赖

```bash
pip install -r requirements.txt
```

MQTT Broker 使用 EMQX Cloud 托管（地址、认证见 `config.py`），无需本地启动。

### 1. 启动服务器（REST API + MQTT 订阅）

```bash
cd server
python main.py
```

服务启动后：
- REST API：<http://127.0.0.1:8000>
- 自动订阅 `shm/+/data`、`shm/+/heartbeat`、`shm/+/alarm`

### 2. 启动模拟监测节点（另开一个终端）

```bash
cd server
python simulator.py
```

每 5 秒向 `shm/NODE_001/data` 批量上报 9 类传感器数据。

## API 文档

### 鉴权说明

- 写操作（阈值修改、设备增删改）需登录，请求头携带 `Authorization: Bearer <token>`；读操作公开。
- 默认账号：`admin / change-me`（见 `config.py` 的 `DEFAULT_USERS`）。

### 认证

| 方法 | 路径 | 说明 | 鉴权 |
|------|------|------|------|
| POST | `/api/auth/login` | 登录，返回 JWT token | 否 |
| GET | `/api/auth/me` | 当前登录用户 | 是 |

登录请求体 `{"username": "admin", "password": "change-me"}`，响应 `{"token": "...", "username": "admin", "role": "admin"}`。

### 告警阈值

| 方法 | 路径 | 说明 | 鉴权 |
|------|------|------|------|
| GET | `/api/thresholds` | 读取所有传感器告警阈值 | 否 |
| PUT | `/api/thresholds` | 修改阈值 | 是 |

修改请求体：`{"thresholds": {"temperature": 55.0, "vibration": 0.15}}`。

### 设备管理

| 方法 | 路径 | 说明 | 鉴权 |
|------|------|------|------|
| GET | `/api/devices` | 设备列表 | 否 |
| POST | `/api/devices` | 添加设备 | 是 |
| PUT | `/api/devices/{device_id}` | 更新设备 | 是 |
| DELETE | `/api/devices/{device_id}` | 删除设备 | 是 |

设备字段：`device_id`（必填）、`name`、`location`、`latitude`、`longitude`（经纬度用于 GIS 地图标注）。

### 数据查询

| 方法 | 路径 | 说明 | 鉴权 |
|------|------|------|------|
| GET | `/api/data` | 历史监测数据 | 否 |
| GET | `/api/alarms` | 告警记录 | 否 |

`/api/data` 查询参数：`device_id`、`type`、`start`、`end`、`limit`（默认 1000）。
`/api/alarms` 查询参数：`limit`（默认 200）。

### 统计与导出

| 方法 | 路径 | 说明 | 鉴权 |
|------|------|------|------|
| GET | `/api/stats` | 按传感器类型统计 | 否 |
| GET | `/api/data/export` | 导出 CSV 文件 | 否 |

`/api/stats` 查询参数：`device_id`、`type`、`start`、`end`。响应示例：

```json
[
  {"type": "temperature", "count": 120, "min": 20.5, "max": 35.2, "avg": 27.3},
  {"type": "vibration", "count": 120, "min": 0.01, "max": 0.05, "avg": 0.02}
]
```

### 设备上报数据格式（MQTT）

设备发布 JSON 到 `shm/{device_id}/data`：

```json
{
  "device_id": "NODE_001",
  "timestamp": "2026-08-27T09:00:00",
  "data": [
    {"type": "temperature", "value": 26.5, "unit": "℃"},
    {"type": "vibration", "value": 0.02, "unit": "g"}
  ]
}
```

传感器类型：`vibration` / `stress` / `temperature` / `humidity` / `water_level` / `seepage` / `displacement` / `crack` / `load`。

## 说明

- 数据落库到 `monitor.db`（SQLite，自动创建）。
- 告警阈值存于数据库 `thresholds` 表（默认值见 `config.py`），可通过 `PUT /api/thresholds` 修改；越限自动写入 alarms 表。
- 真实 STM32 节点联网后，只需按 `shm/{device_id}/data` 主题发布相同 JSON 报文即可接入，无需改动服务器。
