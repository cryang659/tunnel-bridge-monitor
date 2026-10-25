# -*- coding: utf-8 -*-
"""
main.py —— CanMV-K210 视觉模块主程序（隧道裂缝识别）

跑在 K210 上，与 STM32（隧道板 NODE_002）串口通信。

┌─ 硬件接线（STM32 核心板）───────────────────────────────┐
│  K210 4 针线             STM32 核心板                    │
│  TX   ─────────────────► PA10  (USART1_RX)             │
│  RX   ◄───────────────── PA9   (USART1_TX)             │
│  GND  ─────────────────  GND                           │
│  VCC  ──── 独立 5V 电源（**不要**接板载 3V3/5V）        │
│                                                        │
│  模块上的 4 针外扩串口 = K210 的 IO6(TX) / IO7(RX)，     │
│  与 USB 口(IO4/IO5，烧录+调试)是两条独立的线，可同时用。 │
└────────────────────────────────────────────────────────┘

┌─ 通信协议 ─────────────────────────────────────────────┐
│  请求  STM32 → K210 :  "$Q#"                           │
│  应答  K210 → STM32 :  "$K,<status>,<crack>,<vehicle>,<person>#" │
│      status  位掩码 bit0=裂缝有效 bit1=车辆有效 bit2=人员有效 │
│      crack   0~100 裂缝置信度百分比（无裂缝为 0）       │
│      vehicle 0~255 车辆计数（本版恒 0，预留）           │
│      person  0~255 人员计数（本版恒 0，预留）           │
└────────────────────────────────────────────────────────┘

═══════════════════════════════════════════════════════════
⭐ 算法：**分块最暗值对比**（零分配，只用语义确定的字段）
═══════════════════════════════════════════════════════════

【要解决的难题】
裂缝的特征是「**局部**比周围暗」。而全图统计量（mean/min/std）是**全局**的，
天生看不见局部对比 —— 实测：白墙有光照渐变时，min 很低、std 很大，
和真裂缝的数字完全撞车，分不开。

【本方案】
把检测区切成 4×4 = 16 小块，每块单独取 l_min（最暗值）：

  · 光照渐变的暗角 → 暗是**整片**的，各块 l_min 都低且**彼此接近**
  · 真有裂缝       → 只有**经过裂缝的那几块** l_min 低，
                     其余块正常 → 块与块之间**差异明显**

判据 = 「最暗那块的 l_min」与「各块 l_min 的中位数」的**落差**。

【为什么用 l_min / l_median】
这两个字段语义**确定**（最暗值 / 中位数），不像
get_statistics(thresholds=) 那样返回语义不明、实测白墙裂缝同值。
踩过那个坑后，本方案只用可以确定含义的字段。

【零分配】
只用 get_statistics(roi=...)，每帧 16 次硬件统计，不产生任何逐像素/逐线段对象。
（LSD 就是因为每帧返回几百个对象、GC 追不上而卡死的，绝不能再用。）
"""

import time
from machine import UART
from fpioa_manager import fm

import sensor, image, lcd

# ============================================================
# 一、接线与串口
# ============================================================
UART_TX_PIN = 6      # K210 的 IO6 → STM32 的 PA10
UART_RX_PIN = 7      # K210 的 IO7 ← STM32 的 PA9
UART_BAUD   = 115200  # ⚠️ 必须与固件 MX_USART1_UART_Init() 的波特率一致

# ⚠️ 别把这两个改成 IO4/IO5——那是板载 USB 转串口芯片占用的，
#    两个输出会顶在一起。

# ============================================================
# 二、识别参数
# ============================================================
# 检测区划分：4x4 = 16 块。块越多定位越准，但块太小噪声也大。
NX, NY          = 4, 4

# ⭐ 判据①：落差 = 各块 l_min 的中位数 − 最暗块的 l_min（灰度级）
#   光照渐变下各块彼此接近 → 落差小；有裂缝时那几块明显更暗 → 落差大
#   实测：白墙 40 / 裂缝 63 → 取中间偏白墙一侧
DROP_TH         = 50

# ⭐ 判据②：至少要有几块「明显比中位数暗」才算裂缝
#   光照渐变/阴影往往只压暗 1 块；裂缝是一条线，会同时压暗好几块。
#   实测：白墙 0~1 块 / 裂缝 3~4 块 → 取 2，既拦住单块阴影又不漏真裂缝。
MIN_BLOCKS      = 2

MAX_CONF        = 100     # 置信度上限（协议是 0~100 整数）
SKIP_TOP        = 0.15    # 跳过画面上部比例（滤掉灯带/反光/顶部走线）
PANEL_H         = 64      # 屏幕底部数据面板高度（同时从检测区里排除）

# 诊断：每 N 帧往 REPL 打印一次（0 = 关闭）
DEBUG_TIMING    = 0

# ============================================================
# 三、全局状态
# ============================================================
_result = (0, 0, 0, 0)     # (status, crack, vehicle, person)，初值 = 全部无效
_rx_buf = b""              # 串口残留数据

# 最近一帧的调试值（显示在屏上）
_dark_min = 0              # 最暗那块的 l_min
_dark_med = 0              # 各块 l_min 的中位数
_drop     = 0              # 落差 = med - min
_n_dark   = 0              # 明显偏暗的块数

# ============================================================
# 四、裂缝检测（分块最暗值对比，零分配）
# ============================================================
def detect_crack(img):
    """返回 (conf, drop)：conf 0~MAX_CONF 置信度；drop 落差（灰度级）用于标定

    ⭐ 两个判据**同时满足**才判为裂缝：
       ① drop(落差)  ≥ DROP_TH   —— 最暗块比中位数低多少
       ② n_dark(暗块数) ≥ MIN_BLOCKS —— 有几块明显偏暗

    为什么要第二个判据（实测数据）：
       白墙 d40 / 裂缝 d63  → 分离度只有 1.6 倍，太窄，现场光照一变就误报。
       但「暗块个数」的差别大得多：
         光照渐变/阴影 → 往往只有 **1 块**暗
         裂缝（一条线穿过画面）→ 会**同时压暗好几块**（4×4 网格下通常 3~4 块）
       所以再加一条 n_dark ≥ MIN_BLOCKS，误报率大幅下降。

    ⭐ 只用 get_statistics(roi=...).l_min()，语义确定，不猜任何 API。
    """
    global _dark_min, _dark_med, _drop, _n_dark

    w, h = img.width(), img.height()
    y0 = int(h * SKIP_TOP)                  # 跳过画面上部
    y1 = h - PANEL_H                        # 排除底部数据面板
    if y1 - y0 < NY or w < NX:
        return (0, 0)

    # ---- 把检测区切成 NX×NY 块，逐块取 l_min ----
    bw = w // NX
    bh = (y1 - y0) // NY
    mins = []

    for iy in range(NY):
        for ix in range(NX):
            rx = ix * bw
            ry = y0 + iy * bh
            st = img.get_statistics(roi=(rx, ry, bw, bh))
            mins.append(st.l_min())

    # ---- 各块 l_min 的中位数（16 个元素，普通排序足够快）----
    mins.sort()
    n = len(mins)
    dark_med = mins[n // 2]
    dark_min = mins[0]

    drop = dark_med - dark_min

    # ---- 判据②：数一数有多块「明显比中位数暗」----
    th = dark_med - DROP_TH
    n_dark = 0
    for v in mins:
        if v < th:
            n_dark += 1

    _dark_min, _dark_med, _drop, _n_dark = dark_min, dark_med, drop, n_dark

    # ---- 两个判据都要满足 ----
    if drop < DROP_TH or n_dark < MIN_BLOCKS:
        return (0, drop)

    # ---- 置信度：落差超出越多、暗块越多，越确信 ----
    over = (drop - DROP_TH) / float(DROP_TH)
    conf = int(60 + over * 40)
    # 暗块数加成：每多一块 +5，最多 +20
    conf += (n_dark - MIN_BLOCKS) * 5
    if conf > MAX_CONF:
        conf = MAX_CONF
    return (conf, drop)

# ============================================================
# 五、串口轮询（立即应答）
# ============================================================
def poll_uart(uart):
    """非阻塞检查串口：收到 $Q# 就立刻回上一次的识别结果。

    每帧调用一次。读的是 machine.UART 的内部接收缓冲区（底层中断填充），
    所以不会漏数据，也不需要额外的中断回调。
    """
    global _rx_buf
    n = uart.any()
    if not n:
        return
    data = uart.read(n)
    if not data:
        return
    _rx_buf += data
    if len(_rx_buf) > 64:                   # 防垃圾数据撑爆缓冲
        _rx_buf = _rx_buf[-32:]

    if b"$Q#" in _rx_buf:
        _rx_buf = b""                       # 清掉，避免重复应答
        st, ck, ve, pe = _result
        uart.write("$K,%d,%d,%d,%d#" % (st, ck, ve, pe))
        uart.write("\n")

# ============================================================
# 六、主循环
# ============================================================
def main():
    global _result

    # ---- 串口 ----
    # K210 用 fpioa_manager 注册引脚功能（不是 K230 的 machine.FPIOA）
    fm.register(UART_TX_PIN, fm.fpioa.UART1_TX, force=True)
    fm.register(UART_RX_PIN, fm.fpioa.UART1_RX, force=True)

    # bitwidth=8, parity=None, stop=1（K210 用数值，不是 EIGHTBITS 常量）
    uart = UART(UART.UART1, UART_BAUD, 8, None, 1,
                timeout=10, read_buf_len=256)

    # ---- 摄像头 ----
    sensor.reset()
    sensor.set_pixformat(sensor.GRAYSCALE)   # 裂缝只需灰度，更快
    sensor.set_framesize(sensor.QVGA)        # 320x240
    sensor.set_hmirror(False)
    sensor.set_vflip(False)
    sensor.skip_frames(time=500)

    # 关闭自动曝光/增益（测量任务必须固定曝光，否则前后两次数值不可比）
    for fn in ("set_auto_exposure", "set_auto_gain", "set_auto_whitebal"):
        if hasattr(sensor, fn):
            try:
                getattr(sensor, fn)(False)
            except Exception:
                pass

    # ---- LCD ----
    show_lcd = False
    try:
        lcd.init()
        show_lcd = True
    except Exception as e:
        print("lcd.init 失败:", e)

    # 启动自检
    _probe = sensor.snapshot()
    print("=" * 44)
    print("DROP_TH =", DROP_TH, " 块划分 =", NX, "x", NY)
    print("show_lcd =", show_lcd)
    print("sensor size =", _probe.width(), "x", _probe.height())
    print("=" * 44)

    fps_t = time.ticks_ms()
    fps = 0
    frames = 0

    while True:
        t0 = time.ticks_ms()
        img = sensor.snapshot()

        # ① 先应答串口请求（用上一帧结果，不等本次识别）——保证 STM32 不超时
        poll_uart(uart)

        # ② 做识别（零分配，很快）
        t1 = time.ticks_ms()
        conf, drop = detect_crack(img)
        t2 = time.ticks_ms()

        if DEBUG_TIMING and (frames % DEBUG_TIMING == 0):
            print("snap=%dms detect=%dms drop=%d" % (t1 - t0, t2 - t1, drop))

        _result = (1, conf, 0, 0)   # status bit0=1：裂缝结果有效（0 表示无裂缝）

        # ---- LCD 显示：上半屏真实画面，下半屏黑底白字数据面板 ----
        if show_lcd:
            frames += 1
            now = time.ticks_ms()
            if time.ticks_diff(now, fps_t) >= 1000:
                fps = frames
                frames = 0
                fps_t = now

            w, h = img.width(), img.height()
            panel_y = h - PANEL_H

            img.draw_rectangle(0, panel_y, w, PANEL_H, color=0, fill=True)

            # 第 1 行（小字）：原始数据 —— 标定/排查全靠这行
            #   mn/md = 最暗块 / 中位数   d = 落差   nd = 暗块数
            #   T = 落差阈值   B = 暗块数阈值
            img.draw_string(4, panel_y + 4,
                            "mn%d md%d d%d nd%d T%d B%d %df"
                            % (_dark_min, _dark_med, _drop, _n_dark,
                               DROP_TH, MIN_BLOCKS, fps),
                            color=(255, 255, 255), scale=1)

            # 第 2 行（大字）：置信度（红=报警 白=正常）
            img.draw_string(6, panel_y + 20, "cf%d" % conf,
                            color=(255, 0, 0) if conf > 0 else (255, 255, 255),
                            scale=2)

            lcd.display(img)

# ============================================================
if __name__ == "__main__":
    main()
