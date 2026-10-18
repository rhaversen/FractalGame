// Compares Web/src/lib.js generateReferenceOrbit with the C++ FractalMath::GenerateReferenceOrbit (via orbit_dump).
// usage: node orbit_compare.js /path/to/orbit_dump
const { execFileSync } = require('child_process');
const { makeLib } = require('../src/lib.js');
const L = makeLib();
const dump = process.argv[2];
const cases = [
	[8, 200, [0.6, 1e-17], [0.8, -3e-18], [1.1, 0]],
	[8, 400, [-0.5218656746202388, 1.7e-17], [0.48671020173521873, -2e-18], [-0.45155472885019898, 3e-18]],
	[5.5, 200, [0.31, 0], [-0.44, 1e-18], [0.71, 0]],
	[3, 300, [-0.7, 0], [0.2, 0], [0.51, 0]],
	[12, 200, [0.45, 2e-18], [0.38, 0], [0.77, 0]],
	[2.37, 300, [-0.9, 0], [0.3, 1e-17], [0.2, 0]],
];
// plus long orbits next to the surface (a camera placed 1e-14 and 1e-10 from it), where the series rows matter
for (const [start, dir, depth, power] of [[[0.3, -1.6, -1.5], [-0.15, 0.7, 0.7], 1e-14, 8], [[2.0, 0.3, 0.2], [-1, -0.12, -0.07], 1e-10, 5.5]]) {
	const l = Math.hypot(...dir);
	const pos = L.surfaceCamera(start, dir.map((x) => x / l), depth, power, 10, 150);
	cases.push([power, 150, pos[0], pos[1], pos[2]]);
}
let worst = 0, worstWhere = '', bad = 0, total = 0;
for (const [p, n, cx, cy, cz] of cases) {
	const cpp = JSON.parse(execFileSync(dump, [p, n, ...cx, ...cy, ...cz].map(String)).toString());
	const js = L.generateReferenceOrbit([cx, cy, cz], p, 10, n);
	if (cpp.count !== js.count) { console.log(`p=${p}: point count C++ ${cpp.count} vs JS ${js.count}`); process.exit(1); }
	for (let i = 0; i < cpp.floats.length; i++) {
		const a = cpp.floats[i], b = js.floats[i];
		const scale = Math.max(Math.abs(a), Math.abs(b), 1e-30);
		const rel = a === b ? 0 : Math.abs(a - b) / scale;
		total++;
		if (rel > 1e-6) bad++;
		if (rel > worst) { worst = rel; worstWhere = `p=${p} point ${Math.floor(i / 24)} slot ${i % 24}: C++ ${a} JS ${b}`; }
	}
	console.log(`p=${p}: ${cpp.count} points match`);
}
console.log(`worst relative difference ${worst.toExponential(2)} (${worstWhere}); ${bad} of ${total} floats differ by > 1e-6`);
process.exit(bad > 0 ? 1 : 0);
