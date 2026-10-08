// CPU side of the Mandelbulb perturbation renderer.
//
// The GPU never sees absolute fractal coordinates. Every sample point is written as
//     c = C_ref + dc,        z_n = Z_n + d_n
// where C_ref / Z_n (the reference point and its orbit) are computed here in double-double precision
// and dc / d_n (per-pixel offsets) are tracked in float on the GPU (FractalPerturbation.ush).
//
// This header contains:
//   - the trigonometric Mandelbulb power map g_p for double and double-double (templated),
//   - reference orbit generation and packing into the GPU layout (FOrbitPointGPU),
//   - a high-precision distance estimator and the reference-ray march that picks C_ref,
// and depends only on <cmath>, so it is shared verbatim by the Unreal plugin and Tools/PerturbationLab.
//
// Conventions (identical to the shader and to the previous FractalRenderer code):
//   r = |z|,  theta = atan2(sqrt(x^2+y^2), z) in [0, pi]  (angle from +Z),  phi = atan2(y, x)
//   g_p(z) = r^p (sin(p theta) cos(p phi), sin(p theta) sin(p phi), cos(p theta))
//   z_0 = 0, z_{n+1} = g_p(z_n) + c, escape when |z_n| > Bailout, DE = 0.5 r ln(r) / |dz/dc|
//   with the scalar running derivative dr_{n+1} = p r_n^(p-1) dr_n + 1, dr_1 = 1.

#pragma once

#include "DoubleDouble.h"

#include <cmath>
#include <cstdint>

#if defined(_MSC_VER) || defined(__clang__)
#pragma float_control(precise, on, push)
#endif

namespace FractalMath
{

// ---------------------------------------------------------------------------------------------
// Scalar abstraction so the same code runs in double and double-double
// ---------------------------------------------------------------------------------------------

inline double RToDouble(double A) { return A; }
inline double RToDouble(const FDD& A) { return A.ToDouble(); }
inline double RSqrt(double A) { return A > 0.0 ? std::sqrt(A) : 0.0; }
inline FDD RSqrt(const FDD& A) { return DDSqrt(A); }
inline double RAtan2(double Y, double X) { return std::atan2(Y, X); }
inline FDD RAtan2(const FDD& Y, const FDD& X) { return DDAtan2(Y, X); }
inline void RSinCos(double A, double& OutS, double& OutC) { OutS = std::sin(A); OutC = std::cos(A); }
inline void RSinCos(const FDD& A, FDD& OutS, FDD& OutC) { DDSinCos(A, OutS, OutC); }
inline double RPow(double A, double P) { return A > 0.0 ? std::pow(A, P) : 0.0; }
inline FDD RPow(const FDD& A, double P) { return DDPow(A, P); }
inline double RLog(double A) { return std::log(A); }
inline FDD RLog(const FDD& A) { return DDLog(A); }

template <typename T>
inline T RPowInt(const T& A, int N)
{
	T Result(1.0);
	T Base = A;
	int E = N;
	while (E > 0)
	{
		if (E & 1)
		{
			Result = Result * Base;
		}
		E >>= 1;
		if (E > 0)
		{
			Base = Base * Base;
		}
	}
	return Result;
}

template <typename T>
struct TFractalVec3
{
	T X;
	T Y;
	T Z;

	TFractalVec3() : X(0.0), Y(0.0), Z(0.0) {}
	TFractalVec3(const T& InX, const T& InY, const T& InZ) : X(InX), Y(InY), Z(InZ) {}
};

using FDDVec3 = TFractalVec3<FDD>;
using FDVec3 = TFractalVec3<double>;

template <typename T>
inline TFractalVec3<T> operator+(const TFractalVec3<T>& A, const TFractalVec3<T>& B) { return TFractalVec3<T>(A.X + B.X, A.Y + B.Y, A.Z + B.Z); }
template <typename T>
inline TFractalVec3<T> operator-(const TFractalVec3<T>& A, const TFractalVec3<T>& B) { return TFractalVec3<T>(A.X - B.X, A.Y - B.Y, A.Z - B.Z); }
template <typename T>
inline T Dot(const TFractalVec3<T>& A, const TFractalVec3<T>& B) { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }

inline FDVec3 ToDoubleVec(const FDDVec3& A) { return FDVec3(A.X.ToDouble(), A.Y.ToDouble(), A.Z.ToDouble()); }
inline FDVec3 ToDoubleVec(const FDVec3& A) { return A; }
inline FDDVec3 ToDDVec(const FDVec3& A) { return FDDVec3(FDD(A.X), FDD(A.Y), FDD(A.Z)); }
inline FDDVec3 ToDDVec(const FDDVec3& A) { return A; }

/** Base + Dir * Distance with the product rounded to double-double (Dir and Distance are doubles). */
inline FDDVec3 OffsetDD(const FDDVec3& Base, const double Dir[3], double Distance)
{
	return FDDVec3(Base.X + DDTwoProd(Dir[0], Distance), Base.Y + DDTwoProd(Dir[1], Distance), Base.Z + DDTwoProd(Dir[2], Distance));
}
inline FDVec3 OffsetDD(const FDVec3& Base, const double Dir[3], double Distance)
{
	return FDVec3(Base.X + Dir[0] * Distance, Base.Y + Dir[1] * Distance, Base.Z + Dir[2] * Distance);
}

/** Integer powers 2..64 use the exact trig-free complex formulation (faster and more accurate in dd). */
inline bool IsIntegerPower(double Power)
{
	return Power >= 2.0 && Power <= 64.0 && Power == std::floor(Power);
}

// ---------------------------------------------------------------------------------------------
// Mandelbulb power map
// ---------------------------------------------------------------------------------------------

/**
 * g_p(W). For integer p this uses w = z + i*rho, u = x + i*y:
 *   r^p e^{i p theta} = r^p (w/r)^p,  e^{i p phi} = (u/rho)^p
 * which equals the trigonometric definition exactly (theta = arg(w) in [0, pi], phi = arg(u)).
 */
template <typename T>
inline TFractalVec3<T> MandelbulbPower(const TFractalVec3<T>& W, double Power)
{
	const T Rho2 = W.X * W.X + W.Y * W.Y;
	const T R2 = Rho2 + W.Z * W.Z;
	if (RToDouble(R2) <= 0.0)
	{
		return TFractalVec3<T>();
	}
	const T Rho = RSqrt(Rho2);
	const T R = RSqrt(R2);

	T SinPT, CosPT, SinPP, CosPP, RPowP;
	if (IsIntegerPower(Power))
	{
		const int N = static_cast<int>(Power);
		// (cos theta + i sin theta)^N
		T CRe = W.Z / R;
		T CIm = Rho / R;
		T PRe(1.0), PIm(0.0);
		{
			T BRe = CRe, BIm = CIm;
			int E = N;
			while (E > 0)
			{
				if (E & 1)
				{
					const T NewRe = PRe * BRe - PIm * BIm;
					PIm = PRe * BIm + PIm * BRe;
					PRe = NewRe;
				}
				E >>= 1;
				if (E > 0)
				{
					const T NewBRe = BRe * BRe - BIm * BIm;
					BIm = (BRe * BIm) * 2.0;
					BRe = NewBRe;
				}
			}
		}
		CosPT = PRe;
		SinPT = PIm;

		// (cos phi + i sin phi)^N, phi := 0 on the polar axis
		T URe(1.0), UIm(0.0);
		if (RToDouble(Rho) > 0.0)
		{
			URe = W.X / Rho;
			UIm = W.Y / Rho;
		}
		T QRe(1.0), QIm(0.0);
		{
			T BRe = URe, BIm = UIm;
			int E = N;
			while (E > 0)
			{
				if (E & 1)
				{
					const T NewRe = QRe * BRe - QIm * BIm;
					QIm = QRe * BIm + QIm * BRe;
					QRe = NewRe;
				}
				E >>= 1;
				if (E > 0)
				{
					const T NewBRe = BRe * BRe - BIm * BIm;
					BIm = (BRe * BIm) * 2.0;
					BRe = NewBRe;
				}
			}
		}
		CosPP = QRe;
		SinPP = QIm;
		RPowP = RPowInt(R, N);
	}
	else
	{
		const T Theta = RAtan2(Rho, W.Z);
		const T Phi = RToDouble(Rho) > 0.0 ? RAtan2(W.Y, W.X) : T(0.0);
		RSinCos(Theta * Power, SinPT, CosPT);
		RSinCos(Phi * Power, SinPP, CosPP);
		RPowP = RPow(R, Power);
	}

	const T Radial = RPowP * SinPT;
	return TFractalVec3<T>(Radial * CosPP, Radial * SinPP, RPowP * CosPT);
}

// ---------------------------------------------------------------------------------------------
// Reference orbit
// ---------------------------------------------------------------------------------------------

/**
 * One reference orbit point as consumed by FractalPerturbation.ush: 6 x float4 = 96 bytes (reference + series skip).
 * All derived quantities are computed (in double) from the float-rounded position so that the GPU sees a
 * self-consistent reference Z~ = (X, Y, Z). They only act as coefficients of small terms, so float
 * precision is sufficient; the orbit recurrence itself is carried out in double-double.
 */
struct FOrbitPointGPU
{
	float X, Y, Z, R;                          // Z~_m and |Z~_m|
	float Rho, RPow, Phi, InvR;                // sqrt(X^2+Y^2), |Z~_m|^p, atan2(Y, X) (0 on the axis), 1/|Z~_m|
	float SinPTheta, CosPTheta, SinPPhi, CosPPhi; // sin/cos(p*theta), sin/cos(p*phi)
	// Linear series skip (see ComputeSeriesStep): d_m ~= A_m * dc for |dc| <= SkipRadius.
	float A00, A01, A02, SkipRadius;           // A_m row 0, largest |dc| for which iterations 1..m-1 are linear
	float A10, A11, A12, SkipDerivative;       // A_m row 1, reference running derivative dr_m
	float A20, A21, A22, Reserved;             // A_m row 2
};
static_assert(sizeof(FOrbitPointGPU) == 96, "FOrbitPointGPU must match the shader layout (6 x float4)");

inline float ClampToFloatRange(double V)
{
	constexpr double Limit = 1.0e30;
	return static_cast<float>(V > Limit ? Limit : (V < -Limit ? -Limit : V));
}

template <typename T>
inline FOrbitPointGPU PackOrbitPoint(const TFractalVec3<T>& Z, double Power)
{
	FOrbitPointGPU P;
	P.X = ClampToFloatRange(RToDouble(Z.X));
	P.Y = ClampToFloatRange(RToDouble(Z.Y));
	P.Z = ClampToFloatRange(RToDouble(Z.Z));

	const double X = P.X, Y = P.Y, ZZ = P.Z;
	const double Rho = std::sqrt(X * X + Y * Y);
	const double R = std::sqrt(X * X + Y * Y + ZZ * ZZ);
	const double Theta = std::atan2(Rho, ZZ);
	const double Phi = Rho > 0.0 ? std::atan2(Y, X) : 0.0;

	P.R = ClampToFloatRange(R);
	P.Rho = ClampToFloatRange(Rho);
	P.RPow = ClampToFloatRange(R > 0.0 ? std::pow(R, Power) : 0.0);
	P.Phi = static_cast<float>(Phi);
	P.InvR = R > 0.0 ? ClampToFloatRange(1.0 / R) : 0.0f;

	double SinPT, CosPT, SinPP, CosPP;
	if (IsIntegerPower(Power) && R > 0.0)
	{
		// sin(p theta) must keep *relative* accuracy near the polar axis (it multiplies an O(1) azimuth
		// difference there) and be exactly 0 on it. std::sin(8 * pi) returns ~1e-15, the complex power
		// (cos theta + i sin theta)^p does not.
		const int N = static_cast<int>(Power);
		double ARe = ZZ / R, AIm = Rho / R;
		double BRe = Rho > 0.0 ? X / Rho : 1.0, BIm = Rho > 0.0 ? Y / Rho : 0.0;
		double TRe = 1.0, TIm = 0.0, PRe = 1.0, PIm = 0.0;
		for (int E = N; E > 0; E >>= 1)
		{
			if (E & 1)
			{
				const double NT = TRe * ARe - TIm * AIm;
				TIm = TRe * AIm + TIm * ARe;
				TRe = NT;
				const double NP = PRe * BRe - PIm * BIm;
				PIm = PRe * BIm + PIm * BRe;
				PRe = NP;
			}
			const double NA = ARe * ARe - AIm * AIm;
			AIm = 2.0 * ARe * AIm;
			ARe = NA;
			const double NB = BRe * BRe - BIm * BIm;
			BIm = 2.0 * BRe * BIm;
			BRe = NB;
		}
		CosPT = TRe;
		SinPT = TIm;
		CosPP = PRe;
		SinPP = PIm;
	}
	else
	{
		SinPT = std::sin(Power * Theta);
		CosPT = std::cos(Power * Theta);
		SinPP = std::sin(Power * Phi);
		CosPP = std::cos(Power * Phi);
	}
	P.SinPTheta = static_cast<float>(SinPT);
	P.CosPTheta = static_cast<float>(CosPT);
	P.SinPPhi = static_cast<float>(SinPP);
	P.CosPPhi = static_cast<float>(CosPP);
	P.A00 = P.A01 = P.A02 = P.A10 = P.A11 = P.A12 = P.A20 = P.A21 = P.A22 = 0.0f;
	P.SkipRadius = 0.0f;
	P.SkipDerivative = 0.0f;
	P.Reserved = 0.0f;
	return P;
}

/**
 * Linear series skip. While |d_n| << |Z_n| the perturbation is linear, d_{n+1} = J_n d_n + dc with J_n the
 * Jacobian of g_p at Z_n, so d_n = A_n dc with A_1 = I, A_{n+1} = J_n A_n + I. A sample whose |dc| is below the
 * validity radius of A_m can start iterating at m instead of 1 (the iterations that grow with zoom depth).
 *
 * Validity: one linear step is accurate to relative error ~ (p-1)/2 |d| / |Z| (Hessian ~ p(p-1) R^(p-2)), and
 * next to the polar axis the map is not smooth (its Hessian grows like 1/rho), so we require
 *     |d_n| <= Tolerance * min(|Z_n|, rho_n)   for n = 1 .. m-1,   with |d_n| <= ||A_n||_F |dc|.
 * The skip also stops once the reference leaves the pixel bailout (pixels near it escape there too).
 */
struct FSeriesState
{
	double A[3][3] = {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};
	double Derivative = 1.0;  // dr_m of the reference (dr_1 = 1)
	double Radius = 1.0e30;   // skipping to m = 1 is always valid
	bool bStopped = false;
};

inline void StoreSeriesState(const FSeriesState& S, FOrbitPointGPU& P)
{
	P.A00 = ClampToFloatRange(S.A[0][0]); P.A01 = ClampToFloatRange(S.A[0][1]); P.A02 = ClampToFloatRange(S.A[0][2]);
	P.A10 = ClampToFloatRange(S.A[1][0]); P.A11 = ClampToFloatRange(S.A[1][1]); P.A12 = ClampToFloatRange(S.A[1][2]);
	P.A20 = ClampToFloatRange(S.A[2][0]); P.A21 = ClampToFloatRange(S.A[2][1]); P.A22 = ClampToFloatRange(S.A[2][2]);
	P.SkipRadius = S.bStopped ? 0.0f : ClampToFloatRange(S.Radius);
	P.SkipDerivative = ClampToFloatRange(S.Derivative);
}

/** Advances the series from point m (reference value Z) to m + 1. */
inline void ComputeSeriesStep(FSeriesState& S, const FDVec3& Z, double Power, double PixelBailout, double Tolerance)
{
	if (S.bStopped)
	{
		return;
	}
	const double R = std::sqrt(Z.X * Z.X + Z.Y * Z.Y + Z.Z * Z.Z);
	const double Rho = std::sqrt(Z.X * Z.X + Z.Y * Z.Y);
	if (!(Tolerance > 0.0) || R > PixelBailout || !(Rho > 0.0))
	{
		S.bStopped = true; // on the polar axis g_p is not differentiable
		return;
	}
	double NormA2 = 0.0;
	for (int I = 0; I < 3; ++I)
	{
		for (int J = 0; J < 3; ++J)
		{
			NormA2 += S.A[I][J] * S.A[I][J];
		}
	}
	const double Limit = Tolerance * (R < Rho ? R : Rho) / std::sqrt(NormA2);
	S.Radius = Limit < S.Radius ? Limit : S.Radius;

	// Jacobian of g_p at Z by central differences (h << rho keeps both samples on the same side of the axis).
	const double H = 1.0e-6 * (R < Rho ? R : Rho);
	double Jac[3][3];
	for (int Col = 0; Col < 3; ++Col)
	{
		FDVec3 Plus = Z, Minus = Z;
		(Col == 0 ? Plus.X : (Col == 1 ? Plus.Y : Plus.Z)) += H;
		(Col == 0 ? Minus.X : (Col == 1 ? Minus.Y : Minus.Z)) -= H;
		const FDVec3 GP = MandelbulbPower(Plus, Power);
		const FDVec3 GM = MandelbulbPower(Minus, Power);
		Jac[0][Col] = (GP.X - GM.X) / (2.0 * H);
		Jac[1][Col] = (GP.Y - GM.Y) / (2.0 * H);
		Jac[2][Col] = (GP.Z - GM.Z) / (2.0 * H);
	}
	double NewA[3][3];
	for (int I = 0; I < 3; ++I)
	{
		for (int J = 0; J < 3; ++J)
		{
			NewA[I][J] = Jac[I][0] * S.A[0][J] + Jac[I][1] * S.A[1][J] + Jac[I][2] * S.A[2][J] + (I == J ? 1.0 : 0.0);
		}
	}
	for (int I = 0; I < 3; ++I)
	{
		for (int J = 0; J < 3; ++J)
		{
			S.A[I][J] = NewA[I][J];
		}
	}
	S.Derivative = Power * std::pow(R, Power - 1.0) * S.Derivative + 1.0;
}

/** Default tolerance of the linear series skip (validated in Tools/PerturbationLab). */
constexpr double DefaultSeriesTolerance = 1.0e-8;

/**
 * Escape radius used for the reference orbit. It must exceed 2x the pixel bailout so that whenever the
 * reference runs out before a pixel escapes, the forced rebase satisfies |z| < |d| (no precision loss).
 * Bounded so that |Z|^p of the last point stays inside float range.
 */
inline double ReferenceEscapeRadius(double PixelBailout, double Power)
{
	const double Wanted = 4.0 * PixelBailout;
	const double FloatSafe = std::pow(10.0, 28.0 / Power);
	return Wanted < FloatSafe ? Wanted : (FloatSafe > 2.0 * PixelBailout ? FloatSafe : 2.0 * PixelBailout);
}

/**
 * Computes Z_0 = 0, Z_{m+1} = g_p(Z_m) + C until |Z_m| > EscapeRadius (that point is kept) or
 * MaxIterations + 1 points exist. Writes GPU-packed points to OutPoints (capacity MaxIterations + 1)
 * and optionally the full precision orbit to OutOrbit. Returns the number of points.
 */
template <typename T>
inline int GenerateReferenceOrbit(const TFractalVec3<T>& C, double Power, int MaxIterations, double EscapeRadius,
	FOrbitPointGPU* OutPoints, TFractalVec3<T>* OutOrbit = nullptr,
	double PixelBailout = 0.0, double SeriesTolerance = DefaultSeriesTolerance)
{
	TFractalVec3<T> Z;
	int Count = 0;
	const double Escape2 = EscapeRadius * EscapeRadius;
	FSeriesState Series;
	Series.bStopped = !(PixelBailout > 0.0) || !(SeriesTolerance > 0.0);
	for (int M = 0; M <= MaxIterations; ++M)
	{
		if (OutPoints)
		{
			OutPoints[Count] = PackOrbitPoint(Z, Power);
			if (M >= 1)
			{
				StoreSeriesState(Series, OutPoints[Count]);
				ComputeSeriesStep(Series, ToDoubleVec(Z), Power, PixelBailout, SeriesTolerance);
			}
		}
		if (OutOrbit)
		{
			OutOrbit[Count] = Z;
		}
		++Count;
		if (RToDouble(Dot(Z, Z)) > Escape2 || M == MaxIterations)
		{
			break;
		}
		Z = MandelbulbPower(Z, Power) + C;
	}
	return Count;
}

// ---------------------------------------------------------------------------------------------
// High-precision distance estimator and reference ray march
// ---------------------------------------------------------------------------------------------

struct FDistanceEstimate
{
	double Distance = 0.0;   // in fractal units; 0 when the orbit did not escape
	int Iterations = 0;      // n at which |z_n| > Bailout (or MaxIterations)
	bool bEscaped = false;
};

/** Direct (non-perturbed) DE with the orbit in T precision and the derivative in double. */
template <typename T>
inline FDistanceEstimate DistanceEstimate(const TFractalVec3<T>& C, double Power, int MaxIterations, double Bailout)
{
	FDistanceEstimate Result;
	TFractalVec3<T> Z = C; // z_1 = c
	double DR = 1.0;
	double R = 0.0;
	int N = 1;
	for (; N < MaxIterations; ++N)
	{
		const FDVec3 ZD = ToDoubleVec(Z);
		R = std::sqrt(ZD.X * ZD.X + ZD.Y * ZD.Y + ZD.Z * ZD.Z);
		if (R > Bailout)
		{
			Result.bEscaped = true;
			break;
		}
		DR = Power * (R > 0.0 ? std::pow(R, Power - 1.0) : 0.0) * DR + 1.0;
		Z = MandelbulbPower(Z, Power) + C;
	}
	Result.Iterations = N;
	Result.Distance = Result.bEscaped ? 0.5 * R * std::log(R) / DR : 0.0;
	return Result;
}

struct FReferenceMarchSettings
{
	double Power = 8.0;
	int MaxIterations = 150;
	double Bailout = 10.0;
	int MaxSteps = 300;
	double MaxDistance = 1.0e6;          // world units
	double PixelRadiusPerUnitDistance = 0.001; // hit threshold = t * this (world units)
	int InsideSearchSteps = 8;           // after a hit, probe this many points further in for a non-escaping one
};

struct FReferenceMarchResult
{
	bool bHit = false;
	double HitDistance = 0.0;       // world units along the ray
	double ReferenceDistance = 0.0; // world units along the ray where C_ref was placed
	bool bReferenceInside = false;  // C_ref does not escape within MaxIterations
	double CameraDistanceEstimate = 0.0; // DE at the ray origin, world units
	int Steps = 0;
};

/**
 * Marches the reference ray Origin + t * Dir (t in world units, fractal = Origin + Dir * t * Scale)
 * in T precision and returns where to put the reference point:
 *  - on a hit, a point just past the surface that does not escape (long orbit), else the hit point;
 *  - on a miss, the point of closest approach (smallest DE / t ratio).
 */
template <typename T>
inline FReferenceMarchResult MarchReferenceRay(const TFractalVec3<T>& Origin, const double Dir[3], double Scale,
	const FReferenceMarchSettings& Settings)
{
	FReferenceMarchResult Result;
	double Dist = 0.0;
	double BestRatio = 1.0e300;
	double BestDist = 0.0;
	for (int Step = 0; Step < Settings.MaxSteps && Dist < Settings.MaxDistance; ++Step)
	{
		const TFractalVec3<T> P = OffsetDD(Origin, Dir, Dist * Scale);
		const FDistanceEstimate DE = DistanceEstimate(P, Settings.Power, Settings.MaxIterations, Settings.Bailout);
		const double DEWorld = DE.Distance / Scale;
		if (Step == 0)
		{
			Result.CameraDistanceEstimate = DEWorld;
		}
		Result.Steps = Step + 1;
		const double PixelRadius = Dist * Settings.PixelRadiusPerUnitDistance;
		if (!DE.bEscaped || DEWorld <= PixelRadius)
		{
			Result.bHit = true;
			Result.HitDistance = Dist;
			break;
		}
		const double Ratio = DEWorld / (Dist > 0.0 ? Dist : 1.0e-300);
		if (Step > 0 && Ratio < BestRatio)
		{
			BestRatio = Ratio;
			BestDist = Dist;
		}
		Dist += DEWorld > 0.5 * PixelRadius ? DEWorld : 0.5 * PixelRadius;
	}

	if (!Result.bHit)
	{
		Result.ReferenceDistance = BestDist;
		return Result;
	}

	Result.ReferenceDistance = Result.HitDistance;
	const double Probe = (Result.HitDistance > 0.0 ? Result.HitDistance : 1.0) * Settings.PixelRadiusPerUnitDistance;
	for (int Step = 0; Step <= Settings.InsideSearchSteps; ++Step)
	{
		const double Dist2 = Result.HitDistance + Probe * static_cast<double>(Step);
		const TFractalVec3<T> P = OffsetDD(Origin, Dir, Dist2 * Scale);
		const FDistanceEstimate DE = DistanceEstimate(P, Settings.Power, Settings.MaxIterations, Settings.Bailout);
		if (!DE.bEscaped)
		{
			Result.ReferenceDistance = Dist2;
			Result.bReferenceInside = true;
			break;
		}
	}
	return Result;
}

} // namespace FractalMath

#if defined(_MSC_VER) || defined(__clang__)
#pragma float_control(pop)
#endif
