# -*- coding: utf-8 -*-
"""
kpu_test.py —— KPU 链路探测（第 2 版）

┌─ 第 1 版实测结果（2026-09-26）─────────────────────────┐
│  import KPU as kpu       → 失败（无此模块）            │
│  from maix import KPU    → ✅ 成功                     │
│  KPU 可用函数：                                         │
│    load_kmodel, init_yolo2, run_with_output,           │
│    regionlayer_yolo2, deinit, feature_compare,         │
│    lp_recog, lp_recog_load_weight_data, sigmoid,       │
│    softmax                                              │
│    ⚠️ **没有 load()** —— 只能用 load_kmodel             │
│  kpu.load(0x300000)      → 失败（无 load 函数）        │
│  kpu.load_kmodel(0x300000) → 失败: "path error!"       │
│    ⇒ 报错说的是「路径」，说明它要**文件路径**不是地址  │
│    ⇒ 板子的 Flash 0x300000 处**没有内置模型**          │
└────────────────────────────────────────────────────────┘

本脚本做三件事：
  1. 检查有没有 TF 卡，卡上有没有 .kmodel 文件
  2. 试 load_kmodel 的几种可能写法，看哪种能被接受
  3. 报告结论：需要你准备什么

用法：CanMV IDE 打开 → ▶ 运行 → 把 REPL 全部输出发回。
"""

import os

print("=" * 58)
print("kpu_test v2 —— KPU 链路探测")
print("=" * 58)

# ============================================================
# 一、KPU 模块
# ============================================================
kpu = None
try:
    from maix import KPU as kpu        # 第 1 版已知这条路通
    print("[OK]   from maix import KPU")
except Exception as e:
    print("[FAIL] from maix import KPU ->", e)
    try:
        import KPU as kpu
        print("[OK]   import KPU")
    except Exception as e2:
        print("[FAIL] import KPU ->", e2)

if kpu is None:
    print("\n!! 没有 KPU 模块，固件不支持 AI。请烧带 AI 的 CanMV 固件。")
    raise SystemExit

# ============================================================
# 二、文件系统：找 TF 卡和 kmodel
# ============================================================
print("\n--- 1. 文件系统 ---")

def ls(path):
    """列目录，失败返回 None"""
    try:
        return os.listdir(path)
    except Exception as e:
        print("   [FAIL] os.listdir(%r) -> %s" % (path, e))
        return None

print("os.listdir('/')       ->", end=" ")
root = ls("/")
print(root if root is not None else "（失败）")

print("os.listdir('/sd')     ->", end=" ")
sd = ls("/sd")
print(sd if sd is not None else "（失败，可能没插 TF 卡）")

print("os.listdir('/flash')  ->", end=" ")
fl = ls("/flash")
print(fl if fl is not None else "（失败）")

# 在能访问的目录里找 .kmodel
print("\n--- 2. 搜索 .kmodel 文件 ---")
found = []
for base in ("/", "/sd", "/flash", "/sd/model", "/sd/kmodel"):
    files = ls(base)
    if not files:
        continue
    for f in files:
        if str(f).lower().endswith(".kmodel"):
            full = base.rstrip("/") + "/" + f
            try:
                size = os.stat(full)[6]
            except Exception:
                size = -1
            print("   ★ 找到: %s  (%d 字节)" % (full, size))
            found.append(full)

if not found:
    print("   （没找到任何 .kmodel）")

# ============================================================
# 三、试 load_kmodel 的各种写法
# ============================================================
print("\n--- 3. 试 load_kmodel 的写法 ---")

def try_load(label, *args):
    try:
        t = kpu.load_kmodel(*args)
        print("   [OK]   %-46s -> %s" % (label, type(t).__name__))
        return t
    except Exception as e:
        print("   [FAIL] %-46s -> %s: %s" % (label, type(e).__name__, e))
        return None

# 有找到的文件就先试它
if found:
    try_load("load_kmodel('%s')" % found[0], found[0])
else:
    print("   （没有 .kmodel 文件，只能试各种占位写法看报错信息）")

# 摸清参数个数和类型：看看是「要 1 个参数」还是「要 2 个参数」
try_load("load_kmodel(0x300000, 0)", 0x300000, 0)
try_load("load_kmodel(0x300000, 2660000)", 0x300000, 2660000)
try_load("load_kmodel('/sd/test.kmodel', 0)", "/sd/test.kmodel", 0)

# ============================================================
# 四、摄像头（确认没问题，顺便给结论）
# ============================================================
print("\n--- 4. 摄像头 ---")
try:
    import sensor
    sensor.reset()
    sensor.set_pixformat(sensor.RGB565)
    sensor.set_framesize(sensor.QVGA)
    sensor.skip_frames(time=300)
    print("   [OK]   摄像头就绪")
except Exception as e:
    print("   [FAIL] 摄像头 ->", e)

# ============================================================
# 五、结论
# ============================================================
print("\n" + "=" * 58)
print("结论")
print("=" * 58)
print("KPU 硬件/固件支持 : ✅ 正常")
if found:
    print("TF 卡上的模型     : ✅ %d 个" % len(found))
    print("  → 可以直接试跑：kpu.init_yolo2(task, ...) 然后 run_with_output()")
elif sd is not None:
    print("TF 卡             : ✅ 有卡，但卡上没有 .kmodel")
    print("  → 需要：把训练好的 crack.kmodel 拷到 TF 卡根目录")
else:
    print("TF 卡             : ❌ 没插卡 / 读不到")
    print("  → 需要：插一张 FAT32 格式的 TF 卡，把 .kmodel 拷到根目录")

print("""
注意：KPU 可用函数里有 load_kmodel 但**没有 load**，
      说明这个固件只能从**文件**加载模型，不能从 Flash 地址加载。
      所以模型必须放 TF 卡（烧录到 Flash 那条路走不通）。
""")
