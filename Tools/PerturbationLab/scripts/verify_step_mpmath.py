#!/usr/bin/env python3
"""Scores GPU perturbation steps with tiny offsets (|d|/|Z| < 1e-25) against 80-digit mpmath.

__float128 cannot resolve g(Z+d) - g(Z) when |d|/|Z| ~ 1e-30 (it only has ~34 digits), so these
cases are exported by test_gpu_math and checked here instead.
"""
import csv, sys, math
import mpmath as mp

mp.mp.dps = 80

def g(w, p):
    x, y, z = w
    r = mp.sqrt(x*x + y*y + z*z)
    if r == 0:
        return (mp.mpf(0),) * 3
    rho = mp.sqrt(x*x + y*y)
    th = mp.atan2(rho, z)
    ph = mp.atan2(y, x) if rho > 0 else mp.mpf(0)
    rp = r**p
    return (rp*mp.sin(p*th)*mp.cos(p*ph), rp*mp.sin(p*th)*mp.sin(p*ph), rp*mp.cos(p*th))

path = sys.argv[1] if len(sys.argv) > 1 else "out/step_tiny_cases.csv"
worst = {}
count = {}
for row in csv.reader(open(path)):
    p = mp.mpf(row[0]); kind = row[1]
    Z = [mp.mpf(v) for v in row[2:5]]; D = [mp.mpf(v) for v in row[5:8]]; G = [mp.mpf(v) for v in row[8:11]]
    W = [Z[i] + D[i] for i in range(3)]
    ex = [a - b for a, b in zip(g(W, p), g(Z, p))]
    exl = mp.sqrt(sum(e*e for e in ex))
    if exl < mp.mpf('1e-35'):
        continue  # below float normal range, negligible next to dc
    R = mp.sqrt(sum(v*v for v in Z)); dl = mp.sqrt(sum(v*v for v in D))
    natural = max(exl, p * R**(p-1) * dl)
    err = float(mp.sqrt(sum((G[i]-ex[i])**2 for i in range(3))) / natural)
    key = (float(p), kind)
    worst[key] = max(worst.get(key, 0.0), err)
    count[key] = count.get(key, 0) + 1

fail = False
for key in sorted(worst):
    tol = 5e-6 if key[0] == int(key[0]) else 1e-4
    ok = worst[key] <= tol
    fail |= not ok
    print(f"  power {key[0]:5.2f} {key[1]:12s} n={count[key]:5d}  max err (natural scale) {worst[key]:.2e}  {'OK' if ok else 'FAIL'}")
print("ALL TINY-OFFSET STEPS OK" if not fail else "SOME TINY-OFFSET STEPS FAILED")
sys.exit(1 if fail else 0)
