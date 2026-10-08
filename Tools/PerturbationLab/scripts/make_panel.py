#!/usr/bin/env python3
"""Builds comparison panels (naive float | perturbation | ground truth), one per formula directory in
out/images/<formula>/*.ppm, written to <dst_dir>/comparison_<formula>.png."""
import sys, os, re
from PIL import Image, ImageDraw, ImageFont

src_root = sys.argv[1] if len(sys.argv) > 1 else "out/images"
dst_dir = sys.argv[2] if len(sys.argv) > 2 else "out"
cols = [("naive", "Plain float (old)"), ("perturbation", "GPU perturbation"), ("truth", "CPU double-double truth")]
try:
    font = ImageFont.truetype("DejaVuSans.ttf", 15)
except OSError:
    font = ImageFont.load_default()

def zoom_key(s):
    m = re.search(r"1e-?\d+", s)
    return float(m.group(0)) if m else 0.0

for formula in sorted(os.listdir(src_root)):
    src = os.path.join(src_root, formula)
    if not os.path.isdir(src):
        continue
    scenes = sorted({re.sub(r"_(perturbation|naive|truth)\.ppm$", "", f) for f in os.listdir(src) if f.endswith(".ppm")},
                    key=zoom_key, reverse=True)
    if not scenes:
        continue
    imgs = {(s, c): Image.open(os.path.join(src, f"{s}_{c}.ppm")) for s in scenes for c, _ in cols
            if os.path.exists(os.path.join(src, f"{s}_{c}.ppm"))}
    w, h = next(iter(imgs.values())).size
    scale = max(1, 360 // w)
    w2, h2 = w * scale, h * scale
    pad, label_w, head_h = 6, 110, 28
    panel = Image.new("RGB", (label_w + len(cols) * (w2 + pad), head_h + len(scenes) * (h2 + pad)), (24, 24, 28))
    d = ImageDraw.Draw(panel)
    d.text((8, 6), formula, fill=(255, 210, 120), font=font)
    for j, (_, title) in enumerate(cols):
        d.text((label_w + j * (w2 + pad) + 8, 6), title, fill=(230, 230, 230), font=font)
    for i, s in enumerate(scenes):
        y = head_h + i * (h2 + pad)
        d.text((8, y + h2 // 2 - 8), s, fill=(230, 230, 230), font=font)
        for j, (c, _) in enumerate(cols):
            if (s, c) in imgs:
                panel.paste(imgs[(s, c)].resize((w2, h2), Image.NEAREST), (label_w + j * (w2 + pad), y))
    dst = os.path.join(dst_dir, "comparison_" + formula.replace(" ", "") + ".png")
    panel.save(dst)
    print("wrote", dst, panel.size)
