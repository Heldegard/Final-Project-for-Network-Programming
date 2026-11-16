# -*- coding: utf-8 -*-
"""Render real source snippets as syntax-highlighted images for the deck."""

import re
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

FONT_PATH = "C:/Windows/Fonts/consola.ttf"
FONT_BOLD = "C:/Windows/Fonts/consolab.ttf"
SCALE = 2

BG = (16, 22, 31)
HEADER_BG = (23, 32, 45)
GUTTER = (110, 124, 140)
TEXT = (212, 212, 212)
KEYWORD = (86, 156, 214)
TYPE = (78, 201, 176)
STRING = (206, 145, 120)
COMMENT = (106, 153, 85)
NUMBER = (181, 206, 168)

KEYWORDS = {
    "void", "bool", "if", "return", "const", "static", "struct", "class", "inline",
    "for", "while", "else", "true", "false", "nullptr", "auto", "namespace",
    "break", "continue", "switch", "case", "default", "using", "public", "private",
    "template", "typename", "new", "delete", "sizeof", "override", "constexpr",
}
TYPES = {
    "uint8_t", "uint16_t", "uint32_t", "uint64_t", "int8_t", "int16_t", "int32_t",
    "int64_t", "size_t", "std", "INPUT", "WORD", "DWORD", "BOOL", "NTSTATUS",
    "Frame", "InputEvent", "ClipBlob", "Socket", "Sha256Stream", "Config",
}


def highlight(line):
    """Return [(text, color), ...] for one source line."""
    # split off a trailing comment (good enough: no // inside strings here)
    code, comment = line, ""
    idx = line.find("//")
    if idx >= 0:
        code, comment = line[:idx], line[idx:]

    parts = []
    for token in re.split(r'("[^"]*")', code):
        if not token:
            continue
        if token.startswith('"'):
            parts.append((token, STRING))
            continue
        for piece in re.split(r"([A-Za-z_][A-Za-z0-9_]*|\d+)", token):
            if not piece:
                continue
            if piece.isdigit():
                parts.append((piece, NUMBER))
            elif piece in KEYWORDS:
                parts.append((piece, KEYWORD))
            elif piece in TYPES:
                parts.append((piece, TYPE))
            else:
                parts.append((piece, TEXT))
    if comment:
        parts.append((comment, COMMENT))
    return parts


def extract(path, start_marker, end_marker=None, max_lines=40):
    lines = Path(path).read_text(encoding="utf-8").splitlines()
    start = next(i for i, l in enumerate(lines) if start_marker in l)
    end = start
    if end_marker is None:
        for i in range(start + 1, min(start + max_lines, len(lines))):
            if lines[i] == "}":
                end = i
                break
    else:
        for i in range(start + 1, min(start + max_lines, len(lines))):
            if end_marker in lines[i]:
                end = i
                break
    return lines[start:end + 1], start + 1


def render(snippet, first_line_no, file_label, out_path):
    font = ImageFont.truetype(FONT_PATH, 17 * SCALE)
    head_font = ImageFont.truetype(FONT_BOLD, 13 * SCALE)
    char_w = font.getlength("M")
    line_h = 26 * SCALE
    pad = 18 * SCALE
    gutter_w = char_w * 4

    width = int(max(font.getlength(l) for l in snippet) + gutter_w + pad * 2)
    width = max(width, int(head_font.getlength(file_label) + pad * 2))
    height = line_h * len(snippet) + pad * 2 + 34 * SCALE

    img = Image.new("RGB", (width, height), BG)
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, width, 34 * SCALE], fill=HEADER_BG)
    d.text((pad, 9 * SCALE), file_label, font=head_font, fill=(150, 170, 190))

    y = 34 * SCALE + pad
    for i, line in enumerate(snippet):
        num = str(first_line_no + i).rjust(3)
        d.text((pad, y), num, font=font, fill=GUTTER)
        x = pad + gutter_w
        for text, color in highlight(line):
            d.text((x, y), text, font=font, fill=color)
            x += font.getlength(text)
        y += line_h

    img.save(out_path)
    print("saved", out_path, img.size)


SNIPPETS = [
    ("src/common/protocol.cpp", "void encode_frame", None,
     "encode_frame()  protocol.cpp"),
    ("src/common/types.h", "inline uint16_t normalize", None,
     "normalize()  types.h"),
    ("src/common/inject.cpp", "void Injector::key", "send_one(in);",
     "Injector::key()  inject.cpp"),
    ("src/agent/agent.cpp", "it->second.out.close();", "files.erase(it);",
     "receive_file_end()  agent.cpp"),
]

for idx, (path, start, end, label) in enumerate(SNIPPETS, 1):
    lines, first = extract(path, start, end)
    render(lines, first, label, f"docs/code-{idx}.png")
