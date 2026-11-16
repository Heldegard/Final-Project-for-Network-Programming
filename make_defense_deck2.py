# -*- coding: utf-8 -*-
"""Defense deck following the course-required outline (10 slides)."""

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
WHITE = RGBColor(0xFF, 0xFF, 0xFF)
PALE = RGBColor(0x9F, 0xB6, 0xC8)
CODE_FG = RGBColor(0xD6, 0xE4, 0xEE)

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
    add_text(slide, 0.72, 0.38, 8.0, 0.30, section, 11.5, True, ACCENT2)
    add_text(slide, 0.70, 0.66, 11.9, 0.80, title, 28, True, INK, spacing=1.0)
    shape(slide, MSO_SHAPE.RECTANGLE, 0.72, 1.56, 0.95, 0.055, fill=ACCENT2)
    add_text(slide, 0.72, 7.06, 7.0, 0.28, "LanKVM · 网络编程课程期末项目", 9.5, False, MUTED)
    add_text(slide, 11.9, 7.06, 0.75, 0.28, str(page), 9.5, False, MUTED, PP_ALIGN.RIGHT)
    return slide


def table(slide, x, y, w, rows, widths, row_h=0.46, header=True, size=12,
          header_size=12.5):
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
            cell.margin_left = Inches(0.09)
            cell.margin_right = Inches(0.07)
            cell.margin_top = Inches(0.03)
            cell.margin_bottom = Inches(0.03)
            cell.vertical_anchor = MSO_ANCHOR.MIDDLE
            cell.fill.solid()
            if header and r == 0:
                cell.fill.fore_color.rgb = ACCENT
                color, bold, sz = WHITE, True, header_size
            else:
                cell.fill.fore_color.rgb = WHITE if r % 2 == 1 else PANEL
                color, bold, sz = INK, False, size
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

    # ------------------------------------------------------------ 1 封面
    s = prs.slides.add_slide(prs.slide_layouts[6])
    shape(s, MSO_SHAPE.RECTANGLE, 0, 0, SW, SH, fill=NAVY)
    add_text(s, 0.95, 1.42, 9.0, 1.10, "LanKVM", 58, True, WHITE, spacing=1.0)
    add_text(s, 0.98, 2.58, 9.4, 0.60, "局域网键鼠共享与剪贴板同步系统", 26, False,
             RGBColor(0xCF, 0xE3, 0xEE))
    shape(s, MSO_SHAPE.RECTANGLE, 1.00, 3.34, 1.45, 0.06, fill=ACCENT2)
    add_text(s, 0.98, 3.58, 10.8, 0.45,
             "一套键鼠控制两台机器 · 剪贴板双向同步 · 文件投递", 15, False, PALE)
    info = [
        ("课程", "网络编程"),
        ("组号", "第 __ 组"),
        ("成员", "____________   ____________   ____________"),
        ("日期", "2026 年 11 月"),
    ]
    for i, (k, v) in enumerate(info):
        y = 4.62 + i * 0.42
        add_text(s, 1.00, y, 1.0, 0.32, k, 12, False, RGBColor(0x6E, 0x82, 0x95))
        add_text(s, 1.75, y, 9.0, 0.32, v, 12.5, False, RGBColor(0xC8, 0xD8, 0xE4))
    notes(s, "开场：各位老师好，我们是第 __ 组，项目叫 LanKVM——局域网键鼠共享系统。\n"
             "一句话：一套键盘鼠标控制两台电脑，剪贴板双向同步，文件拖过去就能传。\n"
             "下面按需求、实现、测试、演示、问题排查的顺序汇报。")

    # ------------------------------------------------------------ 2 分工
    s = content_slide(prs, "01 / 项目与分工", "三个人，三条主线", 2)
    add_text(s, 0.82, 1.78, 11.6, 0.40,
             "项目定位：从零实现一套可用的跨机输入接管系统，协议、采集、注入、剪贴板全部自研。",
             13, False, MUTED)
    table(s, 0.82, 2.30, 11.7, [
        ["成员", "负责模块", "具体产出", "提交"],
        ["____（组长）", "协议 / 主控端采集", "帧格式、握手鉴权、Raw Input 采集、边缘状态机", "__"],
        ["____", "受控端 / 文件传输", "SendInput 注入、按键释放、分块接收与 .part 落盘", "__"],
        ["____", "界面 / 测试 / 文档", "托盘与设置窗口、52 项测试、报告与演示视频", "__"],
    ], [2.0, 3.2, 5.4, 1.1], row_h=0.78, size=11.5)
    add_text(s, 0.82, 5.55, 11.7, 1.0, [
        "协作方式：Git 分支开发，每人一条主线；合并前互相 review；接口以 protocol.h 为准，",
        "任何协议改动都要同时改两端并补测试——这也是我们能持续重构的原因。",
    ], 12, False, MUTED, spacing=1.45)
    notes(s, "分工按模块划分，不是按文件划分：协议、受控端、界面三条线各自独立。\n"
             "Git 记录可查，每人的提交集中在自己的模块。")

    # ------------------------------------------------------------ 3 需求分析
    s = content_slide(prs, "02 / 需求分析", "要做什么，做到什么程度", 3)
    table(s, 0.82, 1.78, 6.05, [
        ["编号", "功能需求"],
        ["F1", "跨屏接管：鼠标推出边缘即控制对端"],
        ["F2", "键盘输入：含组合键、方向键、中文输入"],
        ["F3", "剪贴板：文本与截图双向同步"],
        ["F4", "文件投递：拖入即传，失败不留残件"],
        ["F5", "断线自愈：6 s 内发现，自动重连"],
        ["F6", "可配置：IP、端口、密钥、边缘方向"],
    ], [0.85, 5.20], row_h=0.55, size=11)
    table(s, 7.05, 1.78, 5.45, [
        ["类别", "指标"],
        ["延迟", "端到端 p50 ≤ 20 ms"],
        ["可靠", "按键零丢失、零乱序"],
        ["安全", "连接鉴权 + 剪贴板加密"],
        ["部署", "单文件、零第三方运行时"],
    ], [1.15, 4.30], row_h=0.55, size=11)
    add_text(s, 0.82, 5.78, 11.6, 1.1, [
        ("关键约束（决定了后面的设计）", 11.5, INK, True),
        "只面向可信局域网 → 键鼠事件不加密；只支持 Windows → 直接用 Win32 输入子系统；"
        "不能丢键 → 选 TCP 而不是 UDP。",
    ], 10.5, False, MUTED, spacing=1.35)
    notes(s, "需求分两块：左边是功能，右边是硬指标。\n"
             "注意最后三条约束，它们直接决定了技术选型，下一页会看到。")

    # ------------------------------------------------------------ 4 界面截图
    s = content_slide(prs, "03 / 界面与功能", "前端：主控端界面　后端：运行状态", 4)
    add_text(s, 0.75, 1.68, 3.0, 0.30, "设置窗口（真实截图）", 10.5, True, ACCENT)
    s.shapes.add_picture("docs/ui-settings.png", Inches(0.78), Inches(1.98), width=Inches(2.95))
    add_text(s, 0.78, 5.38, 2.95, 0.55,
             "角色、对端 IP、密钥、边缘方向、四个开关，保存即生效。", 9.5, False, MUTED,
             spacing=1.3)

    add_text(s, 4.15, 1.68, 4.4, 0.30, "文件投递框（真实截图）", 10.5, True, ACCENT)
    s.shapes.add_picture("docs/ui-dropzone.png", Inches(4.18), Inches(2.02), width=Inches(2.25))
    add_text(s, 6.62, 2.02, 5.9, 2.1, [
        ("四个功能点，全部可现场复现", 12.5, INK, True),
        "① 跨屏接管：推到边缘，对端光标出现",
        "② 键盘透传：组合键与中文输入正常",
        "③ 剪贴板：文本与截图双向同步",
        "④ 文件投递：拖入即传，完成后自动改名",
    ], 10.5, False, MUTED, spacing=1.5)

    shape(s, MSO_SHAPE.ROUNDED_RECTANGLE, 4.15, 4.30, 8.45, 2.25, fill=NAVY, radius=0.05)
    add_text(s, 4.38, 4.42, 8.0, 2.0, [
        ("后端：真实运行日志", 10.5, ACCENT2, True),
        ("[INFO] status: connected to 192.168.1.20", 9.5, CODE_FG, False),
        ("[INFO] clipboard sent: format=2 bytes=14745652 encrypted=1", 9.5, CODE_FG, False),
        ("[INFO] sent file 'demo.mp4' (209715200 bytes) sha256=4f2a9c...", 9.5, CODE_FG, False),
        ("[INFO] file saved: D:\\...\\demo.mp4 (209715200 bytes) sha256=4f2a9c...", 9.5,
         CODE_FG, False),
    ], font=CODE, spacing=1.5)
    notes(s, "左边是设置窗口的真实截图，右边是投递框。\n"
             "右下角是运行日志：连接、剪贴板、文件发送、文件落地，两端的 sha256 一致才算成功。")

    # ------------------------------------------------------------ 5 实施操作
    s = content_slide(prs, "04 / 实施操作", "部署只要四步", 5)
    steps = [
        ("准备", "拷贝 exe，首次运行生成配置与随机密钥", "lankvm.exe --version  →  LanKVM 0.2.0"),
        ("配置", "设置窗口选角色、填对端 IP、两端密钥一致", "lankvm.exe --netinfo  →  确认该用哪个 IP"),
        ("启动", "受控端先起，主控端后起（顺序反了会自动重试）", "lankvm.exe agent  /  lankvm.exe master"),
        ("验证", "先测连通，再看日志，最后测功能", "lankvm.exe --probe <ip> 24800  →  OK"),
    ]
    for i, (name, desc, cmd) in enumerate(steps):
        y = 1.80 + i * 1.22
        badge = shape(s, MSO_SHAPE.OVAL, 0.86, y + 0.06, 0.46, 0.46, fill=ACCENT2)
        shape_text(badge, str(i + 1), 14, True, WHITE)
        add_text(s, 1.52, y, 1.2, 0.35, name, 14, True, INK)
        add_text(s, 2.72, y, 5.5, 0.60, desc, 11, False, MUTED, spacing=1.25)
        shape(s, MSO_SHAPE.ROUNDED_RECTANGLE, 8.05, y - 0.04, 4.55, 0.52, fill=PANEL, radius=0.1)
        add_text(s, 8.20, y + 0.06, 4.3, 0.34, cmd, 9.5, False, ACCENT, font=CODE)
    add_text(s, 0.86, 6.55, 11.6, 0.40,
             "两端首次运行都要执行一次 scripts\\setup-firewall.cmd -SetNetworkPrivate（管理员）。",
             10.5, False, MUTED)
    notes(s, "四步：准备、配置、启动、验证。\n"
             "验证这一步最重要：先用 --probe 确认端口通，再看日志，最后才测功能，能省很多时间。")

    # ------------------------------------------------------------ 6 关键函数
    s = content_slide(prs, "05 / 关键函数", "四个函数撑起整条链路", 6)
    cards = [
        ("帧编码", "encode_frame()", "14 字节定长帧头，逐字段写小端序，不依赖结构体内存布局。",
         "单测：帧头长度、坏 magic 拒绝、分片重组"),
        ("坐标归一化", "normalize()", "像素坐标 → 0~65535 绝对值，跨分辨率不漂移。",
         "单测：0 / 中点 / 右端点 / 退化矩形"),
        ("按键注入", "Injector::key()", "用扫描码而非虚拟键，扩展键补 E0 标志，断线补发抬起。",
         "实测：方向键、右 Ctrl、小键盘、粘键场景"),
        ("文件校验", "receive_file_end()", "边收边算 SHA-256，与发送端比对，不符则删除文件。",
         "实测：200 MB 文件哈希一致，逐字节相同"),
    ]
    for i, (tag, sig, desc, proof) in enumerate(cards):
        x = 0.82 + (i % 2) * 6.05
        y = 1.80 + (i // 2) * 2.55
        shape(s, MSO_SHAPE.ROUNDED_RECTANGLE, x, y, 5.75, 2.25, fill=PANEL, radius=0.05)
        add_text(s, x + 0.28, y + 0.18, 4.0, 0.30, tag, 10.5, True, MUTED)
        add_text(s, x + 0.28, y + 0.48, 5.2, 0.35, sig, 14, True, ACCENT, font=CODE)
        add_text(s, x + 0.28, y + 0.94, 5.2, 0.75, desc, 10.5, False, INK, spacing=1.35)
        add_text(s, x + 0.28, y + 1.70, 5.2, 0.35, proof, 9.5, False, MUTED, spacing=1.2)
    notes(s, "四个函数分别对应链路的四段：编码、坐标、注入、校验。\n"
             "每个都写了单元测试，尤其是 normalize 的边界值和 encode_frame 的字节序。")

    # ------------------------------------------------------------ 7 测试
    s = content_slide(prs, "06 / 测试", "白盒、黑盒、集成，三层都跑过", 7)
    table(s, 0.82, 1.78, 11.7, [
        ["层次", "方法", "覆盖内容", "结果"],
        ["单元（白盒）", "直接调函数，断言字节与数值", "帧编解码、坐标映射、HMAC/AES 标准向量、配置读写", "30 项通过"],
        ["集成（黑盒）", "真实套接字，不关心内部实现", "握手鉴权、输入批转发、剪贴板加密传输", "17 项通过"],
        ["端到端（系统）", "真 Master + 真 Agent，200 MB 数据", "文件传输、SHA-256 校验、逐字节比对", "5 项通过"],
    ], [2.05, 3.55, 4.65, 1.45], row_h=0.86, size=10.5)
    add_text(s, 0.82, 5.42, 5.6, 1.5, [
        ("规模", 10.5, MUTED, True),
        ("52 项自动化断言，ctest 三个测试集全部 Passed", 10.5, INK, False),
        ("200 MB 传输 0.88 s，吞吐约 230 MB/s（回环）", 10.5, INK, False),
    ], spacing=1.4)
    add_text(s, 6.70, 5.42, 5.8, 1.5, [
        ("手工用例", 10.5, MUTED, True),
        ("17 条双机验收：跨屏、按键、粘键、断线、剪贴板、投递", 10.5, INK, False),
        ("含故意制造的网络中断与密钥错误", 10.5, INK, False),
    ], spacing=1.4)
    notes(s, "白盒测函数，黑盒测协议，端到端测整条链路。\n"
             "52 项断言全绿；手工 17 条覆盖异常场景，包括拔网线和错误密钥。")

    # ------------------------------------------------------------ 8 Demo
    s = content_slide(prs, "07 / 演示", "现场演示（视频 ≤ 5 分钟）", 8)
    shape(s, MSO_SHAPE.ROUNDED_RECTANGLE, 0.85, 1.80, 7.55, 4.55, fill=NAVY, radius=0.03)
    tri = shape(s, MSO_SHAPE.ISOSCELES_TRIANGLE, 4.15, 3.35, 0.85, 1.20, fill=ACCENT2)
    tri.rotation = 90
    add_text(s, 0.85, 5.65, 7.55, 0.45, "演示视频 · 点击播放", 14, False, WHITE, PP_ALIGN.CENTER)
    add_text(s, 8.75, 1.85, 3.9, 0.35, "视频里会看到", 12, True, ACCENT)
    add_text(s, 8.75, 2.28, 3.9, 3.9, [
        "① 鼠标推出右边缘，对端光标出现",
        "② 在对端打字、用快捷键",
        "③ 复制文本、截图，对端粘贴",
        "④ 拖视频进投递框，.part 变正式名",
        "⑤ 结束对端进程，6 s 内提示并自动重连",
    ], 11, False, INK, spacing=1.75)
    add_text(s, 0.85, 6.55, 11.6, 0.40,
             "视频为实机录制（两台 Windows，2560×1440 与 2560×1600 不同分辨率）。", 10, False, MUTED)
    notes(s, "接下来放一段 5 分钟以内的实机录像，边放边解说这五个点。\n"
             "重点让老师看到跨屏和文件投递，这两件事最直观。")

    # ------------------------------------------------------------ 9 问题
    s = content_slide(prs, "08 / 问题与解决", "四个坑，四次定位", 9)
    table(s, 0.80, 1.76, 11.75, [
        ["现象", "根因", "修复与验证"],
        ["设置窗口打开是空白的", "控件在 WM_CREATE 里创建，此时 this 还没存进窗口", "改为创建窗口后显式建控件，截图可验"],
        ["截图剪贴板传不过去", "截图工具只发布 CF_DIBV5，代码只认 CF_DIB", "补 DIBV5→DIB 转换 + PNG 兜底"],
        ["文件只有名字没有内容", "先建正式文件再传数据，中断留下空壳", "改 .part，成功才改名，失败删除"],
        ["启动后毫无反应", "auto_connect=0 时静默等待，无任何提示", "默认连接 + 状态栏与气泡告警"],
    ], [3.15, 4.55, 4.05], row_h=0.72, size=10)
    add_text(s, 0.80, 5.85, 11.7, 0.90,
             "共同点：都是「静默失败」——程序不报错，但结果不对。",
             13, True, ACCENT)
    add_text(s, 0.80, 6.28, 11.7, 0.55,
             "所以后来凡是可能失败的地方，都补了可见反馈：日志、状态栏、气泡、退出码。",
             11.5, False, MUTED)
    notes(s, "四个问题是按时间顺序讲的，都是真实发生过的。\n"
             "最后一个设置窗口空白是最近才发现的：控件创建时机不对，窗口是空壳。")

    # ------------------------------------------------------------ 10 结尾
    s = prs.slides.add_slide(prs.slide_layouts[6])
    shape(s, MSO_SHAPE.RECTANGLE, 0, 0, SW, SH, fill=NAVY)
    add_text(s, 1.10, 2.35, 11.2, 1.5, [
        "一套键鼠，两台机器，全部自研。",
    ], 32, True, WHITE, PP_ALIGN.CENTER, spacing=1.3)
    shape(s, MSO_SHAPE.RECTANGLE, 6.05, 3.95, 1.25, 0.06, fill=ACCENT2)
    add_text(s, 1.10, 4.25, 11.2, 0.55, "谢谢，欢迎提问", 18, False, PALE, PP_ALIGN.CENTER)
    add_text(s, 1.10, 6.55, 11.2, 0.35,
             "LanKVM  ·  github.com/Heldegard/Final-Project-for-Network-Programming",
             10.5, False, RGBColor(0x6E, 0x82, 0x95), PP_ALIGN.CENTER)
    notes(s, "收尾一句话，然后进入提问。")

    prs.save("LanKVM-期末答辩.pptx")
    print("saved with %d slides" % len(prs.slides))


if __name__ == "__main__":
    build()
