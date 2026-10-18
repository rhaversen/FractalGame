// CPU side of the browser renderer, a port of Plugins/FractalRenderer/Source/FractalRenderer/Public/FractalMath for the
// Mandelbulb: double-double arithmetic (DoubleDouble.h), the power map (PowerMap.h), the reference orbit with the linear
// series skip and the reference-ray march (FractalReferenceOrbit.h), the reference fan (FractalCamera.h) and the
// iteration budget (FractalFormulas.h). Written as one factory function so the page can run it on the main thread and,
// through Function.prototype.toString, in a Web Worker.
function makeLib() {
	'use strict';

	// ---------------------------------------------------------------------------------------------
	// Double-double (DoubleDouble.h): a number is [hi, lo], ~106 bits.
	// ---------------------------------------------------------------------------------------------
	const SPLITTER = 134217729; // 2^27 + 1
	const SPLIT_THRESHOLD = 6.69692879491417e+299;
	const DD_EPS = 4.93038065763132e-32; // 2^-104
	const DD_HUGE = 1.7976931348623157e308;

	function twoSum(a, b) {
		const s = a + b;
		const bb = s - a;
		return [s, (a - (s - bb)) + (b - bb)];
	}
	function quickTwoSum(a, b) {
		const s = a + b;
		return [s, b - (s - a)];
	}
	function split(a) {
		if (a > SPLIT_THRESHOLD || a < -SPLIT_THRESHOLD) {
			const scaled = a * 3.7252902984619140625e-09;
			const t = SPLITTER * scaled;
			const hi = t - (t - scaled);
			return [hi * 268435456.0, (scaled - hi) * 268435456.0];
		}
		const t = SPLITTER * a;
		const hi = t - (t - a);
		return [hi, a - hi];
	}
	function twoProd(a, b) {
		const p = a * b;
		const [ah, al] = split(a);
		const [bh, bl] = split(b);
		return [p, ((ah * bh - p) + ah * bl + al * bh) + al * bl];
	}
	function twoSqr(a) {
		const p = a * a;
		const [ah, al] = split(a);
		return [p, ((ah * ah - p) + 2.0 * ah * al) + al * al];
	}

	function ddAdd(x, y) {
		let s = twoSum(x[0], y[0]);
		const t = twoSum(x[1], y[1]);
		s = quickTwoSum(s[0], s[1] + t[0]);
		return quickTwoSum(s[0], s[1] + t[1]);
	}
	function ddAddD(x, d) {
		const s = twoSum(x[0], d);
		return quickTwoSum(s[0], s[1] + x[1]);
	}
	function ddNeg(x) { return [-x[0], -x[1]]; }
	function ddSub(x, y) { return ddAdd(x, [-y[0], -y[1]]); }
	function ddMul(x, y) {
		const p = twoProd(x[0], y[0]);
		return quickTwoSum(p[0], p[1] + (x[0] * y[1] + x[1] * y[0]));
	}
	function ddMulD(x, d) {
		const p = twoProd(x[0], d);
		return quickTwoSum(p[0], p[1] + x[1] * d);
	}
	function ddSqr(x) {
		const p = twoSqr(x[0]);
		return quickTwoSum(p[0], p[1] + 2.0 * x[0] * x[1] + x[1] * x[1]);
	}
	function ddMulPow2(x, b) { return [x[0] * b, x[1] * b]; }
	function ddDivD(x, b) {
		const q1 = x[0] / b;
		const p = twoProd(q1, b);
		const s = twoSum(x[0], -p[0]);
		const lo = s[1] - p[1] + x[1];
		const q2 = (s[0] + lo) / b;
		return quickTwoSum(q1, q2);
	}
	function ddDiv(x, y) {
		const q1 = x[0] / y[0];
		let r = ddSub(x, ddMulD(y, q1));
		const q2 = r[0] / y[0];
		r = ddSub(r, ddMulD(y, q2));
		const q3 = r[0] / y[0];
		return ddAddD(quickTwoSum(q1, q2), q3);
	}
	function ddSqrt(x) {
		if (x[0] <= 0) return [0, 0];
		const g = 1 / Math.sqrt(x[0]);
		const ax = x[0] * g;
		return twoSum(ax, ddSub(x, twoSqr(ax))[0] * (g * 0.5));
	}
	function ddLdexp(x, e) { const f = Math.pow(2, e); return [x[0] * f, x[1] * f]; }
	function ddNint(x) {
		let hiInt = Math.floor(x[0] + 0.5);
		if (hiInt === x[0]) return quickTwoSum(hiInt, Math.floor(x[1] + 0.5));
		if (Math.abs(hiInt - x[0]) === 0.5 && x[1] < 0) hiInt -= 1;
		return [hiInt, 0];
	}
	function ddToDouble(x) { return x[0] + x[1]; }

	const DD_PI = [3.141592653589793116e+00, 1.224646799147353207e-16];
	const DD_TWO_PI = [6.283185307179586232e+00, 2.449293598294706414e-16];
	const DD_HALF_PI = [1.570796326794896558e+00, 6.123233995736766036e-17];
	const DD_QUARTER_PI = [7.853981633974482790e-01, 3.061616997868383018e-17];
	const DD_THREE_QUARTER_PI = [2.356194490192344837e+00, 9.1848509936051484375e-17];
	const DD_LN2 = [6.931471805599452862e-01, 2.319046813846299558e-17];

	function ddExp(a) {
		const invK = 1 / 512;
		if (a[0] <= -709) return [0, 0];
		if (a[0] >= 709) return [DD_HUGE, 0];
		if (a[0] === 0 && a[1] === 0) return [1, 0];
		const m = Math.floor(a[0] / DD_LN2[0] + 0.5);
		const r = ddMulPow2(ddSub(a, ddMulD(DD_LN2, m)), invK);
		let p = ddSqr(r);
		let s = ddAdd(r, ddMulPow2(p, 0.5));
		p = ddMul(p, r);
		let factorial = 6;
		let t = ddDivD(p, factorial);
		let index = 3;
		do {
			s = ddAdd(s, t);
			p = ddMul(p, r);
			index++;
			factorial *= index;
			t = ddDivD(p, factorial);
		} while (Math.abs(t[0]) > invK * DD_EPS && index < 12);
		s = ddAdd(s, t);
		for (let i = 0; i < 9; i++) s = ddAdd(ddMulPow2(s, 2), ddSqr(s));
		s = ddAddD(s, 1);
		return ddLdexp(s, m);
	}
	function ddLog(a) {
		if (a[0] <= 0) return [-DD_HUGE, 0];
		if (a[0] === 1 && a[1] === 0) return [0, 0];
		const x = [Math.log(a[0]), 0];
		return ddAddD(ddAdd(x, ddMul(a, ddExp(ddNeg(x)))), -1);
	}
	function sinCosTaylor(t) {
		if (t[0] === 0 && t[1] === 0) return [[0, 0], [1, 0]];
		const x2 = ddNeg(ddSqr(t));
		const threshold = 0.5 * Math.abs(t[0]) * DD_EPS;
		let sum = t, term = t, n = 1;
		for (let i = 0; i < 30; i++) {
			term = ddDivD(ddMul(term, x2), (n + 1) * (n + 2));
			n += 2;
			sum = ddAdd(sum, term);
			if (Math.abs(term[0]) <= threshold) break;
		}
		const sin = sum;
		sum = [1, 0]; term = [1, 0]; n = 0;
		for (let i = 0; i < 30; i++) {
			term = ddDivD(ddMul(term, x2), (n + 1) * (n + 2));
			n += 2;
			sum = ddAdd(sum, term);
			if (Math.abs(term[0]) <= 0.5 * DD_EPS) break;
		}
		return [sin, sum];
	}
	function ddSinCos(a) {
		if (a[0] === 0 && a[1] === 0) return [[0, 0], [1, 0]];
		const z = ddNint(ddDiv(a, DD_TWO_PI));
		const r = ddSub(a, ddMul(DD_TWO_PI, z));
		const q = Math.floor(r[0] / DD_HALF_PI[0] + 0.5);
		const t = ddSub(r, ddMulD(DD_HALF_PI, q));
		const [s, c] = sinCosTaylor(t);
		switch (((q % 4) + 4) % 4) {
			case 0: return [s, c];
			case 1: return [c, ddNeg(s)];
			case 2: return [ddNeg(s), ddNeg(c)];
			default: return [ddNeg(c), s];
		}
	}
	function ddAtan2(y, x) {
		if (x[0] === 0) {
			if (y[0] === 0) return [0, 0];
			return y[0] > 0 ? DD_HALF_PI : ddNeg(DD_HALF_PI);
		}
		if (y[0] === 0) return x[0] > 0 ? [0, 0] : DD_PI;
		if (x[0] === y[0] && x[1] === y[1]) return y[0] > 0 ? DD_QUARTER_PI : ddNeg(DD_THREE_QUARTER_PI);
		if (x[0] === -y[0] && x[1] === -y[1]) return y[0] > 0 ? DD_THREE_QUARTER_PI : ddNeg(DD_QUARTER_PI);
		const r = ddSqrt(ddAdd(ddSqr(x), ddSqr(y)));
		const xx = ddDiv(x, r), yy = ddDiv(y, r);
		let z = [Math.atan2(ddToDouble(y), ddToDouble(x)), 0];
		const [sinZ, cosZ] = ddSinCos(z);
		if (Math.abs(xx[0]) > Math.abs(yy[0])) z = ddAdd(z, ddDiv(ddSub(yy, sinZ), cosZ));
		else z = ddSub(z, ddDiv(ddSub(xx, cosZ), sinZ));
		return z;
	}
	function ddPow(a, p) {
		if (a[0] <= 0) return [0, 0];
		return ddExp(ddMulD(ddLog(a), p));
	}
	function ddPowInt(a, n) {
		let result = [1, 0], base = a;
		for (let e = n; e > 0;) {
			if (e & 1) result = ddMul(result, base);
			e >>= 1;
			if (e > 0) base = ddSqr(base);
		}
		return result;
	}

	// 3-vectors of double-double
	function v3Add(a, b) { return [ddAdd(a[0], b[0]), ddAdd(a[1], b[1]), ddAdd(a[2], b[2])]; }
	function v3Sub(a, b) { return [ddSub(a[0], b[0]), ddSub(a[1], b[1]), ddSub(a[2], b[2])]; }
	function v3AddDoubles(a, d) { return [ddAddD(a[0], d[0]), ddAddD(a[1], d[1]), ddAddD(a[2], d[2])]; }
	function v3Dot(a, b) { return ddAdd(ddAdd(ddMul(a[0], b[0]), ddMul(a[1], b[1])), ddMul(a[2], b[2])); }
	function v3ToDoubles(a) { return [a[0][0] + a[0][1], a[1][0] + a[1][1], a[2][0] + a[2][1]]; }
	/** Base + Dir * Distance (FractalScalar.h OffsetDD). */
	function offsetDD(p, dir, dist) {
		return [ddAddD(p[0], dir[0] * dist), ddAddD(p[1], dir[1] * dist), ddAddD(p[2], dir[2] * dist)];
	}

	// ---------------------------------------------------------------------------------------------
	// Power map (PowerMap.h)
	// ---------------------------------------------------------------------------------------------
	/** Integer powers 2..64 use exact trig-free complex formulations. */
	function isIntegerPower(p) { return p >= 2 && p <= 64 && p === Math.floor(p); }

	/** g_p(W) in double-double. */
	function mandelbulbPowerDD(w, p) {
		const rho2 = ddAdd(ddMul(w[0], w[0]), ddMul(w[1], w[1]));
		const r2 = ddAdd(rho2, ddMul(w[2], w[2]));
		if (ddToDouble(r2) <= 0) return [[0, 0], [0, 0], [0, 0]];
		const rho = ddSqrt(rho2);
		const r = ddSqrt(r2);
		let sinPT, cosPT, sinPP, cosPP, rPow;
		if (isIntegerPower(p)) {
			let aRe = ddDiv(w[2], r), aIm = ddDiv(rho, r);
			let bRe = [1, 0], bIm = [0, 0];
			if (ddToDouble(rho) > 0) { bRe = ddDiv(w[0], rho); bIm = ddDiv(w[1], rho); }
			let tRe = [1, 0], tIm = [0, 0], pRe = [1, 0], pIm = [0, 0];
			for (let e = p; e > 0; e >>= 1) {
				if (e & 1) {
					const nt = ddSub(ddMul(tRe, aRe), ddMul(tIm, aIm));
					tIm = ddAdd(ddMul(tRe, aIm), ddMul(tIm, aRe)); tRe = nt;
					const np = ddSub(ddMul(pRe, bRe), ddMul(pIm, bIm));
					pIm = ddAdd(ddMul(pRe, bIm), ddMul(pIm, bRe)); pRe = np;
				}
				if (e > 1) {
					const na = ddSub(ddMul(aRe, aRe), ddMul(aIm, aIm));
					aIm = ddMulD(ddMul(aRe, aIm), 2); aRe = na;
					const nb = ddSub(ddMul(bRe, bRe), ddMul(bIm, bIm));
					bIm = ddMulD(ddMul(bRe, bIm), 2); bRe = nb;
				}
			}
			cosPT = tRe; sinPT = tIm; cosPP = pRe; sinPP = pIm;
			// RPowInt (FractalScalar.h): binary powering with plain products
			rPow = [1, 0];
			let base = r;
			for (let e = p; e > 0;) { if (e & 1) rPow = ddMul(rPow, base); e >>= 1; if (e > 0) base = ddMul(base, base); }
		} else {
			const theta = ddAtan2(rho, w[2]);
			const phi = ddToDouble(rho) > 0 ? ddAtan2(w[1], w[0]) : [0, 0];
			[sinPT, cosPT] = ddSinCos(ddMulD(theta, p));
			[sinPP, cosPP] = ddSinCos(ddMulD(phi, p));
			rPow = ddPow(r, p);
		}
		const radial = ddMul(rPow, sinPT);
		return [ddMul(radial, cosPP), ddMul(radial, sinPP), ddMul(rPow, cosPT)];
	}

	/** g_p(W) in double (the same template instantiated for double). */
	function mandelbulbPowerD(w, p) {
		const rho2 = w[0] * w[0] + w[1] * w[1];
		const r2 = rho2 + w[2] * w[2];
		if (r2 <= 0) return [0, 0, 0];
		const rho = Math.sqrt(rho2), r = Math.sqrt(r2);
		let sinPT, cosPT, sinPP, cosPP, rPow;
		if (isIntegerPower(p)) {
			let aRe = w[2] / r, aIm = rho / r, bRe = 1, bIm = 0;
			if (rho > 0) { bRe = w[0] / rho; bIm = w[1] / rho; }
			let tRe = 1, tIm = 0, pRe = 1, pIm = 0;
			for (let e = p; e > 0; e >>= 1) {
				if (e & 1) {
					const nt = tRe * aRe - tIm * aIm; tIm = tRe * aIm + tIm * aRe; tRe = nt;
					const np = pRe * bRe - pIm * bIm; pIm = pRe * bIm + pIm * bRe; pRe = np;
				}
				if (e > 1) {
					const na = aRe * aRe - aIm * aIm; aIm = (aRe * aIm) * 2; aRe = na;
					const nb = bRe * bRe - bIm * bIm; bIm = (bRe * bIm) * 2; bRe = nb;
				}
			}
			cosPT = tRe; sinPT = tIm; cosPP = pRe; sinPP = pIm;
			rPow = 1;
			let base = r;
			for (let e = p; e > 0;) { if (e & 1) rPow *= base; e >>= 1; if (e > 0) base *= base; }
		} else {
			const theta = Math.atan2(rho, w[2]);
			const phi = rho > 0 ? Math.atan2(w[1], w[0]) : 0;
			sinPT = Math.sin(theta * p); cosPT = Math.cos(theta * p);
			sinPP = Math.sin(phi * p); cosPP = Math.cos(phi * p);
			rPow = r > 0 ? Math.pow(r, p) : 0;
		}
		const radial = rPow * sinPT;
		return [radial * cosPP, radial * sinPP, rPow * cosPT];
	}

	/** Mandelbulb step z -> g_p(z) + c in double-double (FMandelbulbFormula::Step). */
	function bulbStep(z, c, p) {
		const g = mandelbulbPowerDD(z, p);
		return [ddAdd(g[0], c[0]), ddAdd(g[1], c[1]), ddAdd(g[2], c[2])];
	}
	/** dr' = p r^(p-1) dr + 1 (FMandelbulbFormula::DerivativeStep). */
	function derivativeStep(zd, dr, p) {
		const r = Math.sqrt(zd[0] * zd[0] + zd[1] * zd[1] + zd[2] * zd[2]);
		return p * (r > 0 ? Math.pow(r, p - 1) : 0) * dr + 1;
	}

	const f32 = Math.fround;
	function clampToFloatRange(v) { return f32(v > 1e30 ? 1e30 : (v < -1e30 ? -1e30 : v)); }

	/** GPU reference block for g_p at Z (12 floats, PackPowerRef). */
	function packPowerRef(z, p, out, o) {
		const FX = clampToFloatRange(ddToDouble(z[0])), FY = clampToFloatRange(ddToDouble(z[1])), FZ = clampToFloatRange(ddToDouble(z[2]));
		const X = FX, Y = FY, ZZ = FZ;
		const rho = Math.sqrt(X * X + Y * Y);
		const r = Math.sqrt(X * X + Y * Y + ZZ * ZZ);
		const theta = Math.atan2(rho, ZZ);
		const phi = rho > 0 ? Math.atan2(Y, X) : 0;
		let sinPT, cosPT, sinPP, cosPP;
		if (isIntegerPower(p) && r > 0) {
			let aRe = ZZ / r, aIm = rho / r;
			let tRe = 1, tIm = 0;
			let bRe = rho > 0 ? X / rho : 1, bIm = rho > 0 ? Y / rho : 0;
			let pRe = 1, pIm = 0;
			for (let e = p; e > 0; e >>= 1) {
				if (e & 1) {
					const nt = tRe * aRe - tIm * aIm; tIm = tRe * aIm + tIm * aRe; tRe = nt;
					const np = pRe * bRe - pIm * bIm; pIm = pRe * bIm + pIm * bRe; pRe = np;
				}
				const na = aRe * aRe - aIm * aIm; aIm = 2 * aRe * aIm; aRe = na;
				const nb = bRe * bRe - bIm * bIm; bIm = 2 * bRe * bIm; bRe = nb;
			}
			cosPT = tRe; sinPT = tIm; cosPP = pRe; sinPP = pIm;
		} else {
			sinPT = Math.sin(p * theta); cosPT = Math.cos(p * theta);
			sinPP = Math.sin(p * phi); cosPP = Math.cos(p * phi);
		}
		out[o] = FX; out[o + 1] = FY; out[o + 2] = FZ; out[o + 3] = clampToFloatRange(r);
		out[o + 4] = clampToFloatRange(rho); out[o + 5] = clampToFloatRange(r > 0 ? Math.pow(r, p) : 0);
		out[o + 6] = f32(phi); out[o + 7] = r > 0 ? clampToFloatRange(1 / r) : 0;
		out[o + 8] = f32(sinPT); out[o + 9] = f32(cosPT); out[o + 10] = f32(sinPP); out[o + 11] = f32(cosPP);
	}

	/**
	 * Series step of g_p at Z (PowerMapSeriesStep): Jacobian by central differences and the linear validity limit
	 * |d| <= Tolerance * min(|Z|, rho) (and < |y| next to the branch cut of non-integer powers).
	 */
	function powerMapSeriesStep(z, p, tolerance, J) {
		const r = Math.sqrt(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]);
		const rho = Math.sqrt(z[0] * z[0] + z[1] * z[1]);
		if (!(rho > 0)) return 0;
		const smooth = r < rho ? r : rho;
		const toBranchCut = (!isIntegerPower(p) && z[0] < 0) ? Math.abs(z[1]) : 1e300;
		if (!(toBranchCut > 0)) return 0;
		const h = 1e-6 * (smooth < toBranchCut ? smooth : toBranchCut);
		for (let col = 0; col < 3; col++) {
			const plus = z.slice(), minus = z.slice();
			plus[col] += h; minus[col] -= h;
			const gp = mandelbulbPowerD(plus, p), gm = mandelbulbPowerD(minus, p);
			for (let row = 0; row < 3; row++) J[row][col] = (gp[row] - gm[row]) / (2 * h);
		}
		const linear = tolerance * smooth;
		return linear < toBranchCut ? linear : toBranchCut;
	}

	/** Radius of a sphere around the origin that contains the whole set (PowerMapBoundingRadius, Mandelbrot type). */
	function boundingRadius(p) {
		if (p < 1.05) return Infinity;
		return Math.max(1, Math.pow(2, 1 / (p - 1)));
	}
	/** PowerMapReferenceEscapeRadius. */
	function referenceEscapeRadius(bailout, p) {
		const wanted = 4 * bailout;
		const floatSafe = Math.pow(10, 28 / (p > 1 ? p : 1));
		return wanted < floatSafe ? wanted : (floatSafe > 2 * bailout ? floatSafe : 2 * bailout);
	}

	// ---------------------------------------------------------------------------------------------
	// Reference orbit with the linear series skip (FractalReferenceOrbit.h). GPU layout per point m, 6 float4:
	//   [0..2]  reference block (packPowerRef)
	//   [3..5]  series rows i: (A_m[i][0], A_m[i][1], A_m[i][2], extra_i), extra_0 = skip radius,
	//           extra_1 = reference running derivative dr_m, extra_2 = Bailout^2 - |Z_m|^2
	// Point 0 is the zero state (the rebase target); the orbit proper starts at m = 1.
	// ---------------------------------------------------------------------------------------------
	const PAYLOAD_FLOAT4 = 3, STATE_DIM = 3;
	const ORBIT_STRIDE = PAYLOAD_FLOAT4 + STATE_DIM;
	const DEFAULT_SERIES_TOLERANCE = 1e-8;
	const MAX_ITERATIONS = 1000;

	function makeSeriesState() {
		return {
			A: [[1, 0, 0], [0, 1, 0], [0, 0, 1]], // Start() is the identity for the Mandelbulb, A_1 = E
			derivative: 1, radius: 1e30, stopped: false,
		};
	}
	function seriesStore(s, out, o) {
		for (let row = 0; row < 3; row++) {
			for (let col = 0; col < 3; col++) out[o + 4 * row + col] = clampToFloatRange(s.A[row][col]);
			out[o + 4 * row + 3] = 0;
		}
		out[o + 3] = s.stopped ? 0 : clampToFloatRange(s.radius);
		out[o + 7] = clampToFloatRange(s.derivative);
	}
	function seriesAdvance(s, z, p, bailout, tolerance) {
		if (s.stopped) return;
		if (!(tolerance > 0) || !(z[0] * z[0] + z[1] * z[1] + z[2] * z[2] <= bailout * bailout)) { s.stopped = true; return; }
		const J = [[0, 0, 0], [0, 0, 0], [0, 0, 0]];
		let limit = powerMapSeriesStep(z, p, tolerance, J);
		const toBailout = 0.5 * (bailout - Math.sqrt(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]));
		limit = limit < toBailout ? limit : toBailout;
		let normA2 = 0;
		for (let row = 0; row < 3; row++) for (let col = 0; col < 3; col++) normA2 += s.A[row][col] * s.A[row][col];
		if (!(limit > 0) || !(normA2 > 0)) { s.stopped = true; return; }
		const radiusHere = limit / Math.sqrt(normA2);
		s.radius = radiusHere < s.radius ? radiusHere : s.radius;
		const E = [[1, 0, 0], [0, 1, 0], [0, 0, 1]];
		const next = [[0, 0, 0], [0, 0, 0], [0, 0, 0]];
		for (let row = 0; row < 3; row++) {
			for (let col = 0; col < 3; col++) {
				let sum = E[row][col]; // the Mandelbulb adds c every step
				for (let k = 0; k < 3; k++) sum += J[row][k] * s.A[k][col];
				next[row][col] = sum;
			}
		}
		s.A = next;
		s.derivative = derivativeStep(z, s.derivative, p);
	}

	/** Orbit of the reference point c (GenerateReferenceOrbit): { floats, count }. */
	function generateReferenceOrbit(c, p, bailout, maxIterations, tolerance) {
		if (tolerance === undefined) tolerance = DEFAULT_SERIES_TOLERANCE;
		maxIterations = Math.max(maxIterations, 1);
		const floatsPerPoint = 4 * ORBIT_STRIDE;
		const out = new Float32Array((maxIterations + 1) * floatsPerPoint);
		const escape = referenceEscapeRadius(bailout, p);
		const b2 = [bailout * bailout, 0];
		const series = makeSeriesState();
		const write = (index, z, withSeries) => {
			const o = index * floatsPerPoint;
			packPowerRef(z, p, out, o);
			const rows = o + 4 * PAYLOAD_FLOAT4;
			if (withSeries) seriesStore(series, out, rows);
			else for (let i = 0; i < 4 * STATE_DIM; i++) out[rows + i] = 0;
			out[rows + 4 * 2 + 3] = clampToFloatRange(ddToDouble(ddSub(b2, v3Dot(z, z))));
		};
		write(0, [[0, 0], [0, 0], [0, 0]], false);
		let z = c;
		let count = 1;
		for (let m = 1; m <= maxIterations; m++) {
			write(m, z, true);
			count++;
			if (ddToDouble(v3Dot(z, z)) > escape * escape || m === maxIterations) break;
			seriesAdvance(series, v3ToDoubles(z), p, bailout, tolerance);
			z = bulbStep(z, c, p);
		}
		return { floats: out, count };
	}

	// ---------------------------------------------------------------------------------------------
	// Distance estimate and reference march (FractalReferenceOrbit.h)
	// ---------------------------------------------------------------------------------------------
	/**
	 * Escape-time distance estimate at the double-double point c, plus `safe`: the estimate capped by the distance to
	 * the bounding sphere (BoundedDistanceEstimate / SafeDistance). Hits are decided on `distance`.
	 */
	function distanceEstimate(c, p, bailout, maxIterations) {
		const b2 = [bailout * bailout, 0];
		let z = c, dr = 1, r = 0, escaped = false, n = 1;
		for (; n < maxIterations; n++) {
			const r2 = v3Dot(z, z);
			r = Math.sqrt(ddToDouble(r2));
			if (r2[0] > b2[0] || (r2[0] === b2[0] && r2[1] > 0)) { escaped = true; break; }
			dr = derivativeStep(v3ToDoubles(z), dr, p);
			z = bulbStep(z, c, p);
		}
		const dist = escaped ? 0.5 * r * Math.log(r) / dr : 0;
		const cd = v3ToDoubles(c);
		const outside = Math.sqrt(cd[0] * cd[0] + cd[1] * cd[1] + cd[2] * cd[2]) - boundingRadius(p);
		return { distance: dist, safe: outside > 0 && outside < dist ? outside : dist, escaped, iterations: n };
	}

	/** Iterations a pixel footprint (fractal units) needs (ComputeIterationBudget, escape-time family). */
	function iterationBudget(p, base, bailout, footprint) {
		base = base > 2 ? base : 2;
		if (!(footprint > 0)) return base;
		const growth = Math.max(Math.abs(p), 1.5);
		const needed = 2 * Math.log((bailout > 1 ? bailout : 1) / footprint) / Math.log(growth) + 20;
		const capped = needed < 0 ? 0 : (needed > 1000 ? 1000 : needed);
		let rounded = Math.ceil(capped / 50) * 50;
		rounded = rounded < base ? base : rounded;
		return rounded > 1000 ? 1000 : rounded;
	}

	/** MarchReferenceRay: where to put the reference along origin + t * dir (fractal units; Scale = 1). */
	function marchReferenceRay(origin, dir, s) {
		const result = { hit: false, hitDistance: 0, referenceDistance: 0, inside: false, steps: 0 };
		result.cameraDE = distanceEstimate(origin, s.power, s.bailout, s.iterations).safe;
		let dist = 0, end = s.maxDistance;
		// A ray that misses the bounding sphere hits nothing; one that crosses it is marched from where it enters.
		const radius = boundingRadius(s.power) * 1.001;
		if (radius < 1e300) {
			const o = v3ToDoubles(origin);
			const c = o[0] * o[0] + o[1] * o[1] + o[2] * o[2] - radius * radius;
			if (c > 0) {
				const b = o[0] * dir[0] + o[1] * dir[1] + o[2] * dir[2], disc = b * b - c;
				if (b >= 0 || disc < 0) return result;
				const root = Math.sqrt(disc);
				dist = -b - root;
				end = end < -b + root ? end : -b + root;
			}
		}
		for (let step = 0; step < s.maxSteps && dist < end; step++) {
			const de = distanceEstimate(offsetDD(origin, dir, dist), s.power, s.bailout, s.iterations);
			result.steps = step + 1;
			const pixelRadius = dist * s.pixelRadiusPerUnitDistance;
			if (!de.escaped || de.distance <= pixelRadius) {
				result.hit = true;
				result.hitDistance = dist;
				break;
			}
			dist += de.distance > 0.5 * pixelRadius ? de.distance : 0.5 * pixelRadius;
		}
		if (!result.hit) return result;
		result.referenceDistance = result.hitDistance;
		const probe = (result.hitDistance > 0 ? result.hitDistance : 1) * s.pixelRadiusPerUnitDistance;
		for (let step = 0; step <= 8; step++) {
			const d2 = result.hitDistance + probe * step;
			if (!distanceEstimate(offsetDD(origin, dir, d2), s.power, s.bailout, s.iterations).escaped) {
				result.referenceDistance = d2;
				result.inside = true;
				break;
			}
		}
		return result;
	}

	/** MarchReferenceFan: the nearest hit over all rays; the camera when nothing is hit or the hit is > 1000x farther than the camera's distance. */
	function marchReferenceFan(origin, dirs, s) {
		let best = null;
		const limited = Object.assign({}, s);
		for (let i = 0; i < dirs.length; i++) {
			const r = marchReferenceRay(origin, dirs[i], limited);
			r.dirIndex = i;
			if (i === 0) best = r;
			else if (r.hit && (!best.hit || r.hitDistance < best.hitDistance)) {
				const cameraDE = best.cameraDE;
				best = r;
				best.cameraDE = cameraDE;
			}
			if (best.hit) {
				const reach = best.hitDistance > 0 ? best.hitDistance : s.pixelRadiusPerUnitDistance;
				limited.maxDistance = reach < s.maxDistance ? reach : s.maxDistance;
			}
		}
		const reference = best.cameraDE > 1e-3 ? best.cameraDE : 1e-3;
		if (!best.hit || best.hitDistance > 1000 * reference) {
			best.hit = false; best.referenceDistance = 0; best.inside = false;
		}
		return best;
	}

	// Reference fan (FractalCamera.h BuildReferenceFan): the view centre first, then the 3x3 ring, then the rest of a
	// 5x5 grid. The first REFERENCE_FAN_QUICK_COUNT rays are a complete coarse set.
	const REFERENCE_FAN_COUNT = 25, REFERENCE_FAN_QUICK_COUNT = 9;
	/** dirAt(px, py) gives the (unnormalised) world direction through full-resolution pixel coordinates (px, py). */
	function buildReferenceFan(dirAt, width, height) {
		const fractions = [0.1, 0.3, 0.5, 0.7, 0.9];
		const used = new Set();
		const out = [];
		const add = (gx, gy) => {
			const key = gy * 5 + gx;
			if (used.has(key)) return;
			used.add(key);
			const d = dirAt(fractions[gx] * width, fractions[gy] * height);
			const l = Math.sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
			out.push([d[0] / l, d[1] / l, d[2] / l]);
		};
		add(2, 2);
		for (let gy = 0; gy < 5; gy += 2) for (let gx = 0; gx < 5; gx += 2) add(gx, gy);
		for (let gy = 0; gy < 5; gy++) for (let gx = 0; gx < 5; gx++) add(gx, gy);
		return out;
	}

	/**
	 * Complete reference for a camera (FFractalReferenceManager::Generate): the fan march (only the quick set when
	 * `quick`) places the reference point, then its orbit with the series skip.
	 */
	function buildReference(req) {
		const origin = req.position, bailout = req.bailout, p = req.power;
		const dirs = req.quick ? req.dirs.slice(0, REFERENCE_FAN_QUICK_COUNT) : req.dirs;
		const march = marchReferenceFan(origin, dirs, {
			power: p, bailout, iterations: req.iterations,
			maxSteps: req.maxSteps, maxDistance: req.maxDistance,
			pixelRadiusPerUnitDistance: req.pixelRadiusPerUnitDistance,
		});
		const center = offsetDD(origin, dirs[march.dirIndex || 0], march.referenceDistance);
		const orbit = generateReferenceOrbit(center, p, bailout, req.iterations);
		return { center, orbit: orbit.floats, count: orbit.count, hit: march.hit, inside: march.inside, cameraDE: march.cameraDE };
	}

	/**
	 * Teleport: the camera about `depth` (distance estimate) in front of the surface met by the ray start + t * dir, or
	 * null. The surface is the one the renderer draws with `iterations` (the boundary of the points escaping within
	 * them): a coarse march brackets it, double-double bisection narrows it, then the camera steps back until the
	 * distance estimate reaches `depth`.
	 */
	function surfaceCamera(start, dir, depth, power, bailout, iterations) {
		const precision = depth * 0.01;
		const origin = [[start[0], 0], [start[1], 0], [start[2], 0]];
		const at = (t) => [ddAdd(origin[0], ddMulD(t, dir[0])), ddAdd(origin[1], ddMulD(t, dir[1])), ddAdd(origin[2], ddMulD(t, dir[2]))];
		const escapes = (t) => distanceEstimate(at(t), power, bailout, iterations).escaped;
		let t = 0;
		const r = boundingRadius(power) * 1.001;
		const c = start[0] * start[0] + start[1] * start[1] + start[2] * start[2] - r * r;
		if (r < 1e300 && c > 0) {
			const b = start[0] * dir[0] + start[1] * dir[1] + start[2] * dir[2], disc = b * b - c;
			if (b >= 0 || disc < 0) return null;
			t = -b - Math.sqrt(disc);
		}
		let outside = null;
		for (let i = 0; i < 400; i++) {
			const de = distanceEstimate(at([t, 0]), power, bailout, 150);
			if (!de.escaped) break;
			outside = t;
			if (de.distance <= 1e-3) break;
			t += de.distance;
			if (t > 100) return null;
		}
		if (outside === null) return null;
		let lo = [outside, 0], inc = 1e-3, hi = null;
		for (let i = 0; i < 40 && !hi; i++) {
			const probe = ddAddD(lo, inc);
			if (escapes(probe)) { lo = probe; inc *= 2; } else hi = probe;
		}
		if (!hi) return null;
		for (let i = 0; i < 140; i++) {
			if (ddToDouble(ddSub(hi, lo)) < precision) break;
			const mid = ddMulPow2(ddAdd(lo, hi), 0.5);
			if (escapes(mid)) lo = mid; else hi = mid;
		}
		let fallback = null;
		for (let back = depth, i = 0; i < 400; i++, back *= 1.08) {
			const pos = at(ddAddD(lo, -back));
			const de = distanceEstimate(pos, power, bailout, iterations);
			if (!de.escaped || de.distance < depth) continue;
			if (de.distance <= 8 * depth) return pos;
			if (!fallback || de.distance < fallback.de) fallback = { pos, de: de.distance };
		}
		return fallback ? fallback.pos : null;
	}

	// ---------------------------------------------------------------------------------------------
	// Text
	// ---------------------------------------------------------------------------------------------
	/** A double as an exact BigInt numerator over 10^decimals (truncated). */
	function doubleScaled(x, decimals) {
		if (x === 0 || !isFinite(x)) return 0n;
		const neg = x < 0;
		x = Math.abs(x);
		const dv = new DataView(new ArrayBuffer(8));
		dv.setFloat64(0, x);
		const bits = dv.getBigUint64(0);
		const exp = Number((bits >> 52n) & 0x7ffn);
		let mant = bits & 0xfffffffffffffn;
		let e;
		if (exp === 0) { e = -1074; } else { mant |= 1n << 52n; e = exp - 1075; }
		let v = mant * 10n ** BigInt(decimals);
		v = e >= 0 ? v << BigInt(e) : v >> BigInt(-e);
		return neg ? -v : v;
	}
	/** hi + lo printed with the given number of decimals (rounded). */
	function formatDD(x, decimals) {
		decimals = Math.max(0, Math.min(decimals, 30));
		const guard = 2;
		let v = doubleScaled(x[0], decimals + guard) + doubleScaled(x[1], decimals + guard);
		const neg = v < 0n;
		if (neg) v = -v;
		const unit = 10n ** BigInt(guard);
		v = (v + unit / 2n) / unit;
		const s = v.toString().padStart(decimals + 1, '0');
		const whole = s.slice(0, s.length - decimals);
		const frac = s.slice(s.length - decimals);
		return (neg && v !== 0n ? '-' : '') + whole + (decimals ? '.' + frac : '');
	}

	return {
		ddAdd, ddSub, ddMul, ddDiv, ddSqrt, ddAddD, ddMulD, ddToDouble, ddExp, ddLog, ddSinCos, ddAtan2, ddPow,
		v3Add, v3Sub, v3Dot, v3AddDoubles, v3ToDoubles, offsetDD,
		isIntegerPower, mandelbulbPowerDD, mandelbulbPowerD, bulbStep, distanceEstimate, boundingRadius, referenceEscapeRadius,
		iterationBudget, generateReferenceOrbit, marchReferenceFan, buildReferenceFan, buildReference, surfaceCamera, formatDD,
		ORBIT_STRIDE, MAX_ITERATIONS, REFERENCE_FAN_COUNT, REFERENCE_FAN_QUICK_COUNT,
	};
}
if (typeof module !== 'undefined') module.exports = { makeLib };
