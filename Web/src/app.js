'use strict';
(function () {
const Lib = makeLib();
const $ = (id) => document.getElementById(id);
const canvas = $('view');

// ---------------------------------------------------------------------------------------------
// Settings (FFractalParameter, FractalFormulaTypes.h preset, FractalPlayerController)
// ---------------------------------------------------------------------------------------------
const MAX_DEPTH_LOG = 27;          // precision limit of double-double positions
const BAILOUT = 10;
const BASE_ITERATIONS = 150;
const MAX_RAY_STEPS = 150;
const MAX_RAY_DISTANCE = 500;
const DIRECT_FOOTPRINT = 1e-4;     // fractal-space pixel footprint above which plain float iteration is used
const POWER_MIN = 1, POWER_MAX = 16, POWER_DEFAULT = 8;
const FOV_H = 90 * Math.PI / 180;  // Unreal's default horizontal field of view
const TAN_HALF_H = Math.tan(FOV_H / 2);
const ASYNC_RATIO = 50, SYNC_RATIO = 5000; // reference offset / surface distance (FFractalReferenceManager)
const MIN_CAMERA_DISTANCE = 1e-30;
const TARGET_FRAME_RATES = [0, 30, 45, 60, 90, 120, 144];
const MIN_RESOLUTION_SCALE = 0.3;
const START = { pos: [0.0, -2.35, 0.0], dir: [0, 1, 0] }; // v1.1: 2350 cm from the origin at the default scale, facing it
// World units (Unreal centimetres): fractal = world * scale. The scale starts at the user scale (v1.1's 0.001) and the
// zoom band rescales it around the camera so that the surface stays 2..8 world units away (target 4); speeds and
// accelerations are defined in world units (FractalPlayerController, UFractalControlSubsystem::ZoomAroundCamera).
const USER_SCALE = 0.001, MIN_FRACTAL_SCALE = 1e-30;
const ZOOM_IN_BELOW = 2, ZOOM_OUT_ABOVE = 8, ZOOM_TARGET = 4;

// Deep-zoom presets (lab scenes of the desktop renderer); the camera ends up about `depth` from the surface.
const PRESETS = [
	{ label: '1e-6', depth: 1e-6, start: [0.6, 0.8, 1.6], dir: [-0.3, -0.45, -0.85] },
	{ label: '1e-14', depth: 1e-14, start: [0.3, -1.6, -1.5], dir: [-0.15, 0.7, 0.7] },
	{ label: '1e-18', depth: 1e-18, start: [-1.3, 1.2, -1.1], dir: [0.6, -0.55, 0.5] },
	{ label: '1e-22', depth: 1e-22, start: [-1.3, 1.2, -1.1], dir: [0.6, -0.55, 0.5] },
	{ label: '1e-26', depth: 1e-26, start: [0.3, -1.6, -1.5], dir: [-0.15, 0.7, 0.7] },
];

// Renderer options (Web/bench overrides them through window.FRACTAL_OPTS to compare variants).
// Defaults measured with Web/bench: over-relaxed sphere tracing (fewer steps where rays graze surfaces), the cone pre-pass on
// 8 x 8 tiles (empty space traversed once per tile), and the series tolerance validated in Tools/PerturbationLab.
const OPTS = Object.assign({ relax: 1.6, seriesTolerance: 1e-6, cone: 8, virtual: null }, window.FRACTAL_OPTS || {});
// Benchmark only: OPTS.virtual = [W, H] renders the centre window of a W x H frame (the canvas size), so that pixel
// footprints, hit thresholds and cone tiles are those of the full frame while only a few pixels are computed.
function frameSize() { return OPTS.virtual ? OPTS.virtual : [canvas.width, canvas.height]; }

const S = {
	pos: null, right: null, up: null, fwd: null, vel: [0, 0, 0],
	power: POWER_DEFAULT, powerVel: 0, rollVel: 0, speedPercent: 50, scale: USER_SCALE,
	camDE: 2, camEscaped: true, lastGoodDE: 2, centerHit: -1, probeIters: 0, probeLimit: 0, probeValid: false, rayIters: 0, raySkipped: 0,
	ref: null, refPending: false, refId: 0, lastRequest: null, lastRequestTime: -1,
	targetFps: 0, resolutionScale: 1, measuredMs: 0, sinceResolutionChange: 0,
	nativeRes: false, keys: new Set(), mouse: new Set(), locked: false, dragging: false,
	started: false, autofly: false, mode: '', iterations: BASE_ITERATIONS,
};

// ---------------------------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------------------------
function norm(v) { const l = Math.hypot(v[0], v[1], v[2]) || 1; return [v[0] / l, v[1] / l, v[2] / l]; }
function cross(a, b) { return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]; }
function dot(a, b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
function lookAlong(dir) {
	S.fwd = norm(dir);
	const up = Math.abs(S.fwd[2]) > 0.95 ? [0, 1, 0] : [0, 0, 1];
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

function resetToStart() {
	S.pos = START.pos.map((v) => [v, 0]);
	lookAlong(START.dir);
	S.vel = [0, 0, 0]; S.powerVel = 0; S.rollVel = 0;
	S.speedPercent = 50;
	S.scale = USER_SCALE;
	setPower(POWER_DEFAULT);
	S.camDE = 1.2; S.lastGoodDE = 1.2; S.probeValid = false;
}

// Full-resolution ray basis (BuildRayBasis): direction through pixel centre (px, py), forward component 1.
function dirAt(px, py, W, H) {
	const tanV = TAN_HALF_H * H / W;
	const x = (px / W) * 2 - 1, y = 1 - (py / H) * 2;
	return [0, 1, 2].map((c) => S.fwd[c] + x * TAN_HALF_H * S.right[c] + y * tanV * S.up[c]);
}
function pixelRadiusPerUnitDistance() { const [W] = frameSize(); return TAN_HALF_H / W; }

// ---------------------------------------------------------------------------------------------
// Controller (FractalPlayerController)
// ---------------------------------------------------------------------------------------------
// 0% = stationary, 100% = 0.01 s to the surface; logarithmic in the time to the surface (ComputeSpeedFromPercent).
function speedFromPercent(percent, distance) {
	percent = Math.max(0, Math.min(100, percent));
	if (percent <= 0) return 0;
	const logMin = Math.log(0.01), logMax = Math.log(1000);
	const time = Math.exp(logMax + (logMin - logMax) * Math.pow(percent / 100, 0.25));
	return Math.max(0.1, Math.min(1000, distance / Math.max(time, 0.01)));
}
// Moves a velocity towards input * maxRate; decays at stopRate without input (StepAdjustVelocity).
function stepAdjustVelocity(v, input, maxRate, rate, opposingRate, stopRate, dt) {
	if (Math.abs(input) < 1e-8) return v > 0 ? Math.max(0, v - stopRate * dt) : Math.min(0, v + stopRate * dt);
	const target = input * maxRate;
	const r = (input > 0 && v < 0) || (input < 0 && v > 0) ? opposingRate : rate;
	return v < target ? Math.min(target, v + r * dt) : Math.max(target, v - r * dt);
}

function controllerTick(dt) {
	const k = S.keys;
	// roll (Q/E): 90 deg/s, accelerates at 180 deg/s^2, stops at 360 deg/s^2
	const rollIn = (k.has('KeyE') ? 1 : 0) - (k.has('KeyQ') ? 1 : 0);
	S.rollVel = stepAdjustVelocity(S.rollVel, rollIn, 90, 180, 180, 360, dt);
	if (S.rollVel) roll(S.rollVel * dt * Math.PI / 180);
	// power (hold right / left mouse button while the pointer is captured): 2 units/s, ramps at 0.5, stops at 4
	const powerIn = (S.mouse.has(2) ? 1 : 0) - (S.mouse.has(0) ? 1 : 0);
	S.powerVel = stepAdjustVelocity(S.powerVel, S.locked ? powerIn : 0, 2, 0.5, 4, 4, dt);
	if (S.powerVel) setPower(S.power + S.powerVel * dt);
	if (k.has('ArrowLeft')) yaw(-1.4 * dt);
	if (k.has('ArrowRight')) yaw(1.4 * dt);
	if (k.has('ArrowUp')) pitch(1.0 * dt);
	if (k.has('ArrowDown')) pitch(-1.0 * dt);
	orthonormalize();

	// Zoom band (UpdateZoom): rescale world units around the camera when the surface is nearer than 2 or (below the user
	// scale) farther than 8 world units. Velocities are kept in fractal units, so the rescale is invisible.
	let distance = S.camDE / S.scale;
	if ((distance > 0 && distance < ZOOM_IN_BELOW) || (distance > ZOOM_OUT_ABOVE && S.scale < USER_SCALE * (1 - 1e-9))) {
		S.scale = Math.max(MIN_FRACTAL_SCALE, Math.min(USER_SCALE, S.scale * distance / ZOOM_TARGET));
		distance = S.camDE / S.scale;
	}
	const unit = S.scale; // fractal units per world unit
	let v = S.vel.map((x) => x / unit);
	const maxSpeed = speedFromPercent(S.speedPercent, distance);
	const accel = Math.max(0.2 * maxSpeed, 2.0);
	let mx = 0, my = 0, mz = 0;
	if (k.has('KeyW') || S.autofly) mz += 1;
	if (k.has('KeyS')) mz -= 1;
	if (k.has('KeyD')) mx += 1;
	if (k.has('KeyA')) mx -= 1;
	if (k.has('Space')) my += 1;
	if (k.has('ShiftLeft') || k.has('ShiftRight')) my -= 1;
	const input = [0, 1, 2].map((c) => S.fwd[c] * mz + S.right[c] * mx + S.up[c] * my);
	const inLen = Math.hypot(input[0], input[1], input[2]);
	if (inLen > 1e-6) {
		const n = input.map((x) => x / Math.max(inLen, 1)); // clamped to unit length
		const nl = Math.hypot(n[0], n[1], n[2]);
		const delta = Math.max(accel * dt * nl, 2.0 * dt);
		v = v.map((x, c) => x + (n[c] / nl) * delta);
		const speed = Math.hypot(v[0], v[1], v[2]);
		const along = dot(v, n);
		if (speed > 0 && along < 0) { // brake the component opposing the input
			const brake = Math.min(Math.max(4.0 * speed, 1.0) * dt, Math.abs(along));
			v = v.map((x, c) => x + n[c] * brake);
		}
	} else {
		const speed = Math.hypot(v[0], v[1], v[2]);
		const decel = Math.max(3.0 * speed, 0.3) * dt; // UFloatingPawnMovement deceleration
		const f = speed > decel ? (speed - decel) / speed : 0;
		v = v.map((x) => x * f);
	}
	const speed = Math.hypot(v[0], v[1], v[2]);
	if (speed > maxSpeed) v = v.map((x) => x * maxSpeed / speed);
	S.maxSpeedWorld = maxSpeed;
	S.vel = v.map((x) => x * unit);
	S.pos = Lib.v3AddDoubles(S.pos, [S.vel[0] * dt, S.vel[1] * dt, S.vel[2] * dt]);
}

function setPower(p) {
	p = Math.max(POWER_MIN, Math.min(POWER_MAX, p));
	if (p === S.power) return;
	S.power = p;
	$('pow').textContent = p.toFixed(2);
}

// ---------------------------------------------------------------------------------------------
// Adaptive resolution (UFractalControlSubsystem::UpdateAdaptiveResolution), opt-in
// ---------------------------------------------------------------------------------------------
function updateAdaptiveResolution(dt, frameMs) {
	const blend = 1 - Math.exp(-dt / 0.3);
	S.measuredMs = S.measuredMs > 0 ? S.measuredMs + (frameMs - S.measuredMs) * blend : frameMs;
	if (S.targetFps <= 0) { S.resolutionScale = 1; return; }
	S.sinceResolutionChange += dt;
	if (S.sinceResolutionChange < 0.25) return;
	const ratio = S.measuredMs / (1000 / S.targetFps);
	let scale = S.resolutionScale;
	if (ratio > 1.05) scale *= Math.max(0.7, Math.min(0.99, Math.sqrt(1 / ratio)));
	else if (ratio < 0.85 && scale < 1) scale *= Math.min(Math.sqrt(1 / ratio), 1.06);
	scale = Math.max(MIN_RESOLUTION_SCALE, Math.min(1, scale));
	if (scale > 0.97) scale = 1;
	if (Math.abs(scale - S.resolutionScale) > 0.005) { S.resolutionScale = scale; S.sinceResolutionChange = 0; }
}
function stepTargetFrameRate(direction) {
	let index = 0;
	for (let i = 0; i < TARGET_FRAME_RATES.length; i++) if (Math.abs(TARGET_FRAME_RATES[i] - S.targetFps) < Math.abs(TARGET_FRAME_RATES[index] - S.targetFps)) index = i;
	S.targetFps = TARGET_FRAME_RATES[Math.max(0, Math.min(TARGET_FRAME_RATES.length - 1, index + direction))];
}

// ---------------------------------------------------------------------------------------------
// Reference (FFractalReferenceManager): built in a Web Worker
// ---------------------------------------------------------------------------------------------
const workerSource = `const Lib = (${makeLib.toString()})();
onmessage = (e) => {
	const m = e.data;
	try {
		if (m.type === 'reference') {
			const t0 = performance.now();
			const r = Lib.buildReference(m.req);
			postMessage({ type: 'reference', id: m.id, power: m.req.power, iterations: r.iterations, center: r.center, orbit: r.orbit,
				count: r.count, hit: r.hit, cameraDE: r.cameraDE, cameraIterations: r.cameraIterations, ms: performance.now() - t0 }, [r.orbit.buffer]);
		} else if (m.type === 'teleport') {
			const pos = Lib.surfaceCamera(m.start, m.dir, m.depth, m.power, m.bailout, m.iterations);
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
		if (m.id !== S.refId || m.power !== S.power) return;
		gpu.uploadOrbit(m.orbit, m.count);
		S.ref = { center: m.center, count: m.count, iterations: m.iterations, power: m.power, hit: m.hit, cameraDE: m.cameraDE,
			cameraIterations: m.cameraIterations, ms: m.ms };
		// Until the GPU probes the camera with this reference, the worker's double-double estimate is the trustworthy one.
		if (!S.probeValid && m.cameraDE > 0) { S.camDE = m.cameraDE; S.lastGoodDE = m.cameraDE; S.camEscaped = true; }
	} else if (m.type === 'teleport') {
		if (m.id !== teleportId) return;
		if (!m.pos) { showHint('No surface found along that line for this power.'); return; }
		S.pos = m.pos; S.vel = [0, 0, 0]; S.ref = null;
		lookAlong(m.dir);
		S.camDE = m.depth; S.lastGoodDE = m.depth; S.camEscaped = true; S.probeValid = false;
		hideHint();
	} else if (m.type === 'error') {
		showHint('Reference worker error: ' + m.message);
	}
};

function referenceOffset() {
	if (!S.ref) return Infinity;
	const d = Lib.v3ToDoubles(Lib.v3Sub(S.pos, S.ref.center));
	return Math.hypot(d[0], d[1], d[2]);
}
// FFractalReferenceManager floors the surface distance at 1e-3 world units (a camera inside the set has none).
function surfaceDistance() { return Math.max(S.camDE, 1e-3 * S.scale, MIN_CAMERA_DISTANCE); }
function referenceUsable() { return !!S.ref && S.ref.power === S.power && referenceOffset() / surfaceDistance() <= SYNC_RATIO; }

// Iterations for the current view: the footprint budget (ComputeIterationBudget), or, next to the surface, the budget
// sized from the camera's own orbit (ComputeOrbitIterationBudget) when that is larger. A camera whose orbit does not
// escape within the current count asks for twice as many.
function cameraOrbit() {
	if (S.probeValid) return S.camEscaped ? { n: S.probeIters, de: S.camDE } : null;
	return S.ref && S.ref.cameraIterations ? { n: S.ref.cameraIterations, de: S.ref.cameraDE } : null;
}
function wantedIterations() {
	const formula = Lib.iterationBudget(S.power, BASE_ITERATIONS, BAILOUT, surfaceDistance() * pixelRadiusPerUnitDistance());
	const current = S.ref ? S.ref.iterations : formula;
	const cam = cameraOrbit();
	// the camera did not escape within the limit its probe ran with: twice that limit
	if (!cam) return S.probeValid ? Math.max(current, Math.min(Lib.MAX_ITERATIONS, 2 * S.probeLimit)) : current;
	const wanted = Math.max(formula, Lib.orbitIterationBudget(cam.n, cam.de, pixelRadiusPerUnitDistance()));
	// keep the current count unless it is too short, or much longer than needed (each rebuild costs a reference)
	return wanted > current || wanted < 0.6 * current ? wanted : current;
}

// EvaluateNeed: sync (quick fan, the view waits on plain float) when there is no usable reference, async when the
// reference has drifted, the iteration count changed, or the camera moved or turned.
function maintainReference(now) {
	if (S.refPending) return;
	const iterations = wantedIterations();
	let need = null;
	if (!referenceUsable()) need = 'sync';
	else if (now - S.lastRequestTime >= 50) {
		const L = S.lastRequest;
		const moved = L ? Math.hypot(...Lib.v3ToDoubles(Lib.v3Sub(S.pos, L.pos))) : Infinity;
		const turned = L ? Math.acos(Math.max(-1, Math.min(1, dot(S.fwd, L.fwd)))) : Infinity;
		if (referenceOffset() / surfaceDistance() > ASYNC_RATIO || S.ref.iterations !== iterations || moved > 0.01 * surfaceDistance() || turned > 0.01) need = 'async';
	}
	if (!need) return;
	S.refPending = true;
	S.refId++;
	S.lastRequestTime = now;
	S.lastRequest = { pos: S.pos, fwd: S.fwd.slice() };
	const [W, H] = frameSize();
	worker.postMessage({ type: 'reference', id: S.refId, req: {
		position: S.pos, dirs: Lib.buildReferenceFan((px, py) => dirAt(px, py, W, H), W, H), quick: need === 'sync',
		power: S.power, bailout: BAILOUT, iterations, pixelRadiusPerUnitDistance: pixelRadiusPerUnitDistance(),
		maxSteps: MAX_RAY_STEPS, maxDistance: MAX_RAY_DISTANCE, seriesTolerance: OPTS.seriesTolerance,
	} });
}

// ---------------------------------------------------------------------------------------------
// WebGPU
// ---------------------------------------------------------------------------------------------
const gpu = {
	device: null, ctx: null, format: null, uniform: null, orbit: null, probe: null, readback: null, readbackBusy: false,
	pipes: {}, pipePresent: null, bg: {}, bgPresent: null, tex: null, sampler: null,
	uf: new Float32Array(32), ui: null, w: 0, h: 0, orbitCount: 0,

	async init() {
		if (!navigator.gpu) throw new Error('WebGPU is not available in this browser.');
		const adapter = await navigator.gpu.requestAdapter({ powerPreference: 'high-performance' });
		if (!adapter) throw new Error('No WebGPU adapter was found. Check that hardware acceleration is on.');
		const device = this.device = await adapter.requestDevice();
		this.adapterName = adapter.info ? [adapter.info.vendor, adapter.info.architecture, adapter.info.description].filter(Boolean).join(' ') : '';
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
		// Two permutations, as in Unreal: power 8 as a constant, and any power.
		this.bench = !!window.FRACTAL_BENCH;
		// Explicit bind group layouts: a layout derived from the shader ('auto') depends on which bindings an implementation
		// considers used, e.g. whether it strips code behind a constant-false override, and a mismatch invalidates every pass.
		const C = GPUShaderStage.COMPUTE;
		const uniform = (binding) => ({ binding, visibility: C, buffer: { type: 'uniform' } });
		const storage = (binding, type) => ({ binding, visibility: C, buffer: { type } });
		const mainLayout = device.createBindGroupLayout({ entries: [
			uniform(0), storage(1, 'read-only-storage'),
			{ binding: 2, visibility: C, storageTexture: { access: 'write-only', format: 'rgba8unorm' } },
			storage(4, 'storage'),
			{ binding: 6, visibility: C, texture: { sampleType: 'unfilterable-float' } }] });
		const coneLayout = device.createBindGroupLayout({ entries: [
			uniform(0), storage(1, 'read-only-storage'), storage(4, 'storage'),
			{ binding: 5, visibility: C, storageTexture: { access: 'write-only', format: 'r32float' } }] });
		const probeLayout = device.createBindGroupLayout({ entries: [uniform(0), storage(1, 'read-only-storage'), storage(3, 'storage')] });
		this.layouts = { main: mainLayout, cone: coneLayout, probe: probeLayout };
		const pipelineLayout = (l) => device.createPipelineLayout({ bindGroupLayouts: [l] });
		for (const [name, power] of [['p8', 8], ['any', 0]]) {
			const constants = { STATIC_POWER: power, BENCH: this.bench ? 1 : 0, RELAX: OPTS.relax, CONE_TILE: OPTS.cone };
			this.pipes[name] = {
				cone: OPTS.cone > 0 ? await device.createComputePipelineAsync({ layout: pipelineLayout(coneLayout), compute: { module, entryPoint: 'cone', constants } }) : null,
				main: await device.createComputePipelineAsync({ layout: pipelineLayout(mainLayout), compute: { module, entryPoint: 'main', constants } }),
				probe: await device.createComputePipelineAsync({ layout: pipelineLayout(probeLayout), compute: { module, entryPoint: 'probe', constants } }),
			};
		}
		const pmod = device.createShaderModule({ code: PRESENT });
		this.pipePresent = await device.createRenderPipelineAsync({
			layout: 'auto', vertex: { module: pmod, entryPoint: 'vs' },
			fragment: { module: pmod, entryPoint: 'fs', targets: [{ format: this.format }] }, primitive: { topology: 'triangle-list' },
		});
		this.uniform = device.createBuffer({ size: 128, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
		this.orbit = device.createBuffer({ size: (Lib.MAX_ITERATIONS + 2) * Lib.ORBIT_STRIDE * 16, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST });
		this.probe = device.createBuffer({ size: 32, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC });
		this.readback = device.createBuffer({ size: 32, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
		this.stats = device.createBuffer({ size: 32, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC | GPUBufferUsage.COPY_DST });
		this.sampler = device.createSampler({ magFilter: 'linear', minFilter: 'linear' });
		for (const name of Object.keys(this.pipes)) {
			this.bg[name] = { probe: device.createBindGroup({ layout: this.layouts.probe, entries: [
				{ binding: 0, resource: { buffer: this.uniform } }, { binding: 1, resource: { buffer: this.orbit } }, { binding: 3, resource: { buffer: this.probe } }] }) };
		}
		this.uploadOrbit(new Float32Array(Lib.ORBIT_STRIDE * 4), 0);
	},

	uploadOrbit(floats, count) {
		this.device.queue.writeBuffer(this.orbit, 0, floats.buffer, floats.byteOffset, Math.max(count, 1) * Lib.ORBIT_STRIDE * 16);
		this.orbitCount = count;
	},

	resize() {
		const dpr = S.nativeRes ? Math.min(window.devicePixelRatio || 1, 2) : 1;
		const cw = Math.max(1, Math.floor(canvas.clientWidth * dpr)), ch = Math.max(1, Math.floor(canvas.clientHeight * dpr));
		if (canvas.width !== cw || canvas.height !== ch) { canvas.width = cw; canvas.height = ch; }
		const w = Math.max(1, Math.round(cw * S.resolutionScale)), h = Math.max(1, Math.round(ch * S.resolutionScale));
		if (w === this.w && h === this.h && this.tex) return;
		this.w = w; this.h = h;
		if (this.tex) this.tex.destroy();
		this.tex = this.device.createTexture({ size: [w, h], format: 'rgba8unorm', usage: GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_SRC });
		const view = this.tex.createView();
		// cone pre-pass output: one start distance per tile of marched pixels (1 x 1 when the pre-pass is off)
		if (this.coneTex) this.coneTex.destroy();
		this.tiles = OPTS.cone > 0 ? [Math.ceil(w / OPTS.cone), Math.ceil(h / OPTS.cone)] : [1, 1];
		this.coneTex = this.device.createTexture({ size: this.tiles, format: 'r32float', usage: GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING });
		const coneView = this.coneTex.createView();
		for (const name of Object.keys(this.pipes)) {
			this.bg[name].main = this.device.createBindGroup({ layout: this.layouts.main, entries: [
				{ binding: 0, resource: { buffer: this.uniform } }, { binding: 1, resource: { buffer: this.orbit } }, { binding: 2, resource: view },
				{ binding: 4, resource: { buffer: this.stats } }, { binding: 6, resource: coneView }] });
			if (this.pipes[name].cone) {
				this.bg[name].cone = this.device.createBindGroup({ layout: this.layouts.cone, entries: [
					{ binding: 0, resource: { buffer: this.uniform } }, { binding: 1, resource: { buffer: this.orbit } },
					{ binding: 4, resource: { buffer: this.stats } }, { binding: 5, resource: coneView }] });
			}
		}
		this.bgPresent = this.device.createBindGroup({ layout: this.pipePresent.getBindGroupLayout(0), entries: [
			{ binding: 0, resource: view }, { binding: 1, resource: this.sampler }] });
	},

	writeUniforms() {
		const f = this.uf, i = this.ui;
		const [W, H] = frameSize();
		// pixel (0, 0) of the canvas; a virtual frame shows its centre window
		const ox = OPTS.virtual ? (W - canvas.width) / 2 : 0, oy = OPTS.virtual ? (H - canvas.height) / 2 : 0;
		const d00 = dirAt(ox + 0.5, oy + 0.5, W, H), d10 = dirAt(ox + 1.5, oy + 0.5, W, H), d01 = dirAt(ox + 0.5, oy + 1.5, W, H);
		const usable = referenceUsable();
		const center = usable ? S.ref.center : S.pos;
		const off = Lib.v3ToDoubles(Lib.v3Sub(S.pos, center));
		const cf = Lib.v3ToDoubles(center);
		for (let c = 0; c < 3; c++) {
			f[c] = d00[c]; f[4 + c] = d10[c] - d00[c]; f[8 + c] = d01[c] - d00[c];
			f[12 + c] = off[c]; f[16 + c] = cf[c];
		}
		f[3] = pixelRadiusPerUnitDistance(); f[7] = S.power; f[11] = BAILOUT; f[15] = MAX_RAY_DISTANCE;
		f[19] = Lib.boundingRadius(S.power);
		if (!isFinite(f[19])) f[19] = 1e30;
		const iterations = usable ? S.ref.iterations : BASE_ITERATIONS;
		i[20] = this.w; i[21] = this.h; i[22] = usable ? S.ref.count : 0; i[23] = iterations;
		// haze is measured from the camera's own escape count: only detail finer than the pixels fades
		const cam = cameraOrbit();
		i[24] = MAX_RAY_STEPS; i[25] = usable && cam && cam.n < iterations ? Math.floor(cam.n) : 0; i[26] = 0; i[27] = 0;
		f[28] = window.FRACTAL_DEBUG && window.FRACTAL_DEBUG.forceDirect ? 1e30 : DIRECT_FOOTPRINT;
		f[29] = Math.min(1, Math.max(S.camDE > 0 ? S.camDE : S.lastGoodDE, 1e-36)); // Jacobian scale: dz/dc grows like 1 / distance
		f[30] = canvas.width / this.w; f[31] = canvas.height / this.h;
		this.device.queue.writeBuffer(this.uniform, 0, this.uf);
		S.mode = usable ? (S.ref.hit ? 'perturbation' : 'perturbation, camera reference') : 'plain float, building reference';
		S.iterations = iterations;
		return usable;
	},

	frame() {
		const device = this.device;
		this.resize();
		const usable = this.writeUniforms();
		const probeLimit = S.iterations;
		const pipe = S.power === 8 ? 'p8' : 'any';
		const enc = device.createCommandEncoder();
		const cp = enc.beginComputePass();
		if (this.pipes[pipe].cone) {
			cp.setPipeline(this.pipes[pipe].cone); cp.setBindGroup(0, this.bg[pipe].cone);
			cp.dispatchWorkgroups(Math.ceil(this.tiles[0] / 8), Math.ceil(this.tiles[1] / 8));
		}
		cp.setPipeline(this.pipes[pipe].main); cp.setBindGroup(0, this.bg[pipe].main);
		cp.dispatchWorkgroups(Math.ceil(this.w / 8), Math.ceil(this.h / 8));
		const wantProbe = !this.readbackBusy;
		if (wantProbe) { cp.setPipeline(this.pipes[pipe].probe); cp.setBindGroup(0, this.bg[pipe].probe); cp.dispatchWorkgroups(1); }
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
				// A probe taken without a usable reference is plain float: only trusted where the surface is far.
				// A probe run with an older, shorter orbit than the current one says nothing about it.
				S.probeValid = usable && (!S.ref || probeLimit >= S.ref.iterations);
				S.probeLimit = probeLimit;
				if (S.probeValid || (v[1] > 0.5 && v[0] > 1e-4)) {
					S.camDE = v[0]; S.camEscaped = v[1] > 0.5; S.centerHit = v[2]; S.probeSteps = v[3]; S.probeIters = v[4];
					S.rayIters = v[6]; S.raySkipped = v[7];
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
function decimalsFor() { return Math.max(4, Math.min(30, Math.ceil(-Math.log10(surfaceDistance())) + 4)); }

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
	const host = gaugeEl.parentElement;
	for (const e of [0, 9, 18, 27]) {
		const t = document.createElement('div');
		t.className = 'gauge-label';
		t.style.left = `calc(12px + (100% - 24px) * ${e / MAX_DEPTH_LOG})`;
		t.style.transform = `translateX(${e === 0 ? '0' : e === MAX_DEPTH_LOG ? '-100%' : '-50%'})`;
		t.textContent = e === 0 ? '1x' : '1e' + e + 'x';
		host.appendChild(t);
	}
}

let hudTimer = 0, fpsAccum = 0, fpsFrames = 0, fpsShown = 0;
function updateHud(now) {
	if (now - hudTimer < 100) return;
	hudTimer = now;
	const dec = decimalsFor();
	const axis = ['x', 'y', 'z'];
	$('r-de').textContent = S.camEscaped ? fmtLen(S.camDE) : 'inside the set';
	$('r-mag').textContent = sci(2 / surfaceDistance(), 2) + 'x';
	$('r-ahead').textContent = S.centerHit >= 0 ? fmtLen(S.centerHit) : 'no surface in range';
	$('r-speed').textContent = S.speedPercent.toFixed(0) + '%  (' + sci(Math.hypot(...S.vel), 2) + ' /s)';
	$('r-target').textContent = S.targetFps ? S.targetFps + ' fps' : 'off';
	$('r-res').textContent = gpu.w + ' x ' + gpu.h + (S.resolutionScale < 1 ? ` (${Math.round(S.resolutionScale * 100)}%)` : '');
	$('r-pos').innerHTML = S.pos.map((c, i) => `<div><span>${axis[i]}</span>${Lib.formatDD(c, dec)}</div>`).join('');
	$('r-face').innerHTML = `<div><span>f</span>${S.fwd.map((v) => v.toFixed(4)).join('  ')}</div><div><span>u</span>${S.up.map((v) => v.toFixed(4)).join('  ')}</div>`;
	const m = $('r-mode');
	m.textContent = S.mode;
	m.className = referenceUsable() ? 'state-ok' : 'state-wait';
	const skipShare = S.rayIters + S.raySkipped > 0 ? S.raySkipped / (S.rayIters + S.raySkipped) : 0;
	$('r-iter').textContent = S.iterations + (S.camEscaped && S.probeValid ? `, camera escapes at ${S.probeIters}` : '');
	$('r-skip').textContent = (skipShare * 100).toFixed(0) + '% of iterations skipped';
	$('r-orbit').textContent = S.ref ? S.ref.count + ' (' + S.ref.ms.toFixed(0) + ' ms)' + (S.refPending ? ', rebuilding' : '') : (S.refPending ? 'building' : '-');
	$('r-ref').textContent = S.ref ? sci(referenceOffset() / surfaceDistance(), 1) + 'x surface distance' : '-';
	$('r-fps').textContent = fpsShown.toFixed(0) + ' fps, ' + S.measuredMs.toFixed(1) + ' ms';
	const depth = Math.max(0, Math.min(MAX_DEPTH_LOG, -Math.log10(Math.max(S.camDE / 2, 1e-40))));
	const x = (depth / MAX_DEPTH_LOG) * 1000;
	$('g-fill').setAttribute('width', x); $('g-dot').setAttribute('cx', x);
}

function showHint(text) { const h = $('hint'); h.textContent = text; h.hidden = false; }
function hideHint() { $('hint').hidden = true; }

// ---------------------------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------------------------
function teleport(preset) {
	teleportId++;
	showHint('Finding the surface in double-double precision...');
	worker.postMessage({ type: 'teleport', id: teleportId, start: preset.start, dir: norm(preset.dir), depth: preset.depth, power: S.power, bailout: BAILOUT,
		iterations: 2000 }); // long orbits: next to the surface they escape slowly, so short ones find a surface in front of the real one
}
function toggleAutofly() {
	S.autofly = !S.autofly;
	$('autofly').setAttribute('aria-pressed', S.autofly ? 'true' : 'false');
}

function bindInput() {
	window.addEventListener('keydown', (e) => {
		if (['Space', 'ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', 'PageUp', 'PageDown'].includes(e.code)) e.preventDefault();
		const first = !S.keys.has(e.code);
		S.keys.add(e.code);
		if (!first) return;
		if (e.code === 'KeyR') { teleportId++; resetToStart(); hideHint(); }
		else if (e.code === 'KeyF') toggleAutofly();
		else if (e.code === 'KeyH') $('help').hidden = false;
		else if (e.code === 'PageUp') stepTargetFrameRate(1);
		else if (e.code === 'PageDown') stepTargetFrameRate(-1);
	});
	window.addEventListener('keyup', (e) => { S.keys.delete(e.code); if (e.code === 'KeyH') $('help').hidden = true; });
	window.addEventListener('blur', () => { S.keys.clear(); S.mouse.clear(); });
	window.addEventListener('wheel', (e) => {
		if (e.target !== canvas) return;
		S.speedPercent = Math.max(0, Math.min(100, S.speedPercent - Math.sign(e.deltaY) * 5)); // 5% per notch
		e.preventDefault();
	}, { passive: false });
	canvas.addEventListener('contextmenu', (e) => e.preventDefault());
	canvas.addEventListener('mousedown', (e) => {
		S.mouse.add(e.button);
		if (!S.locked) {
			S.dragging = true;
			if (canvas.requestPointerLock) { try { const p = canvas.requestPointerLock(); if (p && p.catch) p.catch(() => {}); } catch (_) {} }
		}
	});
	window.addEventListener('mouseup', (e) => { S.mouse.delete(e.button); S.dragging = false; });
	document.addEventListener('pointerlockchange', () => { S.locked = document.pointerLockElement === canvas; S.mouse.clear(); });
	window.addEventListener('mousemove', (e) => {
		if (!S.locked && !S.dragging) return;
		const k = S.locked ? 0.0022 : 0.004;
		yaw(e.movementX * k); pitch(-e.movementY * k);
	});
	let last = null; // touch: one finger looks, two fingers fly forward
	canvas.addEventListener('touchstart', (e) => { e.preventDefault(); last = e.touches[0]; if (e.touches.length > 1) S.keys.add('KeyW'); }, { passive: false });
	canvas.addEventListener('touchmove', (e) => {
		e.preventDefault();
		const t = e.touches[0];
		if (last) { yaw((t.clientX - last.clientX) * 0.005); pitch(-(t.clientY - last.clientY) * 0.005); }
		last = t;
	}, { passive: false });
	canvas.addEventListener('touchend', (e) => { if (e.touches.length < 2) S.keys.delete('KeyW'); if (!e.touches.length) last = null; });

	$('pow-down').onclick = () => setPower(Math.ceil(S.power - 1));
	$('pow-up').onclick = () => setPower(Math.floor(S.power + 1));
	$('autofly').onclick = toggleAutofly;
	$('fps-down').onclick = () => stepTargetFrameRate(-1);
	$('fps-up').onclick = () => stepTargetFrameRate(1);
	$('native').onchange = (e) => { S.nativeRes = e.target.checked; };
	$('help-btn').onclick = () => { $('start').hidden = false; };
	$('go').onclick = () => { $('start').hidden = true; S.started = true; canvas.focus(); };
	const box = $('presets');
	const overview = document.createElement('button');
	overview.textContent = 'Start';
	overview.onclick = () => { teleportId++; resetToStart(); hideHint(); };
	box.appendChild(overview);
	for (const p of PRESETS) {
		const b = document.createElement('button');
		b.textContent = p.label;
		b.onclick = () => teleport(p);
		box.appendChild(b);
	}
}

// ---------------------------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------------------------
let lastT = 0;
function loop(now) {
	requestAnimationFrame(loop);
	const frameMs = lastT ? now - lastT : 16;
	lastT = now;
	const dt = Math.min(frameMs / 1000, 0.05);
	fpsAccum += frameMs; fpsFrames++;
	if (fpsAccum >= 500) { fpsShown = fpsFrames * 1000 / fpsAccum; fpsAccum = 0; fpsFrames = 0; }
	updateAdaptiveResolution(frameMs / 1000, frameMs);
	if (S.started) controllerTick(dt);
	maintainReference(now);
	gpu.frame();
	updateHud(now);
}

async function main() {
	buildGauge();
	bindInput();
	resetToStart();
	$('pow').textContent = S.power.toFixed(2);
	try {
		await gpu.init();
	} catch (err) {
		const f = $('fail');
		f.hidden = false;
		f.textContent = String(err && err.message || err) + '\n\nThis page needs WebGPU: Chrome or Edge 113+, Safari 26+, or Firefox 141+ on Windows.';
		$('go').hidden = true;
		return;
	}
	window.__fractal = { S, gpu, Lib, lookAlong, wantedIterations, referenceUsable }; // for tests and Web/bench
	requestAnimationFrame(loop);
}
main();
})();
