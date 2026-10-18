// Node check of src/lib.js against mpmath (run by test/run.sh): the double-double orbit must agree with a 60-digit evaluation.
const { makeLib } = require('../src/lib.js');
const L = makeLib();
const cases = JSON.parse(require('fs').readFileSync(process.argv[2], 'utf8'));
let worst = 0;
for (const c of cases) {
	const C = c.c.map(([h, l]) => [h, l]);
	let z = C;
	for (let m = 1; m <= c.n; m++) {
		const ref = c.orbit[m - 1];
		for (let k = 0; k < 3; k++) {
			const diff = L.ddSub(z[k], ref[k]);
			const err = Math.abs(diff[0]) / Math.max(Math.abs(ref[k][0]), 1e-3);
			worst = Math.max(worst, err / (1e-30 * Math.pow(20, m - 1)));
		}
		z = L.bulbStep(z, C, c.p);
	}
}
console.log('worst orbit error vs mpmath, relative to the 1e-30 * 20^m sensitivity bound:', worst.toExponential(2));
if (!(worst < 1)) { console.log('FAIL'); process.exit(1); }
console.log('OK');

// Orbit packing: the layout the shader reads (6 float4 per point: reference block, then 3 series rows whose spare slots
// hold the skip radius, the reference derivative and the escape margin; point 0 is the zero state).
{
	const orbit = L.generateReferenceOrbit([[0.6, 0], [0.8, 0], [1.1, 0]], 8, 10, 40);
	const f = orbit.floats, S = L.ORBIT_STRIDE * 4;
	let ok = S === 24 && f[3] === 0 && f[23] === 100; // point 0: R = 0, margin = Bailout^2
	for (let m = 1; m < orbit.count && ok; m++) {
		const x = f[m * S], y = f[m * S + 1], z = f[m * S + 2], r = f[m * S + 3];
		ok = Math.abs(r - Math.hypot(x, y, z)) < 1e-5 * r && Math.abs(f[m * S + 23] - (100 - r * r)) < 1e-3 * Math.max(1, r * r)
			&& Math.abs(f[m * S + 4] - Math.hypot(x, y)) < 1e-5 * r && Math.abs(f[m * S + 7] * r - 1) < 1e-5;
	}
	ok = ok && f[S + 15] === Math.fround(1e30) && f[S + 12] === 1 && f[S + 17] === 1 && f[S + 22] === 1; // A_1 = identity, radius 1e30
	console.log('orbit layout:', ok ? 'OK' : 'FAIL', '(' + orbit.count + ' points)');
	if (!ok) process.exit(1);
}

// Double-double transcendental functions against mpmath values.
{
	const close = (got, want, tol) => Math.abs((got[0] - want[0]) + got[1] - want[1]) <= tol * Math.abs(want[0]);
	const checks = [
		['exp(1)', L.ddExp([1, 0]), [2.718281828459045, 1.4456468917292502e-16]],
		['log(2)', L.ddLog([2, 0]), [0.6931471805599453, 2.3190468138462996e-17]],
		['sin(1)', L.ddSinCos([1, 0])[0], [0.8414709848078965, 1.776845092935536e-18]],
		['atan2(1, 2)', L.ddAtan2([1, 0], [2, 0]), [0.4636476090008061, 2.2698777452961687e-17]],
	];
	let ok = true;
	for (const [name, got, want] of checks) { const pass = close(got, want, 1e-29); ok = ok && pass; if (!pass) console.log('  ', name, got, want); }
	console.log('double-double functions:', ok ? 'OK' : 'FAIL');
	if (!ok) process.exit(1);
}
