// CPU side of the browser renderer: double-double arithmetic, the Mandelbulb in double-double, the reference orbit,
// the reference-ray march and the iteration budget. A port of Plugins/FractalRenderer/Source/FractalRenderer/Public/FractalMath
// (integer powers only). Written as one factory function so the page can run it on the main thread and, through
// Function.prototype.toString, in a Web Worker.
function makeLib() {
	'use strict';

	// ---------------------------------------------------------------------------------------------
	// Double-double: a number is [hi, lo] with |lo| <= ulp(hi)/2, about 32 significant digits.
	// ---------------------------------------------------------------------------------------------
	const SPLITTER = 134217729; // 2^27 + 1

	function twoSum(a, b) {
		const s = a + b;
		const v = s - a;
		return [s, (a - (s - v)) + (b - v)];
	}
	function quickTwoSum(a, b) {
		const s = a + b;
		return [s, b - (s - a)];
	}
	function split(a) {
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

	function ddAdd(x, y) {
		const s = twoSum(x[0], y[0]);
		const t = twoSum(x[1], y[1]);
		let lo = s[1] + t[0];
		const u = quickTwoSum(s[0], lo);
		lo = u[1] + t[1];
		return quickTwoSum(u[0], lo);
	}
	function ddNeg(x) { return [-x[0], -x[1]]; }
	function ddSub(x, y) { return ddAdd(x, [-y[0], -y[1]]); }
	function ddAddD(x, d) {
		const s = twoSum(x[0], d);
		return quickTwoSum(s[0], s[1] + x[1]);
	}
	function ddMul(x, y) {
		const p = twoProd(x[0], y[0]);
		return quickTwoSum(p[0], p[1] + (x[0] * y[1] + x[1] * y[0]));
	}
	function ddMulD(x, d) {
		const p = twoProd(x[0], d);
		return quickTwoSum(p[0], p[1] + x[1] * d);
	}
	function ddDiv(x, y) {
		const q1 = x[0] / y[0];
		let r = ddSub(x, ddMulD(y, q1));
		const q2 = r[0] / y[0];
		r = ddSub(r, ddMulD(y, q2));
		const q3 = r[0] / y[0];
		const q = quickTwoSum(q1, q2);
		return ddAddD(q, q3);
	}
	function ddSqrt(x) {
		if (x[0] <= 0) return [0, 0];
		const g = 1 / Math.sqrt(x[0]);
		const ax = x[0] * g;
		const d = ddSub(x, twoProd(ax, ax));
		return twoSum(ax, d[0] * (g * 0.5));
	}
	function ddToDouble(x) { return x[0] + x[1]; }

	// 3-vectors of double-double
	function v3Add(a, b) { return [ddAdd(a[0], b[0]), ddAdd(a[1], b[1]), ddAdd(a[2], b[2])]; }
	function v3Sub(a, b) { return [ddSub(a[0], b[0]), ddSub(a[1], b[1]), ddSub(a[2], b[2])]; }
	function v3AddDoubles(a, d) { return [ddAddD(a[0], d[0]), ddAddD(a[1], d[1]), ddAddD(a[2], d[2])]; }
	function v3Dot(a, b) { return ddAdd(ddAdd(ddMul(a[0], b[0]), ddMul(a[1], b[1])), ddMul(a[2], b[2])); }
	function v3ToDoubles(a) { return [a[0][0] + a[0][1], a[1][0] + a[1][1], a[2][0] + a[2][1]]; }
	// P + Dir * Dist, Dir a double vector, Dist a double
	function offsetDD(p, dir, dist) {
		return [ddAddD(p[0], dir[0] * dist), ddAddD(p[1], dir[1] * dist), ddAddD(p[2], dir[2] * dist)];
	}

	// ---------------------------------------------------------------------------------------------
	// Mandelbulb in double-double (integer power p >= 2): z -> g_p(z) + c with
	// g_p(z) = r^p (sin(p theta) cos(p phi), sin(p theta) sin(p phi), cos(p theta)). The rotations are formed as complex
	// powers of (cos, sin) pairs, so no trigonometric function is ever needed.
	// ---------------------------------------------------------------------------------------------
	const DD_ONE = [1, 0];
	const DD_ZERO = [0, 0];

	function cMul(a, b) { // (a.re + i a.im)(b.re + i b.im), each part a dd
		return [ddSub(ddMul(a[0], b[0]), ddMul(a[1], b[1])), ddAdd(ddMul(a[0], b[1]), ddMul(a[1], b[0]))];
	}
	function cPow(a, p) {
		let result = [DD_ONE, DD_ZERO];
		let base = a;
		for (let e = p; e > 0; e >>= 1) {
			if (e & 1) result = cMul(result, base);
			if (e > 1) base = cMul(base, base);
		}
		return result;
	}
	function ddPow(x, p) {
		let result = DD_ONE;
		let base = x;
		for (let e = p; e > 0; e >>= 1) {
			if (e & 1) result = ddMul(result, base);
			if (e > 1) base = ddMul(base, base);
		}
		return result;
	}

	function bulbPower(z, p) {
		const rho2 = ddAdd(ddMul(z[0], z[0]), ddMul(z[1], z[1]));
		const r2 = ddAdd(rho2, ddMul(z[2], z[2]));
		if (!(r2[0] > 0)) return [DD_ZERO, DD_ZERO, DD_ZERO];
		const rho = ddSqrt(rho2);
		const r = ddSqrt(r2);
		const a = [ddDiv(z[2], r), ddDiv(rho, r)]; // cos theta, sin theta
		const b = rho[0] > 0 ? [ddDiv(z[0], rho), ddDiv(z[1], rho)] : [DD_ONE, DD_ZERO];
		const t = cPow(a, p);
		const q = cPow(b, p);
		const rp = ddPow(r, p);
		const st = ddMul(rp, t[1]);
		return [ddMul(st, q[0]), ddMul(st, q[1]), ddMul(rp, t[0])];
	}
	function bulbStep(z, c, p) {
		const g = bulbPower(z, p);
		return [ddAdd(g[0], c[0]), ddAdd(g[1], c[1]), ddAdd(g[2], c[2])];
	}

	/** Radius of a sphere around the origin that contains the whole set (see FractalMath::PowerMapBoundingRadius). */
	function boundingRadius(p) {
		if (p < 1.05) return Infinity;
		return Math.max(1, Math.pow(2, 1 / (p - 1)));
	}

	/** Reference escape radius of the orbit (FractalMath::PowerMapReferenceEscapeRadius). */
	function referenceEscapeRadius(bailout, p) {
		const wanted = 4 * bailout;
		const floatSafe = Math.pow(10, 28 / Math.max(p, 1));
		return wanted < floatSafe ? wanted : Math.max(floatSafe, 2 * bailout);
	}

	/** Escape-time distance estimate at the double-double point c (fractal units), limited by the bounding sphere. */
	function distanceEstimate(c, p, bailout, maxIterations) {
		const b2 = bailout * bailout;
		let z = c;
		let dr = 1;
		let r = 0;
		let escaped = false;
		let n = 1;
		for (; n < maxIterations; n++) {
			const r2 = v3Dot(z, z);
			r = Math.sqrt(r2[0]);
			if (r2[0] + r2[1] > b2) { escaped = true; break; }
			dr = p * Math.pow(r, p - 1) * dr + 1;
			z = bulbStep(z, c, p);
		}
		const dist = escaped ? 0.5 * r * Math.log(r) / dr : 0;
		// The estimate overshoots far from the set; the set cannot be closer than its bounding sphere. `safe` is the
		// estimate capped by that (used for speed and placement); hits are decided on `distance`.
		const cd = v3ToDoubles(c);
		const outside = Math.hypot(cd[0], cd[1], cd[2]) - boundingRadius(p);
		return { distance: dist, safe: outside > 0 && outside < dist ? outside : dist, escaped, iterations: n };
	}

	/** Iterations a pixel footprint (fractal units) needs; FractalMath::ComputeIterationBudget. */
	function iterationBudget(p, base, bailout, footprint) {
		base = Math.max(base, 2);
		if (!(footprint > 0)) return base;
		const growth = Math.max(Math.abs(p), 1.5);
		const needed = 2 * Math.log(Math.max(bailout, 1) / footprint) / Math.log(growth) + 20;
		const capped = Math.min(Math.max(needed, 0), 1000);
		return Math.min(Math.max(Math.ceil(capped / 50) * 50, base), 1000);
	}

	// ---------------------------------------------------------------------------------------------
	// Reference orbit in the GPU layout: per point m four float4:
	//   [0] Zx Zy Zz |Z|   [1] rho |Z|^p phi 1/|Z|   [2] sin(p theta) cos(p theta) sin(p phi) cos(p phi)
	//   [3] Bailout^2 - |Z|^2 (exact margin of the escape test), 0, 0, 0
	// with Z rounded to float first. Point 0 is the zero state, the rebase target. See FractalMath::PackPowerRef.
	// ---------------------------------------------------------------------------------------------
	const ORBIT_STRIDE = 4;
	const MAX_ITERATIONS = 1000;
	const f32 = Math.fround;

	function packPoint(out, base, z, bailout, p, margin) {
		const X = f32(z[0][0]), Y = f32(z[1][0]), Z = f32(z[2][0]);
		const rho = Math.sqrt(X * X + Y * Y);
		const r = Math.sqrt(X * X + Y * Y + Z * Z);
		const phi = rho > 0 ? Math.atan2(Y, X) : 0;
		let cosT = 1, sinT = 0, cosP = 1, sinP = 0;
		if (r > 0) {
			let aRe = Z / r, aIm = rho / r, bRe = rho > 0 ? X / rho : 1, bIm = rho > 0 ? Y / rho : 0;
			let tRe = 1, tIm = 0, pRe = 1, pIm = 0;
			for (let e = p; e > 0; e >>= 1) {
				if (e & 1) {
					const nt = tRe * aRe - tIm * aIm; tIm = tRe * aIm + tIm * aRe; tRe = nt;
					const np = pRe * bRe - pIm * bIm; pIm = pRe * bIm + pIm * bRe; pRe = np;
				}
				const na = aRe * aRe - aIm * aIm; aIm = 2 * aRe * aIm; aRe = na;
				const nb = bRe * bRe - bIm * bIm; bIm = 2 * bRe * bIm; bRe = nb;
			}
			cosT = tRe; sinT = tIm; cosP = pRe; sinP = pIm;
		}
		const o = base * ORBIT_STRIDE * 4;
		out[o] = X; out[o + 1] = Y; out[o + 2] = Z; out[o + 3] = r;
		out[o + 4] = rho; out[o + 5] = r > 0 ? Math.pow(r, p) : 0; out[o + 6] = phi; out[o + 7] = r > 0 ? 1 / r : 0;
		out[o + 8] = sinT; out[o + 9] = cosT; out[o + 10] = sinP; out[o + 11] = cosP;
		out[o + 12] = margin;
	}

	/** Orbit of the reference point c; returns { floats, count }. */
	function generateReferenceOrbit(c, p, bailout, maxIterations) {
		const out = new Float32Array((maxIterations + 1) * ORBIT_STRIDE * 4);
		const escape = referenceEscapeRadius(bailout, p);
		const b2 = bailout * bailout;
		packPoint(out, 0, [DD_ZERO, DD_ZERO, DD_ZERO], bailout, p, f32(b2));
		let z = c;
		let count = 1;
		for (let m = 1; m <= maxIterations; m++) {
			const r2 = v3Dot(z, z);
			packPoint(out, m, z, bailout, p, f32(ddSub([b2, 0], r2)[0]));
			count++;
			if (r2[0] > escape * escape || m === maxIterations) break;
			z = bulbStep(z, c, p);
		}
		return { floats: out, count };
	}

	// ---------------------------------------------------------------------------------------------
	// Reference placement: march rays from the camera with the double-double estimator (FractalMath::MarchReferenceFan).
	// ---------------------------------------------------------------------------------------------
	function marchReferenceRay(origin, dir, s) {
		const result = { hit: false, hitDistance: 0, referenceDistance: 0, cameraDE: 0, inside: false };
		result.cameraDE = distanceEstimate(origin, s.power, s.bailout, s.iterations).safe;
		let dist = 0, end = s.maxDistance;
		// A ray that misses the bounding sphere hits nothing; one that crosses it is marched from where it enters.
		const o = v3ToDoubles(origin), radius = boundingRadius(s.power) * 1.001;
		const c = o[0] * o[0] + o[1] * o[1] + o[2] * o[2] - radius * radius;
		if (c > 0) {
			const b = o[0] * dir[0] + o[1] * dir[1] + o[2] * dir[2], disc = b * b - c;
			if (b >= 0 || disc < 0) return result;
			dist = -b - Math.sqrt(disc);
			end = Math.min(end, -b + Math.sqrt(disc));
		}
		for (let step = 0; step < s.maxSteps && dist < end; step++) {
			const de = distanceEstimate(offsetDD(origin, dir, dist), s.power, s.bailout, s.iterations);
			const pixelRadius = dist * s.pixelRadiusPerUnitDistance;
			if (!de.escaped || de.distance <= pixelRadius) {
				result.hit = true;
				result.hitDistance = dist;
				break;
			}
			dist += Math.max(de.distance, 0.5 * pixelRadius);
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

	/** Nearest hit over all rays; the camera itself when nothing is hit or the hit is farther than 1000x the camera's distance. */
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
				limited.maxDistance = Math.min(reach, s.maxDistance);
			}
		}
		const reference = Math.max(best.cameraDE, 1e-3);
		if (!best.hit || best.hitDistance > 1000 * reference) {
			best.hit = false; best.referenceDistance = 0; best.inside = false;
		}
		return best;
	}

	/**
	 * Complete reference for a camera: DE at the camera, iteration budget for its pixel footprint, the fan march that places
	 * the reference point and its orbit.
	 */
	function buildReference(req) {
		const origin = req.position; // [dd, dd, dd]
		const bailout = req.bailout, p = req.power;
		let cam = distanceEstimate(origin, p, bailout, req.baseIterations);
		// A camera next to the surface may need a long orbit just to escape (the orbits there expand slowly), and its
		// pixels, a few hundred times closer to the surface, need that many plus ~20 more: size the orbit from it.
		for (let its = req.baseIterations; !cam.escaped && its < MAX_ITERATIONS; ) { its = Math.min(MAX_ITERATIONS, its * 2); cam = distanceEstimate(origin, p, bailout, its); }
		const footprint = Math.max(cam.safe, 1e-30) * req.pixelRadiusPerUnitDistance;
		const iterations = Math.min(MAX_ITERATIONS, Math.max(iterationBudget(p, req.baseIterations, bailout, footprint),
			cam.escaped ? Math.ceil((cam.iterations + 60) * 1.3 / 50) * 50 : MAX_ITERATIONS));
		const march = marchReferenceFan(origin, req.dirs, {
			power: p, bailout, iterations,
			maxSteps: req.maxSteps, maxDistance: req.maxDistance,
			pixelRadiusPerUnitDistance: req.pixelRadiusPerUnitDistance,
		});
		const center = march.hit ? offsetDD(origin, req.dirs[march.dirIndex || 0], march.referenceDistance) : origin;
		const orbit = generateReferenceOrbit(center, p, bailout, iterations);
		return {
			center, orbit: orbit.floats, count: orbit.count, iterations,
			hit: march.hit, inside: march.inside, cameraDE: cam.safe, cameraEscaped: cam.escaped, cameraIterations: cam.iterations,
		};
	}

	/**
	 * Teleport: the camera `depth` in front of the surface met by the ray start + t * dir (null if there is none). The
	 * surface is the boundary of the escaping points: a coarse DE march brackets it, then bisection in double-double
	 * narrows it to a hundredth of `depth`. (A DE march alone converges too slowly towards a surface at 1e-24: the
	 * estimate is conservative, so each step only closes a fraction of the gap.)
	 */
	function surfaceCamera(start, dir, depth, power, bailout) {
		const precision = depth * 0.01;
		// Near a surface the orbits expand by only ~1.4x per iteration, so a point 1e-20 away needs ~150 iterations to escape
		// and one at 1e-27 several hundred: judge escape with a long orbit or the "surface" found is far from the real one.
		const iterations = depth < 1e-9 ? 1000 : Math.max(iterationBudget(power, 150, bailout, precision), 300);
		const origin = [[start[0], 0], [start[1], 0], [start[2], 0]];
		// position at dd parameter t along the ray (t a double-double, so a gap of 1e-24 survives)
		const at = (t) => [ddAdd(origin[0], ddMulD(t, dir[0])), ddAdd(origin[1], ddMulD(t, dir[1])), ddAdd(origin[2], ddMulD(t, dir[2]))];
		const escapes = (t) => distanceEstimate(at(t), power, bailout, iterations).escaped;

		let t = 0;
		const r = boundingRadius(power) * 1.001;
		const c = start[0] * start[0] + start[1] * start[1] + start[2] * start[2] - r * r;
		if (c > 0) {
			const b = start[0] * dir[0] + start[1] * dir[1] + start[2] * dir[2], disc = b * b - c;
			if (b >= 0 || disc < 0) return null;
			t = -b - Math.sqrt(disc);
		}
		// coarse march to within 1e-3 of the surface
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
		// bracket: lo escapes, hi does not
		let lo = [outside, 0], inc = 1e-3, hi = null;
		for (let i = 0; i < 40 && !hi; i++) {
			const probe = ddAddD(lo, inc);
			if (escapes(probe)) { lo = probe; inc *= 2; } else hi = probe;
		}
		if (!hi) return null;
		for (let i = 0; i < 140; i++) {
			if (hi[0] - lo[0] < precision) break;
			const mid = ddMulD(ddAdd(lo, hi), 0.5);
			if (escapes(mid)) lo = mid; else hi = mid;
		}
		// Step back until the distance estimate (the nearest part of the set in any direction, not just along the ray)
		// reaches `depth`: the surface is fractal, so other filaments can be much closer than the one the ray found.
		let fallback = null;
		for (let back = depth, i = 0; i < 400; i++, back *= 1.08) {
			const p = at(ddAddD(lo, -back));
			const de = distanceEstimate(p, power, bailout, iterations);
			if (!de.escaped || de.distance < depth) continue;
			if (de.distance <= 8 * depth) return p;
			if (!fallback || de.distance < fallback.de) fallback = { p, de: de.distance };
		}
		if (fallback) return fallback.p;
		return null;
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
		let s = v.toString().padStart(decimals + 1, '0');
		const whole = s.slice(0, s.length - decimals);
		const frac = s.slice(s.length - decimals);
		return (neg && v !== 0n ? '-' : '') + whole + (decimals ? '.' + frac : '');
	}

	return {
		ddAdd, ddSub, ddMul, ddDiv, ddSqrt, ddAddD, ddMulD, ddToDouble,
		v3Add, v3Sub, v3Dot, v3AddDoubles, v3ToDoubles, offsetDD,
		bulbStep, bulbPower, distanceEstimate, boundingRadius, referenceEscapeRadius, iterationBudget,
		generateReferenceOrbit, marchReferenceFan, buildReference, surfaceCamera, formatDD,
		ORBIT_STRIDE, MAX_ITERATIONS,
	};
}
if (typeof module !== 'undefined') module.exports = { makeLib };
