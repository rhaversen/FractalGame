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
// Benchmark pipelines count the work per frame in Stats (distance estimates, executed and skipped iterations).
override BENCH: bool = false;
// Over-relaxed sphere tracing (Keinert et al. 2014): steps of RELAX times the distance while consecutive spheres overlap,
// falling back to plain steps at the first miss. 1 = plain sphere tracing.
override RELAX: f32 = 1.0;
// Cone pre-pass: one ray per CONE_TILE x CONE_TILE marched pixels finds how far all of them can start (0 = off).
override CONE_TILE: u32 = 0u;

struct Params {
	ray00: vec4<f32>,   // direction through pixel (0, 0), unnormalised (xyz); hit threshold per unit distance (w)
	rayDX: vec4<f32>,   // per-pixel step to the right (xyz); power (w)
	rayDY: vec4<f32>,   // per-pixel step down (xyz); bailout (w)
	camOff: vec4<f32>,  // camera - C_ref (xyz); max ray distance (w)
	refC: vec4<f32>,    // float(C_ref) (xyz); bounding radius (w)
	sizeI: vec4<i32>,   // marched width, height, orbit length, max iterations
	marchI: vec4<i32>,  // max ray steps, haze base (iterations the camera's own orbit needs), 0, 0
	misc: vec4<f32>,    // footprint above which plain float iteration is used (x); Jacobian scale (y); full-resolution pixels per marched pixel (zw)
};

@group(0) @binding(0) var<uniform> P: Params;
@group(0) @binding(1) var<storage, read> Orbit: array<vec4<f32>>;
@group(0) @binding(2) var Out: texture_storage_2d<rgba8unorm, write>;
@group(0) @binding(3) var<storage, read_write> Probe: array<f32>;
@group(0) @binding(4) var<storage, read_write> Stats: array<atomic<u32>, 8>;
@group(0) @binding(5) var ConeOut: texture_storage_2d<r32float, write>;
@group(0) @binding(6) var ConeIn: texture_2d<f32>;

var<private> gEstimates: u32 = 0u;
var<private> gIterations: u32 = 0u;
var<private> gSkipped: u32 = 0u;

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

// One power-map step: the value (g_p(W), or its difference to g_p(Z~)), |W|^(p-1), and the cos / sin of the multiplied
// angles p theta and p phi at W (for the Jacobian).
struct FPPowerStep {
	Value: vec3<f32>,
	RWPowM1: f32,
	Angles: vec4<f32>, // cos(p theta), sin(p theta), cos(p phi), sin(p phi)
}

// g_p(W) evaluated directly, |W| = rw, 1/|W| = invRw.
fn FP_MandelbulbPower(w: vec3<f32>, rw: f32, invRw: f32, power: f32, powerInt: i32) -> FPPowerStep {
	var o: FPPowerStep;
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
		o.Value = rwPow * vec3<f32>(t.y * q.x, t.y * q.y, t.x);
		o.RWPowM1 = rwPowM1;
		o.Angles = vec4<f32>(t.x, t.y, q.x, q.y);
		return o;
	}
	let rho = sqrt(w.x * w.x + w.y * w.y);
	let theta = FP_Atan2(rho, w.z);
	let phi = FP_Atan2(w.y, w.x);
	let rwPow = select(0.0, exp2(power * log2(rw)), rw > 0.0);
	let st = FP_SinCos(power * theta);
	let sp = FP_SinCos(power * phi);
	o.Value = rwPow * vec3<f32>(st.x * sp.y, st.x * sp.x, st.y);
	o.RWPowM1 = rwPow * invRw;
	o.Angles = vec4<f32>(st.y, st.x, sp.y, sp.x);
	return o;
}

// g_p(Z~ + D) - g_p(Z~) for the reference point Ref, W = Z~ + D.
fn FP_PerturbedPowerDelta(rf: FPPowerRef, d: vec3<f32>, w: vec3<f32>, rw: f32, invRw: f32, power: f32, powerInt: i32) -> FPPowerStep {
	// The reference is negligible next to the full point (after a rebase Ref = 0): evaluate directly.
	if (rf.R <= 0.5 * rw) {
		var gw = FP_MandelbulbPower(w, rw, invRw, power, powerInt);
		gw.Value = gw.Value - rf.RPow * vec3<f32>(rf.SinPT * rf.CosPP, rf.SinPT * rf.SinPP, rf.CosPT);
		return gw;
	}
	var o: FPPowerStep;

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
		o.Value = rf.RPow * (u * fwi + dfi);
		o.RWPowM1 = rf.RPow * onePlusU * invRw;
		o.Angles = vec4<f32>(cosTW, sinTW, cosPW, sinPW);
		return o;
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
	o.Value = rf.RPow * (u * fw + df);
	o.RWPowM1 = rf.RPow * onePlusU * invRw;
	o.Angles = vec4<f32>(cosTW, sinTW, cosPW, sinPW);
	return o;
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

// J = dz/dc as three rows, scaled by s = P.misc.y (about the camera's distance to the surface, so that the entries stay in
// float range at any depth; the derivative grows like 1 / distance). J' = J_g(W) J + s I with
//     J_g = p r^(p-1) [ e'_r e_r^T + e'_theta e_theta^T + (sin(p theta) / sin(theta)) e'_phi e_phi^T ]
// (unit vectors at W and at the multiplied angles). The map stretches the azimuth by sin(p theta) / sin(theta) less (or more)
// than the other directions; the scalar running derivative of the classic estimate ignores that, and over the hundreds of
// iterations of an orbit next to the surface its error compounds into distances wrong by many orders of magnitude.
struct FPJacobian {
	R0: vec3<f32>,
	R1: vec3<f32>,
	R2: vec3<f32>,
}

fn FP_JacobianStep(j: FPJacobian, w: vec3<f32>, invRw: f32, g: FPPowerStep, power: f32) -> FPJacobian {
	let s = P.misc.y;
	let maxXY = max(abs(w.x), abs(w.y));
	let xyScale = FP_Pow2Normalizer(maxXY);
	let sx = w.x * xyScale;
	let sy = w.y * xyScale;
	let sRho2 = sx * sx + sy * sy;
	let invSRho = select(0.0, inverseSqrt(sRho2), sRho2 > 0.0);
	let cp = select(1.0, sx * invSRho, sRho2 > 0.0);
	let sp = sy * invSRho;
	let st = sRho2 * invSRho * FP_Pow2Denormalizer(maxXY) * invRw;
	let ct = w.z * invRw;
	let er = vec3<f32>(st * cp, st * sp, ct);
	let et = vec3<f32>(ct * cp, ct * sp, -st);
	let cpt = g.Angles.x;
	let spt = g.Angles.y;
	let cpp = g.Angles.z;
	let spp = g.Angles.w;
	let az = select(power * sign(ct), clamp(spt / st, -abs(power), abs(power)), st > 1.0e-30);
	let k = power * g.RWPowM1;
	let a = er.x * j.R0 + er.y * j.R1 + er.z * j.R2;
	let b = et.x * j.R0 + et.y * j.R1 + et.z * j.R2;
	let c = (-sp * j.R0 + cp * j.R1) * az;
	var o: FPJacobian;
	o.R0 = k * (spt * cpp * a + cpt * cpp * b - spp * c) + vec3<f32>(s, 0.0, 0.0);
	o.R1 = k * (spt * spp * a + cpt * spp * b + cpp * c) + vec3<f32>(0.0, s, 0.0);
	o.R2 = k * (cpt * a - spt * b) + vec3<f32>(0.0, 0.0, s);
	return o;
}

// 0.5 r ln r / |grad r| with grad r = J^T W / r (fractal units; J is scaled by s).
fn FP_FinalDistance(escaped: bool, w: vec3<f32>, rw: f32, j: FPJacobian) -> f32 {
	let u = w / max(rw, 1.0e-30);
	let grad = u.x * j.R0 + u.y * j.R1 + u.z * j.R2;
	// |grad| without squaring (the squares of components above ~1e19 overflow float)
	let m = max(max(abs(grad.x), abs(grad.y)), abs(grad.z));
	let gradLength = m * length(grad / max(m, 1.0e-37));
	return select(0.0, 0.5 * rw * log(max(rw, 1.0e-6)) * P.misc.y / max(gradLength, 1.0e-37), escaped);
}

fn FP_Result(n: i32, w: vec3<f32>, rw: f32, j: FPJacobian, escaped: bool, skipped: i32) -> FPDEResult {
	if (BENCH) {
		gEstimates = gEstimates + 1u;
		gIterations = gIterations + u32(n - skipped);
		gSkipped = gSkipped + u32(skipped);
	}
	var r: FPDEResult;
	r.Iterations = n;
	r.bEscaped = escaped;
	r.Skipped = skipped;
	r.Smooth = FP_SmoothIteration(n, rw, P.rayDY.w, P.rayDX.w, escaped);
	r.Distance = FP_FinalDistance(escaped, w, rw, j);
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
	let s0 = P.misc.y;
	var jac = FPJacobian(vec3<f32>(s0, 0.0, 0.0), vec3<f32>(0.0, s0, 0.0), vec3<f32>(0.0, 0.0, s0));
	var lastW = vec3<f32>(0.0);
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
			// the series matrix A_m is dz_m / dc
			jac = FPJacobian(a0.xyz * s0, a1.xyz * s0, a2.xyz * s0);
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
		lastW = w;
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
		jac = FP_JacobianStep(jac, w, invRw, g, power);
		d = g.Value + dc;
		m = m + 1;
		n = n + 1;
	}
	return FP_Result(n, lastW, rw, jac, escaped, skipped);
}

// Plain float DE at an absolute position.
fn FP_DirectDE(c: vec3<f32>) -> FPDEResult {
	let power = P.rayDX.w;
	let powerInt = FP_IntegerPower(power);
	let bailout2 = P.rayDY.w * P.rayDY.w;
	let maxIterations = P.sizeI.w;
	var z = c;
	let s0 = P.misc.y;
	var jac = FPJacobian(vec3<f32>(s0, 0.0, 0.0), vec3<f32>(0.0, s0, 0.0), vec3<f32>(0.0, 0.0, s0));
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
		jac = FP_JacobianStep(jac, z, invRw, g, power);
		z = g.Value + c;
		n = n + 1;
	}
	return FP_Result(n, z, rw, jac, escaped, 0);
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

fn FP_MarchRay(dir: vec3<f32>, tStart: f32) -> FPMarchResult {
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
	// the cone pre-pass proved that nothing is hit before tStart (negative: nothing is hit at all)
	if (tStart < 0.0 || tStart >= tEnd) {
		r.Status = 2;
		r.Distance = tEnd;
		return r;
	}
	t = max(t, tStart);
	var omega = RELAX;
	var prevRadius = 0.0;
	var stepLength = 0.0;
	loop {
		if (r.Steps >= P.marchI.x) { break; }
		if (t >= tEnd) { r.Status = 2; break; }
		r.Steps = r.Steps + 1;
		let pixelRadius = t * P.ray00.w;
		let de = FP_DistanceEstimate(P.camOff.xyz + dir * t, pixelRadius);
		r.TotalIterations = r.TotalIterations + de.Iterations;
		r.TotalSkipped = r.TotalSkipped + de.Skipped;
		let radius = de.Distance;
		if (omega > 1.0 && radius + prevRadius < stepLength) {
			// the relaxed step left the previous sphere: go back inside it and continue with plain steps
			stepLength = stepLength - omega * stepLength;
			omega = 1.0;
		} else {
			r.LastIterations = de.Iterations;
			r.LastSmooth = de.Smooth;
			r.bLastEscaped = de.bEscaped;
			if (radius <= pixelRadius) { r.Status = 1; break; }
			stepLength = max(radius, pixelRadius * 0.5) * omega;
		}
		prevRadius = radius;
		t = t + stepLength;
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
	// Detail limit: the share of the iterations beyond the camera's own escape count (the haze base) a hit needed.
	let hazeBase = f32(P.marchI.y);
	let used = select(1.0, (r.LastSmooth - hazeBase) / max(f32(P.sizeI.w) - hazeBase, 1.0), r.bLastEscaped);
	color = mix(color, FP_HazeColor(background), smoothstep(FP_HAZE_START, 1.0, used));
	let fog = 1.0 - exp(-r.Distance * 0.25);
	return mix(color, background, clamp(fog, 0.0, 1.0));
}

// Cone pre-pass: the ray through the centre of a tile of CONE_TILE x CONE_TILE marched pixels, widened to a cone of
// half-angle c that contains all their rays. A fine ray is within t c of the cone axis at distance t, so while the
// distance estimate D at the axis exceeds t c, a step of (D - t c) / (1 + c) keeps every fine ray inside the empty
// sphere. Writes the distance all of them can start from, or -1 when the cone leaves the scene without meeting anything.
fn FP_ConeMarch(dir: vec3<f32>, c: f32) -> f32 {
	var t = 0.0;
	var tEnd = P.camOff.w;
	{
		let o = P.refC.xyz + P.camOff.xyz;
		let radius = P.refC.w * 1.001 + c * (length(o) + P.refC.w); // the sphere grown by the cone's width
		let cc = dot(o, o) - radius * radius;
		if (cc > 0.0) {
			let b = dot(o, dir);
			let disc = b * b - cc;
			if (b >= 0.0 || disc < 0.0) { return -1.0; }
			let root = sqrt(disc);
			t = -b - root;
			tEnd = min(tEnd, -b + root);
		}
	}
	for (var i = 0; i < P.marchI.x; i = i + 1) {
		if (t >= tEnd) { return -1.0; }
		let de = FP_DistanceEstimate(P.camOff.xyz + dir * t, t * c);
		if (de.Distance < 2.0 * t * c || de.Distance <= 0.0) { break; }
		t = t + (de.Distance - t * c) / (1.0 + c);
	}
	return t;
}

@compute @workgroup_size(8, 8, 1)
fn cone(@builtin(global_invocation_id) id: vec3<u32>) {
	let tile = max(CONE_TILE, 1u);
	let tiles = (vec2<u32>(u32(P.sizeI.x), u32(P.sizeI.y)) + tile - 1u) / tile;
	if (id.x >= tiles.x || id.y >= tiles.y) { return; }
	let half = f32(CONE_TILE) * 0.5;
	let dir = FP_PixelRayDirection(vec2<f32>(f32(id.x) * f32(CONE_TILE) + half - 0.5, f32(id.y) * f32(CONE_TILE) + half - 0.5));
	// half-diagonal of the tile in direction space (marched pixel = PixelStep full-resolution pixels), plus margin
	let hx = (half + 0.5) * length(P.rayDX.xyz) * P.misc.z;
	let hy = (half + 0.5) * length(P.rayDY.xyz) * P.misc.w;
	let c = sqrt(hx * hx + hy * hy) * 1.05;
	textureStore(ConeOut, vec2<i32>(id.xy), vec4<f32>(FP_ConeMarch(dir, c), 0.0, 0.0, 0.0));
	if (BENCH) {
		atomicAdd(&Stats[6], gEstimates);
		atomicAdd(&Stats[7], gIterations);
	}
}

@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) id: vec3<u32>) {
	if (id.x >= u32(P.sizeI.x) || id.y >= u32(P.sizeI.y)) { return; }
	let dir = FP_PixelRayDirection(vec2<f32>(f32(id.x), f32(id.y)));
	var tStart = 0.0;
	if (CONE_TILE > 0u) { tStart = textureLoad(ConeIn, vec2<i32>(id.xy / max(CONE_TILE, 1u)), 0).x; }
	let r = FP_MarchRay(dir, tStart);
	textureStore(Out, vec2<i32>(id.xy), vec4<f32>(FP_Shade(r, dir, Background(dir)), 1.0));
	if (BENCH) {
		atomicAdd(&Stats[0], gEstimates);
		atomicAdd(&Stats[1], gIterations);
		atomicAdd(&Stats[2], gSkipped);
		atomicAdd(&Stats[3], u32(r.Steps));
		atomicAdd(&Stats[4], select(0u, 1u, r.Status == 1));
		atomicAdd(&Stats[5], 1u);
	}
}

// One thread: the distance at the camera (capped by the bounding sphere, FDistanceEstimate::SafeDistance) and a march
// of the centre ray, read back by the page for the speed, the HUD and the reference upkeep.
@compute @workgroup_size(1)
fn probe() {
	let de = FP_DistanceEstimate(P.camOff.xyz, 0.0);
	let outside = length(P.refC.xyz + P.camOff.xyz) - P.refC.w;
	let full = vec2<f32>(f32(P.sizeI.x), f32(P.sizeI.y)) * P.misc.zw * 0.5 - 0.5;
	let dir = normalize(P.ray00.xyz + full.x * P.rayDX.xyz + full.y * P.rayDY.xyz);
	let r = FP_MarchRay(dir, 0.0);
	Probe[0] = select(de.Distance, min(de.Distance, outside), outside > 0.0);
	Probe[1] = select(0.0, 1.0, de.bEscaped);
	Probe[2] = select(-1.0, r.Distance, r.Status == 1);
	Probe[3] = f32(r.Steps);
	Probe[4] = f32(de.Iterations);
	Probe[5] = f32(r.Status);
	Probe[6] = f32(r.TotalIterations);
	Probe[7] = f32(r.TotalSkipped);
}
