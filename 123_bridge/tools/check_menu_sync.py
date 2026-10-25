# -*- coding: utf-8 -*-
"""
check_menu_sync.py —— 定长数组一致性 / 板名自指 两项点检查

背景：两板 menu.c 已实质性分化（传感器表、执行器页、超声波/灯带板块都不同），
      原来「框架逐行一致」的前提不再成立（那个版本会报上千处误报）。
      本脚本改查 check_font.py 覆盖不到、又确实踩过的坑：

      ① 定长数组：menu.c 的 SENSOR_NAME/SENSOR_UNIT、edge_alarm.c 的 THRESHOLDS
         元素数 == sensor_types.h 物模型项数（不含 SENSOR_TYPE_COUNT 自身）
         —— 改物模型时最易漏，漏一处编译报 "excess elements"
      ② 板名自指：两板 menu.c 头注释的「本文件与 123_xxx」交叉引用写反
         —— 两板文件相互复制时最容易残留

字库 / TXT_ 宏的检查由 tools/check_font.py 负责，两者互补，别重复。

用法（在仓库任意位置）：  python 123_bridge/tools/check_menu_sync.py
退出码：0 = 全过，1 = 有问题
"""
import re
import sys
from pathlib import Path

# 以本脚本位置反推仓库根： <repo>/123_bridge/tools/check_menu_sync.py
ROOT = Path(__file__).resolve().parents[2]
BRIDGE = ROOT / "123_bridge"
TUNNEL = ROOT / "123_tunnel"


def read_text(path):
    if not path.exists():
        sys.exit("找不到文件：%s" % path)
    return path.read_text(encoding="utf-8", errors="replace")


def strip_comments(text):
    """去掉 C 注释，便于按逗号切数组元素"""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def array_elems(text, name):
    """抽出 `name[...] = { ... };` 里的顶层元素；找不到返回 None"""
    m = re.search(re.escape(name) + r"\s*\[[^\]]*\]\s*=\s*\{", text)
    if not m:
        return None
    i, depth, buf = m.end(), 1, []
    while i < len(text) and depth:
        ch = text[i]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                break
        buf.append(ch)
        i += 1
    return [e.strip() for e in strip_comments("".join(buf)).split(",") if e.strip()]


def sensor_count(board_dir):
    """物模型项数 = SensorType 枚举成员数（SENSOR_TYPE_COUNT 是哨兵，不算）"""
    types_h = read_text(board_dir / "Core" / "Inc" / "sensor_types.h")
    em = re.search(r"typedef enum\s*\{(.*?)\}\s*SensorType", types_h, flags=re.S)
    if not em:
        return None
    members = [e.strip() for e in strip_comments(em.group(1)).split(",") if e.strip()]
    return len([m for m in members if m != "SENSOR_TYPE_COUNT"])


def check_board(board_dir):
    """返回 (物模型项数, 问题列表)"""
    problems = []
    count = sensor_count(board_dir)
    if count is None:
        return None, ["sensor_types.h 解析不出 SensorType 枚举"]

    for src, arr in (("menu.c", "SENSOR_NAME"),
                     ("menu.c", "SENSOR_UNIT"),
                     ("edge_alarm.c", "THRESHOLDS")):
        elems = array_elems(read_text(board_dir / "Core" / "Src" / src), arr)
        if elems is None:
            problems.append("%s 里找不到 %s[]" % (src, arr))
        elif len(elems) != count:
            problems.append("%s 的 %s[] %d 项 ≠ 物模型 %d 项"
                            % (src, arr, len(elems), count))
    return count, problems


def check_self_reference():
    """两板 menu.c 头注释都写「本文件与 123_<同一块板>」——复制时最易写反"""
    problems = []
    for board, board_dir in (("bridge", BRIDGE), ("tunnel", TUNNEL)):
        menu_c = read_text(board_dir / "Core" / "Src" / "menu.c")
        if "本文件与 123_%s" % board in menu_c:
            other = "tunnel" if board == "bridge" else "bridge"
            problems.append("%s/Core/Src/menu.c 头注释自指（应写 123_%s）"
                            % (board_dir.name, other))
    return problems


def main():
    # 控制台按 UTF-8 输出，规避 Windows 默认 GBK 报错（必须在任何 print 之前）
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    failures = []

    for board, board_dir in (("桥板", BRIDGE), ("隧板", TUNNEL)):
        print("=" * 56)
        print("【%s】 %s" % (board, board_dir.name))
        count, problems = check_board(board_dir)
        if count is None:
            print("  ① 定长数组：解析失败  ✗")
        elif problems:
            print("  ① 物模型 %d 项，定长数组  ✗" % count)
        else:
            print("  ① 物模型 %d 项，SENSOR_NAME/UNIT + THRESHOLDS 三项一致  ✓" % count)
        failures.extend("%s：%s" % (board_dir.name, p) for p in problems)

    selfref = check_self_reference()
    if selfref:
        failures.extend(selfref)
        for p in selfref:
            print("  ✗ %s" % p)
    else:
        print("  ② 板名自指  ✓")

    print("=" * 56)
    if failures:
        print("FAIL - 共 %d 个问题：" % len(failures))
        for p in failures:
            print("   - %s" % p)
        print("（字库/TXT_ 宏请另跑 python <板>/tools/check_font.py）")
        return 1
    print("OK - 全部检查通过")
    return 0


if __name__ == "__main__":
    sys.exit(main())
