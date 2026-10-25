# -*- coding: utf-8 -*-
"""校验 menu_text.h / menu.c / st7735_font_cn.h 里用到的每个汉字都在字库中。"""
import re
import sys

BASE = r"C:/Users/21573/Desktop/8.25/tunnel-bridge-monitor/123_tunnel"
sys.stdout.reconfigure(encoding="utf-8")

font = open(BASE + "/Core/Inc/st7735_font_cn.h", encoding="ascii").read()
codes = set()
for m in re.finditer(r"\{\{(0x[0-9A-F]{2}), (0x[0-9A-F]{2})\}", font):
    codes.add(bytes([int(m.group(1), 16), int(m.group(2), 16)]))
print("字库字数:", len(codes))

ESC = re.compile(r'"((?:\\x[0-9A-F]{2})*)"')
BYTES = re.compile(r"\\x([0-9A-F]{2})")

need = []
for path in ("/Core/Inc/menu_text.h", "/Core/Src/menu.c"):
    src = open(BASE + path, encoding="utf-8", errors="replace").read()
    for m in ESC.finditer(src):
        raw = m.group(1)
        if not raw:
            continue
        b = bytes(int(x, 16) for x in BYTES.findall(raw))
        need.append((path, b))

missing = []
seen = set()
for path, b in need:
    for i in range(0, len(b) - 1, 2):
        ch = b[i:i + 2]
        if ch in codes or ch in seen:
            continue
        seen.add(ch)
        try:
            s = ch.decode("gbk")
        except UnicodeDecodeError:
            s = "<非法GBK>"
        missing.append((path.split("/")[-1], s, ch.hex()))

if missing:
    print("!! 缺失汉字:")
    for p, s, h in missing:
        print("   %s  字=%s  GBK=%s" % (p, s, h))
else:
    print("OK: 所有菜单用到的汉字都在字库中")

# menu.c 引用的 TXT_ 宏必须在 menu_text.h 中有定义。
# 漏定义时整个字符串不存在，上面的字库检查扫不到（编译器只报
# "use of undeclared identifier"，且只报第一个）。
menu_c = open(BASE + "/Core/Src/menu.c", encoding="utf-8", errors="replace").read()
menu_h = open(BASE + "/Core/Inc/menu_text.h", encoding="ascii", errors="replace").read()
used = set(re.findall(r"\bTXT_[A-Z_0-9]+\b", menu_c))
defined = set(re.findall(r"#define\s+(TXT_[A-Z_0-9]+)", menu_h))
undef = sorted(used - defined)
if undef:
    print("!! menu.c 引用了但 menu_text.h 未定义的宏:")
    for name in undef:
        print("   %s" % name)
else:
    print("OK: menu.c 引用的 TXT_ 宏都已定义")
