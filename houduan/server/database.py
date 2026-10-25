# -*- coding: utf-8 -*-
"""SQLite 数据库操作"""
import sqlite3
from datetime import datetime, timedelta

from config import DB_PATH, THRESHOLDS, ONLINE_TIMEOUT_SECONDS

SCHEMA = """
CREATE TABLE IF NOT EXISTS devices (
    device_id   TEXT PRIMARY KEY,
    name        TEXT DEFAULT '',
    location    TEXT DEFAULT '',
    latitude    REAL,
    longitude   REAL,
    last_seen   TEXT
);
CREATE TABLE IF NOT EXISTS data (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id   TEXT NOT NULL,
    type        TEXT NOT NULL,
    value       REAL,
    unit        TEXT,
    status      TEXT,
    timestamp   TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS alarms (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id   TEXT NOT NULL,
    type        TEXT NOT NULL,
    value       REAL,
    threshold   REAL,
    level       TEXT,
    timestamp   TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_data_device_time ON data(device_id, timestamp);
CREATE TABLE IF NOT EXISTS users (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    username      TEXT UNIQUE NOT NULL,
    password_hash TEXT NOT NULL,
    role          TEXT DEFAULT 'user'
);
CREATE TABLE IF NOT EXISTS thresholds (
    type  TEXT PRIMARY KEY,
    alarm REAL NOT NULL
);
"""


def get_conn():
    conn = sqlite3.connect(DB_PATH, check_same_thread=False)
    conn.row_factory = sqlite3.Row
    return conn


def _migrate():
    """旧库迁移：为 devices 表补经纬度列（若缺失）"""
    conn = get_conn()
    cols = [r["name"] for r in conn.execute("PRAGMA table_info(devices)").fetchall()]
    if "latitude" not in cols:
        conn.execute("ALTER TABLE devices ADD COLUMN latitude REAL")
    if "longitude" not in cols:
        conn.execute("ALTER TABLE devices ADD COLUMN longitude REAL")
    conn.commit()
    conn.close()


def init_db():
    conn = get_conn()
    conn.executescript(SCHEMA)
    conn.commit()
    conn.close()
    _migrate()
    init_thresholds()


def upsert_device(device_id, timestamp, latitude=None, longitude=None):
    conn = get_conn()
    conn.execute(
        "INSERT INTO devices (device_id, last_seen, latitude, longitude) "
        "VALUES (?, ?, ?, ?) "
        "ON CONFLICT(device_id) DO UPDATE SET "
        "last_seen=excluded.last_seen, "
        "latitude=COALESCE(excluded.latitude, latitude), "
        "longitude=COALESCE(excluded.longitude, longitude)",
        (device_id, timestamp, latitude, longitude),
    )
    conn.commit()
    conn.close()


def insert_data(device_id, type_, value, unit, status, timestamp):
    conn = get_conn()
    conn.execute(
        "INSERT INTO data (device_id, type, value, unit, status, timestamp) "
        "VALUES (?, ?, ?, ?, ?, ?)",
        (device_id, type_, value, unit, status, timestamp),
    )
    conn.commit()
    conn.close()


def insert_alarm(device_id, type_, value, threshold, level, timestamp):
    conn = get_conn()
    conn.execute(
        "INSERT INTO alarms (device_id, type, value, threshold, level, timestamp) "
        "VALUES (?, ?, ?, ?, ?, ?)",
        (device_id, type_, value, threshold, level, timestamp),
    )
    conn.commit()
    conn.close()


def list_devices():
    conn = get_conn()
    rows = conn.execute("SELECT * FROM devices ORDER BY device_id").fetchall()
    conn.close()
    now = datetime.now()
    devices = []
    for r in rows:
        d = dict(r)
        d["online"] = False
        last_seen = d.get("last_seen")
        if last_seen:
            try:
                last = datetime.fromisoformat(last_seen)
                d["online"] = (now - last) < timedelta(seconds=ONLINE_TIMEOUT_SECONDS)
            except (ValueError, TypeError):
                pass
        devices.append(d)
    return devices


def query_data(device_id=None, type_=None, start=None, end=None, limit=1000):
    sql = "SELECT * FROM data WHERE 1=1"
    params = []
    if device_id:
        sql += " AND device_id=?"
        params.append(device_id)
    if type_:
        sql += " AND type=?"
        params.append(type_)
    if start:
        sql += " AND timestamp>=?"
        params.append(start)
    if end:
        sql += " AND timestamp<=?"
        params.append(end)
    sql += " ORDER BY timestamp DESC LIMIT ?"
    params.append(limit)
    conn = get_conn()
    rows = conn.execute(sql, params).fetchall()
    conn.close()
    return [dict(r) for r in rows]


def list_alarms(limit=200):
    conn = get_conn()
    rows = conn.execute(
        "SELECT * FROM alarms ORDER BY timestamp DESC LIMIT ?", (limit,)
    ).fetchall()
    conn.close()
    return [dict(r) for r in rows]


# ============ 告警阈值（内存缓存 + 数据库） ============
_THRESHOLDS = {}


def init_thresholds():
    """同步 config 默认阈值到库：新增项补入、已移除项删除、已有项保留（用户可能改过）"""
    conn = get_conn()
    for t, v in THRESHOLDS.items():
        conn.execute(
            "INSERT INTO thresholds (type, alarm) VALUES (?, ?) "
            "ON CONFLICT(type) DO NOTHING", (t, v))
    placeholders = ",".join("?" * len(THRESHOLDS))
    conn.execute(
        f"DELETE FROM thresholds WHERE type NOT IN ({placeholders})",
        tuple(THRESHOLDS.keys()))
    conn.commit()
    _THRESHOLDS.clear()
    for r in conn.execute("SELECT type, alarm FROM thresholds").fetchall():
        _THRESHOLDS[r["type"]] = r["alarm"]
    conn.close()


def get_thresholds():
    return dict(_THRESHOLDS)


def set_thresholds(d):
    conn = get_conn()
    for t, v in d.items():
        conn.execute(
            "INSERT INTO thresholds (type, alarm) VALUES (?, ?) "
            "ON CONFLICT(type) DO UPDATE SET alarm=excluded.alarm", (t, float(v)))
        _THRESHOLDS[t] = float(v)
    conn.commit()
    conn.close()
    return dict(_THRESHOLDS)


# ============ 用户 ============
def create_user(username, password_hash, role="user"):
    conn = get_conn()
    conn.execute(
        "INSERT INTO users (username, password_hash, role) VALUES (?, ?, ?) "
        "ON CONFLICT(username) DO UPDATE SET password_hash=excluded.password_hash, "
        "role=excluded.role",
        (username, password_hash, role))
    conn.commit()
    conn.close()


def get_user_by_username(username):
    conn = get_conn()
    row = conn.execute("SELECT * FROM users WHERE username=?", (username,)).fetchone()
    conn.close()
    return dict(row) if row else None


# ============ 设备管理 ============
def insert_device(device_id, name="", location="", latitude=None, longitude=None):
    conn = get_conn()
    conn.execute(
        "INSERT INTO devices (device_id, name, location, latitude, longitude) "
        "VALUES (?, ?, ?, ?, ?) "
        "ON CONFLICT(device_id) DO UPDATE SET name=excluded.name, location=excluded.location, "
        "latitude=excluded.latitude, longitude=excluded.longitude",
        (device_id, name, location, latitude, longitude))
    conn.commit()
    conn.close()


def update_device(device_id, name=None, location=None, latitude=None, longitude=None):
    conn = get_conn()
    row = conn.execute("SELECT * FROM devices WHERE device_id=?", (device_id,)).fetchone()
    if row is None:
        conn.close()
        return None
    name = row["name"] if name is None else name
    location = row["location"] if location is None else location
    latitude = row["latitude"] if latitude is None else latitude
    longitude = row["longitude"] if longitude is None else longitude
    conn.execute(
        "UPDATE devices SET name=?, location=?, latitude=?, longitude=? WHERE device_id=?",
        (name, location, latitude, longitude, device_id))
    conn.commit()
    conn.close()
    return {"device_id": device_id, "name": name, "location": location,
            "latitude": latitude, "longitude": longitude}


def delete_device(device_id):
    conn = get_conn()
    cur = conn.execute("DELETE FROM devices WHERE device_id=?", (device_id,))
    conn.commit()
    deleted = cur.rowcount
    conn.close()
    return deleted > 0


# ============ 统计 ============
def query_stats(device_id=None, type_=None, start=None, end=None):
    sql = ("SELECT type, COUNT(*) AS cnt, MIN(value) AS mn, MAX(value) AS mx, "
           "AVG(value) AS avg FROM data WHERE 1=1")
    params = []
    if device_id:
        sql += " AND device_id=?"
        params.append(device_id)
    if type_:
        sql += " AND type=?"
        params.append(type_)
    if start:
        sql += " AND timestamp>=?"
        params.append(start)
    if end:
        sql += " AND timestamp<=?"
        params.append(end)
    sql += " GROUP BY type ORDER BY type"
    conn = get_conn()
    rows = conn.execute(sql, params).fetchall()
    conn.close()
    return [
        {"type": r["type"], "count": r["cnt"], "min": r["mn"], "max": r["mx"],
         "avg": round(r["avg"], 3) if r["avg"] is not None else None}
        for r in rows
    ]
