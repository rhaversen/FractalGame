// Quaternion Julia set, power 5, c_J = (0.264, 0.542, 0, 0.542); the 3D slice fits in |p| < 1.4. Locations are rays
// found to hit it with a random ray probe (rays from a sphere of radius 3 towards points near the centre) that also
// hit the sets of powers 2 .. 25 (at power 2 the set is a dust of shells: c_J's complex shadow is not in M_2), plus
// two rays along the real axis (orbits starting next to it; for non-integer powers the negative real axis is the
// singular line of q^p).
inline FFormulaLab Quaternion()
{
	FFormulaLab L;
	L.Power = 5.0;
	L.Locations = {
		{"top", {-1.470, -0.150, 2.611}, {0.469, -0.030, -0.883}, false},
		{"bottom", {1.292, -1.110, -2.469}, {-0.488, 0.308, 0.817}, false},
		{"-y side", {0.877, -2.799, -0.630}, {-0.216, 0.937, 0.274}, false},
		{"upper back", {-2.478, -0.806, 1.486}, {0.825, 0.102, -0.556}, false},
		{"lower front", {-1.725, -2.346, 0.724}, {0.601, 0.786, -0.145}, false},
		{"+x real axis", {3.0, 1.0e-9, 2.0e-9}, {-1.0, 0.0, 0.0}, false},
		{"-x real axis", {-3.0, 2.0e-9, -1.0e-9}, {1.0, 0.0, 0.0}, false},
	};
	L.Scenes = {
		{"z1e-1", {-1.470, -0.150, 2.611}, {0.469, -0.030, -0.883}, 1e-1, 20.0, 8, 4, 10},
		{"z1e-5", {1.292, -1.110, -2.469}, {-0.488, 0.308, 0.817}, 1e-5, 3.0, 10, 8, -15},
		{"z1e-10", {0.877, -2.799, -0.630}, {-0.216, 0.937, 0.274}, 1e-10, 3.0, -12, 6, 40},
		{"z1e-15", {-2.478, -0.806, 1.486}, {0.825, 0.102, -0.556}, 1e-15, 3.0, 15, -5, 0},
		{"z1e-20", {-1.725, -2.346, 0.724}, {0.601, 0.786, -0.145}, 1e-20, 3.0, -8, 10, 70},
		{"z1e-25", {3.0, 1.0e-9, 2.0e-9}, {-1.0, 0.0, 0.0}, 1e-25, 3.0, 6, -9, -30},
	};
	return L;
}
