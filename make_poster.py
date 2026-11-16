# -*- coding: utf-8 -*-
"""Poster frame for the demo video (1280x720, matches the deck palette)."""

from PIL import Image, ImageDraw, ImageFont

W, H = 1280, 720
NAVY = (14, 26, 43)
CYAN = (0, 180, 216)
PALE = (159, 182, 200)
WHITE = (255, 255, 255)

img = Image.new("RGB", (W, H), NAVY)
d = ImageDraw.Draw(img)

# subtle grid for a screen-like feel
for x in range(0, W, 64):
    d.line([(x, 0), (x, H)], fill=(22, 38, 58), width=1)
for y in range(0, H, 64):
    d.line([(0, y), (W, y)], fill=(22, 38, 58), width=1)

# play button
cx, cy, r = W // 2, H // 2 - 10, 78
d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=CYAN, width=4)
d.polygon([(cx - 26, cy - 38), (cx - 26, cy + 38), (cx + 40, cy)], fill=CYAN)

try:
    font_big = ImageFont.truetype("C:/Windows/Fonts/msyhbd.ttc", 46)
    font_small = ImageFont.truetype("C:/Windows/Fonts/msyh.ttc", 26)
except OSError:
    font_big = ImageFont.load_default()
    font_small = font_big


def center(text, y, font, color):
    box = d.textbbox((0, 0), text, font=font)
    d.text(((W - (box[2] - box[0])) / 2, y), text, font=font, fill=color)


center("LanKVM 实机演示", 92, font_big, WHITE)
center("跨屏接管 · 剪贴板同步 · 文件投递 · 断线重连", 196, font_small, PALE)
center("2 分 49 秒", 486, font_small, PALE)

img.save("docs/video-poster.png")
print("saved docs/video-poster.png")
