// Mandelbulb perturbation ray marcher (WGSL). A port of Plugins/FractalRenderer/Shaders (FractalPowerMap.ush,
// FractalDE.ush, FractalRender.ush) for integer powers 2..16, without the linear series skip.
//
// Every sample is c = C_ref + dc with its orbit z_n = Z_n + d_n. Z_n is the reference orbit computed on the CPU in
// double-double (lib.js); dc and d_n are f32. Each step evaluates g_p(Z + d) - g_p(Z) with exact identities (no
// cancellation), and a pixel that gets closer to the origin than to the reference rebases onto orbit point 0.

struct Params {
	ray00: vec4<f32>,   // direction through pixel (0, 0) (xyz, unnormalised), hit threshold per unit distance (w)
	rayDX: vec4<f32>,   // per-pixel step to the right, power (w)
	rayDY: vec4<f32>,   // per-pixel step down, bailout (w)
	camOff: vec4<f32>,  // camera - C_ref, max ray distance (w)
	refC: vec4<f32>,    // float(C_ref) (only used without a reference orbit), bounding radius (w)
	sizeI: vec4<i32>,   // width, height, orbit points, max iterations
	marchI: vec4<i32>,  // max ray steps, integer power, 0, 0
	misc: vec4<f32>,    // footprint above which plain float iteration is used, haze start, 0, 0
};

@group(0) @binding(0) var<uniform> P: Params;
@group(0) @binding(1) var<storage, read> Orbit: array<vec4<f32>>;
@group(0) @binding(2) var Out: texture_storage_2d<rgba8unorm, write>;
@group(0) @binding(3) var<storage, read_write> Probe: array<f32>;

const STRIDE: i32 = 4;

// ---------------------------------------------------------------------------------------------
// Helpers (FractalMathCommon.ush)
// ---------------------------------------------------------------------------------------------

// Exact power of two s such that v * s is in [1, 2) (2^127 for v == 0).
fn pow2Normalizer(v: f32) -> f32 {
	return bitcast<f32>(0x7F000000u - (bitcast<u32>(v) & 0x7F800000u));
}
// 1 / pow2Normalizer(v): the power of two with v's exponent (0 for v == 0).
fn pow2Denormalizer(v: f32) -> f32 {
	return bitcast<f32>(bitcast<u32>(v) & 0x7F800000u);
}

// (a + ib)^p for integer p >= 1 by binary powering.
fn complexPowInt(a: f32, b: f32, p: i32) -> vec2<f32> {
	var ra = a;
	var rb = b;
	var bit = i32(firstLeadingBit(u32(p))) - 1;
	loop {
		if (bit < 0) { break; }
		let na = ra * ra - rb * rb;
		rb = 2.0 * ra * rb;
		ra = na;
		if (((p >> u32(bit)) & 1) != 0) {
			let ma = ra * a - rb * b;
			rb = ra * b + rb * a;
			ra = ma;
		}
		bit = bit - 1;
	}
	return vec2<f32>(ra, rb);
}

// A rotation stored as (V, S) = (1 - cos a, sin a), raised to the integer power p (no cancellation for tiny angles).
fn versSinPowInt(v: f32, s: f32, p: i32) -> vec2<f32> {
	var rv = v;
	var rs = s;
	var bit = i32(firstLeadingBit(u32(p))) - 1;
	loop {
		if (bit < 0) { break; }
		let ns = 2.0 * rs * (1.0 - rv);
		rv = rv * (2.0 - rv) + rs * rs;
		rs = ns;
		if (((p >> u32(bit)) & 1) != 0) {
			let ms = rs * (1.0 - v) + (1.0 - rv) * s;
			rv = rv + v - rv * v + rs * s;
			rs = ms;
		}
		bit = bit - 1;
	}
	return vec2<f32>(rv, rs);
}

fn versineFromCosSin(c: f32, s: f32) -> f32 {
	if (c >= 0.0) { return s * s / (1.0 + c); }
	return 1.0 - c;
}

// ---------------------------------------------------------------------------------------------
// Mandelbulb power map (FractalPowerMap.ush, integer power)
// ---------------------------------------------------------------------------------------------

// g_p(W) with |W| = rw and 1/|W| = invRw; .w = |W|^(p-1).
fn bulbPower(w: vec3<f32>, rw: f32, invRw: f32, pInt: i32) -> vec4<f32> {
	let maxXY = max(abs(w.x), abs(w.y));
	let xyScale = pow2Normalizer(maxXY);
	let swx = w.x * xyScale;
	let swy = w.y * xyScale;
	let srho2 = swx * swx + swy * swy;
	var invSRho = 0.0;
	if (srho2 > 0.0) { invSRho = inverseSqrt(srho2); }
	let rhoW = srho2 * invSRho * pow2Denormalizer(maxXY);
	let t = complexPowInt(w.z * invRw, rhoW * invRw, pInt);
	var bx = 1.0;
	if (srho2 > 0.0) { bx = swx * invSRho; }
	let q = complexPowInt(bx, swy * invSRho, pInt);
	let rwPowM1 = complexPowInt(rw, 0.0, pInt - 1).x;
	let rwPow = rwPowM1 * rw;
	return vec4<f32>(rwPow * vec3<f32>(t.y * q.x, t.y * q.y, t.x), rwPowM1);
}

// g_p(Z + d) - g_p(Z) for the reference point stored at Orbit[base ..], W = Z + d. .w = |W|^(p-1).
fn perturbedPowerDelta(base: i32, d: vec3<f32>, w: vec3<f32>, rw: f32, invRw: f32, pInt: i32) -> vec4<f32> {
	let a = Orbit[base];
	let b = Orbit[base + 1];
	let c = Orbit[base + 2];
	let refZ = a.xyz;
	let refR = a.w;
	let refRho = b.x;
	let refRPow = b.y;
	let refInvR = b.w;
	let sinPT = c.x;
	let cosPT = c.y;
	let sinPP = c.z;
	let cosPP = c.w;

	// The reference is negligible next to the full point (after a rebase it is 0): evaluate directly.
	if (refR <= 0.5 * rw) {
		let gw = bulbPower(w, rw, invRw, pInt);
		let gref = refRPow * vec3<f32>(sinPT * cosPP, sinPT * sinPP, cosPT);
		return vec4<f32>(gw.xyz - gref, gw.w);
	}

	// (r/R)^p - 1 with s = dr/R, dr computed without cancellation, by binary powering on A = (1+s)^k - 1, B = (1+s)^k.
	let s = (2.0 * dot(refZ, d) + dot(d, d)) / (rw + refR) * refInvR;
	let onePlusS = 1.0 + s;
	var pa = s;
	var pb = onePlusS;
	var bit = i32(firstLeadingBit(u32(pInt))) - 1;
	loop {
		if (bit < 0) { break; }
		pa = pa * (pa + 2.0);
		pb = pb * pb;
		if (((pInt >> u32(bit)) & 1) != 0) {
			pa = pa * onePlusS + s;
			pb = pb * onePlusS;
		}
		bit = bit - 1;
	}
	let u = pa;
	let onePlusU = pb;

	// xy-plane quantities are rescaled by an exact power of two so products of small components never underflow.
	let maxXY = max(max(abs(refZ.x), abs(refZ.y)), max(abs(w.x), abs(w.y)));
	let xyScale = pow2Normalizer(maxXY);
	let invXYScale = pow2Denormalizer(maxXY);
	let sx = refZ.x * xyScale;
	let sy = refZ.y * xyScale;
	let sdx = d.x * xyScale;
	let sdy = d.y * xyScale;
	let swx = w.x * xyScale;
	let swy = w.y * xyScale;
	let sRho = refRho * xyScale;
	let sRhoW = sqrt(swx * swx + swy * swy);
	let rhoW = sRhoW * invXYScale;

	// Azimuth difference: the exact angle between (X, Y) and (wx, wy). On the axis phi_ref := 0.
	let onAxis = refRho <= 0.0;
	let ux = select(sx, 1.0, onAxis);
	let uy = select(sy, 0.0, onAxis);
	let crossP = ux * sdy - uy * sdx;
	let dotP = ux * swx + uy * swy;

	// rho_w - rho without cancellation, then the polar angle difference in the meridian half-plane.
	let sdRho = ((2.0 * sx + sdx) * sdx + (2.0 * sy + sdy) * sdy) / max(sRhoW + sRho, 1.0e-30);
	let dRho = sdRho * invXYScale;
	let crossT = refZ.z * dRho - refRho * d.z;
	let dotT = refRho * rhoW + refZ.z * w.z;

	// Trig-free: (1 - cos, sin) of dtheta and dphi from the normalised cross/dot products, multiplied by p.
	let normT = refInvR * invRw;
	let sinDT = crossT * normT;
	let cosDT = dotT * normT;
	let vsT = versSinPowInt(versineFromCosSin(cosDT, sinDT), sinDT, pInt);

	let sRhoU2 = ux * ux + uy * uy;
	var normP = 0.0;
	if (sRhoW > 0.0) { normP = inverseSqrt(sRhoU2) / sRhoW; }
	let sinDP = crossP * normP;
	let cosDP = select(1.0, dotP * normP, sRhoW > 0.0);
	let vsP = versSinPowInt(versineFromCosSin(cosDP, sinDP), sinDP, pInt);

	let dSinT = cosPT * vsT.y - sinPT * vsT.x;
	let dCosT = -sinPT * vsT.y - cosPT * vsT.x;
	let dSinP = cosPP * vsP.y - sinPP * vsP.x;
	let dCosP = -sinPP * vsP.y - cosPP * vsP.x;
	let sinTW = sinPT + dSinT;
	let cosTW = cosPT + dCosT;
	let sinPW = sinPP + dSinP;
	let cosPW = cosPP + dCosP;
	let fw = vec3<f32>(sinTW * cosPW, sinTW * sinPW, cosTW);
	let df = vec3<f32>(dSinT * cosPW + sinPT * dCosP, dSinT * sinPW + sinPT * dSinP, dCosT);
	return vec4<f32>(refRPow * (u * fw + df), refRPow * onePlusU * invRw);
}

// ---------------------------------------------------------------------------------------------
// Distance estimate (FractalDE.ush)
// ---------------------------------------------------------------------------------------------

struct DE {
	dist: f32,
	iters: i32,
	escaped: bool,
	smooth_: f32,
}

fn smoothIteration(n: i32, rw: f32, bailout: f32, power: f32, escaped: bool) -> f32 {
	if (!escaped) { return f32(n); }
	let ratio = log(max(rw, 1.0001)) / log(max(bailout, 1.0001));
	return f32(n) + 1.0 - log(max(ratio, 1.0)) / log(max(abs(power), 1.5));
}

fn finishDE(n: i32, rw: f32, drs: f32, escaped: bool) -> DE {
	var r: DE;
	let power = P.rayDX.w;
	r.iters = n;
	r.escaped = escaped;
	r.smooth_ = smoothIteration(n, rw, P.rayDY.w, power, escaped);
	r.dist = select(0.0, 0.5 * rw * log(max(rw, 1.0e-6)) / max(drs, 1.0e-37), escaped);
	return r;
}

// DE at c = C_ref + dc by perturbation.
fn perturbedDE(dc: vec3<f32>) -> DE {
	let pInt = P.marchI.y;
	let power = P.rayDX.w;
	let bailout = P.rayDY.w;
	let maxIt = P.sizeI.w;
	let orbitLen = P.sizeI.z;
	var d = dc;
	var m = 1;
	var drs = 1.0;
	var rw = 0.0;
	var escaped = false;
	var n = 1;
	loop {
		if (n >= maxIt) { break; }
		let refZ = Orbit[STRIDE * m].xyz;
		let w = refZ + d;
		let rw2 = dot(w, w);
		var invRw = 0.0;
		if (rw2 > 0.0) { invRw = inverseSqrt(rw2); }
		rw = rw2 * invRw;
		// |Z + d|^2 - Bailout^2 = 2 Z.d + d.d - margin, with the margin exact from the CPU.
		if (dot(2.0 * refZ + d, d) > Orbit[STRIDE * m + 3].x) {
			escaped = true;
			break;
		}
		// Left the reference (|z| < |d|) or ran out of orbit: rebase onto orbit point 0 (d := z).
		if (rw2 < dot(d, d) || m >= orbitLen - 1) {
			d = w;
			m = 0;
		}
		let g = perturbedPowerDelta(STRIDE * m, d, w, rw, invRw, pInt);
		drs = power * g.w * drs + 1.0;
		d = g.xyz + dc;
		m = m + 1;
		n = n + 1;
	}
	return finishDE(n, rw, drs, escaped);
}

// Plain float DE at an absolute position.
fn directDE(c: vec3<f32>) -> DE {
	let pInt = P.marchI.y;
	let power = P.rayDX.w;
	let bailout2 = P.rayDY.w * P.rayDY.w;
	let maxIt = P.sizeI.w;
	var z = c;
	var drs = 1.0;
	var rw = 0.0;
	var escaped = false;
	var n = 1;
	loop {
		if (n >= maxIt) { break; }
		let rw2 = dot(z, z);
		var invRw = 0.0;
		if (rw2 > 0.0) { invRw = inverseSqrt(rw2); }
		rw = rw2 * invRw;
		if (rw2 > bailout2) {
			escaped = true;
			break;
		}
		let g = bulbPower(z, rw, invRw, pInt);
		drs = power * g.w * drs + 1.0;
		z = g.xyz + c;
		n = n + 1;
	}
	return finishDE(n, rw, drs, escaped);
}

// Hybrid estimate: plain float where the pixel footprint is large (exact enough, cheaper) or while there is no
// reference, perturbation otherwise.
fn distanceEstimate(dc: vec3<f32>, pixelRadius: f32) -> DE {
	var r: DE;
	if (P.sizeI.z < 2 || pixelRadius > P.misc.x) {
		r = directDE(P.refC.xyz + dc);
	} else {
		r = perturbedDE(dc);
	}
	return r;
}

// ---------------------------------------------------------------------------------------------
// March and shading (FractalRender.ush)
// ---------------------------------------------------------------------------------------------

struct March {
	t: f32,
	steps: i32,
	status: i32, // 1 hit, 2 left the scene, 3 out of steps
	smooth_: f32,
	escaped: bool,
	iters: i32,
}

fn pixelDirection(px: vec2<f32>) -> vec3<f32> {
	return normalize(P.ray00.xyz + px.x * P.rayDX.xyz + px.y * P.rayDY.xyz);
}

fn marchRay(dir: vec3<f32>) -> March {
	var r: March;
	r.t = 0.0;
	r.steps = 0;
	r.status = 3;
	r.smooth_ = 0.0;
	r.escaped = false;
	r.iters = 0;
	var t = 0.0;
	var tEnd = P.camOff.w;
	// Bounding sphere: the escape-time estimate overshoots far from the set. A ray that misses the sphere hits
	// nothing; one that crosses it is marched from where it enters.
	let radius = P.refC.w * 1.001;
	let o = P.refC.xyz + P.camOff.xyz;
	let c = dot(o, o) - radius * radius;
	if (c > 0.0) {
		let b = dot(o, dir);
		let disc = b * b - c;
		if (b >= 0.0 || disc < 0.0) {
			r.status = 2;
			return r;
		}
		let root = sqrt(disc);
		t = -b - root;
		tEnd = min(tEnd, -b + root);
	}
	loop {
		if (r.steps >= P.marchI.x) { break; }
		if (t >= tEnd) { r.status = 2; break; }
		r.steps = r.steps + 1;
		let pr = t * P.ray00.w;
		let de = distanceEstimate(P.camOff.xyz + dir * t, pr);
		r.smooth_ = de.smooth_;
		r.escaped = de.escaped;
		r.iters = de.iters;
		if (de.dist <= pr) { r.status = 1; break; }
		t = t + max(de.dist, pr * 0.5);
	}
	r.t = t;
	return r;
}

fn palette(t: f32) -> vec3<f32> {
	return vec3<f32>(0.42, 0.52, 0.66) + vec3<f32>(0.30, 0.28, 0.26) * cos(6.2831853 * (t + vec3<f32>(0.00, 0.12, 0.30)));
}

fn background(dir: vec3<f32>) -> vec3<f32> {
	let h = clamp(0.5 + 0.5 * dir.y, 0.0, 1.0);
	return mix(vec3<f32>(0.105, 0.135, 0.215), vec3<f32>(0.030, 0.042, 0.085), h);
}

fn hazeColor(bg: vec3<f32>) -> vec3<f32> {
	return mix(bg, vec3<f32>(0.50, 0.64, 0.86), 0.6);
}

// Gradient of the distance estimate at the hit (tetrahedron taps).
fn surfaceNormal(dir: vec3<f32>, t: f32, pr: f32) -> vec3<f32> {
	let eps = max(pr, 1.0e-30);
	let dch = P.camOff.xyz + dir * t;
	let k0 = vec3<f32>(1.0, -1.0, -1.0);
	let k1 = vec3<f32>(-1.0, -1.0, 1.0);
	let k2 = vec3<f32>(-1.0, 1.0, -1.0);
	let k3 = vec3<f32>(1.0, 1.0, 1.0);
	let g = k0 * distanceEstimate(dch + eps * k0, pr).dist
		+ k1 * distanceEstimate(dch + eps * k1, pr).dist
		+ k2 * distanceEstimate(dch + eps * k2, pr).dist
		+ k3 * distanceEstimate(dch + eps * k3, pr).dist;
	let l2 = dot(g, g);
	if (l2 > 0.0) { return g * inverseSqrt(l2); }
	return -dir;
}

fn shade(r: March, dir: vec3<f32>) -> vec3<f32> {
	let bg = background(dir);
	let stepFactor = clamp(f32(r.steps) / max(f32(P.marchI.x), 1.0), 0.0, 1.0);
	if (r.status == 2) {
		return mix(bg, vec3<f32>(0.35, 0.55, 0.85), 0.25 * stepFactor * stepFactor);
	}
	let pr = r.t * P.ray00.w;
	var n = surfaceNormal(dir, r.t, pr);
	if (dot(n, dir) > 0.0) { n = -n; }
	let headlight = clamp(dot(n, -dir), 0.0, 1.0);
	let key = clamp(dot(n, normalize(vec3<f32>(0.5, 0.7, 0.4))), 0.0, 1.0);
	let light = 0.12 + 0.5 * headlight + 0.3 * key;
	var ao = 1.0 - 0.7 * sqrt(stepFactor);
	if (r.status == 3) { ao = ao * 0.6; }
	let albedo = palette(log2(1.0 + r.smooth_) * 0.22);
	var color = albedo * light * ao;
	// Detail limit: fade into haze as the orbit uses up the iteration limit.
	var used = 1.0;
	if (r.escaped) { used = r.smooth_ / max(f32(P.sizeI.w), 1.0); }
	color = mix(color, hazeColor(bg), smoothstep(P.misc.y, 1.0, used));
	let fog = 1.0 - exp(-r.t * 0.06);
	return mix(color, bg, clamp(fog, 0.0, 1.0));
}

@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) id: vec3<u32>) {
	if (id.x >= u32(P.sizeI.x) || id.y >= u32(P.sizeI.y)) { return; }
	let dir = pixelDirection(vec2<f32>(f32(id.x), f32(id.y)));
	let r = marchRay(dir);
	textureStore(Out, vec2<i32>(id.xy), vec4<f32>(shade(r, dir), 1.0));
}

// One thread: distance estimate at the camera (drives the movement speed and the reference upkeep) and a march of the
// centre ray (what the crosshair looks at).
@compute @workgroup_size(1)
fn probe() {
	let de = distanceEstimate(P.camOff.xyz, 0.0);
	let dir = pixelDirection(vec2<f32>(f32(P.sizeI.x) * 0.5, f32(P.sizeI.y) * 0.5));
	let r = marchRay(dir);
	// Distance to the set, capped by the distance to the bounding sphere (the estimate overshoots far away).
	let outside = length(P.refC.xyz + P.camOff.xyz) - P.refC.w;
	Probe[0] = select(de.dist, min(de.dist, outside), outside > 0.0);
	Probe[1] = select(0.0, 1.0, de.escaped);
	Probe[2] = select(-1.0, r.t, r.status == 1);
	Probe[3] = f32(r.steps);
	Probe[4] = f32(de.iters);
	Probe[5] = f32(r.status);
}
