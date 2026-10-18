// Mandelbulb perturbation ray marcher: a WGSL port of Plugins/FractalRenderer/Shaders for the Mandelbulb
// (FractalMathCommon.ush, FractalPowerMap.ush, FormulaMandelbulb.ush, FractalDE.ush, FractalRender.ush). Function names
// keep the FP_ prefix of the originals so the two can be read side by side.
//
// Every sample is c = C_ref + dc with its orbit z_n = Z_n + d_n. Z_n is the reference orbit computed on the CPU in
// double-double (lib.js) and dc, d_n are f32. Each step evaluates g_p(Z + d) - g_p(Z) with exact identities, a pixel that
// leaves the reference rebases onto orbit point 0, and the linear series skip lets each pixel start where its
// perturbation stops being linear. World units are fractal units here (Scale = 1).

// Compiled as two pipelines: STATIC_POWER = 8 (the classic Mandelbulb, loops unrolled; the FP_STATIC_POWER_8 permutation)
// and 0 (any power).
override STATIC_POWER: i32 = 0;

struct Params {
	ray00: vec4<f32>,   // direction through pixel (0, 0), unnormalised (xyz); hit threshold per unit distance (w)
	rayDX: vec4<f32>,   // per-pixel step to the right (xyz); power (w)
	rayDY: vec4<f32>,   // per-pixel step down (xyz); bailout (w)
	camOff: vec4<f32>,  // camera - C_ref (xyz); max ray distance (w)
	refC: vec4<f32>,    // float(C_ref) (xyz); bounding radius (w)
	sizeI: vec4<i32>,   // marched width, height, orbit length, max iterations
	marchI: vec4<i32>,  // max ray steps, 0, 0, 0
	misc: vec4<f32>,    // footprint above which plain float iteration is used (x); full-resolution pixels per marched pixel (zw)
};

@group(0) @binding(0) var<uniform> P: Params;
@group(0) @binding(1) var<storage, read> Orbit: array<vec4<f32>>;
@group(0) @binding(2) var Out: texture_storage_2d<rgba8unorm, write>;
@group(0) @binding(3) var<storage, read_write> Probe: array<f32>;

const FP_PI: f32 = 3.14159265358979;
const FP_TWO_PI: f32 = 6.28318530717959;
const FP_HALF_PI: f32 = 1.57079632679490;
const FP_TWO_OVER_PI: f32 = 0.636619772367581;

// Orbit layout (lib.js generateReferenceOrbit): per point 3 float4 of reference data, then 3 series rows.
const FP_ORBIT_STRIDE: i32 = 6;
const FP_SKIP_ROW: i32 = 3;

// ---------------------------------------------------------------------------------------------
// FractalMathCommon.ush
// ---------------------------------------------------------------------------------------------

// sin/cos with relative accuracy for tiny |x| (Cody-Waite reduction by pi/2, Cephes polynomials). Returns (sin, cos).
fn FP_SinCos(x: f32) -> vec2<f32> {
	let k = round(x * FP_TWO_OVER_PI);
	var r = x - k * 1.5703125;
	r = r - k * 4.837512969970703125e-4;
	r = r - k * 7.54978995489188216e-8;
	let r2 = r * r;
	let sinR = r + r * r2 * (-1.6666654611e-1 + r2 * (8.3321608736e-3 + r2 * -1.9515295891e-4));
	let cosR = 1.0 - 0.5 * r2 + r2 * r2 * (4.166664568298827e-2 + r2 * (-1.388731625493765e-3 + r2 * 2.443315711809948e-5));
	let q = i32(k) & 3;
	let s = select(sinR, cosR, (q & 1) != 0);
	let c = select(cosR, sinR, (q & 1) != 0);
	return vec2<f32>(select(s, -s, (q & 2) != 0), select(c, -c, ((q + 1) & 2) != 0));
}

// atan(t) for t in [0, 1], minimax in relative error.
fn FP_AtanUnit(t: f32) -> f32 {
	let z = t * t;
	var p = 2.849885728e-03;
	p = p * z - 1.606861316e-02;
	p = p * z + 4.269149527e-02;
	p = p * z - 7.504292578e-02;
	p = p * z + 1.064093262e-01;
	p = p * z - 1.420364380e-01;
	p = p * z + 1.999261975e-01;
	p = p * z - 3.333307207e-01;
	p = p * z + 1.0;
	return t * p;
}

// atan2 with relative accuracy for small angles. atan2(0, 0) = 0.
fn FP_Atan2(y: f32, x: f32) -> f32 {
	let ax = abs(x);
	let ay = abs(y);
	let swap = ay > ax;
	let num = select(ay, ax, swap);
	let den = select(ax, ay, swap);
	var a = FP_AtanUnit(select(0.0, num / den, den > 0.0));
	a = select(a, FP_HALF_PI - a, swap);
	a = select(a, FP_PI - a, x < 0.0);
	return select(a, -a, y < 0.0);
}

// log(1 + x) with relative accuracy for small |x|.
fn FP_Log1p(x: f32) -> f32 {
	if (abs(x) > 0.5) { return log(1.0 + x); }
	let t = x / (2.0 + x);
	let t2 = t * t;
	let p = 0.333333333 + t2 * (0.2 + t2 * (0.142857143 + t2 * (0.111111111 + t2 * (0.0909090909 + t2 * (0.0769230769 + t2 * 0.0666666667)))));
	return 2.0 * t + 2.0 * t * t2 * p;
}

// exp(x) - 1 with relative accuracy for small |x|.
fn FP_Expm1(x: f32) -> f32 {
	if (abs(x) > 1.0) { return exp(x) - 1.0; }
	return x * (1.0 + x * (0.5 + x * (0.166666667 + x * (0.0416666667 + x * (0.00833333333 + x * (0.00138888889
		+ x * (1.98412698e-4 + x * (2.48015873e-5 + x * (2.75573192e-6 + x * (2.75573192e-7 + x * 2.50521084e-8))))))))));
}

fn FP_Pow2Normalizer(v: f32) -> f32 { return bitcast<f32>(0x7F000000u - (bitcast<u32>(v) & 0x7F800000u)); }
fn FP_Pow2Denormalizer(v: f32) -> f32 { return bitcast<f32>(bitcast<u32>(v) & 0x7F800000u); }

// Integer power for the trig-free paths, 0 for non-integer powers.
fn FP_IntegerPower(power: f32) -> i32 {
	if (STATIC_POWER > 0) { return STATIC_POWER; }
	if (power == floor(power) && power >= 2.0 && power <= 32.0) { return i32(power); }
	return 0;
}

// (a + ib)^p for integer p >= 1 by binary powering.
fn FP_ComplexPowInt(a: f32, b: f32, p: i32) -> vec2<f32> {
	var ra = a;
	var rb = b;
	for (var bit = i32(firstLeadingBit(u32(p))) - 1; bit >= 0; bit = bit - 1) {
		let na = ra * ra - rb * rb;
		rb = 2.0 * ra * rb;
		ra = na;
		if (((p >> u32(bit)) & 1) != 0) {
			let ma = ra * a - rb * b;
			rb = ra * b + rb * a;
			ra = ma;
		}
	}
	return vec2<f32>(ra, rb);
}

// A rotation (V, S) = (1 - cos a, sin a) raised to the integer power p without cancellation.
fn FP_VersSinPowInt(v: f32, s: f32, p: i32) -> vec2<f32> {
	var rv = v;
	var rs = s;
	for (var bit = i32(firstLeadingBit(u32(p))) - 1; bit >= 0; bit = bit - 1) {
		let ns = 2.0 * rs * (1.0 - rv);
		rv = rv * (2.0 - rv) + rs * rs;
		rs = ns;
		if (((p >> u32(bit)) & 1) != 0) {
			let ms = rs * (1.0 - v) + (1.0 - rv) * s;
			rv = rv + v - rv * v + rs * s;
			rs = ms;
		}
	}
	return vec2<f32>(rv, rs);
}

fn FP_VersineFromCosSin(c: f32, s: f32) -> f32 {
	if (c >= 0.0) { return s * s / (1.0 + c); }
	return 1.0 - c;
}

// ---------------------------------------------------------------------------------------------
// FractalPowerMap.ush
// ---------------------------------------------------------------------------------------------

struct FPPowerRef {
	Z: vec3<f32>,
	R: f32,
	Rho: f32,
	RPow: f32,
	Phi: f32,
	InvR: f32,
	SinPT: f32,
	CosPT: f32,
	SinPP: f32,
	CosPP: f32,
}

fn FP_LoadPowerRef(base: i32) -> FPPowerRef {
	let a = Orbit[base];
	let b = Orbit[base + 1];
	let c = Orbit[base + 2];
	var r: FPPowerRef;
	r.Z = a.xyz; r.R = a.w;
	r.Rho = b.x; r.RPow = b.y; r.Phi = b.z; r.InvR = b.w;
	r.SinPT = c.x; r.CosPT = c.y; r.SinPP = c.z; r.CosPP = c.w;
	return r;
}

// g_p(W) evaluated directly, |W| = rw, 1/|W| = invRw; .w = |W|^(p-1).
fn FP_MandelbulbPower(w: vec3<f32>, rw: f32, invRw: f32, power: f32, powerInt: i32) -> vec4<f32> {
	if (powerInt > 0) {
		let maxXY = max(abs(w.x), abs(w.y));
		let xyScale = FP_Pow2Normalizer(maxXY);
		let swx = w.x * xyScale;
		let swy = w.y * xyScale;
		let sRho2 = swx * swx + swy * swy;
		let invSRho = select(0.0, inverseSqrt(sRho2), sRho2 > 0.0);
		let rhoW = sRho2 * invSRho * FP_Pow2Denormalizer(maxXY);
		let t = FP_ComplexPowInt(w.z * invRw, rhoW * invRw, powerInt);
		let q = FP_ComplexPowInt(select(1.0, swx * invSRho, sRho2 > 0.0), swy * invSRho, powerInt);
		var rwPowM1 = 1.0;
		if (powerInt > 1) { rwPowM1 = FP_ComplexPowInt(rw, 0.0, powerInt - 1).x; }
		let rwPow = rwPowM1 * rw;
		return vec4<f32>(rwPow * vec3<f32>(t.y * q.x, t.y * q.y, t.x), rwPowM1);
	}
	let rho = sqrt(w.x * w.x + w.y * w.y);
	let theta = FP_Atan2(rho, w.z);
	let phi = FP_Atan2(w.y, w.x);
	let rwPow = select(0.0, exp2(power * log2(rw)), rw > 0.0);
	let st = FP_SinCos(power * theta);
	let sp = FP_SinCos(power * phi);
	return vec4<f32>(rwPow * vec3<f32>(st.x * sp.y, st.x * sp.x, st.y), rwPow * invRw);
}

// g_p(Z~ + D) - g_p(Z~) for the reference point Ref, W = Z~ + D; .w = |W|^(p-1).
fn FP_PerturbedPowerDelta(rf: FPPowerRef, d: vec3<f32>, w: vec3<f32>, rw: f32, invRw: f32, power: f32, powerInt: i32) -> vec4<f32> {
	// The reference is negligible next to the full point (after a rebase Ref = 0): evaluate directly.
	if (rf.R <= 0.5 * rw) {
		let gw = FP_MandelbulbPower(w, rw, invRw, power, powerInt);
		let gref = rf.RPow * vec3<f32>(rf.SinPT * rf.CosPP, rf.SinPT * rf.SinPP, rf.CosPT);
		return vec4<f32>(gw.xyz - gref, gw.w);
	}

	let zz = rf.Z.z;
	// Radial factor u = (r/R)^p - 1 with s = dr/R, dr computed without cancellation.
	let s = (2.0 * dot(rf.Z, d) + dot(d, d)) / (rw + rf.R) * rf.InvR;
	var u: f32;
	var onePlusU: f32;
	if (powerInt > 0) {
		let base = 1.0 + s;
		var a = s;
		var b = base;
		for (var bit = i32(firstLeadingBit(u32(powerInt))) - 1; bit >= 0; bit = bit - 1) {
			a = a * (a + 2.0);
			b = b * b;
			if (((powerInt >> u32(bit)) & 1) != 0) {
				a = a * base + s;
				b = b * base;
			}
		}
		u = a;
		onePlusU = b;
	} else {
		u = FP_Expm1(power * FP_Log1p(s));
		onePlusU = 1.0 + u;
	}

	// xy-plane quantities rescaled by an exact power of two so that products of small components never underflow.
	let maxXY = max(max(abs(rf.Z.x), abs(rf.Z.y)), max(abs(w.x), abs(w.y)));
	let xyScale = FP_Pow2Normalizer(maxXY);
	let invXYScale = FP_Pow2Denormalizer(maxXY);
	let sx = rf.Z.x * xyScale;
	let sy = rf.Z.y * xyScale;
	let sdx = d.x * xyScale;
	let sdy = d.y * xyScale;
	let swx = w.x * xyScale;
	let swy = w.y * xyScale;
	let sRho = rf.Rho * xyScale;
	let sRhoW = sqrt(swx * swx + swy * swy);
	let rhoW = sRhoW * invXYScale;

	// Azimuth difference: exact angle between (X, Y) and (wx, wy). On the axis phi_ref := 0.
	let onAxis = rf.Rho <= 0.0;
	let ux = select(sx, 1.0, onAxis);
	let uy = select(sy, 0.0, onAxis);
	let crossP = ux * sdy - uy * sdx;
	let dotP = ux * swx + uy * swy;

	let sdRho = ((2.0 * sx + sdx) * sdx + (2.0 * sy + sdy) * sdy) / max(sRhoW + sRho, 1.0e-30);
	let dRho = sdRho * invXYScale;
	let crossT = zz * dRho - rf.Rho * d.z;
	let dotT = rf.Rho * rhoW + zz * w.z;

	if (powerInt > 0) {
		let normT = rf.InvR * invRw;
		let sinDT = crossT * normT;
		let cosDT = dotT * normT;
		let vsT = FP_VersSinPowInt(FP_VersineFromCosSin(cosDT, sinDT), sinDT, powerInt);
		let sRhoU2 = ux * ux + uy * uy;
		let normP = select(0.0, inverseSqrt(sRhoU2) / sRhoW, sRhoW > 0.0);
		let sinDP = crossP * normP;
		let cosDP = select(1.0, dotP * normP, sRhoW > 0.0);
		let vsP = FP_VersSinPowInt(FP_VersineFromCosSin(cosDP, sinDP), sinDP, powerInt);
		let dSinT = rf.CosPT * vsT.y - rf.SinPT * vsT.x;
		let dCosT = -rf.SinPT * vsT.y - rf.CosPT * vsT.x;
		let dSinP = rf.CosPP * vsP.y - rf.SinPP * vsP.x;
		let dCosP = -rf.SinPP * vsP.y - rf.CosPP * vsP.x;
		let sinTW = rf.SinPT + dSinT;
		let cosTW = rf.CosPT + dCosT;
		let sinPW = rf.SinPP + dSinP;
		let cosPW = rf.CosPP + dCosP;
		let fwi = vec3<f32>(sinTW * cosPW, sinTW * sinPW, cosTW);
		let dfi = vec3<f32>(dSinT * cosPW + rf.SinPT * dCosP, dSinT * sinPW + rf.SinPT * dSinP, dCosT);
		return vec4<f32>(rf.RPow * (u * fwi + dfi), rf.RPow * onePlusU * invRw);
	}

	var dPhi = FP_Atan2(crossP, dotP);
	// Keep phi_ref + dphi inside (-pi, pi] like atan2 does (the seam of non-integer powers).
	let phiW = rf.Phi + dPhi;
	dPhi = dPhi + select(select(0.0, FP_TWO_PI, phiW <= -FP_PI), -FP_TWO_PI, phiW > FP_PI);
	let dTheta = FP_Atan2(crossT, dotT);

	let ab = FP_SinCos(0.5 * power * dTheta); // (SA, CA)
	let bb = FP_SinCos(0.5 * power * dPhi);   // (SB, CB)
	let sinTMid = rf.SinPT * ab.y + rf.CosPT * ab.x;
	let cosTMid = rf.CosPT * ab.y - rf.SinPT * ab.x;
	let sinPMid = rf.SinPP * bb.y + rf.CosPP * bb.x;
	let cosPMid = rf.CosPP * bb.y - rf.SinPP * bb.x;
	let dSinT = 2.0 * cosTMid * ab.x;
	let dCosT = -2.0 * sinTMid * ab.x;
	let dSinP = 2.0 * cosPMid * bb.x;
	let dCosP = -2.0 * sinPMid * bb.x;
	let sinTW = rf.SinPT + dSinT;
	let cosTW = rf.CosPT + dCosT;
	let sinPW = rf.SinPP + dSinP;
	let cosPW = rf.CosPP + dCosP;
	let fw = vec3<f32>(sinTW * cosPW, sinTW * sinPW, cosTW);
	let df = vec3<f32>(dSinT * cosPW + rf.SinPT * dCosP, dSinT * sinPW + rf.SinPT * dSinP, dCosT);
	return vec4<f32>(rf.RPow * (u * fw + df), rf.RPow * onePlusU * invRw);
}

// ---------------------------------------------------------------------------------------------
// FractalDE.ush (Mandelbulb: adds c, escape-time family)
// ---------------------------------------------------------------------------------------------

struct FPDEResult {
	Distance: f32,
	Iterations: i32,
	bEscaped: bool,
	Skipped: i32,
	Smooth: f32,
}

fn FP_SmoothIteration(n: i32, rw: f32, bailout: f32, power: f32, escaped: bool) -> f32 {
	if (!escaped) { return f32(n); }
	let ratio = log(max(rw, 1.0001)) / log(max(bailout, 1.0001));
	return f32(n) + 1.0 - log(max(ratio, 1.0)) / log(max(abs(power), 1.5));
}

// |W| from |W|^2 and 1/|W|; |W|^2 overflows float above ~1.8e19 (high exponents).
fn FP_SafeLength(w: vec3<f32>, rw2: f32, invRw: f32) -> f32 {
	if (rw2 > 3.0e38) { return length(w * 5.42101086e-20) * 1.84467441e19; }
	return rw2 * invRw;
}

fn FP_FinalDistance(escaped: bool, rw: f32, drs: f32) -> f32 {
	return select(0.0, 0.5 * rw * log(max(rw, 1.0e-6)) / max(drs, 1.0e-37), escaped);
}

fn FP_Result(n: i32, rw: f32, drs: f32, escaped: bool, skipped: i32) -> FPDEResult {
	var r: FPDEResult;
	r.Iterations = n;
	r.bEscaped = escaped;
	r.Skipped = skipped;
	r.Smooth = FP_SmoothIteration(n, rw, P.rayDY.w, P.rayDX.w, escaped);
	r.Distance = FP_FinalDistance(escaped, rw, drs);
	return r;
}

// DE at c = C_ref + dc.
fn FP_PerturbedDE(dc: vec3<f32>) -> FPDEResult {
	let power = P.rayDX.w;
	let powerInt = FP_IntegerPower(power);
	let orbitLength = P.sizeI.z;
	let maxIterations = P.sizeI.w;
	var d = dc;
	var m = 1;
	var drs = 1.0;
	var rw = 0.0;
	var escaped = false;
	var n = 1;
	var skipped = 0;

	// Linear series skip: while the perturbation stays linear d_m = A_m dc. Jump to the deepest m whose validity radius
	// covers |dc| (radii are non-increasing in m).
	{
		let dcLength = 1.7320508 * max(max(abs(dc.x), abs(dc.y)), abs(dc.z));
		var lo = 1;
		var hi = orbitLength - 1;
		loop {
			if (lo >= hi) { break; }
			let mid = (lo + hi + 1) >> 1u;
			if (Orbit[FP_ORBIT_STRIDE * mid + FP_SKIP_ROW].w >= dcLength) { lo = mid; } else { hi = mid - 1; }
		}
		if (lo > 1) {
			let row = FP_ORBIT_STRIDE * lo + FP_SKIP_ROW;
			let a0 = Orbit[row];
			let a1 = Orbit[row + 1];
			let a2 = Orbit[row + 2];
			d = vec3<f32>(dot(a0.xyz, dc), dot(a1.xyz, dc), dot(a2.xyz, dc));
			drs = a1.w;
			m = lo;
			n = lo;
			skipped = lo - 1;
		}
	}

	loop {
		if (n >= maxIterations) { break; }
		let base = FP_ORBIT_STRIDE * m;
		let refZ = Orbit[base].xyz;
		let w = refZ + d;
		let rw2 = dot(w, w);
		let invRw = select(0.0, inverseSqrt(rw2), rw2 > 0.0);
		rw = FP_SafeLength(w, rw2, invRw);
		// |Z + d|^2 - Bailout^2 = 2 Z.d + d.d - (Bailout^2 - |Z|^2), the margin exact from the CPU.
		if (dot(2.0 * refZ + d, d) > Orbit[base + FP_SKIP_ROW + 2].w) {
			escaped = true;
			break;
		}
		// Left the reference (|z| < |d|) or ran out of orbit: rebase onto orbit point 0.
		var b = base;
		if (rw2 < dot(d, d) || m >= orbitLength - 1) {
			d = w;
			m = 0;
			b = 0;
		}
		let g = FP_PerturbedPowerDelta(FP_LoadPowerRef(b), d, w, rw, invRw, power, powerInt);
		drs = power * g.w * drs + 1.0;
		d = g.xyz + dc;
		m = m + 1;
		n = n + 1;
	}
	return FP_Result(n, rw, drs, escaped, skipped);
}

// Plain float DE at an absolute position.
fn FP_DirectDE(c: vec3<f32>) -> FPDEResult {
	let power = P.rayDX.w;
	let powerInt = FP_IntegerPower(power);
	let bailout2 = P.rayDY.w * P.rayDY.w;
	let maxIterations = P.sizeI.w;
	var z = c;
	var drs = 1.0;
	var rw = 0.0;
	var escaped = false;
	var n = 1;
	loop {
		if (n >= maxIterations) { break; }
		let rw2 = dot(z, z);
		let invRw = select(0.0, inverseSqrt(rw2), rw2 > 0.0);
		rw = FP_SafeLength(z, rw2, invRw);
		if (rw2 > bailout2) {
			escaped = true;
			break;
		}
		let g = FP_MandelbulbPower(z, rw, invRw, power, powerInt);
		drs = power * g.w * drs + 1.0;
		z = g.xyz + c;
		n = n + 1;
	}
	return FP_Result(n, rw, drs, escaped, 0);
}

// ---------------------------------------------------------------------------------------------
// FractalRender.ush
// ---------------------------------------------------------------------------------------------

// Plain float where the pixel footprint is large enough for absolute float coordinates (the hybrid switch) or while
// there is no reference orbit, perturbation otherwise.
fn FP_DistanceEstimate(dc: vec3<f32>, pixelRadius: f32) -> FPDEResult {
	if (P.sizeI.z < 2 || (P.misc.x > 0.0 && pixelRadius > P.misc.x)) {
		return FP_DirectDE(P.refC.xyz + dc);
	}
	return FP_PerturbedDE(dc);
}

struct FPMarchResult {
	Distance: f32,
	Steps: i32,
	Status: i32,          // 1 hit, 2 left the scene, 3 out of steps
	TotalIterations: i32,
	TotalSkipped: i32,
	LastIterations: i32,
	LastSmooth: f32,
	bLastEscaped: bool,
}

// Direction through the centre of the marched pixel; the basis is in full-resolution pixels.
fn FP_PixelRayDirection(pixel: vec2<f32>) -> vec3<f32> {
	let full = (pixel + 0.5) * P.misc.zw - 0.5;
	return normalize(P.ray00.xyz + full.x * P.rayDX.xyz + full.y * P.rayDY.xyz);
}

fn FP_MarchRay(dir: vec3<f32>) -> FPMarchResult {
	var r: FPMarchResult;
	r.Distance = 0.0;
	r.Steps = 0;
	r.Status = 3;
	r.TotalIterations = 0;
	r.TotalSkipped = 0;
	r.LastIterations = 0;
	r.LastSmooth = 0.0;
	r.bLastEscaped = false;
	var t = 0.0;
	var tEnd = P.camOff.w;
	// Bounding sphere of the set: a ray that misses it hits nothing, one that crosses it is marched from where it enters.
	{
		let radius = P.refC.w * 1.001;
		let o = P.refC.xyz + P.camOff.xyz;
		let c = dot(o, o) - radius * radius;
		if (c > 0.0) {
			let b = dot(o, dir);
			let disc = b * b - c;
			if (b >= 0.0 || disc < 0.0) {
				r.Status = 2;
				return r;
			}
			let root = sqrt(disc);
			t = -b - root;
			tEnd = min(tEnd, -b + root);
		}
	}
	loop {
		if (r.Steps >= P.marchI.x) { break; }
		if (t >= tEnd) { r.Status = 2; break; }
		r.Steps = r.Steps + 1;
		let pixelRadius = t * P.ray00.w;
		let de = FP_DistanceEstimate(P.camOff.xyz + dir * t, pixelRadius);
		r.TotalIterations = r.TotalIterations + de.Iterations;
		r.TotalSkipped = r.TotalSkipped + de.Skipped;
		r.LastIterations = de.Iterations;
		r.LastSmooth = de.Smooth;
		r.bLastEscaped = de.bEscaped;
		if (de.Distance <= pixelRadius) { r.Status = 1; break; }
		t = t + max(de.Distance, pixelRadius * 0.5);
	}
	r.Distance = t;
	return r;
}

fn FP_Palette(t: f32) -> vec3<f32> {
	return vec3<f32>(0.42, 0.52, 0.66) + vec3<f32>(0.30, 0.28, 0.26) * cos(6.2831853 * (t + vec3<f32>(0.00, 0.12, 0.30)));
}

fn FP_SurfaceNormal(dir: vec3<f32>, t: f32, pixelRadius: f32) -> vec3<f32> {
	let eps = max(pixelRadius, 1.0e-6 * max(t, 1.0e-30));
	let dch = P.camOff.xyz + dir * t;
	let k0 = vec3<f32>(1.0, -1.0, -1.0);
	let k1 = vec3<f32>(-1.0, -1.0, 1.0);
	let k2 = vec3<f32>(-1.0, 1.0, -1.0);
	let k3 = vec3<f32>(1.0, 1.0, 1.0);
	let g = k0 * FP_DistanceEstimate(dch + eps * k0, pixelRadius).Distance
		+ k1 * FP_DistanceEstimate(dch + eps * k1, pixelRadius).Distance
		+ k2 * FP_DistanceEstimate(dch + eps * k2, pixelRadius).Distance
		+ k3 * FP_DistanceEstimate(dch + eps * k3, pixelRadius).Distance;
	let l2 = dot(g, g);
	return select(-dir, g * inverseSqrt(l2), l2 > 0.0);
}

// Share of the iteration limit at which a hit starts to fade into haze (fully hazed at the limit).
const FP_HAZE_START: f32 = 0.55;

fn FP_HazeColor(background: vec3<f32>) -> vec3<f32> {
	return mix(background, vec3<f32>(0.50, 0.64, 0.86), 0.6);
}

// The scene behind the fractal (Unreal draws the level's sky there): a cool vertical gradient.
fn Background(dir: vec3<f32>) -> vec3<f32> {
	let h = clamp(0.5 + 0.5 * dir.z, 0.0, 1.0);
	return mix(vec3<f32>(0.105, 0.135, 0.215), vec3<f32>(0.030, 0.042, 0.085), h);
}

fn FP_Shade(r: FPMarchResult, dir: vec3<f32>, background: vec3<f32>) -> vec3<f32> {
	let stepFactor = clamp(f32(r.Steps) / max(f32(P.marchI.x), 1.0), 0.0, 1.0);
	if (r.Status == 2) {
		return mix(background, vec3<f32>(0.35, 0.55, 0.85), 0.25 * stepFactor * stepFactor);
	}
	let pixelRadius = r.Distance * P.ray00.w;
	var n = FP_SurfaceNormal(dir, r.Distance, pixelRadius);
	n = select(n, -n, dot(n, dir) > 0.0);
	let headlight = clamp(dot(n, -dir), 0.0, 1.0);
	let key = clamp(dot(n, normalize(vec3<f32>(0.5, 0.7, 0.4))), 0.0, 1.0);
	let light = 0.12 + 0.5 * headlight + 0.3 * key;
	var ao = 1.0 - 0.7 * sqrt(stepFactor);
	if (r.Status == 3) { ao = ao * 0.6; }
	let albedo = FP_Palette(log2(1.0 + r.LastSmooth) * 0.22);
	var color = albedo * light * ao;
	let used = select(1.0, r.LastSmooth / max(f32(P.sizeI.w), 1.0), r.bLastEscaped);
	color = mix(color, FP_HazeColor(background), smoothstep(FP_HAZE_START, 1.0, used));
	let fog = 1.0 - exp(-r.Distance * 0.25);
	return mix(color, background, clamp(fog, 0.0, 1.0));
}

@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) id: vec3<u32>) {
	if (id.x >= u32(P.sizeI.x) || id.y >= u32(P.sizeI.y)) { return; }
	let dir = FP_PixelRayDirection(vec2<f32>(f32(id.x), f32(id.y)));
	let r = FP_MarchRay(dir);
	textureStore(Out, vec2<i32>(id.xy), vec4<f32>(FP_Shade(r, dir, Background(dir)), 1.0));
}

// One thread: the distance at the camera (capped by the bounding sphere, FDistanceEstimate::SafeDistance) and a march
// of the centre ray, read back by the page for the speed, the HUD and the reference upkeep.
@compute @workgroup_size(1)
fn probe() {
	let de = FP_DistanceEstimate(P.camOff.xyz, 0.0);
	let outside = length(P.refC.xyz + P.camOff.xyz) - P.refC.w;
	let full = vec2<f32>(f32(P.sizeI.x), f32(P.sizeI.y)) * P.misc.zw * 0.5 - 0.5;
	let dir = normalize(P.ray00.xyz + full.x * P.rayDX.xyz + full.y * P.rayDY.xyz);
	let r = FP_MarchRay(dir);
	Probe[0] = select(de.Distance, min(de.Distance, outside), outside > 0.0);
	Probe[1] = select(0.0, 1.0, de.bEscaped);
	Probe[2] = select(-1.0, r.Distance, r.Status == 1);
	Probe[3] = f32(r.Steps);
	Probe[4] = f32(de.Iterations);
	Probe[5] = f32(r.Status);
	Probe[6] = f32(r.TotalIterations);
	Probe[7] = f32(r.TotalSkipped);
}
