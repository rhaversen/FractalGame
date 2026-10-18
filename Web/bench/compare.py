#!/usr/bin/env python3
"""Compares the images of two bench runs: mean colour difference and share of pixels off by more than 10%; writes PNGs."""
import json, sys, os
import numpy as np
from PIL import Image
a_dir, b_dir = sys.argv[1], sys.argv[2]
for name in json.load(open(os.path.join(b_dir, 'results.json'))):
    v = name['view']
    pa, pb = os.path.join(a_dir, v + '.json'), os.path.join(b_dir, v + '.json')
    B = json.load(open(pb)); b = np.array(B['px'], dtype=np.float32).reshape(B['h'], B['w'], 4)[:, :, :3]
    Image.fromarray(b.astype(np.uint8)).resize((B['w'] * 3, B['h'] * 3), Image.NEAREST).save(os.path.join(b_dir, v + '.png'))
    if not os.path.exists(pa):
        continue
    A = json.load(open(pa)); a = np.array(A['px'], dtype=np.float32).reshape(A['h'], A['w'], 4)[:, :, :3]
    d = np.abs(a - b).mean(axis=2) / 255
    print(f"{v:6s} mean colour difference {d.mean()*100:5.2f}%  pixels off by > 10%: {(d > 0.1).mean()*100:5.2f}%")
