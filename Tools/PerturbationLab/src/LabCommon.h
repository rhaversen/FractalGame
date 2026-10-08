// Shared helpers for lab programs.
#pragma once
#include "QuadUtil.h"
#include "VkCompute.h"
#include "FractalMath/FractalFormulas.h"
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <cstring>

struct FLabParams
{
	float Power, Scale, Bailout, Unused1;
	int Count, OrbitLength, MaxIterations, Unused2;
	float RefX, RefY, RefZ, Unused0;
	float Pad[4];
};
static_assert(sizeof(FLabParams) == 64, "cbuffer layout");

inline std::string LabSpv(const std::string& Entry) { return std::string(LAB_OUT_DIR) + "/" + Entry + ".spv"; }

struct FStats
{
	std::vector<double> V;
	void Add(double X) { V.push_back(X); }
	double Pct(double P)
	{
		if (V.empty()) return 0;
		std::sort(V.begin(), V.end());
		size_t I = (size_t)std::min<double>((double)V.size() - 1, std::floor(P * (double)(V.size() - 1) + 0.5));
		return V[I];
	}
	double Max() { return V.empty() ? 0 : *std::max_element(V.begin(), V.end()); }
	size_t N() const { return V.size(); }
};

/** Quad-precision reference of the exact difference g(Z+D) - g(Z) for float inputs. */
inline QVec3 QExactDelta(const float Zf[3], const float Df[3], quad P)
{
	QVec3 Z{(quad)Zf[0], (quad)Zf[1], (quad)Zf[2]};
	QVec3 W{(quad)Zf[0] + (quad)Df[0], (quad)Zf[1] + (quad)Df[1], (quad)Zf[2] + (quad)Df[2]};
	// Note: W is formed in quad from the float values, i.e. exactly what Z~ + D means mathematically.
	return QMandelbulbPow(W, P) - QMandelbulbPow(Z, P);
}
