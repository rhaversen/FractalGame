#!/usr/bin/env python3
"""Shows that the Mandelbulb near the +z pole bottleneck (c_z ~ 0.6527) is numerically chaotic:
the escape iteration of the same point changes with the working precision (16..100 digits),
while an ordinary surface point is stable. Used to justify excluding that region from scoring."""
import mpmath as mp
pts = [(6.587744679e-04, 4.648807435e-04, 0.652677396541),
       (1.143897021e-03, 1.522737503e-04, 0.653351432640),
       (-2.498103026e-04, 2.125357182e-03, 0.653467290100),
       (1.136173337e-04, 1.584241304e-03, 0.653806866609),
       (5.244856689e-04, 6.130576576e-04, 0.652853249801),
       # a "normal" point from the diagonal location for contrast
       (-5.214157626e-01, 4.844595632e-01, -0.449526491986)]
def esc(c, dps):
    mp.mp.dps = dps
    C = [mp.mpf(repr(v)) for v in c]
    z = list(C); dr = mp.mpf(1)
    for n in range(1, 400):
        r = mp.sqrt(z[0]**2+z[1]**2+z[2]**2)
        if r > 10: return n, float(0.5*r*mp.log(r)/dr)
        dr = 8*r**7*dr + 1
        rho = mp.sqrt(z[0]**2+z[1]**2); th = mp.atan2(rho, z[2]); ph = mp.atan2(z[1], z[0])
        r8 = r**8
        z = [r8*mp.sin(8*th)*mp.cos(8*ph)+C[0], r8*mp.sin(8*th)*mp.sin(8*ph)+C[1], r8*mp.cos(8*th)+C[2]]
    return 400, 0.0
for c in pts:
    print(c, [esc(c, d) for d in (16, 34, 40, 60, 100)])
