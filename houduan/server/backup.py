# -*- coding: utf-8 -*-
"""数据库定期备份脚本：在线备份 monitor.db 到 backup/，保留最近 N 份

用法：在 server 目录下运行 `python backup.py`
定时：配合 Windows 计划任务定时执行（见《运维手册》）。
"""
import os
import sqlite3
from datetime import datetime

DB_PATH = "monitor.db"   # 与 config.py 的 DB_PATH 一致
BACKUP_DIR = "backup"
KEEP = 7                  # 保留最近 7 份备份


def backup():
    os.makedirs(BACKUP_DIR, exist_ok=True)
    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
    dest = os.path.join(BACKUP_DIR, f"monitor_{ts}.db")

    # 用 SQLite 在线备份 API，避免直接复制文件时读到写入中的不一致数据
    src = sqlite3.connect(DB_PATH)
    dst = sqlite3.connect(dest)
    with dst:
        src.backup(dst)
    dst.close()
    src.close()
    print(f"[备份] 已生成 {dest}")

    # 清理旧备份，只保留最近 KEEP 份
    files = sorted(
        f for f in os.listdir(BACKUP_DIR)
        if f.startswith("monitor_") and f.endswith(".db")
    )
    for f in files[:-KEEP]:
        os.remove(os.path.join(BACKUP_DIR, f))
        print(f"[备份] 清理旧备份 {f}")


if __name__ == "__main__":
    backup()
