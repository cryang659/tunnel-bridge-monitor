# 隧道 / 桥梁结构健康监测系统

团队项目（8 人）· 企业导师制实训 · 周期 1 个月
本人负责 STM32 固件双板（`123_bridge/` + `123_tunnel/`）。

## 目录结构

| 目录 | 说明 | 状态 |
|------|------|------|
| `123_bridge/` + `123_tunnel/` | 嵌入式固件双板（STM32F103RCT6，桥板 4G+GNSS / 隧道板 WiFi） | ✅ 已完成 |
| `houduan/server/` | 后端（FastAPI + SQLite + MQTT 订阅） | ✅ 已完成 |
| `showcase/` | 前端展示（Vue3 + 3D 轮播首页 + 六页功能） | ✅ 已完成 |

## 通信方案

- **MQTT Broker**：EMQX Cloud 专有版，明文 `1883` + 用户名密码认证（示例配置见 `houduan/server/config.py`，运行时替换为自己的实例）
- **架构**：STM32 采集节点（桥板 DX-CT02-A 4G+GNSS / 隧道板 ESP8266 WiFi）→ MQTT Broker → 后端订阅 → SQLite → REST API → 前端展示

## 已完成模块

### 固件（`123_bridge/` + `123_tunnel/`）
- 传感器抽象层 + 9 类物模型（振动/应力/温度/湿度/水位/渗水/位移/荷载/索力；SHT30/MPU-6050/VL53L1X 走 I2C，压力/水位/雨滴走 ADC）
- 手写 MQTT 3.1.1 客户端（CONNECT/PUBLISH/SUBSCRIBE），已接入 MQTT Broker
- 数据 JSON 打包、边缘阈值告警、心跳保活、ST7735 LCD 显示

### 后端（`houduan/server/`）
- FastAPI + SQLite + paho-mqtt 订阅 MQTT Broker
- JWT 登录认证、告警阈值读写、设备管理（含经纬度）、统计/CSV 导出
- 详见 `houduan/server/README.md`

### 前端（`showcase/`）
- Vue 3 + Vite + Leaflet + Three.js，首页 3D 卡片轮播 + 六个功能页：数据看板 / GIS 地图 / 3D 模型 / 告警列表 / 阈值管理 / 设备管理
- 对接后端 REST API，端到端联通验证通过

## 模块对接方式

嵌入式节点 → `shm/{device_id}/data`（MQTT）→ Broker → 后端订阅 → SQLite 入库 + 阈值告警 → REST API → 前端展示。

## 运行前配置

本仓库已去掉所有真实凭据，运行前请替换：

| 位置 | 变量 |
|------|------|
| `123_bridge/Core/Inc/wifi_esp8266.h`、`123_tunnel/Core/Inc/wifi_esp8266.h` | `WIFI_SSID` / `WIFI_PASSWORD` / `SERVER_IP` |
| `123_bridge/Core/Inc/mqtt_client.h`、`123_tunnel/Core/Inc/mqtt_client.h` | `MQTT_USERNAME` / `MQTT_PASSWORD` |
| `houduan/server/config.py` | `MQTT_BROKER` / `MQTT_USERNAME` / `MQTT_PASSWORD` / `JWT_SECRET` / `DEFAULT_USERS` |

## 启动方式

```bash
# 后端
cd houduan/server && pip install -r requirements.txt && python main.py

# 前端
cd showcase && npm install && npm run dev   # 浏览器打开 http://localhost:5173
```

默认登录账号见 `houduan/server/config.py` 的 `DEFAULT_USERS`。
