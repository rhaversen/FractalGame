You want “ELI5 perturbation theory” for “power fractals.” Humans really know how to stack intimidating words into a trench coat and call it a concept.

Here’s the clean version.

## What “power fractals” are

A classic power fractal is made by repeating a rule like:

[
z_{n+1} = z_n^d + c
]

* (z) is a complex number (think: a 2D point with some extra math sauce).
* (d) is the “power” (2 gives the Mandelbrot/Julia-style quadratic world, 3 is cubic, etc.).
* (c) is a constant that picks which Julia set you’re drawing (or it’s the parameter you sweep over for Mandelbrot-like sets).

A fractal shows up because you iterate forever-ish and the boundary between “escapes to infinity” and “stays bounded” is insanely sensitive.

## What perturbation theory means (kid version)

Perturbation theory is just:

> “I already know what happens for a *nearby* case, so I’ll estimate this new case by tracking the *difference*.”

Like:

* You know where a ball lands if you throw it normally.
* If there’s a tiny gust of wind, you don’t re-solve physics from scratch.
* You compute how the gust nudges the landing spot.

## The core trick for fractals

Suppose you want the orbit (the repeated values) for a point, but you already computed a *reference orbit* that’s close.

Let:

* reference orbit: (z_n)
* actual orbit: (w_n)
* difference: (\delta_n = w_n - z_n)

Now iterate both under the same rule (f(z)=z^d+c):

[
z_{n+1}=f(z_n),\quad w_{n+1}=f(w_n)
]

Subtract:

[
\delta_{n+1} = w_{n+1}-z_{n+1} = f(w_n)-f(z_n)
]

Now the “perturbation” idea is: if (w_n) is close to (z_n), then (f(w_n)) is close to (f(z_n)), and the change is approximately linear:

[
f(w_n) \approx f(z_n) + f'(z_n)\delta_n
]

So:

[
\delta_{n+1} \approx f'(z_n)\delta_n
]

That’s the whole vibe: **the error evolves by multiplying by the derivative along the reference orbit.**

## What’s the derivative for power maps?

For (f(z)=z^d+c):

[
f'(z)=d z^{d-1}
]

So the perturbation update becomes:

[
\delta_{n+1} \approx d, z_n^{d-1},\delta_n
]

Meaning:

* If (|d z_n^{d-1}|) is small-ish, the perturbation stays small (good, stable-ish).
* If it’s big, tiny differences explode (hello fractal boundary).

That exploding sensitivity is exactly why fractals have insane detail.

## Why fractal renderers care

When rendering, you often compute many nearby points (pixels).
Instead of iterating each pixel from scratch at full precision:

1. Pick a “reference” pixel and iterate it normally (maybe using high precision).
2. For nearby pixels, iterate only the **perturbation** (\delta_n), which is cheaper and can still be accurate for a while.
3. If (\delta_n) gets too big, you “rebase” with a new reference.

That’s the practical use: **faster deep zooms**, especially when combined with high-precision reference orbits.

## ELI5 summary in one sentence

Perturbation theory for power fractals is:

> “Follow one main orbit, and for nearby points just track the tiny difference, updating it using the derivative (d z^{d-1}), until the difference gets too big.”

If you meant something more specific by “power fractals” (like Burning Ship variants, “power towers,” or 3D Mandelbulb-style power formulas), the same idea applies: pick a reference orbit and evolve a small delta using a local linear approximation.

# 2

You’re basically describing the classic “CPU does the expensive thinking, GPU does the dumb fast labor” arrangement. Shockingly effective, like most unfair divisions of work.

Here’s what the loop *looks like* at the systems level for perturbation/deep-zoom fractals: what gets computed where, what gets shipped between CPU and GPU, and how the iterations flow.

---

## Roles (division of labor)

### CPU (high precision, slow)

* Picks **reference points** (a “pilot” pixel or a small set per tile).
* Computes **reference orbit(s)** at *high precision*: (z_0, z_1, …, z_{N}).
* Optionally computes **helper data per step** derived from the reference orbit (like derivatives / Jacobians / coefficients) so the GPU can update deltas cheaply.
* Decides when/where to **rebase** (choose a new reference) if GPU deltas blow up.

### GPU (parallel, fast, lower precision)

* For each pixel in a tile: tracks a small **delta state** relative to the CPU reference orbit.
* Iterates per step using the CPU-provided reference data.
* Determines escape / iteration count / coloring info.
* Signals “this pixel went out of valid perturbation range” if you want robustness (or just let it degrade and re-render).

---

## Data sent CPU → GPU (per tile, per pass)

Think in *tiles* (e.g., 16×16, 32×32, 64×64) to amortize CPU work.

For each tile, CPU sends:

1. **Reference orbit array**

   * `Z_ref[n]` for n = 0..N (complex values)
   * Usually stored as double-double / quad on CPU, but you *down-convert* to float/double for GPU storage.
   * If you need insane zoom, you might send “split” doubles (hi/lo parts) so GPU can reconstruct more accurate values.

2. **Per-iteration coefficients** (optional but common)

   * Whatever the GPU needs to update deltas cheaply each step.
   * Usually something like `A[n]` where the delta update is `delta = A[n]*delta + ...`.
   * This avoids GPU recomputing expensive powers/etc.

3. **Tile transform / mapping info**

   * The complex coordinate of the tile origin (or center).
   * Pixel step vectors (dx, dy) in complex plane.
   * Any camera/zoom parameters.

4. **Control parameters**

   * `N_max` (max iterations),
   * bailout radius,
   * thresholds like `delta_limit` (when perturbation becomes untrustworthy).

---

## Data sent GPU → CPU (results / feedback)

Typically:

1. **Per-pixel output**

   * iteration count (or smooth iteration),
   * escape flag,
   * optional distance estimate / normal / whatever you shade with.

2. **Optional “needs rebase” mask**

   * A bitmask or list of pixels that exceeded `delta_limit` or otherwise became numerically sketchy.
   * Can be per-tile summary: “tile good” / “tile needs re-render with new reference”.

---

## The iteration flow (what happens each step)

### CPU side (per tile)

1. Choose a reference pixel (often tile center).
2. Compute high-precision reference orbit:

   * `Z_ref[0] = z0_ref`
   * for n in 0..N-1: `Z_ref[n+1] = f(Z_ref[n])`
3. Compute optional per-step coefficients `A[n]` from `Z_ref[n]`.
4. Package and upload (`Z_ref`, `A`, mapping params).

### GPU side (per tile, massively parallel)

Each pixel thread does:

1. Initialize delta:

   * `delta0 = z0_pixel - z0_ref` (computed from mapping params)
2. Initialize any extra per-pixel state (iteration count, escaped flag).
3. For n in 0..N-1:

   * Read `Z_ref[n]` (and maybe `A[n]`) from GPU memory.
   * Update delta using lightweight ops.
   * Form an *approximate current z* if needed:

     * `z ≈ Z_ref[n] + delta`
   * Check escape (usually using that approximate z).
   * If escaped: stop and store iteration count.
   * If `|delta|` too big: mark “invalid” (optional), and either:

     * stop early, or
     * continue but flag quality as bad.
4. Write out result.

---

## The overall render loop (multi-pass, because reality is rude)

A practical deep-zoom renderer usually does this:

### Pass 1: Fast perturbation render

* CPU makes reference orbits for tiles.
* GPU renders all tiles.
* GPU returns image + “bad pixels/tiles” info.

### Pass 2+: Repair / rebase passes (only for trouble spots)

* CPU looks at flagged tiles/pixels.
* For each flagged tile:

  * choose a better reference (maybe closer to the problem area),
  * compute a new high-precision orbit,
  * re-render just that tile (or even sub-tiles).
* Repeat until:

  * no tiles flagged, or
  * you hit a quality/iteration/time budget.

This is how you avoid wasting CPU precision everywhere when only tiny boundary regions need it.

---

## Memory / bandwidth reality check (aka “the part everyone forgets”)

Sending a full orbit `Z_ref[n]` for huge N to GPU can get heavy.

Common tricks:

* Use smaller `N` per pass and do multiple passes.
* Store orbit in GPU constant/texture memory for better caching.
* Send *compressed orbit data* or just coefficients if you can derive everything from them.
* Use per-tile or per-warp shared memory staging if the orbit is reused a lot.

---

## Minimal “what talks to what” summary

Per tile:

**CPU computes once:**

* reference orbit (high precision)
* optional per-iteration coefficients
* mapping params

**CPU uploads to GPU:**

* orbit/coefs + params

**GPU computes per pixel, per iteration:**

* delta update using orbit/coefs
* escape test
* output

**GPU returns:**

* pixel results
* optional “needs rebase” flags

**CPU optionally repeats for flagged regions.**

---

## What this buys you

* CPU only does expensive high-precision *per tile*, not per pixel.
* GPU still does the million-thread grind.
* You get deep zooms without turning your GPU into a slow, sad arbitrary-precision calculator.

If you want the *most common architecture choice*: **tile-based, one reference orbit per tile, repair passes for flagged tiles**. That’s the sweet spot between accuracy, bandwidth, and not hating your life.

# 3

Multiple times. Usually. Because the universe hates “once per frame” simplicity.

### In the ideal case (easy view, not too deep zoom)

You can get away with **one upload per tile** and that’s effectively “once per frame” *if*:

* you use **one reference orbit for the whole frame** (rarely good unless you’re not zoomed much), or
* you precompute references for all tiles on the CPU, upload them all in one big batch, then the GPU renders.

So: **one (batched) upload phase**, one render phase.

### In the normal deep-zoom case (the one you actually care about)

It’s **multiple uploads per frame**, because you’re doing **repair/rebase passes**.

A typical frame looks like:

1. **Pass 1 (bulk render)**

* CPU uploads reference data for each tile (often batched into a few big buffers).
* GPU renders everything.
* GPU outputs image + “bad tile/pixel” flags.

2. **Pass 2 (repair)**

* CPU computes new reference orbits **only for flagged tiles**.
* CPU uploads those new reference chunks.
* GPU re-renders only those tiles.

3. **Pass 3…**

* Repeat until the “bad” set is small enough or gone.

So the number of uploads per frame is basically:

* **1** if you’re lucky / not zoomed hard
* **2–5** in many deep zooms (first pass + a few fixes)
* potentially more near nasty boundary regions or extreme zoom

### What you do NOT want

Uploading per iteration. If you’re sending CPU data every iteration step, you’ve built a very expensive heater.

The CPU should upload **orbit/coefficient arrays covering many iterations at once** (like 1k, 5k, 20k steps), then the GPU runs that whole block.

### The clean mental model

Per frame is usually:

* **Upload phase:** reference orbits/coefs for tiles (possibly incremental in repair passes)
* **Compute phase:** GPU runs iterations for those tiles
* **Optional repair loop:** upload more refs, compute again (for small subsets)

So: **multiple uploads per frame**, but **not continuous**, and definitely **not per-iteration**.

# 4

Simplest compute shader that “renders something” *and* uses perturbation is basically: **one precomputed reference orbit** (for the center pixel) + **per-pixel delta iteration** on the GPU. That’s it. No tiles, no rebasing, no ego.

Below is a minimal **GLSL compute shader** for a **Mandelbrot**-style render using the classic perturbation update:

[
\delta_{n+1} = 2 z_{ref,n},\delta_n + \delta_n^2 + \Delta c
]
where (z_{ref}) is the CPU-provided reference orbit for (c_{ref}), and (\Delta c = c - c_{ref}) is per pixel.

### What must be uploaded CPU → GPU for this to work

* `zref[i]` array for `i=0..maxIter` (reference orbit for `c_ref`, computed in high precision on CPU)
* `c_ref` (complex center)
* mapping (`pixelStep`, `imageSize`)
* output image binding

### Minimal compute shader (GLSL 430)

```glsl
#version 430

layout(local_size_x = 16, local_size_y = 16) in;

// Output image
layout(rgba8, binding = 0) uniform writeonly image2D outImg;

// Reference orbit: zref[i] as vec2 (x=real, y=imag)
layout(std430, binding = 1) buffer RefOrbit {
    vec2 zref[];
};

// Uniforms
uniform ivec2 uImageSize;     // e.g. (width, height)
uniform vec2  uCRef;          // reference c (center)
uniform vec2  uPixelStep;     // complex-plane step per pixel (dx, dy)
uniform int   uMaxIter;       // iterations
uniform float uBailout;       // e.g. 4.0 (radius), use squared below

// Complex multiply
vec2 cmul(vec2 a, vec2 b) {
    return vec2(a.x*b.x - a.y*b.y, a.x*b.y + a.y*b.x);
}

void main() {
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    if (p.x >= uImageSize.x || p.y >= uImageSize.y) return;

    // Map pixel to complex c:
    // c = c_ref + (p - centerPixel) * pixelStep
    vec2 centerPix = vec2(uImageSize) * 0.5;
    vec2 dPix = vec2(p) - centerPix;
    vec2 dc = vec2(dPix.x * uPixelStep.x, dPix.y * uPixelStep.y);

    // Perturbation state
    vec2 delta = vec2(0.0);        // δ0 = 0 because z0 is 0 for Mandelbrot
    float bailout2 = uBailout * uBailout;

    int it = 0;
    for (; it < uMaxIter; ++it) {
        vec2 zr = zref[it];        // reference z at iteration it

        // δ_{n+1} = 2*zr*δ + δ^2 + Δc
        vec2 two_zr = 2.0 * zr;
        delta = cmul(two_zr, delta) + cmul(delta, delta) + dc;

        // z ≈ zr + δ
        vec2 z = zr + delta;

        if (dot(z, z) > bailout2) break;
    }

    // Super simple coloring: grayscale based on iteration count
    float t = float(it) / float(uMaxIter);
    vec4 col = vec4(vec3(t), 1.0);

    imageStore(outImg, p, col);
}
```

### The loop logic (what happens each iteration)

Per pixel thread on GPU:

1. Read `zr = zref[it]` from the uploaded reference orbit
2. Update `delta` using cheap ops (a couple complex multiplies + add `dc`)
3. Compute approximate `z = zr + delta`
4. Escape test on `z`
5. Stop and write color

### The “one-upload vs multiple” reality

This “simplest” version assumes **one reference orbit for the whole frame**. It will render a recognizable Mandelbrot-ish image quickly, but deep zoom accuracy falls apart when pixels are far from `c_ref` or near nasty boundaries.

The “real” version is the same shader idea, except `zref[]` (and `c_ref`) become **per-tile** data and you run repair passes. But this is the smallest thing that proves the pipeline.

That should get you pixels on screen without turning your project into a PhD dissertation disguised as a hobby.

# 5

3D is the same idea as 2D perturbation, except you add one more dimension and about three more kinds of pain. In 2D (Mandelbrot/Julia), each pixel is “one point, one orbit.” In 3D rendering (Mandelbulb-ish stuff), each pixel is “one ray, many points, each point needs an orbit.” That’s the big structural change.

Here’s the simplest “it renders something” pipeline that still counts as perturbation.

---

## What changes in 3D

### 2D

* Pixel → complex point (c)
* Run iterations at (c) (escape-time)
* Done

### 3D (ray-marched surface or volume)

* Pixel → **ray**
* Ray takes steps through space: (p(t))
* At each step point (p), you run iterations to get:

  * escape info and/or a **distance estimate** (so you can march)
* So you get a nested loop: **ray steps (outer)** × **fractal iterations (inner)**.

Perturbation lives in the **inner** loop.

---

## CPU → GPU data (the minimum for perturbation in 3D)

You pick a **reference point** in 3D space, call it `p_ref` (like the center of the region you’re viewing).

CPU computes a **high-precision reference orbit** once:

* `Zref[i]` for i = 0..N (each is a `vec3`-like state, depending on your 3D fractal formula)
* Plus some **linearization helper per iteration** so the GPU can update deltas cheaply

In 2D that helper is basically “multiply by derivative.”
In 3D it’s the same concept but it’s a **3×3 Jacobian matrix** (or some equivalent packed coefficients):

* `Jref[i]` for i = 0..N-1 (each is effectively a `mat3` or a compressed form)

So per reference you upload:

* `p_ref`
* `Zref[]` (vec3 array)
* `Jref[]` (mat3 array, or packed rows)
* render params (camera, ray directions, max steps, thresholds, etc.)

That’s it. No per-pixel high precision, no per-iteration uploads.

---

## GPU work per frame (simple version: one reference for whole frame)

### Outer loop: ray marching (per pixel thread)

For each pixel:

1. Build ray `origin`, `dir`
2. Set `t = 0`
3. Repeat for `rayStep = 0..maxRaySteps`:

   * `p = origin + t * dir`
   * compute DE/escape at `p` using perturbation (inner loop)
   * if “hit surface” (distance small enough): shade and stop
   * else `t += distance` and keep going
   * if `t` too large: background

### Inner loop: fractal iteration using perturbation (per ray step point)

At point `p`:

1. Compute `Δc = p - p_ref` (a vec3)
2. Initialize `delta = 0` (vec3)
3. For `i = 0..maxIter`:

   * read `zr = Zref[i]`
   * read `Ji = Jref[i]`
   * update perturbation (conceptually):

     * `delta = Ji * delta + (small nonlinear term) + Δc`
   * approximate current state:

     * `z ≈ zr + delta`
   * escape check from `z`
4. From the final state, compute either:

   * a distance estimate (for ray marching), or
   * a density/escape metric (for volume rendering)

That’s the 3D analog of the 2D perturbation loop: **you’re evolving a delta, using CPU-supplied “how errors propagate” data per iteration.**

---

## Upload frequency in 3D

### Simplest “make it render” approach

* **Upload once per frame**: one global reference orbit (around the region you’re looking at)
* GPU uses it for every ray sample point

This works if your camera is focused on a relatively small region and you are not demanding perfect accuracy everywhere.

### Practical (less wrong) approach

* **Multiple references per frame**, usually per screen tile or per spatial cell:

  * Upload references for visible tiles/cells (batched)
  * Render
  * Optional repair pass for areas where `|Δc|` or `|delta|` gets too big

3D tends to need more rebasing than 2D because rays wander through space, so “everything is near the same reference” is… optimistic.

---

## The simplest “it won’t implode instantly” rule

On the GPU, if `|Δc|` (or `|delta|`) exceeds some threshold:

* either **fallback** to a cheap non-perturbation iteration in float for that sample, or
* mark “needs rebase” (tile/cell) and fix it with another CPU reference in a later pass

That’s the safety valve that keeps perturbation from turning into “render modern art by numerical breakdown.”

---

## TL;DR loop structure (3D)

* CPU: compute **one (or a few) high-precision reference orbits** + per-iter linearization data → upload
* GPU per pixel:

  * ray-march
  * at each march step point: run inner perturbation iteration using uploaded `Zref[i]` + `Jref[i]`
  * use result to step ray / shade
* Optional: rebase and re-render bad regions

3D perturbation is not conceptually harder than 2D. It’s just more nested loops and more data per iteration (because 3D error propagation is matrix-ish instead of scalar-ish), which is exactly the kind of thing GPUs love and humans regret.

# 6

“Infinite” real-time fractal zoom/rendering is basically three problems wearing a trench coat:

1. **precision** (floats run out of bits and your camera starts teleporting in chunks)
2. **throughput** (3D fractals are expensive: ray steps × iterations)
3. **stability** (tiny parameter changes explode near boundaries, because fractals)

You can get *practically infinite* (user won’t hit the end in any sane session) by combining: **high-precision state on CPU + perturbation on GPU + rebasing + a floating-origin style coordinate system**.

Below is the simplest way to get close, framed for **Unreal + compute shader**, with focus on what moves between CPU/GPU and what runs per frame.

---

## The non-negotiables you need

### 1) Keep “camera position” out of float space

In Unreal, your camera transform is effectively float-ish for rendering. At deep zoom, moving the camera by a “tiny” amount becomes impossible because float granularity gets bigger than your intended motion. That’s the chunking you mentioned.

**Fix:** don’t move the camera in fractal coordinates.
Keep the *render camera* near the origin (0,0,0) and instead move the **fractal center / domain transform** in high precision.

This is the same idea as UE’s “world origin rebasing,” but you apply it to **fractal space**.

**CPU stores (high precision):**

* `Center` (3D) and `Scale` (zoom), plus orientation
* update these from input every frame (in doubles or arbitrary precision)

**GPU receives (low precision but stable):**

* `Center_hi`, `Center_lo` (float pairs, aka “double-double light”)
* `Scale_hi`, `Scale_lo`
* camera basis vectors (float3) since orientation doesn’t need insane precision

Then in the shader you compute positions as:

* `p = Center + local` where `local` stays small because the camera is effectively near origin in fractal space.

That alone kills the “movement in chunks” problem.

---

### 2) Perturbation needs a reference orbit (and you reuse it heavily)

To go deep, you avoid doing “full accurate math” per sample.

**CPU computes a reference orbit** at some reference point (or several):

* `Zref[i]` for i=0..N (high precision)
* and usually some per-iteration helper data so GPU can update deltas cheaply

  * in 3D this is typically **Jacobian-ish** (a 3×3 per step) or a packed equivalent

**GPU then does perturbation**:

* for each evaluated point, it tracks a small `delta` relative to the reference and updates it per iteration using the precomputed helper data.

---

### 3) 3D means ray marching, which multiplies cost

Each pixel does:

* ray steps (outer loop)
* fractal iterations (inner loop)

Perturbation only makes the **inner loop** cheaper/valid deeper. You still must keep ray marching under control:

* capped ray steps
* early-outs
* adaptive step size (distance estimate)
* lower quality while moving, refine when still

---

## Simplest architecture that gets “infinite enough” fast

### Stage A: Use ONE reference per frame (yes, really)

This is the quickest path to “something real-time that zooms stupidly far”:

**Per frame on CPU**

1. Update high-precision `Center/Scale` from user input.
2. Pick `Pref = Center` (reference point at the center of view).
3. Compute high-precision reference orbit arrays:

   * `Zref[0..N]`
   * `Href[0..N]` (whatever helper per step your perturbation update needs)
4. Upload those buffers once for the frame.

**Per frame on GPU (compute shader)**
For each pixel:

1. Build ray from camera basis.
2. March along ray:

   * for each ray sample point `p`:

     * compute `dc = p - Pref` (small-ish if you keep your marching domain centered)
     * run perturbation iteration using `Zref/Href`
     * get an escape metric / distance estimate
     * step forward
3. Shade when hit / escape / max distance.

This will already let you zoom far beyond plain float math, and it’s conceptually clean.

**Limitation:** points far from `Pref` (or nasty boundary regions) will eventually exceed perturbation validity.

---

### Stage B: Add screen tiles (still simple, way more robust)

When the “one reference for all pixels” starts breaking:

* Split screen into tiles (ex: 16×16 or 32×32).
* Each tile gets its own reference point `Pref_tile` (usually the tile center ray at some nominal distance or an estimate of where the interesting surface is).

**Per frame on CPU**

* For each visible tile:

  * compute `Pref_tile`
  * compute `Zref_tile[]`, `Href_tile[]`
* Upload a big packed buffer:

  * `tileRefs[tileId]` + orbit/helper arrays per tile

**GPU**

* Each pixel picks its tile’s reference data.
* Same perturbation + raymarch, but “closer reference” means deltas stay small longer.

This is the basic “real renderer” step without turning your life into a tragedy.

---

### Stage C: Repair pass (optional, but this is what makes it feel infinite)

Instead of trying to make the first pass perfect:

**GPU writes a “badness” flag** when:

* `|delta|` gets too big
* numerical stuff looks unstable
* you exceed a threshold

Then:

* CPU only recomputes references for tiles marked bad
* GPU rerenders just those tiles

So one frame can have:

* **main pass** (bulk)
* **repair pass** (few tiles)
* sometimes a second repair if you want

That’s “multiple uploads per frame,” but only when needed, and only for a small subset.

---

## Handling the camera chunking problem properly

Your instinct is correct: **move the fractal, not the camera**.

Concrete rule:

* In Unreal, keep the render camera transform normal (for view direction etc.).
* But treat its *position in fractal space* as:

  * `FractalCenter += input_motion * Scale`
  * camera position used in shader stays near zero
* You send only the high-precision `FractalCenter/Scale` to the compute shader (split floats).
* The shader computes everything relative to that.

Result: smooth motion even at absurd zoom, because the “local” coordinate stays representable.

---

## What gets sent CPU → GPU in the “good enough” version

Per frame (or per tile batch):

* `Center_hi/lo` (float3 + float3)
* `Scale_hi/lo` (float + float)
* camera basis vectors (float3 right/up/forward)
* packed `Zref[]` (per ref)
* packed `Href[]` (per ref)
* render params: max iters, max ray steps, bailout, epsilon, etc.

GPU → CPU (optional but helpful):

* per-tile bad flags (tiny buffer)
* maybe a histogram / stats for adaptive quality

---

## The simplest path to “close enough to infinite” (practical plan)

1. **Start with 2D perturbation** in UE compute shader (on a fullscreen quad).
   Not because you asked for 2D, but because it validates your CPU↔GPU precision pipeline fast.

2. **Move to 3D** with a basic ray marcher:

   * one reference per frame
   * distance-estimator-ish stepping (even if crude)
   * quality scaling when moving

3. **Add tile references** when artifacts show up.

4. **Add repair pass** if you want it to feel truly unbounded.

5. **Add “floating fractal origin”** (center/scale split floats) immediately, or you’ll hate everything later.

---

## Reality check (the part nobody likes)

“Infinite” is always “until your precision strategy or time budget runs out.” The trick is making the ceiling so high the user cannot realistically hit it:

* CPU precision scales with zoom (bigfloat if needed)
* GPU stays in float but only ever sees *small deltas*
* rebasing keeps deltas small
* adaptive quality keeps it real-time

That’s the whole scam. A good scam, though.

If you implement just **(1) fractal-center movement + (2) one-reference perturbation**, you’ll already get an absurd zoom range and stable motion. Adding **tiles + repair** is how it becomes “effectively endless.”
