'use strict';
(function () {
const Lib = makeLib();
const $ = (id) => document.getElementById(id);
const canvas = $('view');

const MAX_DEPTH_LOG = 27;           // the precision limit of double-double positions
const MAX_ORBIT_POINTS = 1002;      // iteration budget cap + 1 + zero point
const POWER_MIN = 2, POWER_MAX = 16;
const BAILOUT = 10;
const BASE_ITERATIONS = 150;
const MAX_RAY_STEPS = 150, MAX_RAY_DISTANCE = 500;
const FOV_Y = 70 * Math.PI / 180;
const TAN_HALF = Math.tan(FOV_Y / 2);
const DIRECT_FOOTPRINT = 1e-4;      // above this pixel footprint plain float iteration is exact enough
const ASYNC_RATIO = 50, SYNC_RATIO = 5000; // reference offset / surface distance at which a new reference is built / the old one is dropped
const HAZE_START = 0.55;

// Deep-zoom presets: where to aim (lab scenes of the desktop renderer) and how far from the surface to stand.
const PRESETS = [
	{ label: '1e-6', depth: 1e-6, start: [0.6, 0.8, 1.6], dir: [-0.3, -0.45, -0.85] },
	{ label: '1e-12', depth: 1e-12, start: [2.0, 0.3, 0.2], dir: [-1, -0.12, -0.07] },
	{ label: '1e-18', depth: 1e-18, start: [0.6, 0.8, 1.6], dir: [-0.3, -0.45, -0.85] },
	{ label: '1e-24', depth: 1e-24, start: [-1.3, 1.2, -1.1], dir: [0.6, -0.55, 0.5] },
];

const S = {
	pos: [[2.4, 0], [0.4, 0], [0.3, 0]],
	right: [0, 1, 0], up: [0, 0, 1], fwd: [-1, 0, 0],
	vel: [0, 0, 0],
	power: 8, speedFactor: 0.8, scale: 1, autofly: false,
	camDE: 2, camEscaped: true, lastGoodDE: 2, centerHit: -1, probeSteps: 0, probeIters: 0,
	ref: null, refPending: false, refId: 0, probeValid: false,
	keys: new Set(), locked: false, dragging: false,
	frameMs: 16, started: false,
};

// ---------------------------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------------------------
function resetCamera() {
	S.pos = [[2.4, 0], [0.4, 0], [0.3, 0]];
	lookAlong([-2.4, -0.4, -0.3]);
	S.vel = [0, 0, 0];
	S.power = 8;
	S.ref = null;
	S.camDE = 2; S.lastGoodDE = 2;
	updatePowerLabel();
}
function norm(v) { const l = Math.hypot(v[0], v[1], v[2]) || 1; return [v[0] / l, v[1] / l, v[2] / l]; }
function cross(a, b) { return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]; }
function dot(a, b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
function lookAlong(dir) {
	S.fwd = norm(dir);
	let up = Math.abs(S.fwd[2]) > 0.95 ? [0, 1, 0] : [0, 0, 1];
	S.right = norm(cross(S.fwd, up));
	S.up = cross(S.right, S.fwd);
}
function orthonormalize() {
	S.fwd = norm(S.fwd);
	S.right = norm(cross(S.fwd, S.up));
	S.up = cross(S.right, S.fwd);
}
function rotate(a, b, theta) { // a turns towards b
	const c = Math.cos(theta), s = Math.sin(theta);
	return [[a[0] * c + b[0] * s, a[1] * c + b[1] * s, a[2] * c + b[2] * s], [b[0] * c - a[0] * s, b[1] * c - a[1] * s, b[2] * c - a[2] * s]];
}
function yaw(t) { const r = rotate(S.fwd, S.right, t); S.fwd = r[0]; S.right = r[1]; }
function pitch(t) { const r = rotate(S.fwd, S.up, t); S.fwd = r[0]; S.up = r[1]; }
function roll(t) { const r = rotate(S.right, S.up, t); S.right = r[0]; S.up = r[1]; }

function speedLimit() {
	const de = S.camEscaped ? S.camDE : S.lastGoodDE * 0.5;
	return Math.min(de * S.speedFactor, 6);
}

function step(dt) {
	const k = S.keys;
	let mx = 0, my = 0, mz = 0;
	if (k.has('KeyW') || S.autofly) mz += 1;
	if (k.has('KeyS')) mz -= 1;
	if (k.has('KeyD')) mx += 1;
	if (k.has('KeyA')) mx -= 1;
	if (k.has('Space')) my += 1;
	if (k.has('ShiftLeft') || k.has('ShiftRight')) my -= 1;
	const rollIn = (k.has('KeyE') ? 1 : 0) - (k.has('KeyQ') ? 1 : 0);
	if (rollIn) roll(rollIn * 1.2 * dt);
	if (k.has('ArrowLeft')) yaw(-1.4 * dt);
	if (k.has('ArrowRight')) yaw(1.4 * dt);
	if (k.has('ArrowUp')) pitch(1.0 * dt);
	if (k.has('ArrowDown')) pitch(-1.0 * dt);
	orthonormalize();

	const sp = speedLimit();
	const want = [0, 0, 0];
	for (let i = 0; i < 3; i++) want[i] = (S.fwd[i] * mz + S.right[i] * mx + S.up[i] * my) * sp;
	const a = 1 - Math.exp(-9 * dt);
	for (let i = 0; i < 3; i++) S.vel[i] += (want[i] - S.vel[i]) * a;
	S.pos = Lib.v3AddDoubles(S.pos, [S.vel[0] * dt, S.vel[1] * dt, S.vel[2] * dt]);
}

// ---------------------------------------------------------------------------------------------
// Reference orbit (Web Worker): double-double reference point, its orbit, the iteration budget
// ---------------------------------------------------------------------------------------------
const workerSource = `const Lib = (${makeLib.toString()})();
onmessage = (e) => {
	const m = e.data;
	try {
		if (m.type === 'reference') {
			const r = Lib.buildReference(m.req);
			postMessage({ type: 'reference', id: m.id, power: m.req.power, center: r.center, orbit: r.orbit, count: r.count,
				iterations: r.iterations, hit: r.hit, cameraDE: r.cameraDE }, [r.orbit.buffer]);
		} else if (m.type === 'teleport') {
			const pos = Lib.surfaceCamera(m.start, m.dir, m.depth, m.power, m.bailout);
			postMessage({ type: 'teleport', id: m.id, pos, dir: m.dir, depth: m.depth });
		}
	} catch (err) { postMessage({ type: 'error', message: String(err && err.stack || err) }); }
};`;
const worker = new Worker(URL.createObjectURL(new Blob([workerSource], { type: 'text/javascript' })));
let teleportId = 0;

worker.onmessage = (e) => {
	const m = e.data;
	if (m.type === 'reference') {
		S.refPending = false;
		if (m.id !== S.refId) return;
		if (m.power !== S.power) return;
		gpu.uploadOrbit(m.orbit, m.count);
		S.ref = { center: m.center, count: m.count, iterations: m.iterations, power: m.power, hit: m.hit, cameraDE: m.cameraDE };
		// Until the GPU probes the camera with this reference, the worker's double-double estimate is the only trustworthy one
		// (a probe taken in plain float mode is garbage at depth).
		if (!S.probeValid && m.cameraDE > 0) { S.camDE = m.cameraDE; S.lastGoodDE = m.cameraDE; S.camEscaped = true; }
	} else if (m.type === 'teleport') {
		if (m.id !== teleportId) return;
		if (!m.pos) { showHint('No surface found along that line for this power.'); return; }
		S.pos = m.pos; S.vel = [0, 0, 0]; S.ref = null;
		lookAlong(m.dir);
		S.camDE = m.depth; S.lastGoodDE = m.depth; S.camEscaped = true; S.probeValid = false;
		hideHint();
	} else if (m.type === 'error') {
		showHint('Worker error: ' + m.message);
	}
};

function fanDirections() {
	const out = [];
	const add = (fx, fy) => {
		const nx = 2 * fx - 1, ny = 1 - 2 * fy;
		out.push(norm([
			S.fwd[0] + nx * TAN_HALF * aspect() * S.right[0] + ny * TAN_HALF * S.up[0],
			S.fwd[1] + nx * TAN_HALF * aspect() * S.right[1] + ny * TAN_HALF * S.up[1],
			S.fwd[2] + nx * TAN_HALF * aspect() * S.right[2] + ny * TAN_HALF * S.up[2]]));
	};
	add(0.5, 0.5);
	for (const fy of [0.1, 0.5, 0.9]) for (const fx of [0.1, 0.5, 0.9]) if (!(fx === 0.5 && fy === 0.5)) add(fx, fy);
	return out;
}
function aspect() { return canvas.width / Math.max(canvas.height, 1); }
function hitThreshold() { return TAN_HALF / Math.max(canvas.height, 1); } // half a full-resolution pixel, per unit distance

function requestReference() {
	S.refPending = true;
	S.refId++;
	worker.postMessage({ type: 'reference', id: S.refId, req: {
		position: S.pos, dirs: fanDirections(), power: S.power, bailout: BAILOUT, baseIterations: BASE_ITERATIONS,
		pixelRadiusPerUnitDistance: hitThreshold(), maxSteps: MAX_RAY_STEPS, maxDistance: MAX_RAY_DISTANCE,
	} });
}

function referenceOffset() { // |camera - C_ref| in fractal units
	if (!S.ref) return Infinity;
	const d = Lib.v3ToDoubles(Lib.v3Sub(S.pos, S.ref.center));
	return Math.hypot(d[0], d[1], d[2]);
}
function referenceRatio() { return referenceOffset() / Math.max(S.camDE, 1e-30); }
function referenceUsable() { return !!S.ref && S.ref.power === S.power && referenceRatio() <= SYNC_RATIO; }

function maintainReference() {
	if (S.refPending) return;
	let need = false;
	if (!S.ref || S.ref.power !== S.power) need = true;
	else if (referenceRatio() > ASYNC_RATIO) need = true;
	else {
		// The orbit has to outlast the camera's own: when the probe escaped only at the iteration limit, or needs most of it.
		const formula = Lib.iterationBudget(S.power, BASE_ITERATIONS, BAILOUT, Math.max(S.camDE, 1e-30) * hitThreshold());
		const probed = S.camEscaped ? Math.ceil((S.probeIters + 60) * 1.3 / 50) * 50 : Lib.MAX_ITERATIONS;
		const budget = Math.min(Lib.MAX_ITERATIONS, Math.max(formula, probed));
		if (budget > S.ref.iterations) need = true;
	}
	if (need) requestReference();
}

// ---------------------------------------------------------------------------------------------
// WebGPU
// ---------------------------------------------------------------------------------------------
const gpu = {
	device: null, ctx: null, format: null, uniform: null, orbit: null, probe: null, readback: null, readbackBusy: false,
	pipeMain: null, pipeProbe: null, pipePresent: null, bgMain: null, bgProbe: null, bgPresent: null, tex: null, sampler: null,
	uf: new Float32Array(32), ui: null, w: 0, h: 0, orbitCount: 0,

	async init() {
		if (!navigator.gpu) throw new Error('WebGPU is not available in this browser.');
		const adapter = await navigator.gpu.requestAdapter();
		if (!adapter) throw new Error('No WebGPU adapter was found. Check that hardware acceleration is on.');
		const device = this.device = await adapter.requestDevice();
		device.lost.then((i) => showHint('The GPU device was lost: ' + i.message));
		device.addEventListener('uncapturederror', (ev) => showHint('GPU error: ' + ev.error.message));
		this.ctx = canvas.getContext('webgpu');
		this.format = navigator.gpu.getPreferredCanvasFormat();
		this.ctx.configure({ device, format: this.format, alphaMode: 'opaque' });
		this.ui = new Int32Array(this.uf.buffer);

		const module = device.createShaderModule({ code: SHADER });
		const info = await module.getCompilationInfo();
		const errors = info.messages.filter((m) => m.type === 'error');
		if (errors.length) throw new Error('Shader compile error:\n' + errors.map((m) => `line ${m.lineNum}: ${m.message}`).join('\n'));
		this.pipeMain = await device.createComputePipelineAsync({ layout: 'auto', compute: { module, entryPoint: 'main' } });
		this.pipeProbe = await device.createComputePipelineAsync({ layout: 'auto', compute: { module, entryPoint: 'probe' } });
		const pmod = device.createShaderModule({ code: PRESENT });
		this.pipePresent = await device.createRenderPipelineAsync({
			layout: 'auto', vertex: { module: pmod, entryPoint: 'vs' },
			fragment: { module: pmod, entryPoint: 'fs', targets: [{ format: this.format }] }, primitive: { topology: 'triangle-list' },
		});
		this.uniform = device.createBuffer({ size: 128, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
		this.orbit = device.createBuffer({ size: MAX_ORBIT_POINTS * 4 * 16, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST });
		this.probe = device.createBuffer({ size: 32, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC });
		this.readback = device.createBuffer({ size: 32, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
		this.sampler = device.createSampler({ magFilter: 'linear', minFilter: 'linear' });
		this.bgProbe = device.createBindGroup({ layout: this.pipeProbe.getBindGroupLayout(0), entries: [
			{ binding: 0, resource: { buffer: this.uniform } }, { binding: 1, resource: { buffer: this.orbit } }, { binding: 3, resource: { buffer: this.probe } }] });
		this.uploadOrbit(new Float32Array(16 * 4), 0);
	},

	uploadOrbit(floats, count) {
		this.device.queue.writeBuffer(this.orbit, 0, floats.buffer, floats.byteOffset, Math.max(count, 1) * 16 * 4);
		this.orbitCount = count;
	},

	resize() {
		const dpr = Math.min(window.devicePixelRatio || 1, 2);
		const cw = Math.max(1, Math.floor(canvas.clientWidth * dpr)), ch = Math.max(1, Math.floor(canvas.clientHeight * dpr));
		if (canvas.width !== cw || canvas.height !== ch) { canvas.width = cw; canvas.height = ch; }
		const w = Math.max(1, Math.floor(cw * S.scale)), h = Math.max(1, Math.floor(ch * S.scale));
		if (w === this.w && h === this.h && this.tex) return;
		this.w = w; this.h = h;
		if (this.tex) this.tex.destroy();
		this.tex = this.device.createTexture({ size: [w, h], format: 'rgba8unorm', usage: GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_SRC });
		const view = this.tex.createView();
		this.bgMain = this.device.createBindGroup({ layout: this.pipeMain.getBindGroupLayout(0), entries: [
			{ binding: 0, resource: { buffer: this.uniform } }, { binding: 1, resource: { buffer: this.orbit } }, { binding: 2, resource: view }] });
		this.bgPresent = this.device.createBindGroup({ layout: this.pipePresent.getBindGroupLayout(0), entries: [
			{ binding: 0, resource: view }, { binding: 1, resource: this.sampler }] });
	},

	writeUniforms() {
		const f = this.uf, i = this.ui;
		const W = this.w, H = this.h;
		const tx = TAN_HALF * aspect();
		// direction through the centre of marched pixel (0, 0), and the per-pixel steps (the forward component stays 1)
		const nx0 = (0.5 / W) * 2 - 1, ny0 = 1 - (0.5 / H) * 2;
		const dx = (2 / W) * tx, dy = -(2 / H) * TAN_HALF;
		const usable = referenceUsable();
		const center = usable ? S.ref.center : S.pos;
		const off = Lib.v3ToDoubles(Lib.v3Sub(S.pos, center));
		const cf = Lib.v3ToDoubles(center);
		for (let c = 0; c < 3; c++) {
			f[c] = S.fwd[c] + nx0 * tx * S.right[c] + ny0 * TAN_HALF * S.up[c];
			f[4 + c] = dx * S.right[c];
			f[8 + c] = dy * S.up[c];
			f[12 + c] = off[c];
			f[16 + c] = cf[c];
		}
		f[3] = hitThreshold(); f[7] = S.power; f[11] = BAILOUT; f[15] = MAX_RAY_DISTANCE;
		f[19] = Lib.boundingRadius(S.power);
		const budget = usable ? S.ref.iterations : Math.min(BASE_ITERATIONS, 150);
		i[20] = W; i[21] = H; i[22] = usable ? S.ref.count : 0; i[23] = budget;
		i[24] = MAX_RAY_STEPS; i[25] = S.power; i[26] = 0; i[27] = 0;
		f[28] = window.FRACTAL_DEBUG && window.FRACTAL_DEBUG.forceDirect ? 1e30 : DIRECT_FOOTPRINT; f[29] = HAZE_START;
		this.device.queue.writeBuffer(this.uniform, 0, this.uf);
		S.mode = usable ? (S.ref.hit ? 'perturbation' : 'perturbation (camera reference)') : 'plain float (waiting for reference)';
		S.iterations = budget;
	},

	frame() {
		const device = this.device;
		this.resize();
		this.writeUniforms();
		const enc = device.createCommandEncoder();
		const cp = enc.beginComputePass();
		cp.setPipeline(this.pipeMain); cp.setBindGroup(0, this.bgMain);
		cp.dispatchWorkgroups(Math.ceil(this.w / 8), Math.ceil(this.h / 8));
		const wantProbe = !this.readbackBusy;
		const probeUsable = referenceUsable();
		if (wantProbe) { cp.setPipeline(this.pipeProbe); cp.setBindGroup(0, this.bgProbe); cp.dispatchWorkgroups(1); }
		cp.end();
		const rp = enc.beginRenderPass({ colorAttachments: [{ view: this.ctx.getCurrentTexture().createView(), loadOp: 'clear', storeOp: 'store', clearValue: { r: 0, g: 0, b: 0, a: 1 } }] });
		rp.setPipeline(this.pipePresent); rp.setBindGroup(0, this.bgPresent); rp.draw(3); rp.end();
		if (wantProbe) enc.copyBufferToBuffer(this.probe, 0, this.readback, 0, 32);
		device.queue.submit([enc.finish()]);
		if (wantProbe) {
			this.readbackBusy = true;
			this.readback.mapAsync(GPUMapMode.READ).then(() => {
				const v = new Float32Array(this.readback.getMappedRange().slice(0));
				this.readback.unmap();
				this.readbackBusy = false;
				// A probe taken without a usable reference is plain float: only trusted where the surface is far (large distance).
				S.probeValid = probeUsable;
				if (probeUsable || (v[1] > 0.5 && v[0] > 1e-4)) {
					S.camDE = v[0]; S.camEscaped = v[1] > 0.5; S.centerHit = v[2]; S.probeSteps = v[3]; S.probeIters = v[4];
					if (S.camEscaped && v[0] > 0) S.lastGoodDE = v[0];
				}
			}, () => { this.readbackBusy = false; });
		}
	},
};

// ---------------------------------------------------------------------------------------------
// HUD
// ---------------------------------------------------------------------------------------------
function sci(x, digits) {
	if (!isFinite(x)) return '-';
	if (x === 0) return '0';
	return x.toExponential(digits === undefined ? 2 : digits).replace('e+', 'e').replace('e-0', 'e-');
}
function fmtLen(x) { return x >= 0.01 ? x.toFixed(3) : sci(x); }
function decimalsFor() { return Math.max(4, Math.min(30, Math.ceil(-Math.log10(Math.max(S.camDE, 1e-33))) + 4)); }

const gaugeEl = $('gauge');
function buildGauge() {
	let svg = '';
	for (let e = 0; e <= MAX_DEPTH_LOG; e++) {
		const x = (e / MAX_DEPTH_LOG) * 1000;
		const major = e % 3 === 0;
		svg += `<line x1="${x}" x2="${x}" y1="${major ? 6 : 10}" y2="14" stroke="var(--dim)" stroke-width="1" vector-effect="non-scaling-stroke" opacity="${major ? 0.9 : 0.45}"/>`;
	}
	svg += '<line x1="0" x2="1000" y1="14" y2="14" stroke="var(--dim)" stroke-width="1" vector-effect="non-scaling-stroke" opacity="0.6"/>';
	svg += '<rect id="g-fill" x="0" y="11" width="0" height="6" fill="var(--accent)" opacity="0.85"/>';
	svg += '<circle id="g-dot" cx="0" cy="14" r="4" fill="var(--accent)" stroke="var(--bg)" stroke-width="1.5"/>';
	gaugeEl.innerHTML = svg;
	// labels live outside the stretched svg so the text is not distorted
	const host = gaugeEl.parentElement;
	for (const e of [0, 9, 18, 27]) {
		const t = document.createElement('div');
		t.className = 'label';
		t.style.cssText = `position:absolute;top:18px;left:calc(12px + (100% - 24px) * ${e / MAX_DEPTH_LOG});transform:translateX(${e === 0 ? '0' : e === MAX_DEPTH_LOG ? '-100%' : '-50%'});font:400 9.5px var(--font-mono);letter-spacing:0;text-transform:none`;
		t.textContent = e === 0 ? '1x' : '1e' + e + 'x';
		host.appendChild(t);
	}
}
function updateGauge() {
	const depth = Math.max(0, Math.min(MAX_DEPTH_LOG, -Math.log10(Math.max(S.camDE / 2, 1e-40))));
	const x = (depth / MAX_DEPTH_LOG) * 1000;
	const fill = $('g-fill'), dot = $('g-dot');
	if (fill) { fill.setAttribute('width', x); dot.setAttribute('cx', x); }
}

let hudTimer = 0, fpsAccum = 0, fpsFrames = 0, fpsShown = 0, slowSince = 0;
function updateHud(now) {
	if (now - hudTimer < 100) return;
	hudTimer = now;
	const dec = decimalsFor();
	const axis = ['x', 'y', 'z'];
	$('r-de').textContent = S.camEscaped ? fmtLen(S.camDE) : 'inside the set';
	$('r-mag').textContent = sci(2 / Math.max(S.camDE, 1e-40), 2) + 'x';
	$('r-ahead').textContent = S.centerHit >= 0 ? fmtLen(S.centerHit) : 'no surface in range';
	const sp = Math.hypot(S.vel[0], S.vel[1], S.vel[2]);
	$('r-speed').textContent = sci(sp, 2) + ' /s  (' + (S.speedFactor * 100).toFixed(0) + '%/s)';
	$('r-pos').innerHTML = S.pos.map((c, i) => `<div><span>${axis[i]}</span>${Lib.formatDD(c, dec)}</div>`).join('');
	$('r-face').innerHTML = `<div><span>f</span>${S.fwd.map((v) => v.toFixed(4)).join('  ')}</div>`;
	const usable = referenceUsable();
	const m = $('r-mode');
	m.textContent = S.mode || '-';
	m.className = usable || !S.started ? 'state-ok' : 'state-wait';
	$('r-iter').textContent = S.iterations + (S.camEscaped ? '  (camera escapes at ' + S.probeIters + ')' : '  (camera inside the set)');
	$('r-orbit').textContent = S.ref ? S.ref.count + (S.refPending ? '  rebuilding' : '') : (S.refPending ? 'building' : '-');
	$('r-ref').textContent = S.ref ? sci(referenceRatio(), 1) + 'x surface distance' : '-';
	$('r-res').textContent = gpu.w + ' x ' + gpu.h + (S.scale < 1 ? ` (${Math.round(S.scale * 100)}%)` : '');
	$('r-fps').textContent = fpsShown.toFixed(0) + ' fps, ' + S.frameMs.toFixed(1) + ' ms';
	updateGauge();
}

function showHint(text) { const h = $('hint'); h.textContent = text; h.hidden = false; }
function hideHint() { $('hint').hidden = true; }
function updatePowerLabel() { $('pow').textContent = S.power; }

// ---------------------------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------------------------
function setPower(p) {
	p = Math.max(POWER_MIN, Math.min(POWER_MAX, p));
	if (p === S.power) return;
	S.power = p; S.ref = null; updatePowerLabel();
}
function teleport(preset) {
	teleportId++;
	showHint('Finding the surface at double-double precision...');
	const dir = norm(preset.dir);
	worker.postMessage({ type: 'teleport', id: teleportId, start: preset.start, dir, depth: preset.depth, power: S.power, bailout: BAILOUT });
}

function bindInput() {
	window.addEventListener('keydown', (e) => {
		if (e.code === 'Tab') return;
		if (['Space', 'ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight'].includes(e.code)) e.preventDefault();
		if (e.repeat) { S.keys.add(e.code); return; }
		S.keys.add(e.code);
		if (e.code === 'KeyR') { teleportId++; resetCamera(); hideHint(); }
		else if (e.code === 'KeyZ') setPower(S.power - 1);
		else if (e.code === 'KeyX') setPower(S.power + 1);
		else if (e.code === 'KeyF') toggleAutofly();
		else if (e.code === 'KeyH') $('start').hidden = !$('start').hidden;
	});
	window.addEventListener('keyup', (e) => S.keys.delete(e.code));
	window.addEventListener('blur', () => S.keys.clear());
	window.addEventListener('wheel', (e) => {
		if (e.target !== canvas) return;
		S.speedFactor = Math.max(0.05, Math.min(30, S.speedFactor * Math.exp(-e.deltaY * 0.0012)));
		e.preventDefault();
	}, { passive: false });

	canvas.addEventListener('mousedown', () => {
		S.dragging = true;
		if (!S.locked && canvas.requestPointerLock) { try { const p = canvas.requestPointerLock(); if (p && p.catch) p.catch(() => {}); } catch (_) {} }
	});
	window.addEventListener('mouseup', () => { S.dragging = false; });
	document.addEventListener('pointerlockchange', () => { S.locked = document.pointerLockElement === canvas; });
	window.addEventListener('mousemove', (e) => {
		if (!S.locked && !S.dragging) return;
		const k = S.locked ? 0.0022 : 0.004;
		yaw(e.movementX * k); pitch(-e.movementY * k);
	});
	// touch: one finger looks, two fingers fly forward
	let last = null;
	canvas.addEventListener('touchstart', (e) => { e.preventDefault(); last = e.touches[0]; if (e.touches.length > 1) S.keys.add('KeyW'); }, { passive: false });
	canvas.addEventListener('touchmove', (e) => {
		e.preventDefault();
		const t = e.touches[0];
		if (last) { yaw((t.clientX - last.clientX) * 0.005); pitch(-(t.clientY - last.clientY) * 0.005); }
		last = t;
	}, { passive: false });
	canvas.addEventListener('touchend', (e) => { if (e.touches.length < 2) S.keys.delete('KeyW'); if (!e.touches.length) last = null; });

	$('pow-down').onclick = () => setPower(S.power - 1);
	$('pow-up').onclick = () => setPower(S.power + 1);
	$('autofly').onclick = toggleAutofly;
	$('res').onchange = (e) => { S.scale = parseFloat(e.target.value); };
	$('help-btn').onclick = () => { $('start').hidden = false; };
	$('go').onclick = () => { $('start').hidden = true; S.started = true; canvas.focus(); };
	const box = $('presets');
	const overview = document.createElement('button');
	overview.textContent = 'Overview';
	overview.onclick = () => { teleportId++; resetCamera(); hideHint(); };
	box.appendChild(overview);
	for (const p of PRESETS) {
		const b = document.createElement('button');
		b.textContent = p.label;
		b.onclick = () => teleport(p);
		box.appendChild(b);
	}
}
function toggleAutofly() {
	S.autofly = !S.autofly;
	$('autofly').setAttribute('aria-pressed', S.autofly ? 'true' : 'false');
}

// ---------------------------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------------------------
let lastT = 0;
function loop(now) {
	requestAnimationFrame(loop);
	const dtMs = lastT ? now - lastT : 16;
	lastT = now;
	const dt = Math.min(dtMs / 1000, 0.05);
	S.frameMs += (dtMs - S.frameMs) * 0.1;
	fpsAccum += dtMs; fpsFrames++;
	if (fpsAccum >= 500) { fpsShown = fpsFrames * 1000 / fpsAccum; fpsAccum = 0; fpsFrames = 0; }
	if (!$('start').hidden && !S.started) { /* the view is live behind the start card; the camera stays put */ }
	else step(dt);
	maintainReference();
	gpu.frame();
	updateHud(now);
	if (fpsShown && fpsShown < 20 && S.scale === 1 && S.started) {
		slowSince = slowSince || now;
		if (now - slowSince > 4000 && $('hint').hidden) showHint('Running slowly. A lower render resolution in the panel on the right helps.');
	} else slowSince = 0;
}

async function main() {
	buildGauge();
	bindInput();
	resetCamera();
	try {
		await gpu.init();
	} catch (err) {
		const f = $('fail');
		f.hidden = false;
		f.textContent = String(err && err.message || err) + '\n\nThis page needs WebGPU: Chrome or Edge 113+, Safari 18+, or a recent Firefox.';
		$('go').hidden = true;
		return;
	}
	window.__fractal = { S, gpu, Lib }; // for tests
	requestAnimationFrame(loop);
}
main();
})();
