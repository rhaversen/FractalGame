// Presents the texture the compute pass rendered on the canvas (full-screen triangle).

@group(0) @binding(0) var Screen: texture_2d<f32>;
@group(0) @binding(1) var ScreenSampler: sampler;

struct VOut {
	@builtin(position) pos: vec4<f32>,
	@location(0) uv: vec2<f32>,
}

@vertex
fn vs(@builtin(vertex_index) i: u32) -> VOut {
	var p = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0));
	var o: VOut;
	o.pos = vec4<f32>(p[i], 0.0, 1.0);
	o.uv = vec2<f32>(p[i].x * 0.5 + 0.5, 0.5 - p[i].y * 0.5);
	return o;
}

@fragment
fn fs(v: VOut) -> @location(0) vec4<f32> {
	return textureSample(Screen, ScreenSampler, v.uv);
}
