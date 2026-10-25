# -*- coding: utf-8 -*-
"""FastAPI 服务器入口：启动时初始化数据库并拉起 MQTT 订阅线程"""
import csv
import io
import threading
from datetime import datetime
from typing import Optional

import uvicorn
from fastapi import Depends, FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import StreamingResponse
from pydantic import BaseModel

from config import API_HOST, API_PORT, DEFAULT_USERS
from database import (
    init_db, list_devices, query_data, list_alarms,
    insert_device, update_device, delete_device,
    get_thresholds, set_thresholds, query_stats,
    create_user, get_user_by_username,
)
from mqtt_handler import start_mqtt
from auth import hash_password, verify_password, create_access_token, get_current_user

app = FastAPI(title="隧道/桥梁监测系统服务器", version="2.0")

# 允许前端跨域访问（Web 页面与后端分离部署）
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)


@app.on_event("startup")
def _startup():
    init_db()
    # 首次启动写入默认用户
    for u in DEFAULT_USERS:
        if get_user_by_username(u["username"]) is None:
            create_user(u["username"], hash_password(u["password"]), u["role"])
    threading.Thread(target=start_mqtt, daemon=True).start()
    print("[服务器] 已启动，MQTT 订阅线程运行中")


# ---------- 请求体模型 ----------
class LoginBody(BaseModel):
    username: str
    password: str


class ThresholdBody(BaseModel):
    thresholds: dict  # {type: alarm_value}


class DeviceBody(BaseModel):
    device_id: str
    name: str = ""
    location: str = ""
    latitude: Optional[float] = None
    longitude: Optional[float] = None


class DeviceUpdateBody(BaseModel):
    name: Optional[str] = None
    location: Optional[str] = None
    latitude: Optional[float] = None
    longitude: Optional[float] = None


# ---------- 时间对时（板上 LCD 用） ----------
@app.get("/api/time")
def api_time():
    """返回服务器当前时间，供板上节点 HTTP 对时：{"ts": "YYYY-MM-DD HH:MM:SS"}"""
    return {"ts": datetime.now().strftime("%Y-%m-%d %H:%M:%S")}


# ---------- 认证 ----------
@app.post("/api/auth/login")
def login(body: LoginBody):
    user = get_user_by_username(body.username)
    if user is None or not verify_password(body.password, user["password_hash"]):
        raise HTTPException(status_code=401, detail="用户名或密码错误")
    token = create_access_token(user["username"], user["role"])
    return {"token": token, "username": user["username"], "role": user["role"]}


@app.get("/api/auth/me")
def me(user=Depends(get_current_user)):
    return user


# ---------- 告警阈值 ----------
@app.get("/api/thresholds")
def api_get_thresholds():
    return get_thresholds()


@app.put("/api/thresholds")
def api_set_thresholds(body: ThresholdBody, user=Depends(get_current_user)):
    return set_thresholds(body.thresholds)


# ---------- 设备管理 ----------
@app.get("/api/devices")
def api_devices():
    return list_devices()


@app.post("/api/devices")
def api_add_device(body: DeviceBody, user=Depends(get_current_user)):
    insert_device(body.device_id, body.name, body.location, body.latitude, body.longitude)
    return {"ok": True, "device_id": body.device_id}


@app.put("/api/devices/{device_id}")
def api_update_device(device_id: str, body: DeviceUpdateBody, user=Depends(get_current_user)):
    result = update_device(device_id, body.name, body.location, body.latitude, body.longitude)
    if result is None:
        raise HTTPException(status_code=404, detail="设备不存在")
    return result


@app.delete("/api/devices/{device_id}")
def api_delete_device(device_id: str, user=Depends(get_current_user)):
    if not delete_device(device_id):
        raise HTTPException(status_code=404, detail="设备不存在")
    return {"ok": True}


# ---------- 数据查询 ----------
@app.get("/api/data")
def api_data(device_id: Optional[str] = None, type: Optional[str] = None,
             start: Optional[str] = None, end: Optional[str] = None, limit: int = 1000):
    return query_data(device_id, type, start, end, limit)


@app.get("/api/alarms")
def api_alarms(limit: int = 200):
    return list_alarms(limit)


# ---------- 统计 / 导出 ----------
@app.get("/api/stats")
def api_stats(device_id: Optional[str] = None, type: Optional[str] = None,
              start: Optional[str] = None, end: Optional[str] = None):
    return query_stats(device_id, type, start, end)


@app.get("/api/data/export")
def api_export(device_id: Optional[str] = None, type: Optional[str] = None,
               start: Optional[str] = None, end: Optional[str] = None):
    rows = query_data(device_id, type, start, end, limit=100000)
    output = io.StringIO()
    writer = csv.writer(output)
    writer.writerow(["device_id", "type", "value", "unit", "status", "timestamp"])
    for r in rows:
        writer.writerow([r["device_id"], r["type"], r["value"], r["unit"], r["status"], r["timestamp"]])
    output.seek(0)
    filename = f"monitor_data_{device_id or 'all'}.csv"
    return StreamingResponse(
        iter([output.getvalue()]),
        media_type="text/csv; charset=utf-8",
        headers={"Content-Disposition": f"attachment; filename={filename}"},
    )


if __name__ == "__main__":
    uvicorn.run(app, host=API_HOST, port=API_PORT)
