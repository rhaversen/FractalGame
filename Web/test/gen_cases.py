import json, sys
from mpmath import mp, mpf, sqrt, floor
mp.dps = 70
def bulb(z, c, p):
    x, y, zz = z
    rho = sqrt(x*x + y*y); r = sqrt(rho*rho + zz*zz)
    if r == 0: return list(c)
    a = (zz/r, rho/r); b = (x/rho, y/rho) if rho > 0 else (mpf(1), mpf(0))
    def cpow(v, p):
        res = (mpf(1), mpf(0)); base = v; e = p
        while e:
            if e & 1: res = (res[0]*base[0]-res[1]*base[1], res[0]*base[1]+res[1]*base[0])
            base = (base[0]*base[0]-base[1]*base[1], 2*base[0]*base[1]); e >>= 1
        return res
    t = cpow(a, p); q = cpow(b, p); rp = r**p
    return [rp*t[1]*q[0] + c[0], rp*t[1]*q[1] + c[1], rp*t[0] + c[2]]
def dd(x):
    h = float(x); l = float(x - mpf(h)); return [h, l]
cases = []
for p, c in [(8, ('0.6000000000000000123456789', '0.8000000000000000987654321', '1.1')),
             (8, ('0.31', '-0.44', '0.9')), (5, ('-0.7', '0.2', '0.51')), (12, ('0.45', '0.38', '0.77')),
             (8, ('-0.521865674620238806369343365076', '0.486710201735218728202880811295', '-0.451554728850198983103325645060'))]:
    C = [mpf(v) for v in c]; z = list(C); orbit = []
    n = 400
    for m in range(n):
        orbit.append([dd(v) for v in z]); z = bulb(z, C, p)
        if sum(v*v for v in z) > 1e8: n = m + 1; break
    cases.append({'p': p, 'n': n, 'c': [dd(v) for v in C], 'orbit': orbit})
json.dump(cases, open(sys.argv[1], 'w'))
