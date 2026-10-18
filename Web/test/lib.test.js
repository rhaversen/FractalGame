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

// Orbit packing: the layout the shader reads (4 float4 per point, point 0 = zero state with margin Bailout^2).
{
	const orbit = L.generateReferenceOrbit([[0.6, 0], [0.8, 0], [1.1, 0]], 8, 10, 40);
	const f = orbit.floats, S = L.ORBIT_STRIDE * 4;
	let ok = f[3] === 0 && f[12] === 100; // point 0: R = 0, margin = 100
	for (let m = 1; m < orbit.count && ok; m++) {
		const x = f[m * S], y = f[m * S + 1], z = f[m * S + 2], r = f[m * S + 3];
		ok = Math.abs(r - Math.hypot(x, y, z)) < 1e-5 * r && Math.abs(f[m * S + 12] - (100 - r * r)) < 1e-3 * Math.max(1, r * r)
			&& Math.abs(f[m * S + 4] - Math.hypot(x, y)) < 1e-5 * r && Math.abs(f[m * S + 7] * r - 1) < 1e-5;
	}
	console.log('orbit layout:', ok ? 'OK' : 'FAIL', '(' + orbit.count + ' points)');
	if (!ok) process.exit(1);
}
