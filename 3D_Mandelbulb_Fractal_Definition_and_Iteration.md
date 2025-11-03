# 3D Mandelbulb Fractal: Definition and Iteration

The  Mandelbulb  is a three-dimensional analogue of the Mandelbrot set, obtained by iterating a real 3D

vector function in spherical coordinates

1

. In practice one fixes an exponent p (often 8) and iterates

z

=n+1

g (z ) +
n
p

C

where $z,C\in\mathbb{R}^3$. Writing $z=(x,y,z)$ in spherical form with radius $r=\sqrt{x^2+y^2+z^2}$, polar

angle $\theta=\arccos(z/r)$, and azimuth $\phi=\atan2(y,x)$, the mapping is:

g (z) =
p

p
r (sin(pθ) cos(pϕ), sin(pθ) sin(pϕ), cos(pθ)).

Thus each iteration “raises” the point by power $p$ in angle-space and multiplies its radius by $r^{p-1}$.

Starting from $z_0=0$, the full iteration is:

•

Compute $(r,\theta,\phi)$ from current $z_n=(x_n,y_n,z_n)$.

•

If $r$ exceeds the bailout threshold (e.g.\ $>2$), stop and mark the point as diverged.

•

Otherwise set

r
=n+1

p
r ,
n

θ
n+1

=

and convert back to Cartesian:

pθ , ϕ

n

n+1

=

pϕ ,n

z

=n+1

(r

n+1

sin θ

n+1

cos ϕ

n+1

, r

n+1

sin θ

n+1

sin ϕ

n+1

, r

n+1

cos θ

n+1

) +

C.

•

Check if $z_{n+1}$ diverges or reach  maxIter .

In pseudocode (with double precision on CPU), the inner loop looks like:

vec3 z = {0,0,0};

// current orbit point (double)

for(int i=0; i<maxIter; i++){

double x=z.x, y=z.y, zval=z.z;

double r = sqrt(x*x + y*y + zval*zval);

if(r > bailout) { break; }

double theta = acos(zval/r);

double phi

= atan2(y, x);

double r_p

= pow(r, power);

double new_theta = power * theta;

double new_phi

= power * phi;

// Convert back to cartesian for next z:

z.x = r_p * sin(new_theta) * cos(new_phi);

z.y = r_p * sin(new_theta) * sin(new_phi);

z.z = r_p * cos(new_theta);

// Add constant C (the point coordinate in fractal space):

1


z += C;

}

This   computes   the   escape-time   iteration   for   the   Mandelbulb.   Each   point   $C=(C_x,C_y,C_z)$   in   space   is

classified   by   the   number   of   iterations   until   $|z_n|$   exceeds   the   bailout.   (If   it   never   diverges   within
maxIter , the point is typically considered inside the fractal.)

1

2

Perturbation Theory for Deep Zooms

For  deep zooms, the range of $C$ values becomes very small, requiring high precision. GPUs normally

support only 32-bit floats, which quickly lose accuracy under deep zoom . The standard approach is to

3

do   a   hybrid   CPU/GPU   pipeline:   compute   a  high-precision   reference   orbit  on   the   CPU   (using   64-bit

doubles), and then use a perturbation algorithm on the GPU to compute nearby orbits in single precision

relative to the reference

4

3

.

Concretely, pick a reference point $C_0$ (e.g. the center of the view) and compute its orbit ${z_n}$ in double

precision on CPU. For any other point $C = C_0 + \delta$ with small offset $\delta$, define the difference

(perturbation)

ϵ =n

′
z −n

z ,n

where $z'n$ is the orbit of $C$. From the fractal formula $z'=g_p(z'n)+C$ and $z=g_p(z_n)+C_0$, one shows by

algebra that

ϵ
=n+1

g (z +
n
p

ϵ ) −n

g (z ) +
n
p

δ.

In the classical 2D Mandelbrot ($g(z)=z^2$) this simplifies to $\epsilon_{n+1} = 2 z_n \,\epsilon_n +

\epsilon_n^2 + \delta$

5

. For the Mandelbulb’s 3D power map, $g_p$ is more complex, but the same

principle holds: one computes $g_p(z_n+\epsilon_n)$ and subtracts $g_p(z_n)$ (already known from the

reference orbit), then add $\delta$.

In practice on the GPU, we store the reference orbit values $z_n$ (or $g_p(z_n)$) as floats and iterate the

perturbation   in   float   precision.   For   each   pixel   with   coordinate   offset   $\delta=C-C_0$,   initialize   $

\epsilon_0=\delta$, then for each iteration $n$:

•

Let $z_{\text{ref}} = z_n$ (from CPU) in float, and $z' = z_{\text{ref}} + \epsilon_n$.

•

Compute $g_p(z')$ in float (using the same spherical-power formula).

•

Update $\epsilon_{n+1} = (g_p(z') - [z_{n+1}-C_0]) + \delta$.

(Here $z_{n+1}-C_0 = g_p(z_n)$ is also known from the CPU orbit, so in effect we subtract the

reference’s growth and add the offset $\delta$.)

The   pixel   is   marked   diverged   when   $|z_{\text{ref}}   +   \epsilon_n|$   exceeds   the   bailout.   Because   $

\epsilon_n$ remains small (the points are nearby), this keeps most arithmetic within float precision

4

. As

[23]   explains,   “for   most   iterations,   $\epsilon$   does   not   need   more   than   16   significant   figures,   and

2


consequently hardware floating-point may be used”

6

. In effect, only one point (the reference) needs full

double accuracy, while thousands of nearby points are tracked with efficient single-precision on the GPU.

In summary, the perturbation iteration on the GPU can be written (in pseudocode) as:

vec3 C0 = center;

// fractal center (double, on CPU)

vector<vec3> refOrbit = computeRefOrbit(C0); // computed in double on CPU

// Pass refOrbit (converted to float array) and C0 to GPU

// In GPU fragment shader for each pixel:

vec3 delta = pixelC - C0;

// float offset

vec3 eps = delta;

// initial epsilon_0

for(int n=1; n<maxIter; n++){

vec3 z_ref = refOrbit[n];

// reference orbit (float)

if(length(z_ref + eps) > bailout) break;

// Compute g_p(z_ref + eps) in float:

vec3 z_temp = mandelbulb_power(z_ref + eps, power);

// Get reference without constant: z_ref_next_noConst = refOrbit[n+1] - C0

vec3 ref_next = refOrbit[n+1];

vec3 z_ref_next_noConst = ref_next - C0;

// Update perturbation:

eps = (z_temp - z_ref_next_noConst) + delta;

}

color = palette[n];

// color by iteration count (with smoothing)

Here   mandelbulb_power(v,p)   computes   $g_p(v)$   as   above.   (One   can   precompute   and   upload   also

$g_p(z_n)$ or the derivatives if using series acceleration.) This loop shows that all heavy iteration on the
GPU uses only floats, while the CPU provided the double-precision orbit values  refOrbit[n]  and  C0 .

4

5

Series Approximation (Optional)

To achieve even deeper zooms, one can further use a Taylor-series approximation of the perturbation to

skip many iterations

2

. In principle, one computes derivatives of $\epsilon_n$ w.r.t.\ $\delta$ (Jacobian

and   higher-order   tensors)   at   the   reference   point,   then   evaluates   a   truncated   power   series.   This   allows

jumping  several  iterations  ahead  in  a  single  float  evaluation.  As  noted  in  [23],  using  such  series  “often

enables a significant amount of iterations to be skipped,” yielding speedups on the order of 100×

2

. A

practical implementation would calculate the series coefficients on the CPU (in high precision) for each

stage of the orbit, send them to the GPU, and have the GPU evaluate a low-order polynomial in $\delta$.

However, implementing series terms for the 3D power law is complex; a basic renderer may omit series

acceleration and simply iterate one step at a time as above, trading some performance for simplicity.

3


CPU–GPU Pipeline

Putting it all together, the rendering pipeline is:

1.

CPU (double precision): Determine the view parameters (camera position, zoom, focal plane) and

select a reference fractal point $C_0$ (often the center of the view or a periodic “nucleus” deep in the

fractal

7

). Compute the reference Mandelbulb orbit $z_n$ up to the maximum needed iterations

(or until bailout) using 64-bit doubles. Optionally compute derivatives or series coefficients. All heavy

high-precision work (arbitrarily high zoom, deep iterations) is done here, since GPUs lack fast double

precision

3

.

2.

Data Transfer: Send to the GPU the reference center $C_0$ (as floats, losing a small fraction of
precision but acceptable after relative offset), and the reference orbit array  refOrbit[n]  (as

floats). Also pass any series coefficients or precomputed derivatives if used.

3.

GPU (single precision): In the fragment or compute shader, each pixel’s 3D sample point $C = C_0 +

\delta$ is assigned an offset $\delta$ from $C_0$. Initialize $\epsilon = \delta$. Then iterate in float
precision, using the perturbation formula above, to find the escape iteration or maximum depth.

This loop (vector math, trigonometry, etc.) is fully parallel on GPU. All arithmetic here is in floats for

performance.

4.

Correction for Glitches: If some pixel’s orbit diverges in a way that float perturbation is inaccurate

(“glitch”), one may detect this (e.g.\ $\epsilon$ grows unexpectedly) and fall back to computing that

pixel in high precision (e.g. by launching a CPU calculation or using another nearby reference)

7

. In

practice, a common strategy is simply to use the center reference and then if any pixel seems wrong,

spawn a local reference orbit there and recompute only those pixels

8

.

5.

Shading/Display: The GPU uses the iteration counts to color pixels. If doing a ray-marched surface,

the GPU can also use the standard Mandelbulb distance estimator (DE) to shade an isosurface, but

the DE loop similarly benefits from perturbation. (That is, the distance-estimate iteration also uses

$g_p$ and could reuse the same double vs. float scheme.) Finally the image is presented to the

screen in real time.

This pipeline ensures  double precision only on CPU  and  float-only on GPU. As Julius Horsthuis notes,

“current generation of Mandelbulb3D fractals…do not make any use of GPU… fractals use double precision

whereas GPU only uses single precision… real-time fractals can be done, but you can only zoom in to a

certain   extent”

3

.   The   perturbation   method   circumvents   this   limitation   by   using   floats   to   compute

perturbations relative to a double-precision reference. Typical tests show that using one high-precision orbit

plus float perturbations can achieve deep-zoom Mandelbrot/Mandelbulb renders two orders of magnitude

faster than naive all-double methods

2

, while still reaching zoom levels far beyond native floats.

3

7

Summary of Algorithm

In summary, a feasible implementation proceeds as follows:

•

Initialization (CPU): Choose exponent $p$ (e.g.\ 8), bailout radius (e.g.\ 2), and a reference point

$C_0$ in the Mandelbulb space. Compute the reference orbit $z_0,z_1,\dots,z_N$ in double precision,

where $z_0=0$ and $z_{n+1}=g_p(z_n)+C_0$, until either $n=N=maxIter$ or $|z_n|>bailout$.

4


•

Data to GPU: Send $C_0$ and the list ${z_n}$ (as floats) to the GPU along with rendering

parameters. (Optionally also send ${z_n - C_0}$ or derivatives.)

•

GPU Loop (per pixel): For each pixel sample point $C = C_0 + \delta$:

•

Set $\epsilon = \delta$.

•

For $n=0..N-1$:

◦

◦

◦

Let $z_{\text{ref}} = z_n$ (from GPU memory). If $|z_{\text{ref}} + \epsilon|>bailout$, break.

Compute $z_{\text{temp}} = g_p(z_{\text{ref}} + \epsilon)$ in floats.

Compute the next perturbation: $\epsilon = z_{\text{temp}} - (z_{n+1}-C_0) + \delta$.

•

Record iteration count $n$ (or smooth it) and color accordingly.

•

Output: The GPU assembles the colored image. If raymarching, also use the distance estimate

formula $DE(r,\theta,\phi)$ in float per ray step, adding lighting/shadows as needed.

This concrete scheme uses only float math on GPU (no gl_FragCoord double hacks) and reserves doubles

for the CPU’s reference calculations. All mathematical steps above follow the standard Mandelbulb formulas

1

 and the perturbation theory of fractals

4

.

Sources

The Mandelbulb formula (spherical power iteration) is detailed in the literature

1

. The perturbation/series

approach is explained in Mandelbrot-zooming references

4

2

. Hardware considerations (CPU vs GPU

precision)   are   documented   by   fractal   artists

3

.   Practical   strategies   for   reference   selection   and   glitch

correction   come   from   fractal   programming   discussions

7

.   All   equations   and   pseudocode   above   are

consistent with these sources and with common Mandelbulb implementations.

1

C:/Users/CGT G037/Dropbox/pub/isvc2014/fractal/fractal.dvi

https://web.ics.purdue.edu/~tmcgraw/papers/mcgraw_fractal_2014

2

4

5

6

Plotting algorithms for the Mandelbrot set - Wikipedia

https://en.wikipedia.org/wiki/Plotting_algorithms_for_the_Mandelbrot_set

3

Hardware and Rendering for Mandelbulb3D — Julius Horsthuis

http://www.julius-horsthuis.com/blog/2016/12/11/hardware-and-rendering-for-mandelbulb3d

7

8

algorithms - Selecting Reference Orbit for Fractal Rendering with Perturbation Theory - Mathematics

Stack Exchange

https://math.stackexchange.com/questions/2552605/selecting-reference-orbit-for-fractal-rendering-with-perturbation-theory

5
