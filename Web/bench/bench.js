// Benchmark of Web/dist/mandelbulb.html in headless Chromium (software WebGPU, SwiftShader): for each fixed view in
// views.json, waits until the reference for the view is built, then renders frames and reports the time per frame and
// the work counters (distance estimates, executed / skipped iterations, march steps per pixel), and saves the image.
// usage: node bench.js http://localhost:8123/mandelbulb.html outdir [W H frames]
const { chromium } = require('/opt/node-tools/node_modules/playwright');
const fs = require('fs');
const path = require('path');
const [url, outDir] = [process.argv[2], process.argv[3]];
const W = +(process.argv[4] || 96), H = +(process.argv[5] || 60), FRAMES = +(process.argv[6] || 3);
const only = process.env.BENCH_VIEWS ? process.env.BENCH_VIEWS.split(',') : null;
const views = JSON.parse(fs.readFileSync(path.join(__dirname, 'views.json'), 'utf8')).filter((v) => !only || only.includes(v.name));
fs.mkdirSync(outDir, { recursive: true });
(async () => {
	const browser = await chromium.launch({ executablePath: '/opt/pw-browsers/chromium-1194/chrome-linux/chrome',
		args: ['--no-sandbox', '--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-angle=swiftshader', '--use-vulkan=swiftshader', '--disable-vulkan-surface', '--ignore-gpu-blocklist'] });
	const results = [];
	for (const view of views) {
		const page = await browser.newPage({ viewport: { width: W, height: H } });
		page.on('pageerror', (e) => console.log('pageerror:', e.message));
		page.on('console', (m) => { if (m.type() === 'error' || /GPU error|warning/i.test(m.text())) console.log('console:', m.text().slice(0, 300)); });
		await page.addInitScript((opts) => { window.FRACTAL_BENCH = true; window.FRACTAL_OPTS = opts; }, JSON.parse(process.env.BENCH_OPTS || '{}'));
		await page.goto(url);
		await page.waitForFunction(() => !!window.__fractal, null, { timeout: 120000 });
		await page.evaluate((v) => {
			const F = __fractal;
			F.S.pos = v.pos; F.lookAlong(v.dir); F.S.vel = [0, 0, 0]; F.S.ref = null; F.S.probeValid = false;
			F.S.camDE = 1e-3; F.S.started = false; document.getElementById('start').hidden = true;
		}, view);
		// settled: a usable reference built for this view, a valid probe, and no rebuild pending or wanted
		await page.waitForFunction(() => {
			const F = __fractal, S = F.S;
			return S.ref && F.referenceUsable() && S.probeValid && !S.refPending && S.ref.iterations === F.wantedIterations();
		}, null, { timeout: 1800000, polling: 2000 });
		const r = await page.evaluate(async (FRAMES) => {
			window.requestAnimationFrame = () => 0;
			const F = __fractal, g = F.gpu, d = g.device;
			await new Promise((res) => setTimeout(res, 300)); await d.queue.onSubmittedWorkDone();
			g.frame(); await d.queue.onSubmittedWorkDone(); // warm-up
			d.queue.writeBuffer(g.stats, 0, new Uint32Array(8));
			const t0 = performance.now();
			for (let i = 0; i < FRAMES; i++) { g.frame(); await d.queue.onSubmittedWorkDone(); }
			const ms = (performance.now() - t0) / FRAMES;
			const rb = d.createBuffer({ size: 32, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
			const bpr = Math.ceil(g.w * 4 / 256) * 256;
			const img = d.createBuffer({ size: bpr * g.h, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
			const enc = d.createCommandEncoder();
			enc.copyBufferToBuffer(g.stats, 0, rb, 0, 32);
			enc.copyTextureToBuffer({ texture: g.tex }, { buffer: img, bytesPerRow: bpr }, [g.w, g.h]);
			d.queue.submit([enc.finish()]);
			await rb.mapAsync(GPUMapMode.READ); await img.mapAsync(GPUMapMode.READ);
			const st = Array.from(new Uint32Array(rb.getMappedRange().slice(0)));
			const a = new Uint8Array(img.getMappedRange().slice(0));
			const px = [];
			for (let y = 0; y < g.h; y++) for (let x = 0; x < g.w * 4; x++) px.push(a[y * bpr + x]);
			const S = F.S;
			return { ms, stats: st, w: g.w, h: g.h, px, de: S.camDE, iterations: S.iterations, orbit: S.ref.count, mode: S.mode };
		}, FRAMES);
		const pixels = r.stats[5] / FRAMES;
		const res = {
			view: view.name, msPerFrame: +r.ms.toFixed(1), iterations: r.iterations, orbit: r.orbit, de: r.de,
			estimatesPerPixel: +(r.stats[0] / FRAMES / pixels).toFixed(2), stepsPerPixel: +(r.stats[3] / FRAMES / pixels).toFixed(2),
			itersPerPixel: +(r.stats[1] / FRAMES / pixels).toFixed(1), coneItersPerPixel: +(r.stats[7] / FRAMES / pixels).toFixed(1),
			totalItersPerPixel: +((r.stats[1] + r.stats[7]) / FRAMES / pixels).toFixed(1), skippedPerPixel: +(r.stats[2] / FRAMES / pixels).toFixed(1),
			hitShare: +(r.stats[4] / FRAMES / pixels).toFixed(3),
		};
		console.log(JSON.stringify(res));
		results.push(res);
		fs.writeFileSync(path.join(outDir, view.name + '.json'), JSON.stringify({ w: r.w, h: r.h, px: r.px }));
		await page.close();
	}
	fs.writeFileSync(path.join(outDir, 'results.json'), JSON.stringify(results, null, 1));
	await browser.close();
})();
