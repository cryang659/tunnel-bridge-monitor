# -*- coding: utf-8 -*-
"""后端 API 自动化测试：登录/阈值/设备/数据/统计/导出 + 鉴权

用法：先启动后端 `python main.py`，再运行本脚本 `python test_api.py`。
全部通过返回退出码 0，否则返回 1。
"""
import json
import sys
import urllib.request
import urllib.error

from config import THRESHOLDS

sys.stdout.reconfigure(encoding="utf-8")

BASE = "http://127.0.0.1:8000"
results = []  # (名称, PASS/FAIL, 详情)


def record(name, ok, detail=""):
    results.append((name, "PASS" if ok else "FAIL", detail))
    print(f"[{'✓' if ok else '✗'}] {name}  {detail}")


def req(method, path, data=None, token=None):
    url = BASE + path
    body = json.dumps(data).encode("utf-8") if data is not None else None
    r = urllib.request.Request(url, data=body, method=method)
    if data is not None:
        r.add_header("Content-Type", "application/json")
    if token:
        r.add_header("Authorization", "Bearer " + token)
    try:
        resp = urllib.request.urlopen(r, timeout=10)
        return resp.status, json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        try:
            return e.code, json.loads(e.read().decode("utf-8"))
        except Exception:
            return e.code, {}


# ============ 1. 认证 ============
s, r = req("POST", "/api/auth/login", {"username": "admin", "password": "change-me"})
token = r.get("token", "")
record("登录（正确密码）", s == 200 and bool(token), f"status={s} token长度={len(token)}")

s, r = req("POST", "/api/auth/login", {"username": "admin", "password": "wrong"})
record("登录（错误密码应401）", s == 401, f"status={s}")

s, r = req("GET", "/api/auth/me", token=token)
record("当前用户（带token）", s == 200 and r.get("username") == "admin", f"status={s}")

# ============ 2. 告警阈值 ============
s, r = req("GET", "/api/thresholds")
record("读取阈值", s == 200 and len(r) == len(THRESHOLDS), f"status={s} 项数={len(r)}")

s, r = req("PUT", "/api/thresholds", {"thresholds": {"temperature": 50.0}}, token)
record("修改阈值（带token）", s == 200 and r.get("temperature") == 50.0, f"status={s}")

s, r = req("PUT", "/api/thresholds", {"thresholds": {"temperature": 99.0}})
record("修改阈值（无token应401）", s == 401, f"status={s}")

# ============ 3. 设备管理 ============
s, r = req("GET", "/api/devices")
record("设备列表", s == 200, f"status={s} 设备数={len(r)}")

s, r = req("POST", "/api/devices",
           {"device_id": "TEST_001", "name": "测试节点", "location": "测试位置",
            "latitude": 26.65, "longitude": 106.63}, token)
record("添加设备（带经纬度）", s == 200, f"status={s}")

s, r = req("PUT", "/api/devices/TEST_001", {"name": "测试节点-改名"}, token)
record("更新设备", s == 200 and r.get("name") == "测试节点-改名", f"status={s}")

s, r = req("DELETE", "/api/devices/TEST_001", token=token)
record("删除设备", s == 200, f"status={s}")

s, r = req("POST", "/api/devices", {"device_id": "TEST_002"})
record("添加设备（无token应401）", s == 401, f"status={s}")

# ============ 4. 数据查询 ============
s, r = req("GET", "/api/data?limit=10")
record("查询数据", s == 200, f"status={s} 返回条数={len(r)}")

s, r = req("GET", "/api/alarms?limit=10")
record("告警列表", s == 200, f"status={s} 告警数={len(r)}")

# ============ 5. 统计 ============
s, r = req("GET", "/api/stats")
record("统计（按类型）", s == 200, f"status={s} 类型数={len(r)}")

# ============ 6. 导出 CSV ============
r = urllib.request.Request(BASE + "/api/data/export?limit=5")
try:
    resp = urllib.request.urlopen(r, timeout=10)
    csv_head = resp.read().decode("utf-8")
    ok = resp.status == 200 and csv_head.startswith("device_id,type")
    record("导出CSV", ok, f"status={resp.status}")
except Exception as e:
    record("导出CSV", False, str(e))

# ============ 汇总 ============
passed = sum(1 for _, st, _ in results if st == "PASS")
total = len(results)
print(f"\n===== 汇总：{passed}/{total} 通过 =====")
for name, st, detail in results:
    if st == "FAIL":
        print(f"  失败：{name}  {detail}")
sys.exit(0 if passed == total else 1)
