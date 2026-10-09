// Inverted Menger, scale 2.5. The map acts on the multiset of components, so the set is C x C x C with
// C = {x : x -> s|x| - (s - 1) stays in [-1, 1]}: the solid cube for s = 2, a Cantor set of dimension
// log 2 / log s for s > 2. Generic rays meet it only while its dimension 3 log 2 / log s is at least 2
// (s <= 2 sqrt 2); beyond that only rays through exactly representable points of it do, such as the corners
// (+-1, +-1, +-1) (the fixed point (1,1,1) and its fold images), approached along axes and diagonals so that
// the ray passes through them exactly. The location list therefore follows LAB_POWER (test_perturbed_de's
// scale override). Generic locations were found with random rays from a sphere of radius 3 towards the origin.
inline FFormulaLab InvertedMenger()
{
	FFormulaLab L;
	L.Power = 2.5;
	const char* ScaleOverride = std::getenv("LAB_POWER");
	const double Scale = ScaleOverride ? std::atof(ScaleOverride) : L.Power;
	if (Scale <= 2.8)
	{
		L.Locations = {
			{"corner", {1.925, -1.822, 1.405}, {-0.633, 0.605, -0.483}, false},
			{"side", {2.653, 0.085, -1.398}, {-0.897, 0.158, 0.413}, false},
			{"edge", {2.153, -1.322, 1.618}, {-0.632, 0.515, -0.580}, false},
			{"lower", {-1.659, -2.498, -0.085}, {0.633, 0.772, -0.064}, false},
			{"vertex axis", {3.0, 1.0, 1.0}, {-1.0, 0.0, 0.0}, false},
		};
	}
	else
	{
		L.Locations = {
			{"vertex axis", {3.0, 1.0, 1.0}, {-1.0, 0.0, 0.0}, false},
			{"vertex diagonal", {3.0, 3.0, 3.0}, {-1.0, -1.0, -1.0}, false},
			{"vertex face diagonal", {-3.0, -3.0, 1.0}, {1.0, 1.0, 0.0}, false},
			{"vertex below", {1.0, -1.0, -3.0}, {0.0, 0.0, 1.0}, false},
		};
	}
	L.Scenes = {
		{"z1e-1", {1.925, -1.822, 1.405}, {-0.633, 0.605, -0.483}, 1e-1, 8.0, 8, 4, 10},
		{"z1e-5", {2.653, 0.085, -1.398}, {-0.897, 0.158, 0.413}, 1e-5, 3.0, 10, 8, -15},
		{"z1e-10", {2.153, -1.322, 1.618}, {-0.632, 0.515, -0.580}, 1e-10, 3.0, -12, 6, 40},
		{"z1e-15", {-1.659, -2.498, -0.085}, {0.633, 0.772, -0.064}, 1e-15, 3.0, 15, -5, 0},
		{"z1e-20", {1.925, -1.822, 1.405}, {-0.633, 0.605, -0.483}, 1e-20, 3.0, -8, 10, 70},
		{"z1e-25", {2.653, 0.085, -1.398}, {-0.897, 0.158, 0.413}, 1e-25, 3.0, 6, -9, -30},
	};
	return L;
}
