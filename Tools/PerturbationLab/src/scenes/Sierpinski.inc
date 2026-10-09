// Sierpinski tetrahedron, scale 2; vertices (1,1,1), (-1,-1,1), (1,-1,-1), (-1,1,-1). The set is the attractor
// of four copies scaled by 1/s, of dimension log 4 / log s for s >= 2: generic rays meet it only for s <= 2.
// For larger s only rays through exactly representable points of it do: the vertices (the fixed point (1,1,1)
// and its fold images), approached along axes and diagonals so that the ray passes through them exactly.
// The location list therefore follows LAB_POWER (test_perturbed_de's scale override). Generic locations were
// found with random rays from a sphere of radius 3 towards the origin (hitting at scales 1.5 and 2).
inline FFormulaLab Sierpinski()
{
	FFormulaLab L;
	L.Power = 2.0;
	const char* ScaleOverride = std::getenv("LAB_POWER");
	const double Scale = ScaleOverride ? std::atof(ScaleOverride) : L.Power;
	if (Scale <= 2.0)
	{
		L.Locations = {
			{"top", {0.093, -0.133, 2.996}, {-0.023, -0.040, -0.999}, false},
			{"side", {-0.500, 2.948, 0.242}, {0.336, -0.938, -0.088}, false},
			{"lower", {0.618, 2.222, -1.919}, {-0.314, -0.662, 0.680}, false},
			{"back", {-0.913, -2.327, -1.659}, {0.337, 0.790, 0.512}, false},
			{"vertex axis", {3.0, 1.0, 1.0}, {-1.0, 0.0, 0.0}, false},
		};
	}
	else
	{
		L.Locations = {
			{"vertex axis", {3.0, 1.0, 1.0}, {-1.0, 0.0, 0.0}, false},
			{"vertex diagonal", {-3.0, 3.0, -3.0}, {1.0, -1.0, 1.0}, false},
			{"vertex side", {-1.0, -3.0, 1.0}, {0.0, 1.0, 0.0}, false},
			{"vertex below", {1.0, -1.0, -3.0}, {0.0, 0.0, 1.0}, false},
		};
	}
	L.Scenes = {
		{"z1e-1", {0.093, -0.133, 2.996}, {-0.023, -0.040, -0.999}, 1e-1, 8.0, 8, 4, 10},
		{"z1e-5", {-0.500, 2.948, 0.242}, {0.336, -0.938, -0.088}, 1e-5, 3.0, 10, 8, -15},
		{"z1e-10", {0.618, 2.222, -1.919}, {-0.314, -0.662, 0.680}, 1e-10, 3.0, -12, 6, 40},
		{"z1e-15", {-0.913, -2.327, -1.659}, {0.337, 0.790, 0.512}, 1e-15, 3.0, 15, -5, 0},
		{"z1e-20", {0.093, -0.133, 2.996}, {-0.023, -0.040, -0.999}, 1e-20, 3.0, -8, 10, 70},
		{"z1e-25", {-0.500, 2.948, 0.242}, {0.336, -0.938, -0.088}, 1e-25, 3.0, 6, -9, -30},
	};
	return L;
}
