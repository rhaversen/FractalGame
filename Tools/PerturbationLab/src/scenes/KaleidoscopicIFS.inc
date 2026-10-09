// Kaleidoscopic IFS, scale 1.7 rounded to float like the plugin's parameter (the CPU orbit and the GPU then use
// the same scale). The set lies mostly in the negative octant around (-0.7, -0.6, -0.2). Locations are rays that
// were found to hit it at the scales 1.3, 1.7 and 2 (random rays towards points near (-0.7, -0.6, -0.2)).
inline FFormulaLab KaleidoscopicIFS()
{
	FFormulaLab L;
	L.Power = static_cast<double>(1.7f);
	L.Locations = {
		{"front", {1.105, 0.941, 1.635}, {-0.611, -0.483, -0.627}, false},
		{"upper", {0.470, 1.750, 1.253}, {-0.388, -0.797, -0.463}, false},
		{"below", {0.712, -1.001, -2.816}, {-0.552, 0.134, 0.823}, false},
		{"side", {1.236, -2.811, -0.804}, {-0.649, 0.731, 0.209}, false},
		{"rear", {-0.181, -2.618, 1.958}, {-0.213, 0.681, -0.700}, false},
		{"back", {-2.500, 0.449, 1.959}, {0.574, -0.309, -0.758}, false},
	};
	L.Scenes = {
		{"z1e-1", {1.105, 0.941, 1.635}, {-0.611, -0.483, -0.627}, 1e-1, 8.0, 8, 4, 10},
		{"z1e-5", {0.470, 1.750, 1.253}, {-0.388, -0.797, -0.463}, 1e-5, 3.0, 10, 8, -15},
		{"z1e-10", {0.712, -1.001, -2.816}, {-0.552, 0.134, 0.823}, 1e-10, 3.0, -12, 6, 40},
		{"z1e-15", {1.236, -2.811, -0.804}, {-0.649, 0.731, 0.209}, 1e-15, 3.0, 15, -5, 0},
		{"z1e-20", {-0.181, -2.618, 1.958}, {-0.213, 0.681, -0.700}, 1e-20, 3.0, -8, 10, 70},
		{"z1e-25", {1.105, 0.941, 1.635}, {-0.611, -0.483, -0.627}, 1e-25, 3.0, 6, -9, -30},
	};
	return L;
}
