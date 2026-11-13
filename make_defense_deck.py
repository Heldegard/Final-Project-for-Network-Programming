# -*- coding: utf-8 -*-
"""Generate the LanKVM final-defense deck (18 slides, ~15 minutes)."""

from pptx import Presentation
from pptx.util import Inches, Pt
from pptx.dml.color import RGBColor
from pptx.enum.text import PP_ALIGN, MSO_ANCHOR
from pptx.enum.shapes import MSO_SHAPE
from pptx.oxml.ns import qn

FONT = "Microsoft YaHei"
CODE = "Consolas"

NAVY = RGBColor(0x0E, 0x1A, 0x2B)
INK = RGBColor(0x1B, 0x24, 0x33)
MUTED = RGBColor(0x5E, 0x6E, 0x7E)
ACCENT = RGBColor(0x0B, 0x6E, 0x8F)
ACCENT2 = RGBColor(0x00, 0xB4, 0xD8)
PANEL = RGBColor(0xF1, 0xF5, 0xF8)
PANEL2 = RGBColor(0xE4, 0xEF, 0xF5)
LINE = RGBColor(0xD5, 0xDF, 0xE6)
ARROW = RGBColor(0x9F, 0xC8, 0xDC)
WHITE = RGBColor(0xFF, 0xFF, 0xFF)
PALE = RGBColor(0x9F, 0xB6, 0xC8)

SW, SH = 13.333, 7.5


def set_font(run, name=FONT):
    run.font.name = name
    rPr = run._r.get_or_add_rPr()
    ea = rPr.find(qn("a:ea"))
    if ea is None:
        ea = rPr.makeelement(qn("a:ea"), {})
        rPr.append(ea)
    ea.set("typeface", name)


def add_text(slide, x, y, w, h, lines, size=18, bold=False, color=INK,
             align=PP_ALIGN.LEFT, font=FONT, anchor=MSO_ANCHOR.TOP,
             spacing=1.2, space_after=0):
    box = slide.shapes.add_textbox(Inches(x), Inches(y), Inches(w), Inches(h))
    tf = box.text_frame
    tf.word_wrap = True
    tf.margin_left = 0
    tf.margin_right = 0
    tf.margin_top = 0
    tf.margin_bottom = 0
    tf.vertical_anchor = anchor
    if isinstance(lines, str):
        lines = [lines]
    for i, line in enumerate(lines):
        p = tf.paragraphs[0] if i == 0 else tf.add_paragraph()
        p.alignment = align
        p.line_spacing = spacing
        p.space_after = Pt(space_after)
        if isinstance(line, tuple):
            txt = line[0]
            sz = line[1] if len(line) > 1 and line[1] else size
            col = line[2] if len(line) > 2 and line[2] else color
            bd = line[3] if len(line) > 3 else bold
        else:
            txt, sz, col, bd = line, size, color, bold
        run = p.add_run()
        run.text = txt
        run.font.size = Pt(sz)
        run.font.bold = bd
        run.font.color.rgb = col
        set_font(run, font)
    return box


def shape(slide, kind, x, y, w, h, fill=None, line=None, radius=None):
    sh = slide.shapes.add_shape(kind, Inches(x), Inches(y), Inches(w), Inches(h))
    if fill is None:
        sh.fill.background()
    else:
        sh.fill.solid()
        sh.fill.fore_color.rgb = fill
    if line is None:
        sh.line.fill.background()
    else:
        sh.line.color.rgb = line
        sh.line.width = Pt(1)
    sh.shadow.inherit = False
    if radius is not None and kind == MSO_SHAPE.ROUNDED_RECTANGLE:
        sh.adjustments[0] = radius
    return sh


def shape_text(sh, lines, size=14, bold=False, color=INK, align=PP_ALIGN.CENTER,
               font=FONT, spacing=1.05):
    tf = sh.text_frame
    tf.word_wrap = True
    tf.margin_left = Inches(0.06)
    tf.margin_right = Inches(0.06)
    tf.margin_top = Inches(0.03)
    tf.margin_bottom = Inches(0.03)
    tf.vertical_anchor = MSO_ANCHOR.MIDDLE
    if isinstance(lines, str):
        lines = [lines]
    for i, line in enumerate(lines):
        p = tf.paragraphs[0] if i == 0 else tf.add_paragraph()
        p.alignment = align
        p.line_spacing = spacing
        if isinstance(line, tuple):
            txt, sz, col = line[0], line[1], line[2]
            bd = line[3] if len(line) > 3 else bold
        else:
            txt, sz, col, bd = line, size, color, bold
        run = p.add_run()
        run.text = txt
        run.font.size = Pt(sz)
        run.font.bold = bd
        run.font.color.rgb = col
        set_font(run, font)


def content_slide(prs, section, title, page):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    add_text(slide, 0.72, 0.40, 8.0, 0.30, section, 11.5, True, ACCENT2)
    add_text(slide, 0.70, 0.68, 11.9, 0.80, title, 29, True, INK, spacing=1.0)
    bar = shape(slide, MSO_SHAPE.RECTANGLE, 0.72, 1.60, 0.95, 0.055, fill=ACCENT2)
    bar.line.fill.background()
    add_text(slide, 0.72, 7.06, 7.0, 0.28, "LanKVM · 网络编程课程期末项目", 9.5, False, MUTED)
    add_text(slide, 11.9, 7.06, 0.75, 0.28, str(page), 9.5, False, MUTED, PP_ALIGN.RIGHT)
    return slide


def table(slide, x, y, w, rows, widths, row_h=0.46, header=True, size=13,
          header_size=13, first_col_bold=False):
    n_rows, n_cols = len(rows), len(rows[0])
    shp = slide.shapes.add_table(n_rows, n_cols, Inches(x), Inches(y), Inches(w),
                                 Inches(row_h * n_rows))
    tbl = shp.table
    tbl.first_row = header
    tbl.horz_banding = False
    for i, cw in enumerate(widths):
        tbl.columns[i].width = Inches(cw)
    for r, row in enumerate(rows):
        tbl.rows[r].height = Inches(row_h)
        for c, value in enumerate(row):
            cell = tbl.cell(r, c)
            cell.margin_left = Inches(0.10)
            cell.margin_right = Inches(0.08)
            cell.margin_top = Inches(0.04)
            cell.margin_bottom = Inches(0.04)
            cell.vertical_anchor = MSO_ANCHOR.MIDDLE
            cell.fill.solid()
            if header and r == 0:
                cell.fill.fore_color.rgb = ACCENT
                color, bold, sz = WHITE, True, header_size
            else:
                cell.fill.fore_color.rgb = WHITE if r % 2 == 1 else PANEL
                color, bold, sz = INK, (first_col_bold and c == 0), size
            tf = cell.text_frame
            tf.word_wrap = True
            p = tf.paragraphs[0]
            p.alignment = PP_ALIGN.LEFT
            run = p.add_run()
            run.text = value
            run.font.size = Pt(sz)
            run.font.bold = bold
            run.font.color.rgb = color
            set_font(run, FONT)
    return shp


def notes(slide, text):
    slide.notes_slide.notes_text_frame.text = text


def build():
    prs = Presentation()
    prs.slide_width = Inches(SW)
    prs.slide_height = Inches(SH)
    prs.core_properties.title = "LanKVM 局域网键鼠共享系统 — 期末答辩"
    prs.core_properties.author = "LanKVM Team"

    # ---------------------------------------------------------------- S1 title
    s = prs.slides.add_slide(prs.slide_layouts[6])
    shape(s, MSO_SHAPE.RECTANGLE, 0, 0, SW, SH, fill=NAVY)
    add_text(s, 0.95, 1.50, 9.0, 1.15, "LanKVM", 60, True, WHITE, spacing=1.0)
    add_text(s, 0.98, 2.72, 9.0, 0.62, "局域网键鼠共享系统", 28, False, RGBColor(0xCF, 0xE3, 0xEE))
    shape(s, MSO_SHAPE.RECTANGLE, 1.00, 3.52, 1.45, 0.06, fill=ACCENT2)
    add_text(s, 0.98, 3.76, 10.8, 0.45,
             "自研二进制协议 · C++20 · 零第三方依赖 · 单文件部署", 16, False, PALE)
    add_text(s, 0.98, 6.42, 10.8, 0.35,
             "网络编程课程 期末项目    ·    第 __ 组    ·    2026.11", 12, False,
             RGBColor(0x7E, 0x93, 0xA6))
    notes(s, "[0:00-0:30] 开场\n"
             "各位老师好，我们做的是 LanKVM——一套局域网键鼠共享系统。\n"
             "一句话：一套键盘鼠标，可以控制局域网里另外一台电脑，剪贴板双向同步，"
             "文件拖过去就能传。\n"
             "接下来先看 100 秒实机演示，再讲我们是怎么做到的。")

    # ---------------------------------------------------------------- S2 KPI
    s = content_slide(prs, "01 / 项目速览", "一套键鼠，控制两台机器", 2)
    cards = [
        ("1 套", "键盘 + 鼠标", "接管局域网内多台 Windows"),
        ("XX ms", "端到端延迟 p50", "按下到对端生效，实测后填入"),
        ("5,500 行", "纯 C++ 实现", "零第三方运行时依赖"),
    ]
    for i, (big, label, sub) in enumerate(cards):
        x = 0.82 + i * 3.95
        shape(s, MSO_SHAPE.ROUNDED_RECTANGLE, x, 2.05, 3.70, 2.35, fill=PANEL, radius=0.06)
        shape(s, MSO_SHAPE.RECTANGLE, x, 2.05, 3.70, 0.07, fill=ACCENT2)
        add_text(s, x + 0.30, 2.40, 3.05, 0.80, big, 38, True, ACCENT, spacing=1.0)
        add_text(s, x + 0.30, 3.28, 3.05, 0.35, label, 15, True, INK)
        add_text(s, x + 0.30, 3.70, 3.05, 0.50, sub, 11.5, False, MUTED, spacing=1.15)
    add_text(s, 0.82, 4.85, 11.6, 1.30, [
        "52 项自动化测试全部通过   ·   6 次提交   ·   17 条双机手工用例",
        "从零实现：帧协议、挑战应答鉴权、AES-256-GCM、跨分辨率坐标映射、原子落盘",
    ], 13.5, False, MUTED, spacing=1.5)
    notes(s, "[0:30-0:40] 三个数字，十秒讲完，直接切视频。\n"
             "一套键鼠控两台机器；端到端延迟实测 XX 毫秒；五千五百行纯 C++，"
             "没有任何第三方运行时依赖。\n"
             "下面这段是我在宿舍用两台笔记本实机录的。")

    # ---------------------------------------------------------------- S3 video
    s = content_slide(prs, "02 / 演示", "100 秒，四件事", 3)
    shape(s, MSO_SHAPE.ROUNDED_RECTANGLE, 0.85, 1.92, 11.62, 3.48, fill=NAVY, radius=0.03)
    tri = shape(s, MSO_SHAPE.ISOSCELES_TRIANGLE, 6.48, 2.86, 0.85, 1.20, fill=ACCENT2)
    tri.rotation = 90
    add_text(s, 0.85, 4.62, 11.62, 0.45, "演示视频 · 100 秒（点击播放）", 15, False, WHITE,
             PP_ALIGN.CENTER)
    chips = ["跨屏接管", "剪贴板双向", "文件投递", "断线自愈"]
    for i, chip in enumerate(chips):
        x = 0.85 + i * 2.92
        c = shape(s, MSO_SHAPE.ROUNDED_RECTANGLE, x, 5.65, 2.72, 0.52, fill=PANEL2, radius=0.3)
        shape_text(c, chip, 13, True, ACCENT)
    notes(s, "[0:40-2:10] 播放视频并解说（不要沉默看片）\n"
             "· 左边主控、右边受控，现在我把鼠标推出屏幕右边缘——右边光标从左侧出现，键盘也跟着过去。\n"
             "· 接着剪贴板：主控复制、受控粘贴；截图也一样，图片是加密传过去的。\n"
             "· 然后是文件：拖进右下角投递框，注意对端先出现 .part，传完才改名。\n"
             "· 最后我把受控端直接关掉，主控六秒内发现并提示，重启后自动连回来。")

    # ---------------------------------------------------------------- S4 mapping
    s = content_slide(prs, "02 / 演示", "刚才那 100 秒，对应四个技术问题", 4)
    items = [
        ("跨屏接管", "采集与拦截", "Raw Input 管精度\n钩子管拦截"),
        ("剪贴板双向", "格式与加密", "文本 / DIB / DIBV5 / PNG\nAES-256-GCM"),
        ("文件投递", "分块与原子落盘", "64 KB 分块\n.part 传完才改名"),
        ("断线自愈", "心跳与重连", "1 s 心跳、6 s 看门狗\n断开补发 key-up"),
    ]
    for i, (top, mid, bottom) in enumerate(items):
        x = 0.82 + i * 2.98
        shape(s, MSO_SHAPE.ROUNDED_RECTANGLE, x, 2.05, 2.80, 0.52, fill=NAVY, radius=0.25)
        add_text(s, x, 2.12, 2.80, 0.38, top, 13, True, WHITE, PP_ALIGN.CENTER)
        shape(s, MSO_SHAPE.DOWN_ARROW, x + 1.31, 2.62, 0.18, 0.32, fill=ARROW)
        shape(s, MSO_SHAPE.ROUNDED_RECTANGLE, x, 3.00, 2.80, 0.50, fill=PANEL2, radius=0.2)
        add_text(s, x, 3.07, 2.80, 0.36, mid, 13, True, ACCENT, PP_ALIGN.CENTER)
        add_text(s, x + 0.10, 3.70, 2.60, 0.85, bottom, 11, False, MUTED,
                 PP_ALIGN.CENTER, spacing=1.35)
    add_text(s, 0.82, 5.05, 11.6, 0.45,
             "接下来按这个顺序讲：架构 → 协议 → 三个技术点 → 数据 → 踩过的坑。", 14, False, INK)
    notes(s, "[2:10-2:40] 建立结构\n"
             "刚才四件事，正好对应四个技术问题：怎么拿到输入、怎么保证坐标对齐、"
             "怎么传剪贴板和文件、断了怎么办。\n"
             "我先给整体架构，再逐个讲。")

    # ---------------------------------------------------------------- S5 topic
    s = content_slide(prs, "03 / 选题", "选题：一个没人做过的题", 5)
    add_text(s, 0.82, 2.05, 6.05, 3.6, [
        "· 课程示例集中在：聊天室、HTTP 服务、P2P、隧道、Raft",
        "· 我们选了「输入设备共享」：没有现成作业可参考",
        "· 它同时踩四个方向：输入子系统、网络协议、坐标与 DPI、剪贴板格式",
        "· 目标不是「能跑」，而是每一行为什么这么写都能解释",
    ], 14, False, INK, spacing=1.55, space_after=8)
    table(s, 7.25, 2.02, 5.15, [
        ["方向", "我们的做法"],
        ["采集精度", "设备级 Raw Input"],
        ["协议", "14 字节帧头 + 挑战应答"],
        ["坐标系", "跨分辨率、跨 DPI 归一化"],
        ["可靠性", "补发 key-up、原子落盘"],
    ], [1.55, 3.55], row_h=0.62, size=12, header_size=12.5)
    add_text(s, 7.25, 5.55, 5.15, 0.65, "难度对标：高级—挑战档，且与示例完全不重合。",
             12.5, True, ACCENT, spacing=1.3)
    notes(s, "[2:40-3:25] 选题与难度\n"
             "老师给的示例集中在应用层服务，我们刻意避开。\n"
             "输入设备共享这个题目，难点不在业务，而在四个底层方向同时要处理。")

    # ---------------------------------------------------------------- S6 arch
    s = content_slide(prs, "04 / 架构", "整体架构：两个角色，五条线程", 6)
    shape(s, MSO_SHAPE.ROUNDED_RECTANGLE, 0.82, 1.95, 4.35, 2.50, fill=PANEL, radius=0.05)
    add_text(s, 1.05, 2.10, 3.9, 0.35, "Master 主控端", 15, True, ACCENT)
    add_text(s, 1.05, 2.55, 3.9, 1.8, [
        "Raw Input 采集鼠标位移",
        "低级钩子拦截键鼠事件",
        "边缘判定 + 坐标映射",
        "投递框 + 剪贴板监听",
    ], 11.5, False, INK, spacing=1.65, space_after=2)
    shape(s, MSO_SHAPE.ROUNDED_RECTANGLE, 8.20, 1.95, 4.35, 2.50, fill=PANEL, radius=0.05)
    add_text(s, 8.43, 2.10, 3.9, 0.35, "Agent 受控端", 15, True, ACCENT)
    add_text(s, 8.43, 2.55, 3.9, 1.8, [
        "SendInput 注入（扫描码）",
        "剪贴板写入",
        "文件分块落盘",
        "心跳应答 + 断线释放按键",
    ], 11.5, False, INK, spacing=1.65, space_after=2)
    shape(s, MSO_SHAPE.RIGHT_ARROW, 5.42, 3.06, 2.55, 0.42, fill=ACCENT2)
    add_text(s, 5.35, 2.62, 2.70, 0.30, "TCP :24800", 10.5, True, MUTED, PP_ALIGN.CENTER)
    add_text(s, 5.35, 3.55, 2.70, 0.30, "14 字节帧 · HMAC 鉴权", 9.5, False, MUTED,
             PP_ALIGN.CENTER)
    for i, t in enumerate(["UI 线程", "发送线程", "接收线程", "心跳线程", "看门狗"]):
        x = 0.82 + i * 2.40
        c = shape(s, MSO_SHAPE.ROUNDED_RECTANGLE, x, 5.05, 1.95, 0.50, fill=PANEL2, radius=0.3)
        shape_text(c, t, 11.5, True, ACCENT)
    notes(s, "[3:25-4:15] 架构\n"
             "两个角色、五条线程。主控负责采集和判定，受控负责注入和落盘，中间一条 TCP 长连接。\n"
             "线程分开的原因是：钩子回调里绝对不能做网络 IO，否则系统输入会卡。")

    # ---------------------------------------------------------------- S7 protocol
    s = content_slide(prs, "04 / 架构", "协议：14 字节帧头 + 挑战应答", 7)
    segs = [("magic", 2), ("ver", 1), ("type", 1), ("flags", 2), ("seq", 4), ("len", 4)]
    unit = 11.6 / 14.0
    x = 0.85
    for i, (name, width) in enumerate(segs):
        w = width * unit
        sh = shape(s, MSO_SHAPE.RECTANGLE, x, 1.98, w, 0.78, fill=ACCENT if i % 2 == 0 else ACCENT2)
        shape_text(sh, [(name, 11, WHITE, True), ("%dB" % width, 9, WHITE, False)], spacing=1.0)
        x += w
    add_text(s, 0.85, 2.88, 11.6, 0.32, "固定 14 字节、小端序、单帧最大 4 MB；抓包可直读结构。",
             11.5, False, MUTED)
    steps = ["Hello", "Challenge", "Response", "Result", "ScreenInfo"]
    for i, t in enumerate(steps):
        x = 1.05 + i * 2.18
        ch = shape(s, MSO_SHAPE.CHEVRON, x, 3.85, 2.30, 0.78, fill=ACCENT if i % 2 == 0 else ACCENT2)
        shape_text(ch, t, 11, True, WHITE)
    add_text(s, 0.85, 3.48, 11.6, 0.30, "握手：每次连接随机 nonce，HMAC-SHA256 应答，失败即断开",
             11.5, False, MUTED)
    add_text(s, 0.85, 5.05, 11.6, 1.55, [
        "· 自己定协议，是为了可测：帧头固定，单元测试能逐字节断言",
        "· seq 用于排错，当前不做重放窗口，属于已知局限",
        "· type 决定载荷格式，新增消息不影响旧版本解析",
    ], 12.5, False, INK, spacing=1.5, space_after=4)
    notes(s, "[4:15-5:05] 协议\n"
             "所有消息共用 14 字节帧头，抓包能直接看出类型和长度。\n"
             "鉴权是挑战应答：对端给随机数，我用预共享密钥算 HMAC，密钥不上线。")

    # ---------------------------------------------------------------- S8 TCP
    s = content_slide(prs, "04 / 架构", "为什么用 TCP：宁可慢 1 ms，不能丢一个键", 8)
    table(s, 0.85, 2.02, 11.6, [
        ["维度", "TCP（我们的选择）", "UDP"],
        ["按键丢失", "不会，可靠有序", "可能丢，要自己做重传"],
        ["乱序", "不会", "要序号 + 重排缓冲"],
        ["延迟", "TCP_NODELAY + 4 ms 聚合", "更低，但复杂度高"],
        ["结论", "键鼠事件量小、绝对不能丢", "留给 v2 的鼠标快通道"],
    ], [2.10, 4.75, 4.75], row_h=0.66, size=12.5, header_size=13)
    shape(s, MSO_SHAPE.ROUNDED_RECTANGLE, 0.85, 5.55, 11.6, 0.85, fill=PANEL, radius=0.08)
    add_text(s, 1.15, 5.72, 11.0, 0.55,
             "实测 p50 XX ms、p95 XX ms。4 ms 聚合是主要成本，换来的是零丢键。",
             13.5, True, INK, anchor=MSO_ANCHOR.MIDDLE)
    notes(s, "[5:05-5:55] 为什么 TCP\n"
             "键鼠事件量很小，一分钟也就几万字节，但一个按键丢了，用户体验就是「卡了」。\n"
             "所以我们宁可多 1 毫秒，也要保证有序可靠。鼠标移动做了 4 毫秒聚合，避免高频小包。")

    # ---------------------------------------------------------------- S9 capture
    s = content_slide(prs, "05 / 技术点", "采集与拦截：两种手段，各管一件事", 9)
    table(s, 0.85, 2.02, 11.6, [
        ["手段", "负责", "为什么必须这样分工"],
        ["Raw Input", "鼠标位移精度", "光标被限制在屏幕边缘时，仍能拿到设备级位移"],
        ["低级钩子", "拦截本地事件", "只在远程模式安装，平时不给系统输入加延迟"],
    ], [2.10, 3.30, 6.20], row_h=0.78, size=12.5, header_size=13)
    flow = ["按键", "钩子拦截", "入队", "4 ms 聚合", "TCP 发送"]
    for i, t in enumerate(flow):
        x = 0.85 + i * 2.35
        b = shape(s, MSO_SHAPE.ROUNDED_RECTANGLE, x, 4.45, 1.95, 0.62, fill=PANEL2, radius=0.2)
        shape_text(b, t, 11.5, True, ACCENT)
        if i < len(flow) - 1:
            shape(s, MSO_SHAPE.RIGHT_ARROW, x + 1.98, 4.62, 0.34, 0.28, fill=LINE)
    add_text(s, 0.85, 5.45, 11.6, 1.0, [
        "钩子回调里只做一件事：把事件塞进队列，立刻返回。网络 IO 全部在发送线程，",
        "否则系统输入会被网络阻塞，表现为「鼠标一顿一顿」。",
    ], 12.5, False, MUTED, spacing=1.45)
    notes(s, "[5:55-6:55] 采集与拦截\n"
             "这两件事必须分开：Raw Input 只能看不能拦，钩子只能拦但拿不到精度。\n"
             "所以钩子管拦截、Raw Input 管位移，各司其职。")

    # ---------------------------------------------------------------- S10 coords
    s = content_slide(prs, "05 / 技术点", "坐标：不做像素累加，做归一化绝对值", 10)
    shape(s, MSO_SHAPE.ROUNDED_RECTANGLE, 0.85, 1.95, 11.6, 1.05, fill=PANEL, radius=0.08)
    add_text(s, 1.10, 2.10, 11.1, 0.80, [
        ("归一化   nx = (x − sx) × 65535 / (w − 1)", 14, INK, True),
        ("注    入   MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK", 14, INK, True),
    ], font=CODE, spacing=1.45)
    shape(s, MSO_SHAPE.ROUNDED_RECTANGLE, 0.85, 3.55, 4.55, 1.85, fill=WHITE, line=LINE, radius=0.04)
    add_text(s, 1.05, 3.70, 4.1, 0.3, "主控  2560 × 1440", 12, True, MUTED)
    add_text(s, 1.05, 4.15, 4.1, 1.0, ["鼠标推到右边缘", "取纵向比例 t"], 12, False, INK,
             spacing=1.5)
    shape(s, MSO_SHAPE.ROUNDED_RECTANGLE, 7.95, 3.55, 4.55, 1.85, fill=WHITE, line=LINE, radius=0.04)
    add_text(s, 8.15, 3.70, 4.1, 0.3, "受控  2560 × 1600", 12, True, MUTED)
    add_text(s, 8.15, 4.15, 4.1, 1.0, ["光标从左侧出现", "落点 = t × 1600"], 12, False, INK,
             spacing=1.5)
    shape(s, MSO_SHAPE.RIGHT_ARROW, 5.60, 4.30, 2.15, 0.42, fill=ACCENT2)
    add_text(s, 5.35, 3.90, 2.65, 0.30, "0 – 65535", 10, True, MUTED, PP_ALIGN.CENTER)
    add_text(s, 0.85, 5.75, 11.6, 0.55,
             "不做「累加像素差」：每次移动都换算成绝对值下发，误差不会随时间漂移。",
             12.5, False, MUTED)
    notes(s, "[6:55-7:55] 坐标映射\n"
             "如果按像素差累加，两台机器的分辨率和缩放不同，用久了光标就会越走越偏。\n"
             "我们的做法是每帧都换算成 0 到 65535 的绝对值，一次性下发，不累积误差。")

    # ---------------------------------------------------------------- S11 deep dive
    s = content_slide(prs, "05 / 技术点", "深挖：钩子会挡住 Raw Input 吗？", 11)
    panels = [
        ("问题", "低级钩子返回 1（吞掉事件）之后，\nRaw Input 还会不会继续上报位移？\n官方文档没有明确答案。"),
        ("实验", "远程模式下只统计 WM_INPUT 计数，\n用真实鼠标推动，排除注入事件干扰。\n结果：物理设备事件仍持续到达。"),
        ("兜底", "若某些机器确实被抑制，\n改用「钩子内取坐标 + 光标归中」，\n改动只在一个采集函数里。"),
    ]
    for i, (head, body) in enumerate(panels):
        x = 0.82 + i * 3.98
        shape(s, MSO_SHAPE.ROUNDED_RECTANGLE, x, 2.02, 3.78, 2.45, fill=PANEL, radius=0.05)
        add_text(s, x + 0.28, 2.22, 3.2, 0.35, head, 14, True, ACCENT)
        add_text(s, x + 0.28, 2.72, 3.2, 1.6, body, 11.5, False, INK, spacing=1.5)
    add_text(s, 0.82, 4.85, 11.6, 0.62,
             "结论：两条采集通路可以共存；这个结论是测出来的，不是假设的。",
             14, True, ACCENT)
    notes(s, "[7:55-8:55] 深挖（15 分钟版专属，体现工程判断）\n"
             "这是整个项目最不确定的一点：钩子把事件吞了，Raw Input 还看不看得到设备位移。\n"
             "我们设计了最小实验去验证，并且提前准备了替代方案，所以没有卡死在这里。")

    # ---------------------------------------------------------------- S12 clipboard/files
    s = content_slide(prs, "05 / 技术点", "剪贴板与文件：DIBV5 与 .part", 12)
    shape(s, MSO_SHAPE.ROUNDED_RECTANGLE, 0.82, 1.98, 5.62, 3.05, fill=PANEL, radius=0.05)
    add_text(s, 1.08, 2.15, 5.1, 0.35, "剪贴板", 16, True, ACCENT)
    add_text(s, 1.08, 2.65, 5.1, 2.2, [
        "· 格式：文本 / DIB / DIBV5 / PNG",
        "· 截图工具常只发布 DIBV5，必须先转 DIB",
        "· AES-256-GCM，密钥由预共享密钥派生",
        "· SHA-256 去环，避免两端来回弹",
    ], 11.5, False, INK, spacing=1.75, space_after=6)
    shape(s, MSO_SHAPE.ROUNDED_RECTANGLE, 6.80, 1.98, 5.62, 3.05, fill=PANEL, radius=0.05)
    add_text(s, 7.06, 2.15, 5.1, 0.35, "文件", 16, True, ACCENT)
    add_text(s, 7.06, 2.65, 5.1, 2.2, [
        "· 64 KB 分块，流式写盘不占内存",
        "· 先写 名字.part，校验通过才改名",
        "· 重名自动加 (1)，不覆盖原文件",
        "· 中断则删除残件并明确提示",
    ], 11.5, False, INK, spacing=1.75, space_after=6)
    notes(s, "[8:55-9:40] 剪贴板与文件\n"
             "这两个模块各踩了一个坑。截图工具只给 DIBV5，我们原来只认 DIB，读不到就静默放弃。\n"
             "文件更隐蔽：以前先建文件再传内容，传一半断了就留下一个同名空文件，看起来像没传过去。")

    # ---------------------------------------------------------------- S13 data
    s = content_slide(prs, "06 / 数据", "实测数据", 13)
    table(s, 0.90, 2.05, 11.5, [
        ["指标", "实测值", "测量方式"],
        ["端到端延迟 p50", "XX ms", "心跳 Echo 时间戳，取中位数"],
        ["端到端延迟 p95", "XX ms", "同一样本，取 95 分位"],
        ["文件传输吞吐", "XX MB/s", "500 MB 文件 / 总耗时"],
        ["断线重连", "XX s", "结束受控端进程到重新连上"],
        ["8 小时内存增长", "XX MB", "任务管理器前后对比"],
    ], [3.40, 2.60, 5.50], row_h=0.63, size=12.5, header_size=13)
    add_text(s, 0.90, 6.10, 11.5, 0.40,
             "说明：延迟为心跳往返（含对端处理），是保守估计。", 11, False, MUTED)
    notes(s, "[9:40-10:25] 数据\n"
             "这张表答辩前必须填满。填法在下一页，全部可复现。\n"
             "讲的时候停两秒，让老师看清数字，不要急着翻页。")

    # ---------------------------------------------------------------- S14 method
    s = content_slide(prs, "06 / 数据", "数据怎么来的：一条命令可复现", 14)
    steps = ["主控端 lankvm.ini 里把 log_level 改成 debug", "连接受控端，正常操作 2–3 分钟",
             "在 exe 目录执行下面这条命令"]
    for i, t in enumerate(steps):
        y = 2.02 + i * 0.62
        c = shape(s, MSO_SHAPE.OVAL, 0.88, y + 0.04, 0.34, 0.34, fill=ACCENT2)
        shape_text(c, str(i + 1), 11, True, WHITE)
        add_text(s, 1.38, y + 0.05, 10.8, 0.40, t, 13, False, INK)
    shape(s, MSO_SHAPE.ROUNDED_RECTANGLE, 0.88, 4.05, 11.5, 1.95, fill=NAVY, radius=0.04)
    add_text(s, 1.12, 4.20, 11.0, 1.65, [
        "$v = (Select-String .\\lankvm.log 'round trip latency: (\\d+) us').Matches |",
        "      % { [int]$_.Groups[1].Value } | Sort-Object",
        "$n = $v.Count",
        "\"p50={0:N1}ms  p95={1:N1}ms\" -f ($v[[int]($n*0.5)]/1000),",
        "        ($v[[math]::Min($n-1,[int]($n*0.95))]/1000)",
    ], 10.5, False, RGBColor(0xD6, 0xE4, 0xEE), font=CODE, spacing=1.35)
    add_text(s, 0.88, 6.20, 11.5, 0.40,
             "同一份日志也能算出丢包率与重连次数，测试方法写在 docs/TESTING.md。", 11, False, MUTED)
    notes(s, "[10:25-11:10] 方法\n"
             "数字不是估的：打开 debug 日志，正常用两分钟，一条命令就能复现。\n"
             "测试用例和脚本都在仓库里，老师可以当场验。")

    # ---------------------------------------------------------------- S15 bugs
    s = content_slide(prs, "07 / 踩坑", "三次翻车，三次修好", 15)
    rows = [
        ["现象", "根因", "修复"],
        ["截图剪贴板传不过去", "截图工具只发布 DIBV5", "补 DIBV5 转换 + PNG 兜底"],
        ["文件只有名字没有内容", "中断后留下 0 字节空壳", "先写 .part，成功才改名"],
        ["启动后毫无反应", "auto_connect=0 静默等待", "默认连接 + 显式告警"],
    ]
    table(s, 0.90, 2.05, 11.5, rows, [3.60, 3.90, 4.00], row_h=0.80, size=12.5, header_size=13)
    add_text(s, 0.90, 5.30, 11.5, 0.9,
             "共同点：都是「静默失败」——程序不报错，但功能不对。\n"
             "修法不只是改代码，还要让失败可见：日志、气泡、状态栏。", 13, False, INK,
             spacing=1.45)
    notes(s, "[11:10-12:40] 踩坑（讲成故事，不要念表）\n"
             "第一个：截图传不过去，查了半天发现剪贴板里有 DIBV5 没有 DIB。\n"
             "第二个：文件只有名字，因为我把文件先建了出来，断了就留个空壳。\n"
             "第三个最典型：配置里自动连接是关的，程序启动后一声不吭，看起来像坏的。")

    # ---------------------------------------------------------------- S16 limits
    s = content_slide(prs, "08 / 局限", "已知局限与下一步", 16)
    table(s, 0.90, 2.02, 11.5, [
        ["局限", "影响", "下一步"],
        ["仅支持 Windows", "跨平台不可用", "抽平台层，再做 macOS"],
        ["键鼠事件未加密", "同网段可被嗅探", "PSK 派生会话密钥，全链路加密"],
        ["UIPI 限制", "无法操作管理员窗口", "受控端提权或服务化"],
        ["多屏按虚拟桌面外边界", "逐屏切换不精确", "每显示器独立边缘配置"],
        ["非真正 OLE 拖拽", "需拖到投递框", "IDropSource + 对端 DoDragDrop"],
    ], [3.55, 3.75, 4.20], row_h=0.66, size=12, header_size=12.5)
    notes(s, "[12:40-13:30] 局限\n"
             "主动说局限，比被问出来好。每一条我都准备了下一步怎么做。\n"
             "如果时间紧，只念前两条。")

    # ---------------------------------------------------------------- S17 roles
    s = content_slide(prs, "09 / 团队", "分工与工程实践", 17)
    table(s, 0.90, 2.02, 7.30, [
        ["成员", "负责模块", "提交"],
        ["A（示例）", "协议 / 主控端采集", "__"],
        ["B（示例）", "受控端注入 / 文件接收", "__"],
        ["C（示例）", "界面 / 测试 / 文档", "__"],
    ], [2.10, 3.70, 1.50], row_h=0.62, size=12, header_size=12.5)
    stats = [
        ("52", "项自动化测试", "单元 + 环回 + 端到端"),
        ("6", "次提交", "每个模块一条主线"),
        ("3", "份文档", "README / TESTING / ARCHITECTURE"),
    ]
    for i, (num, label, sub) in enumerate(stats):
        y = 2.10 + i * 1.40
        add_text(s, 8.55, y, 1.10, 0.65, num, 30, True, ACCENT, spacing=1.0)
        add_text(s, 9.70, y + 0.02, 2.90, 0.35, label, 12.5, True, INK)
        add_text(s, 9.70, y + 0.40, 2.90, 0.35, sub, 10, False, MUTED)
    notes(s, "[13:30-14:20] 分工\n"
             "每个人负责一条主线，Git 记录可查。\n"
             "测试和文档也是工作量的一部分，不是附属品。")

    # ---------------------------------------------------------------- S18 closing
    s = prs.slides.add_slide(prs.slide_layouts[6])
    shape(s, MSO_SHAPE.RECTANGLE, 0, 0, SW, SH, fill=NAVY)
    add_text(s, 1.10, 2.05, 11.2, 1.9, [
        "我们做的不是又一个聊天室，",
        "而是一套能接管键盘鼠标的协议栈。",
    ], 30, True, WHITE, PP_ALIGN.CENTER, spacing=1.35)
    shape(s, MSO_SHAPE.RECTANGLE, 6.05, 4.25, 1.25, 0.06, fill=ACCENT2)
    add_text(s, 1.10, 4.55, 11.2, 0.55, "谢谢 · 欢迎提问", 18, False, PALE, PP_ALIGN.CENTER)
    add_text(s, 1.10, 6.55, 11.2, 0.35,
             "LanKVM  ·  github.com/Heldegard/Final-Project-for-Network-Programming",
             10.5, False, RGBColor(0x6E, 0x82, 0x95), PP_ALIGN.CENTER)
    notes(s, "[14:20-15:00] 收尾\n"
             "最后一句：我们做的不是又一个聊天室，而是一套能接管键盘鼠标的协议栈。\n"
             "留 10 秒给老师翻回目录页，然后进入提问。")

    prs.save("LanKVM-期末答辩.pptx")
    print("saved LanKVM-期末答辩.pptx with %d slides" % len(prs.slides))


if __name__ == "__main__":
    build()
