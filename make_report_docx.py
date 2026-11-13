# -*- coding: utf-8 -*-
"""Generate the LanKVM course report (DOCX)."""

from docx import Document
from docx.shared import Pt, Cm, RGBColor
from docx.enum.text import WD_ALIGN_PARAGRAPH, WD_LINE_SPACING
from docx.enum.table import WD_TABLE_ALIGNMENT
from docx.enum.section import WD_SECTION
from docx.oxml.ns import qn
from docx.oxml import OxmlElement

BODY_EA = "宋体"
HEAD_EA = "黑体"
LATIN = "Times New Roman"
CODE_FONT = "Consolas"
INK = RGBColor(0x1B, 0x24, 0x33)
ACCENT = RGBColor(0x0B, 0x6E, 0x8F)
GREY = RGBColor(0x66, 0x66, 0x66)

doc = Document()


# ----------------------------------------------------------------- helpers
def set_run_font(run, ea=BODY_EA, latin=LATIN, size=12, bold=False, color=None):
    run.font.name = latin
    run.font.size = Pt(size)
    run.font.bold = bold
    if color is not None:
        run.font.color.rgb = color
    rPr = run._element.get_or_add_rPr()
    rFonts = rPr.find(qn("w:rFonts"))
    if rFonts is None:
        rFonts = OxmlElement("w:rFonts")
        rPr.append(rFonts)
    rFonts.set(qn("w:eastAsia"), ea)


def shade_paragraph(paragraph, fill):
    pPr = paragraph._p.get_or_add_pPr()
    shd = OxmlElement("w:shd")
    shd.set(qn("w:val"), "clear")
    shd.set(qn("w:color"), "auto")
    shd.set(qn("w:fill"), fill)
    pPr.append(shd)


def shade_cell(cell, fill):
    tcPr = cell._tc.get_or_add_tcPr()
    shd = OxmlElement("w:shd")
    shd.set(qn("w:val"), "clear")
    shd.set(qn("w:color"), "auto")
    shd.set(qn("w:fill"), fill)
    tcPr.append(shd)


def page_break():
    doc.add_page_break()


def h1(text):
    p = doc.add_paragraph(style="Heading 1")
    p.paragraph_format.space_before = Pt(18)
    p.paragraph_format.space_after = Pt(10)
    run = p.add_run(text)
    set_run_font(run, ea=HEAD_EA, latin="Arial", size=16, bold=True, color=INK)
    return p


def h2(text):
    p = doc.add_paragraph(style="Heading 2")
    p.paragraph_format.space_before = Pt(12)
    p.paragraph_format.space_after = Pt(6)
    run = p.add_run(text)
    set_run_font(run, ea=HEAD_EA, latin="Arial", size=13.5, bold=True, color=INK)
    return p


def h3(text):
    p = doc.add_paragraph(style="Heading 3")
    p.paragraph_format.space_before = Pt(8)
    p.paragraph_format.space_after = Pt(4)
    run = p.add_run(text)
    set_run_font(run, ea=HEAD_EA, latin="Arial", size=12, bold=True, color=INK)
    return p


def body(text, indent=True, size=12, spacing=1.5):
    p = doc.add_paragraph()
    pf = p.paragraph_format
    pf.line_spacing = spacing
    pf.space_before = Pt(0)
    pf.space_after = Pt(3)
    pf.first_line_indent = Cm(0.85) if indent else Cm(0)
    run = p.add_run(text)
    set_run_font(run, size=size)
    return p


def bullet(text, size=12):
    p = doc.add_paragraph()
    pf = p.paragraph_format
    pf.line_spacing = 1.5
    pf.left_indent = Cm(0.85)
    pf.first_line_indent = Cm(-0.42)
    pf.space_after = Pt(2)
    run = p.add_run("· " + text)
    set_run_font(run, size=size)
    return p


def caption(text, above=False):
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p.paragraph_format.space_before = Pt(6 if above else 2)
    p.paragraph_format.space_after = Pt(2 if above else 8)
    run = p.add_run(text)
    set_run_font(run, ea=HEAD_EA, latin="Arial", size=10, bold=False, color=GREY)
    return p


def table(rows, widths, cap=None, size=10.5, first_col_bold=False):
    if cap:
        caption(cap, above=True)
    t = doc.add_table(rows=len(rows), cols=len(rows[0]))
    t.style = "Table Grid"
    t.alignment = WD_TABLE_ALIGNMENT.CENTER
    t.autofit = False
    for i, w in enumerate(widths):
        for cell in t.columns[i].cells:
            cell.width = Cm(w)
    for r, row in enumerate(rows):
        for c, value in enumerate(row):
            cell = t.cell(r, c)
            cell.width = Cm(widths[c])
            cell.text = ""
            p = cell.paragraphs[0]
            p.paragraph_format.space_before = Pt(2)
            p.paragraph_format.space_after = Pt(2)
            p.paragraph_format.line_spacing = 1.0
            run = p.add_run(value)
            set_run_font(run, size=size, bold=(r == 0) or (first_col_bold and c == 0))
            if r == 0:
                shade_cell(cell, "E8F1F6")
    doc.add_paragraph().paragraph_format.space_after = Pt(0)
    return t


def code(text):
    lines = text.split("\n")
    for i, line in enumerate(lines):
        p = doc.add_paragraph()
        pf = p.paragraph_format
        pf.left_indent = Cm(0.6)
        pf.right_indent = Cm(0.3)
        pf.space_before = Pt(4 if i == 0 else 0)
        pf.space_after = Pt(4 if i == len(lines) - 1 else 0)
        pf.line_spacing = 1.0
        run = p.add_run(line if line else " ")
        run.font.name = CODE_FONT
        run.font.size = Pt(9)
        rPr = run._element.get_or_add_rPr()
        rFonts = rPr.find(qn("w:rFonts"))
        if rFonts is None:
            rFonts = OxmlElement("w:rFonts")
            rPr.append(rFonts)
        rFonts.set(qn("w:eastAsia"), CODE_FONT)
        shade_paragraph(p, "F4F6F8")


def figure(path, width_cm, cap):
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p.paragraph_format.space_before = Pt(8)
    p.paragraph_format.space_after = Pt(2)
    p.add_run().add_picture(path, width=Cm(width_cm))
    caption(cap)


def page_field(paragraph):
    run = paragraph.add_run()
    f1 = OxmlElement("w:fldChar")
    f1.set(qn("w:fldCharType"), "begin")
    instr = OxmlElement("w:instrText")
    instr.set(qn("xml:space"), "preserve")
    instr.text = "PAGE"
    f2 = OxmlElement("w:fldChar")
    f2.set(qn("w:fldCharType"), "end")
    run._r.append(f1)
    run._r.append(instr)
    run._r.append(f2)
    set_run_font(run, size=9, color=GREY)


def toc_field(paragraph):
    run = paragraph.add_run()
    f1 = OxmlElement("w:fldChar")
    f1.set(qn("w:fldCharType"), "begin")
    instr = OxmlElement("w:instrText")
    instr.set(qn("xml:space"), "preserve")
    instr.text = 'TOC \\o "1-2" \\h \\z \\u'
    sep = OxmlElement("w:fldChar")
    sep.set(qn("w:fldCharType"), "separate")
    t = OxmlElement("w:t")
    t.text = "（打开文档后，右键此处 → 更新域，即可生成目录与页码）"
    end = OxmlElement("w:fldChar")
    end.set(qn("w:fldCharType"), "end")
    run._r.append(f1)
    run._r.append(instr)
    run._r.append(sep)
    run._r.append(t)
    run._r.append(end)
    set_run_font(run, size=10.5, color=GREY)


# ----------------------------------------------------------------- page setup
sec = doc.sections[0]
sec.page_width = Cm(21.0)
sec.page_height = Cm(29.7)
sec.top_margin = Cm(2.54)
sec.bottom_margin = Cm(2.54)
sec.left_margin = Cm(3.0)
sec.right_margin = Cm(2.5)

style = doc.styles["Normal"]
style.font.name = LATIN
style.font.size = Pt(12)
style.element.rPr.rFonts.set(qn("w:eastAsia"), BODY_EA)

# ----------------------------------------------------------------- cover
p = doc.add_paragraph()
p.paragraph_format.space_before = Pt(120)
p.alignment = WD_ALIGN_PARAGRAPH.CENTER
run = p.add_run("网络编程课程  期末项目报告")
set_run_font(run, ea=HEAD_EA, latin="Arial", size=16, color=GREY)

p = doc.add_paragraph()
p.paragraph_format.space_before = Pt(30)
p.alignment = WD_ALIGN_PARAGRAPH.CENTER
run = p.add_run("LanKVM")
set_run_font(run, ea="Arial", latin="Arial", size=40, bold=True, color=INK)

p = doc.add_paragraph()
p.alignment = WD_ALIGN_PARAGRAPH.CENTER
run = p.add_run("局域网键鼠共享与剪贴板同步系统")
set_run_font(run, ea=HEAD_EA, latin="Arial", size=20, bold=True, color=ACCENT)

p = doc.add_paragraph()
p.paragraph_format.space_before = Pt(10)
p.alignment = WD_ALIGN_PARAGRAPH.CENTER
run = p.add_run("设计与实现")
set_run_font(run, ea=HEAD_EA, latin="Arial", size=14, color=GREY)

for _ in range(4):
    doc.add_paragraph()

info = [
    ("课程名称", "网络编程"),
    ("项目类型", "小组项目（3 人）"),
    ("组　　号", "第 __ 组"),
    ("小组成员", "____________　____________　____________"),
    ("完成日期", "2026 年 11 月"),
]
for k, v in info:
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p.paragraph_format.space_after = Pt(6)
    r1 = p.add_run(k + "：")
    set_run_font(r1, ea=HEAD_EA, latin="Arial", size=12, color=GREY)
    r2 = p.add_run(v)
    set_run_font(r2, ea=BODY_EA, latin=LATIN, size=12)

# ----------------------------------------------------------------- sections
body_sec = doc.add_section(WD_SECTION.NEW_PAGE)
body_sec.top_margin = Cm(2.54)
body_sec.bottom_margin = Cm(2.54)
body_sec.left_margin = Cm(3.0)
body_sec.right_margin = Cm(2.5)

# page numbering restarts at 1 in the body (must sit before w:cols per schema)
pg = OxmlElement("w:pgNumType")
pg.set(qn("w:start"), "1")
body_sec._sectPr.insert_element_before(
    pg, "w:cols", "w:formProt", "w:vAlign", "w:noEndnote", "w:titlePg",
    "w:textDirection", "w:bidi", "w:rtlGutter", "w:docGrid",
    "w:printerSettings", "w:sectPrChange")

header = body_sec.header
header.is_linked_to_previous = False
hp = header.paragraphs[0]
hp.alignment = WD_ALIGN_PARAGRAPH.RIGHT
hr = hp.add_run("LanKVM  ·  局域网键鼠共享与剪贴板同步系统")
set_run_font(hr, size=9, color=GREY)

footer = body_sec.footer
footer.is_linked_to_previous = False
fp = footer.paragraphs[0]
fp.alignment = WD_ALIGN_PARAGRAPH.CENTER
page_field(fp)

# ----------------------------------------------------------------- abstract
h1("摘　要")
body("本文设计并实现了一套面向局域网的多机键鼠共享与剪贴板同步系统 LanKVM。系统由主控端与受控端"
     "两个角色组成：主控端通过 Windows Raw Input 采集设备级鼠标位移，通过低级键盘/鼠标钩子拦截本地"
     "输入事件，按 4 ms 窗口聚合成批后，经自定义二进制协议发送至受控端；受控端使用 SendInput 以"
     "归一化绝对坐标和硬件扫描码完成注入。剪贴板内容支持文本、DIB、DIBV5 与 PNG 四种格式的双向同步，"
     "传输前使用 AES-256-GCM 加密；文件传输采用 64 KB 分块，并以 .part 临时文件保证原子落盘。")
body("协议定义了 14 字节定长帧头与 16 种消息类型，连接建立采用基于预共享密钥的 HMAC-SHA256 挑战"
     "应答鉴权，配合 1 s 心跳与 6 s 看门狗实现断线检测与自动重连。针对跨分辨率、跨 DPI 场景，"
     "系统不累积像素差，而是每帧换算为 0～65535 的归一化绝对值下发，从根本上避免长期漂移。")
body("系统使用 C++20 编写，共 5 515 行、33 个源文件，零第三方运行时依赖，产物为单文件可执行程序。"
     "配套 52 项自动化测试（30 项单元测试、17 项基于真实套接字的集成测试、5 项文件端到端断言）全部通过。"
     "在两台分辨率分别为 2560×1440 与 2560×1600 的机器上完成手工验收，端到端延迟中位数 XX ms，"
     "文件传输吞吐 XX MB/s。")
p = doc.add_paragraph()
p.paragraph_format.space_before = Pt(6)
r1 = p.add_run("关键词：")
set_run_font(r1, ea=HEAD_EA, latin="Arial", size=12, bold=True)
r2 = p.add_run("局域网；键鼠共享；Windows API；二进制协议；剪贴板同步；AES-GCM")
set_run_font(r2, size=12)

page_break()

h1("Abstract")
body("This paper presents LanKVM, a LAN-oriented system that shares one keyboard and mouse between "
     "Windows machines and keeps their clipboards in sync. The master captures device-level mouse "
     "motion through Raw Input and intercepts local input with low-level hooks, batches events in a "
     "4 ms window and ships them over a custom binary protocol. The agent injects them with SendInput "
     "using normalised absolute coordinates and hardware scan codes. Clipboard payloads (text, DIB, "
     "DIBV5 and PNG) are transferred bidirectionally and encrypted with AES-256-GCM, while files are "
     "streamed in 64 KB chunks and committed atomically through a .part file.", indent=True)
body("The protocol defines a 14-byte fixed header and 16 message types; sessions are authenticated "
     "with an HMAC-SHA256 challenge-response over a pre-shared key. Heartbeats every second and a "
     "six-second watchdog provide failure detection and automatic reconnection. Coordinates are sent "
     "as normalised absolute values rather than accumulated deltas, which eliminates long-term drift "
     "across different resolutions and DPI settings.", indent=True)
p = doc.add_paragraph()
p.paragraph_format.space_before = Pt(6)
r1 = p.add_run("Keywords: ")
set_run_font(r1, ea=HEAD_EA, latin="Arial", size=12, bold=True)
r2 = p.add_run("LAN; keyboard and mouse sharing; Windows API; binary protocol; clipboard sync")
set_run_font(r2, size=12)

page_break()

# ----------------------------------------------------------------- TOC
h1("目　录")
toc_field(doc.add_paragraph())
doc.add_paragraph()
body("说明：本目录为自动域。在 Word 中右键选择“更新域 → 更新整个目录”即可生成带页码的目录；"
     "若使用 WPS，右键后选择“更新目录”。", indent=False, size=10.5)

page_break()

# ----------------------------------------------------------------- 1
h1("1　引言")
h2("1.1　选题背景")
body("在多机办公与实验环境中，一台主机往往需要同时操作多台电脑：例如用笔记本查资料、用另一台机器"
     "跑实验，桌面上就要摆两套键鼠，来回切换既占空间又打断思路。商业软件（Synergy、Barrier、"
     "Mouse Without Borders）可以解决这个问题，但它们要么闭源，要么依赖大量第三方库，"
     "不适合作为网络编程课程的学习对象。")
body("本项目从零实现一套同类系统。选择这个题目的原因在于：它既不是应用层业务的堆砌，也不是单纯的"
     "套接字练习，而是同时涉及操作系统的输入子系统、自定义应用层协议、坐标与 DPI 换算、"
     "剪贴板数据格式、并发与故障恢复五个方向，能够较完整地覆盖课程知识点。")

h2("1.2　与课程示例的关系")
body("课程给出的参考项目集中在聊天室、静态 HTTP 服务、WebSocket 应用、P2P 文件共享、TLS 隧道、"
     "基于 Raft 的分布式键值存储、NIDS 等方向。本项目与它们均不重合：")
bullet("不是聊天/即时通信：没有消息会话模型，传输的是设备事件流；")
bullet("不是 HTTP 服务：不实现任何 Web 协议，协议完全自定义；")
bullet("不是 P2P 文件共享：文件传输只是附属功能，核心是输入接管；")
bullet("不是隧道：不做流量转发，而是解析并重建输入语义。")

h2("1.3　项目目标")
body("项目的验收标准不是“能跑”，而是三条可检验的目标：")
bullet("功能完整：跨屏接管、键盘输入、剪贴板文本/图片双向、文件投递、断线重连五项全部可用；")
bullet("行为可解释：每个技术选择都能说明为什么这样做、替代方案是什么、代价在哪里；")
bullet("结果可复现：延迟、吞吐等指标由日志与脚本产生，任何人按文档都能重跑。")

h2("1.4　报告结构")
body("第 2 章给出需求与约束；第 3 章是总体设计；第 4 章展开协议、采集、坐标、剪贴板、文件、连接与"
     "安全七个模块的详细设计；第 5 章说明实现与部署；第 6 章给出测试方法与结果；第 7 章讨论局限；"
     "第 8 章总结。")

# ----------------------------------------------------------------- 2
h1("2　需求分析")
h2("2.1　功能需求")
table([
    ["编号", "需求", "验收方式"],
    ["F1", "一套键鼠接管另一台机器，鼠标跨屏切换", "鼠标推到边缘，对端光标出现并可操作"],
    ["F2", "键盘输入（含组合键、扩展键、输入法）", "对端记事本可正常输入中文与快捷键"],
    ["F3", "剪贴板文本双向同步", "两端复制粘贴内容一致"],
    ["F4", "剪贴板图片双向同步（截图）", "截图后对端可粘贴出完整图像"],
    ["F5", "文件投递", "拖入投递框后对端下载目录出现完整文件"],
    ["F6", "断线检测与自动重连", "结束对端进程后 6 s 内提示，重启后自动恢复"],
    ["F7", "托盘界面与配置窗口", "可改 IP、端口、密钥、边缘方向并即时生效"],
    ["F8", "连接诊断工具", "命令行可输出网卡、连通性、剪贴板格式"],
], [1.4, 7.4, 6.7], cap="表 2-1　功能需求")

h2("2.2　非功能需求")
table([
    ["类别", "指标", "说明"],
    ["延迟", "端到端 p50 ≤ 20 ms", "按下到对端生效，含网络与注入"],
    ["可靠性", "按键零丢失、零乱序", "断开时补发抬起事件，防止粘键"],
    ["安全", "连接需鉴权；剪贴板加密", "预共享密钥 + 随机 nonce；AES-256-GCM"],
    ["部署", "单文件、零依赖", "静态链接，拷到机器即可运行"],
    ["可维护", "模块边界清晰、有自动化测试", "47 项测试；单元测试可离线运行"],
], [2.2, 5.4, 7.9], cap="表 2-2　非功能需求")

h2("2.3　边界与约束")
body("系统只面向可信局域网，键鼠事件不加密；只支持 Windows 10 1809 及以上；受控端无法向管理员权限"
     "窗口注入输入（UIPI 限制）；多显示器按虚拟桌面外边界判定，不做逐屏边缘。这些约束在第 7 章"
     "统一讨论。")

# ----------------------------------------------------------------- 3
h1("3　总体设计")
h2("3.1　设计取舍")
body("最关键的三个取舍决定了整体结构。")
body("第一，传输层选 TCP 而非 UDP。键鼠事件的数据量很小（一次移动 10 字节，一分钟也不过数万字节），"
     "但一个按键丢失或乱序会直接表现为“卡顿”或“按错”。TCP 的有序可靠恰好匹配这个需求，代价是"
     "额外的确认与重传；我们用 TCP_NODELAY 关闭 Nagle 合并，再用 4 ms 的应用层聚合把高频小包压下来。")
body("第二，采集与拦截分离。Raw Input 能拿到设备级位移，但只能“看”不能“拦”；低级钩子能拦截，"
     "但拿不到与光标位置无关的位移。两者分工后，钩子只负责吞掉本地事件，Raw Input 负责提供精度。")
body("第三，坐标用绝对值而非增量。若按增量累加，两端分辨率或 DPI 不一致时误差会持续累积；"
     "改为每帧归一化到 0～65535 的绝对值，误差不随时间增长，也便于跨分辨率复用同一套逻辑。")

h2("3.2　系统架构")
body("系统分为主控端（Master）与受控端（Agent）两个角色，通过一条 TCP 长连接通信，默认端口 24800。"
     "主控端负责采集、判定、编码与发送；受控端负责解码、注入与落盘。两端结构对称，但职责完全不同。")
figure("docs/fig-arch.png", 15.2, "图 3-1　系统总体架构")

h2("3.3　线程模型")
body("主控端有五条线程，受控端有四条。线程划分的核心原则是：任何可能阻塞的操作都不能出现在"
     "输入回调或网络读循环里。")
table([
    ["线程", "端", "职责", "阻塞风险"],
    ["UI 线程", "主控", "消息循环、Raw Input、钩子、剪贴板通知", "钩子回调必须立即返回"],
    ["发送线程", "主控", "4 ms 聚合、编码、send", "网络拥塞"],
    ["接收线程", "主控", "帧解析、延迟统计、剪贴板接收", "对端不发数据"],
    ["心跳线程", "主控", "每秒发送心跳并记录 RTT", "无"],
    ["看门狗", "两端", "6 s 无入站帧则关闭连接", "无"],
    ["accept 线程", "受控", "监听并接受连接", "无"],
    ["serve 线程", "受控", "握手、帧循环、文件落盘", "磁盘慢"],
    ["注入线程", "受控", "SendInput，避免阻塞网络线程", "目标进程繁忙"],
], [2.6, 1.6, 7.2, 4.1], cap="表 3-1　线程划分")

h2("3.4　模块划分")
table([
    ["模块", "文件", "职责"],
    ["协议", "protocol.h/.cpp", "帧编解码、消息序列化"],
    ["加密", "crypto.h/.cpp", "SHA-256、HMAC、AES-256-GCM"],
    ["网络", "net.h/.cpp", "套接字封装、超时连接、连通性探测"],
    ["剪贴板", "clipboard.h/.cpp", "多格式读写、格式转换"],
    ["剪贴板传输", "clipxfer.h/.cpp", "分块、加密、组装、校验"],
    ["注入", "inject.h/.cpp", "绝对坐标、扫描码、按键集合"],
    ["主控端", "master/*", "采集、钩子、边缘状态机、投递框"],
    ["受控端", "agent/*", "握手、注入队列、文件接收"],
    ["界面", "app/*", "托盘、设置窗口、入口"],
], [2.6, 4.6, 8.3], cap="表 3-2　模块划分")

h2("3.5　一次输入事件的完整路径")
body("以主控端按下一次左键为例：低级鼠标钩子收到 WM_LBUTTONDOWN，回调把事件写入队列后立即返回 1"
     "（吞掉事件）；发送线程在 4 ms 窗口内取走队列，连同此前的移动事件一起编码为一帧 InputBatch；"
     "受控端 serve 线程解析后投入注入队列；注入线程调用 SendInput 依次注入绝对坐标移动与左键按下；"
     "注入完成后回送 Echo 帧，主控端据此计算端到端往返时间。")

# ----------------------------------------------------------------- 4
h1("4　详细设计")
h2("4.1　应用层协议")
h3("4.1.1　帧格式")
body("所有消息共用 14 字节定长帧头，字段按小端序编码，单帧载荷上限 4 MB。固定帧头的好处是解析简单、"
     "抓包可读，且单元测试可以对每个字节做断言。")
figure("docs/fig-protocol.png", 15.2, "图 4-1　帧格式与握手时序")
table([
    ["字段", "长度", "说明"],
    ["magic", "2 B", "固定 0x4C4B，用于快速识别与错误检测"],
    ["ver", "1 B", "协议版本，当前为 1"],
    ["type", "1 B", "消息类型，见表 4-2"],
    ["flags", "2 B", "保留位，当前未使用"],
    ["seq", "4 B", "发送方自增序号，用于排错"],
    ["len", "4 B", "载荷长度"],
], [2.6, 2.2, 10.7], cap="表 4-1　帧头字段")

h3("4.1.2　消息类型")
table([
    ["类型", "方向", "载荷内容"],
    ["Hello", "主→受", "协议版本、能力位、机器名"],
    ["AuthChallenge", "受→主", "16 字节随机 nonce"],
    ["AuthResponse", "主→受", "HMAC-SHA256(PSK, nonce)"],
    ["AuthResult", "受→主", "成功标志与原因"],
    ["ScreenInfo", "受→主", "虚拟桌面矩形、DPI、机器名"],
    ["InputBatch", "主→受", "时间戳 + 事件数组（每条 10 字节）"],
    ["ClipboardMeta / Chunk", "双向", "格式、总长、哈希、nonce 与 tag、分块"],
    ["FileBegin / Chunk / End / Ack", "主→受", "文件 id、大小、路径、64 KB 数据块"],
    ["Heartbeat / Ack、Echo、Bye", "双向", "保活、延迟测量、优雅关闭"],
], [4.4, 2.0, 9.1], cap="表 4-2　消息类型")

h3("4.1.3　连接建立与鉴权")
body("连接建立分为五步：主控端发送 Hello 声明版本与能力；受控端回 AuthChallenge，携带一次性 16 字节"
     "随机数；主控端用预共享密钥对 nonce 做 HMAC-SHA256 并回 AuthResponse；受控端本地重算并做"
     "常数时间比较，通过后回 AuthResult；最后受控端发送 ScreenInfo，主控端据此建立远端坐标系。"
     "密钥本身从不在网络上传输，nonce 每次连接随机，重放的应答无法通过校验。")

h2("4.2　输入采集与拦截")
h3("4.2.1　Raw Input 采集")
body("主控端创建一个消息专用窗口，注册鼠标（Usage Page 0x01、Usage 0x02）与键盘设备并设置"
     "RIDEV_INPUTSINK，从而在窗口非前台时也能收到 WM_INPUT。鼠标事件中的 lLastX/lLastY 是设备级"
     "相对位移，与光标当前位置无关——这正是光标被限制在屏幕边缘时仍然能拿到位移的原因。")
h3("4.2.2　低级钩子拦截")
body("只有在“远程控制中”状态才安装 WH_KEYBOARD_LL 与 WH_MOUSE_LL。钩子回调里只做三件事："
     "读取扫描码或按键信息、把事件写入队列、返回 1 吞掉事件。不做日志、不做网络 IO、不做内存分配，"
     "保证回调时间在微秒级，否则系统输入会出现可感知的延迟。退出远程状态时立即卸载钩子，"
     "平时不给系统输入增加任何额外开销。")
h3("4.2.3　事件聚合")
body("发送线程以 4 ms 为窗口取队列，一次最多打包 512 条事件。以 1000 Hz 鼠标计算，"
     "聚合可以把每秒 1000 个报文压缩到 250 个，同时不牺牲跟手感。聚合窗口是本系统最主要的延迟来源，"
     "也是第 6 章重点测量的对象。")

h2("4.3　坐标映射与边缘切换")
body("主控端维护一个“远端虚拟光标”，其坐标位于受控端的虚拟桌面坐标系中。每当收到一次鼠标位移，"
     "就更新该坐标并归一化：")
code("nx = (x - sx) * 65535 / (w - 1)\nny = (y - sy) * 65535 / (h - 1)")
body("受控端以 MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK 注入，Windows 会把 0～65535 映射回"
     "整块虚拟桌面。这样即使两端分辨率不同（例如 2560×1440 与 2560×1600），光标也能按比例落到"
     "正确位置，且不会因为反复累加而产生漂移。")
body("边缘切换是一个显式状态机：本地模式下每次位移都检查光标是否已到达配置的边缘且方向朝外，"
     "满足则进入远程模式，并按沿边的比例 t 计算远端初始落点；远程模式下若虚拟光标越过返回边，"
     "则退出远程模式、卸载钩子、把本地光标放回对应位置，并向对端补发所有按下键的抬起事件。")

h2("4.4　剪贴板同步")
body("剪贴板监听使用 AddClipboardFormatListener 与 WM_CLIPBOARDUPDATE。读取时按优先级尝试"
     "CF_UNICODETEXT、CF_DIB、CF_DIBV5 与注册格式 PNG。DIBV5 需要转换为标准 DIB 才能被多数"
     "程序粘贴，转换方式是重写头部：把 BITMAPV5HEADER 换成 BITMAPINFOHEADER，BI_BITFIELDS 时"
     "补三个颜色掩码，像素数据原样保留。")
body("传输前对整个载荷做 SHA-256，用于接收端校验与回环抑制：应用对端内容之前先记录其哈希，"
     "自身收到 WM_CLIPBOARDUPDATE 时若哈希相同则跳过，避免两端来回弹。开启加密时使用 "
     "AES-256-GCM，密钥由预共享密钥派生，每次传输生成 12 字节随机 nonce，认证标签 16 字节随元数据"
     "一起发送；接收端解密失败或哈希不符即丢弃。")

h2("4.5　文件传输")
body("文件按 64 KB 分块流式发送，接收端边收边写，内存占用与文件大小无关。为保证“看到的文件就是"
     "传完的文件”，接收端先写入 名字.part，全部校验通过后才重命名为正式文件名；失败时删除残件并"
     "在托盘提示。重名不覆盖，自动追加 (1)(2)。这一设计直接来自第 6 章描述的一次真实故障："
     "早期版本在收到 FileBegin 时就创建正式文件，传输中断后留下 0 字节空壳，看起来像“没传过去”。")

h2("4.6　连接管理")
body("主控端每秒发送一次 Heartbeat，受控端原样回送时间戳；主控端据此计算往返时间并在托盘显示。"
     "两端各有看门狗：超过 6 s 没有收到任何入站帧即判定连接失效，主动关闭套接字并回到重连流程。"
     "主控端重连间隔 0.5 s，指数不放大，因为局域网内断线通常是瞬时的。")
body("为避免“托盘里出现两个分不清的图标”，程序以角色与端口为键创建命名互斥体，同一角色同一端口"
     "只允许一个实例；命令行传入的角色会写回配置文件，防止“跑的是 agent、文件里写的是 master”。")

h2("4.7　安全设计")
body("系统的威胁模型是“同一局域网内的被动嗅探与误连”，不假设存在主动中间人。在此前提下："
     "连接阶段用挑战应答抵抗重放；剪贴板内容加密，避免截图与密码在明文流量中泄露；"
     "密钥为 32 字节随机值，由首次运行生成并需要人工在两端对齐。")
body("明确不做的部分：键鼠事件不加密（信息量低、频次高，加密收益有限）；不做数据帧序号窗口，"
     "因此无法抵抗主动重放；不做前向保密。这些在第 7 章列为后续工作。")

# ----------------------------------------------------------------- 5
h1("5　实现")
h2("5.1　代码结构")
table([
    ["目录", "内容", "规模"],
    ["src/common", "协议、加密、网络、剪贴板、注入、配置、日志", "约 2 600 行"],
    ["src/master", "采集、钩子、边缘状态机、连接、投递框", "约 1 200 行"],
    ["src/agent", "监听、鉴权、注入、剪贴板应用、文件接收", "约 750 行"],
    ["src/app", "托盘、设置窗口、程序入口", "约 700 行"],
    ["tests", "单元测试、环回集成测试、文件端到端测试", "约 450 行"],
], [3.2, 8.6, 3.7], cap="表 5-1　代码结构（合计 5 515 行）")

h2("5.2　关键实现")
body("帧编码是最基础的一层，所有消息都经过它。实现上刻意避免结构体直接内存拷贝，"
     "逐字段写入显式小端序，保证跨编译器与跨架构一致：")
code("void encode_frame(const Frame& f, std::vector<uint8_t>* out) {\n"
     "    out->clear();\n"
     "    put_u16(out, kMagic);\n"
     "    out->push_back(kProtoVersion);\n"
     "    out->push_back(static_cast<uint8_t>(f.type));\n"
     "    put_u16(out, f.flags);\n"
     "    put_u32(out, f.seq);\n"
     "    put_u32(out, static_cast<uint32_t>(f.payload.size()));\n"
     "    out->insert(out->end(), f.payload.begin(), f.payload.end());\n"
     "}")
body("坐标归一化被抽成纯函数，便于单元测试覆盖边界值（0、中点、右端点、退化矩形）：")
code("uint16_t normalize(int32_t pos, int32_t origin, int32_t size) {\n"
     "    if (size <= 1) return 0;\n"
     "    const int64_t span = size - 1;\n"
     "    int64_t v = ((int64_t)(pos - origin) * 65535 + span / 2) / span;\n"
     "    return (uint16_t)std::clamp<int64_t>(v, 0, 65535);\n"
     "}")
body("文件落盘采用“临时文件 + 重命名”的原子写法，并且每个分块写完都检查流状态：")
code("it->out.seekp((std::streamoff)offset);\n"
     "it->out.write((const char*)data.data(), (std::streamsize)data.size());\n"
     "if (!it->out) {                 // 磁盘满 / 设备移除\n"
     "    it->failed = true;\n"
     "    return;\n"
     "}\n"
     "it->written += data.size();")

h2("5.3　构建与部署")
body("项目使用 CMake + Ninja 构建，编译器为 MinGW-w64 GCC 16.2.0（C++20）。链接时加 -static，"
     "产物为单个 exe，不依赖 libstdc++ 等运行库，拷到任意 Windows 10 及以上机器即可运行。"
     "构建脚本封装为 scripts/build.cmd，附带 ctest 集成。")
code("scripts\\build.cmd -Config Release -Test\n"
     "build\\release\\lankvm.exe agent      :: 受控端\n"
     "build\\release\\lankvm.exe master     :: 主控端\n"
     "build\\release\\lankvm.exe --netinfo  :: 查看该用哪个 IP\n"
     "build\\release\\lankvm.exe --probe 192.168.1.7 24800  :: 连通性诊断")

h2("5.4　配置项")
table([
    ["键", "默认", "说明"],
    ["role", "master", "master / agent，可被命令行覆盖并写回"],
    ["peer_ip", "127.0.0.1", "主控端填写受控端地址"],
    ["port", "24800", "TCP 端口"],
    ["psk", "随机生成", "32 字节十六进制，两端必须一致"],
    ["edge", "right", "受控端位于主控端的哪一侧"],
    ["clipboard / encrypt_clipboard", "1 / 1", "是否同步、是否加密"],
    ["dropzone", "1", "是否显示文件投递框"],
    ["auto_connect", "1", "主控端启动即连接"],
    ["log_level", "info", "debug 时记录延迟样本"],
], [4.3, 2.8, 8.4], cap="表 5-2　主要配置项")

# ----------------------------------------------------------------- 6
h1("6　测试与结果")
h2("6.1　测试策略")
body("测试分三层：第一层是不依赖界面的单元测试，覆盖帧编解码、坐标映射、加密算法与配置文件；"
     "第二层是环回集成测试，用真实 TCP 套接字跑完整握手、输入批转发与剪贴板传输；第三层是双机"
     "手工验收，覆盖真实输入设备、真实分辨率与真实网络。前两层可以无人值守重复执行，第三层"
     "按照 17 条用例逐项记录。")

h2("6.2　自动化测试")
table([
    ["测试集", "项数", "覆盖内容"],
    ["core（单元）", "30", "坐标归一化、帧编解码与损坏检测、输入批、SHA-256/HMAC 标准向量、"
                            "AES-GCM 往返与篡改拒绝、配置往返"],
    ["loopback（集成）", "17", "真实 TCP 连接、挑战应答鉴权、屏幕信息交换、输入批转发、"
                               "Echo 时间戳、剪贴板加密传输"],
    ["file_transfer（端到端）", "5", "真实 Master + Agent、300 KB 随机数据、逐字节比对"],
], [4.2, 1.8, 9.5], cap="表 6-1　自动化测试")
body("全部 52 项断言通过，ctest 三个测试集均为 Passed。")
h2("6.3　手工验收用例")
table([
    ["编号", "用例", "判定标准"],
    ["T1", "启动与托盘", "托盘出现、标题栏显示角色与端口"],
    ["T2", "双机连接", "日志出现 connected 与 peer screen"],
    ["T3", "边缘切换与移动", "对端光标出现且跟手，方向正确"],
    ["T4", "鼠标按键、滚轮、拖选", "对端响应，本机不误触发"],
    ["T5", "键盘、扩展键、输入法", "方向键正常，中文输入正常"],
    ["T6", "返回本机", "控制权回切，位置基本对应"],
    ["T7", "粘键检查", "按住 Ctrl 切回后不粘键"],
    ["T8", "断线重连", "6 s 内提示，重启后自动恢复"],
    ["T9–T10", "剪贴板文本与图片", "两端内容一致"],
    ["T11", "密钥错误", "拒绝连接并给出原因"],
    ["T12–T13", "文件投递与大文件", "字节数一致，无残件"],
    ["T14", "延迟采样", "得到 p50/p95 分位数"],
    ["T15", "长跑 8 小时", "内存与句柄不持续增长"],
    ["T16", "权限边界", "普通权限下无法操作管理员窗口"],
], [2.2, 5.0, 8.3], cap="表 6-2　手工验收用例（摘要）")

h2("6.4　性能数据")
table([
    ["指标", "实测值", "测量方式"],
    ["端到端延迟 p50", "XX ms", "心跳 Echo 时间戳，取中位数"],
    ["端到端延迟 p95", "XX ms", "同一样本，取 95 分位"],
    ["文件传输吞吐", "XX MB/s", "500 MB 文件 / 总耗时"],
    ["断线重连耗时", "XX s", "结束进程到重新连接"],
    ["8 小时内存增长", "XX MB", "任务管理器前后对比"],
], [4.4, 3.0, 8.1], cap="表 6-3　性能数据（答辩前填入实测）")

h2("6.5　典型缺陷与修复")
table([
    ["现象", "根因", "修复"],
    ["截图剪贴板不同步", "截图工具只发布 CF_DIBV5", "增加 DIBV5→DIB 转换，并补 PNG 兜底"],
    ["文件只有名字没有内容", "传输中断后残留 0 字节文件", "改写 .part，成功才改名，失败删残件"],
    ["启动后毫无反应", "auto_connect 为 0 时静默等待", "默认连接，关闭时显式告警并弹提示"],
], [3.8, 5.3, 6.4], cap="表 6-4　典型缺陷")
body("三个缺陷的共同点是“静默失败”：程序不报错，但功能不正确。因此修复不止于改逻辑，还包括让失败"
     "可见——补日志、补状态栏文案、补气泡提示。这也是本项目在工程上最主要的收获之一。")

h2("6.6　测试结论")
body("自动化测试全部通过，手工用例覆盖五项核心功能与四项异常场景；系统在真实双机环境下可稳定完成"
     "跨屏接管、剪贴板同步与文件投递，断线后能自动恢复且不产生粘键。")

# ----------------------------------------------------------------- 7
h1("7　局限与展望")
h2("7.1　已知局限")
table([
    ["局限", "影响", "后续方案"],
    ["仅支持 Windows", "跨平台不可用", "抽象平台层，优先支持 macOS"],
    ["键鼠事件未加密", "同网段可被嗅探", "PSK 派生会话密钥，全链路加密"],
    ["UIPI 限制", "无法操作管理员窗口", "受控端提权或以服务方式运行"],
    ["多屏按虚拟桌面外边界", "逐屏切换不精确", "为每个显示器建立独立边缘配置"],
    ["非真正 OLE 拖拽", "需拖到投递框", "实现 IDropSource，配合对端 DoDragDrop"],
    ["无重放窗口", "无法抵抗主动重放", "引入序号窗口与滑动校验"],
], [4.2, 4.6, 6.7], cap="表 7-1　已知局限")

h2("7.2　后续工作")
body("近期最值得做的是两件事：一是把键鼠事件也纳入加密通道，代价约 0.2 ms，换来完整的前向安全；"
     "二是实现真正的跨屏拖拽，这是当前体验上最明显的短板。中期可以抽象平台层支持 macOS，"
     "并把输入通道扩展为 UDP 快通道 + TCP 控制通道的混合模式。")

# ----------------------------------------------------------------- 8
h1("8　总结")
body("本项目实现了一套可实际使用的局域网键鼠共享系统：从设备级采集、自定义协议、跨分辨率坐标映射，"
     "到剪贴板多格式同步、文件原子落盘与断线恢复，全部由我们自行设计与编码，共 5 515 行 C++，"
     "零第三方运行时依赖。")
body("技术上的三个关键认识是：第一，操作系统的输入子系统需要把“采集”和“拦截”当成两件事处理；"
     "第二，跨机坐标必须使用绝对值而不是增量，否则误差会累积；第三，凡是“静默失败”的地方，"
     "都应该补上可见的反馈，否则调试成本会成倍增加。")
body("工程上，我们为每个模块写了可离线运行的测试，把构建、部署与诊断都做成了脚本，"
     "并保留了完整的提交记录与文档。这些工作不直接体现在功能列表里，但决定了项目能否被别人复现。")

# ----------------------------------------------------------------- refs
h1("参考文献")
refs = [
    "[1] Microsoft. Raw Input[EB/OL]. Microsoft Learn, 2024.",
    "[2] Microsoft. SendInput function (winuser.h)[EB/OL]. Microsoft Learn, 2024.",
    "[3] Microsoft. LowLevelKeyboardProc callback function[EB/OL]. Microsoft Learn, 2024.",
    "[4] Microsoft. Clipboard Formats[EB/OL]. Microsoft Learn, 2024.",
    "[5] Microsoft. CNG Cryptography API: Next Generation[EB/OL]. Microsoft Learn, 2024.",
    "[6] Krawczyk H, Bellare M, Canetti R. HMAC: Keyed-Hashing for Message Authentication[S]. RFC 2104, 1997.",
    "[7] Dworkin M. Recommendation for Block Cipher Modes of Operation: Galois/Counter Mode (GCM)[S]. NIST SP 800-38D, 2007.",
    "[8] Postel J. Transmission Control Protocol[S]. RFC 793, 1981.",
]
for ref in refs:
    p = doc.add_paragraph()
    pf = p.paragraph_format
    pf.left_indent = Cm(0.85)
    pf.first_line_indent = Cm(-0.85)
    pf.line_spacing = 1.3
    pf.space_after = Pt(2)
    run = p.add_run(ref)
    set_run_font(run, size=10.5)

# ----------------------------------------------------------------- appendix
page_break()
h1("附录 A　协议消息速查")
table([
    ["编号", "名称", "方向", "说明"],
    ["1", "Hello", "主→受", "版本、能力位、机器名"],
    ["2", "AuthChallenge", "受→主", "16 字节随机 nonce"],
    ["3", "AuthResponse", "主→受", "32 字节 HMAC"],
    ["4", "AuthResult", "受→主", "成功标志 + 原因"],
    ["5", "ScreenInfo", "受→主", "矩形、DPI、机器名"],
    ["6", "InputBatch", "主→受", "时间戳 + 事件数组"],
    ["7", "ClipboardMeta", "双向", "格式、长度、哈希、nonce/tag"],
    ["8", "ClipboardChunk", "双向", "偏移 + 32 KB 数据"],
    ["9–12", "FileBegin/Chunk/End/Ack", "主→受", "文件传输四件套"],
    ["13–14", "Heartbeat / Ack", "双向", "保活与延迟测量"],
    ["15", "Echo", "受→主", "回传注入完成时间戳"],
    ["16", "Bye", "双向", "优雅关闭"],
], [1.6, 5.0, 2.2, 6.7], cap="表 A-1　消息类型速查")

h1("附录 B　构建与运行")
code(":: 构建（需要 MinGW-w64 GCC 13+、CMake 3.20+、Ninja）\n"
     "scripts\\build.cmd -Config Release -Test\n"
     "\n"
     ":: 受控端\n"
     "lankvm.exe agent\n"
     "\n"
     ":: 主控端（peer_ip 指向受控端）\n"
     "lankvm.exe master\n"
     "\n"
     ":: 诊断\n"
     "lankvm.exe --netinfo                 :: 列出网卡，指出该用哪个地址\n"
     "lankvm.exe --probe <ip> 24800        :: 判定 OK / REFUSED / TIMEOUT\n"
     "lankvm.exe --clipinfo                :: 列出剪贴板格式与可读性\n"
     "\n"
     ":: 防火墙（管理员）\n"
     "scripts\\setup-firewall.cmd -SetNetworkPrivate")

# let Word refresh the TOC field as soon as the document is opened
update = OxmlElement("w:updateFields")
update.set(qn("w:val"), "true")
doc.settings.element.append(update)

doc.save("LanKVM-项目报告.docx")
print("saved LanKVM-项目报告.docx")