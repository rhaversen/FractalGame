#!/usr/bin/env python3
"""Builds a comparison panel (naive float | perturbation | ground truth) from out/images/*.ppm."""
import sys, os, re
from PIL import Image, ImageDraw, ImageFont

src = sys.argv[1] if len(sys.argv) > 1 else "out/images"
dst = sys.argv[2] if len(sys.argv) > 2 else "out/comparison.png"
scenes = sorted({re.sub(r"_(perturbation|naive|truth)\.ppm$", "", f) for f in os.listdir(src) if f.endswith(".ppm")},
                key=lambda s: float(s[1:]), reverse=True)
cols = [("naive", "Plain float (old)"), ("perturbation", "GPU perturbation"), ("truth", "CPU double-double truth")]
imgs = {(s, c): Image.open(os.path.join(src, f"{s}_{c}.ppm")) for s in scenes for c, _ in cols if os.path.exists(os.path.join(src, f"{s}_{c}.ppm"))}
w, h = next(iter(imgs.values())).size
scale = max(1, 360 // w)
w2, h2 = w * scale, h * scale
pad, label_w, head_h = 6, 110, 28
panel = Image.new("RGB", (label_w + len(cols) * (w2 + pad), head_h + len(scenes) * (h2 + pad)), (24, 24, 28))
d = ImageDraw.Draw(panel)
try:
    font = ImageFont.truetype("DejaVuSans.ttf", 15)
except OSError:
    font = ImageFont.load_default()
for j, (_, title) in enumerate(cols):
    d.text((label_w + j * (w2 + pad) + 8, 6), title, fill=(230, 230, 230), font=font)
for i, s in enumerate(scenes):
    y = head_h + i * (h2 + pad)
    d.text((8, y + h2 // 2 - 8), "zoom " + s[1:], fill=(230, 230, 230), font=font)
    for j, (c, _) in enumerate(cols):
        if (s, c) in imgs:
            panel.paste(imgs[(s, c)].resize((w2, h2), Image.NEAREST), (label_w + j * (w2 + pad), y))
panel.save(dst)
print("wrote", dst, panel.size)
