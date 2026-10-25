# -*- coding: utf-8 -*-
"""
check_decl_order.py —— 检查 C 源文件里「先使用、后声明」的问题

C 要求先声明后使用。这类错误只有真正编译才暴露；本工程约定不替用户编译，
故用静态检查代替。检查两类：

  1. 函数先调用后定义，且此前没有前向声明
     → error: call to undeclared function
  2. 自定义类型（typedef enum/struct）先用后定义
     → error: unknown type name

用法（在仓库任意位置）：  python 123_bridge/tools/check_decl_order.py

实现说明：不用复杂的单条正则（函数签名常跨多行，正则很容易写错）。
改用**逐字符扫描 + 括号配平**：
  · 先按 `static` 关键字切出候选片段，再检查其后是否紧跟一串标识符后接 `(`，
    并配平到对应的 `)`，据结尾符号（`{` / `;`）判定是定义还是声明。
  · 函数调用则在扫描时跳过所有「签名区间」后统计。
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# 需要检查的文件。新增源文件时请一并加进来 ——
# 曾经只查 menu.c，结果 st7735.c 的同类错误漏了过去。
TARGETS = ["menu.c", "st7735.c"]
BOARDS = ["bridge", "tunnel"]

IDENT = re.compile(r"[A-Za-z_]\w*")
RE_TYPEDEF_END = re.compile(r"\}[ \t]*(\w+)[ \t]*;")


def line_of(text, offset):
    return text.count("\n", 0, offset) + 1


def strip_comments(text):
    """去注释；用等长空白替换且**保留换行**，否则行号会整体偏移。"""
    def blank(m):
        return re.sub(r"[^\n]", " ", m.group(0))
    text = re.sub(r"/\*.*?\*/", blank, text, flags=re.S)
    text = re.sub(r"//[^\n]*", blank, text)
    return text


def scan_functions(text):
    """扫描所有 static 函数，返回 (defs, decls, spans)。
    defs/decls: {函数名: 行号}；spans: [(起, 止)] 为签名区间。"""
    defs, decls, spans = {}, {}, []
    for m in re.finditer(r"\bstatic\b", text):
        i = m.end()
        # 跳过返回类型：一串标识符/空格/星号，直到遇到 '(' 之前的最后一个名字
        # 做法：向后收集「标识符 + 空白/星号」，遇到 '(' 停下
        names = []
        while True:
            mm = IDENT.match(text, i)
            if mm:
                names.append(mm.group(0))
                i = mm.end()
                continue
            if i < len(text) and text[i] in " \t\n\r*":
                i += 1
                continue
            break
        if not names or i >= len(text) or text[i] != "(":
            continue
        fn = names[-1]                      # '(' 前最后一个标识符就是函数名

        # 从 '(' 开始配平括号
        depth, j = 0, i
        while j < len(text):
            if text[j] == "(":
                depth += 1
            elif text[j] == ")":
                depth -= 1
                if depth == 0:
                    break
            j += 1
        if j >= len(text):
            continue
        k = j + 1
        while k < len(text) and text[k] in " \t\n\r":
            k += 1
        if k >= len(text):
            continue

        if text[k] == "{":                  # 定义
            defs.setdefault(fn, line_of(text, m.start()))
            spans.append((m.start(), k))
        elif text[k] == ";":                # 前向声明
            decls.setdefault(fn, line_of(text, m.start()))
            spans.append((m.start(), k))
    return defs, decls, spans


def check(path):
    text = strip_comments(path.read_text(encoding="utf-8", errors="replace"))
    defs, decls, spans = scan_functions(text)

    types = {}
    for m in RE_TYPEDEF_END.finditer(text):
        types.setdefault(m.group(1), line_of(text, m.end()))

    def in_signature(pos):
        return any(a <= pos < b for a, b in spans)

    bad = []
    for m in re.finditer(r"\b(\w+)[ \t]*\(", text):
        if in_signature(m.start(1)):
            continue                        # 这是声明/定义本身
        fn = m.group(1)
        if fn not in defs:
            continue
        call_line = line_of(text, m.start(1))
        def_line = defs[fn]
        if call_line >= def_line:
            continue
        if fn in decls and decls[fn] < call_line:
            continue                        # 有更早的前向声明，合法
        bad.append((call_line, fn, def_line))

    tbad = []
    for tname, tline in types.items():
        for m in re.finditer(r"\b" + re.escape(tname) + r"\b", text):
            use_line = line_of(text, m.start())
            if use_line < tline:
                tbad.append((use_line, tname, tline))
                break                       # 每类型只报第一处，够定位

    return len(defs), bad, tbad


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    failed = False
    for board in BOARDS:
        for name in TARGETS:
            path = ROOT / ("123_" + board) / "Core" / "Src" / name
            label = "%s/%s" % (board, name)
            if not path.exists():
                print("[%s] 找不到 %s" % (label, path))
                failed = True
                continue
            n, bad, tbad = check(path)
            if bad:
                failed = True
                print("[%s] !! %d 处先调用后定义（会报 undeclared function）："
                      % (label, len(bad)))
                for call_line, fn, def_line in bad:
                    print("     第 %d 行调用 %s()，定义在第 %d 行，此前无前向声明"
                          % (call_line, fn, def_line))
            if tbad:
                failed = True
                print("[%s] !! %d 处类型先用后定义（会报 unknown type name）："
                      % (label, len(tbad)))
                for use_line, tname, def_line in tbad:
                    print("     第 %d 行用到类型 %s，定义在第 %d 行"
                          % (use_line, tname, def_line))
            if not bad and not tbad:
                print("[%s] OK：%d 个 static 函数，声明顺序正常" % (label, n))

    if failed:
        print("\nFAIL - 请在文件头部补上前向声明。")
        return 1
    print("\nOK - 所有被检查文件的声明顺序均正常。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
