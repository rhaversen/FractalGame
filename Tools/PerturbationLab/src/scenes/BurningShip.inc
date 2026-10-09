// Burning Ship, power 2 (abs per component, then the Mandelbulb power map). Locations are rays from a random ray probe
// (sphere of radius 3 towards random points within ~0.4 of the origin) whose surface the quad march reaches at powers
// 2, 3, 7.5, 8 and 16. Much of this surface is chaotic at float precision (the abs fold stretches and folds): there an
// ideal perturbation that rounds the offset to float once per step glitches as often as the GPU, so such rays are not
// used as test locations.
inline FFormulaLab BurningShip()
{
	FFormulaLab L;
	L.Power = 2.0;
	L.Locations = {
		{"-y upper lobe", {-0.755, -1.926, 2.173}, {0.093, 0.563, -0.821}, false},
		{"-x end", {-2.527, -0.737, -1.440}, {0.750, 0.181, 0.636}, false},
		{"-y side", {-1.166, -2.743, 0.342}, {0.448, 0.894, 0.027}, false},
		{"+x+y underside", {1.878, 1.789, -1.507}, {-0.637, -0.564, 0.525}, false},
		{"+y, near the centre", {0.288, 2.662, -1.353}, {-0.163, -0.903, 0.397}, false},
		{"+y equator", {0.685, 2.898, -0.368}, {-0.440, -0.892, 0.105}, false},
	};
	L.Scenes = {
		{"z1e-1", {-2.527, -0.737, -1.440}, {0.750, 0.181, 0.636}, 1e-1, 8.0, 8, 4, 10},
		{"z1e-5", {-0.755, -1.926, 2.173}, {0.093, 0.563, -0.821}, 1e-5, 3.0, 10, 8, -15},
		{"z1e-10", {-1.166, -2.743, 0.342}, {0.448, 0.894, 0.027}, 1e-10, 3.0, -12, 6, 40},
		{"z1e-15", {1.878, 1.789, -1.507}, {-0.637, -0.564, 0.525}, 1e-15, 3.0, 15, -5, 0},
		{"z1e-20", {0.288, 2.662, -1.353}, {-0.163, -0.903, 0.397}, 1e-20, 3.0, -8, 10, 70},
		{"z1e-25", {0.685, 2.898, -0.368}, {-0.440, -0.892, 0.105}, 1e-25, 3.0, 6, -9, -30},
	};
	return L;
}
