// The Mandelbulb power map g_p shared by the power-map formulas (Mandelbulb, Burning Ship, Julia Set):
// evaluation in any precision, the GPU reference block consumed by Shaders/FractalPowerMap.ush, and the
// series-skip helpers.
//
// Conventions (identical to the shaders):
//   r = |z|,  theta = atan2(sqrt(x^2+y^2), z) in [0, pi]  (angle from +Z),  phi = atan2(y, x)  (0 on the axis)
//   g_p(z) = r^p (sin(p theta) cos(p phi), sin(p theta) sin(p phi), cos(p theta))

#pragma once

#include "../FractalScalar.h"
#include "../FractalFormulaTypes.h"

#if defined(_MSC_VER) || defined(__clang__)
#pragma float_control(precise, on, push)
#endif

namespace FractalMath
{

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
		// (cos theta + i sin theta)^N and (cos phi + i sin phi)^N, phi := 0 on the polar axis
		T ARe = W.Z / R, AIm = Rho / R;
		T BRe(1.0), BIm(0.0);
		if (RToDouble(Rho) > 0.0)
		{
			BRe = W.X / Rho;
			BIm = W.Y / Rho;
		}
		T TRe(1.0), TIm(0.0), PRe(1.0), PIm(0.0);
		for (int E = N; E > 0; E >>= 1)
		{
			if (E & 1)
			{
				const T NT = TRe * ARe - TIm * AIm;
				TIm = TRe * AIm + TIm * ARe;
				TRe = NT;
				const T NP = PRe * BRe - PIm * BIm;
				PIm = PRe * BIm + PIm * BRe;
				PRe = NP;
			}
			if (E > 1)
			{
				const T NA = ARe * ARe - AIm * AIm;
				AIm = (ARe * AIm) * 2.0;
				ARe = NA;
				const T NB = BRe * BRe - BIm * BIm;
				BIm = (BRe * BIm) * 2.0;
				BRe = NB;
			}
		}
		CosPT = TRe;
		SinPT = TIm;
		CosPP = PRe;
		SinPP = PIm;
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

/**
 * GPU reference block for g_p at Z (3 float4 = 12 floats, decoded by FP_DecodePowerRef):
 *   X, Y, Z, R | Rho, RPow, Phi, InvR | SinPTheta, CosPTheta, SinPPhi, CosPPhi
 * All derived quantities are computed (in double) from the float-rounded position so that the GPU sees a
 * self-consistent reference Z~ = (X, Y, Z). They only act as coefficients of small terms, so float
 * precision is sufficient; the orbit recurrence itself is carried out in double-double.
 */
template <typename T>
inline void PackPowerRef(const TFractalVec3<T>& Z, double Power, float* Out)
{
	const float FX = ClampToFloatRange(RToDouble(Z.X));
	const float FY = ClampToFloatRange(RToDouble(Z.Y));
	const float FZ = ClampToFloatRange(RToDouble(Z.Z));
	const double X = FX, Y = FY, ZZ = FZ;
	const double Rho = std::sqrt(X * X + Y * Y);
	const double R = std::sqrt(X * X + Y * Y + ZZ * ZZ);
	const double Theta = std::atan2(Rho, ZZ);
	const double Phi = Rho > 0.0 ? std::atan2(Y, X) : 0.0;

	double SinPT, CosPT, SinPP, CosPP;
	if (IsIntegerPower(Power) && R > 0.0)
	{
		// sin(p theta) must keep *relative* accuracy near the polar axis (it multiplies an O(1) azimuth
		// difference there) and be exactly 0 on it. std::sin(8 * pi) returns ~1e-15, the complex power
		// (cos theta + i sin theta)^p does not.
		const int N = static_cast<int>(Power);
		double ARe = ZZ / R, AIm = Rho / R;
		double TRe = 1.0, TIm = 0.0;
		double BRe = Rho > 0.0 ? X / Rho : 1.0, BIm = Rho > 0.0 ? Y / Rho : 0.0;
		double PRe = 1.0, PIm = 0.0;
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
	Out[0] = FX;
	Out[1] = FY;
	Out[2] = FZ;
	Out[3] = ClampToFloatRange(R);
	Out[4] = ClampToFloatRange(Rho);
	Out[5] = ClampToFloatRange(R > 0.0 ? std::pow(R, Power) : 0.0);
	Out[6] = static_cast<float>(Phi);
	Out[7] = R > 0.0 ? ClampToFloatRange(1.0 / R) : 0.0f;
	Out[8] = static_cast<float>(SinPT);
	Out[9] = static_cast<float>(CosPT);
	Out[10] = static_cast<float>(SinPP);
	Out[11] = static_cast<float>(CosPP);
}

/** Running derivative of g_p: dr' = p r^(p-1) dr (formulas that add c add 1). */
inline double PowerMapDerivative(const TFractalVec3<double>& Z, double DR, double Power)
{
	const double R = std::sqrt(Z.X * Z.X + Z.Y * Z.Y + Z.Z * Z.Z);
	return Power * (R > 0.0 ? std::pow(R, Power - 1.0) : 0.0) * DR;
}

/**
 * Series step of g_p at Z: Jacobian by central differences (h << rho keeps both samples on the same side of
 * the polar axis) and the linear validity limit. One linear step is accurate to relative error
 * ~ (p-1)/2 |d| / |Z| (Hessian ~ p(p-1) R^(p-2)), and next to the polar axis the map is not smooth (its Hessian
 * grows like 1/rho), so |d| <= Tolerance * min(|Z|, rho). On the axis itself g_p is not differentiable.
 * Non-integer powers are also discontinuous across the azimuth branch cut (y = 0, x < 0), which the pixel
 * must not cross: there |d| < |y| as well.
 */
inline double PowerMapSeriesStep(const TFractalVec3<double>& Z, double Power, double Tolerance, double OutJ[4][4])
{
	const double R = std::sqrt(Z.X * Z.X + Z.Y * Z.Y + Z.Z * Z.Z);
	const double Rho = std::sqrt(Z.X * Z.X + Z.Y * Z.Y);
	if (!(Rho > 0.0))
	{
		return 0.0;
	}
	const double Smooth = R < Rho ? R : Rho;
	const double ToBranchCut = (!IsIntegerPower(Power) && Z.X < 0.0) ? std::fabs(Z.Y) : 1.0e300;
	if (!(ToBranchCut > 0.0))
	{
		return 0.0;
	}
	const double H = 1.0e-6 * (Smooth < ToBranchCut ? Smooth : ToBranchCut);
	for (int Col = 0; Col < 3; ++Col)
	{
		TFractalVec3<double> Plus = Z, Minus = Z;
		Plus[Col] += H;
		Minus[Col] -= H;
		const TFractalVec3<double> GP = MandelbulbPower(Plus, Power);
		const TFractalVec3<double> GM = MandelbulbPower(Minus, Power);
		for (int Row = 0; Row < 3; ++Row)
		{
			OutJ[Row][Col] = (GP[Row] - GM[Row]) / (2.0 * H);
		}
	}
	const double Linear = Tolerance * Smooth;
	return Linear < ToBranchCut ? Linear : ToBranchCut;
}

/**
 * Escape radius of power-map reference orbits. It must exceed 2x the pixel bailout so that whenever the
 * reference runs out before a pixel escapes, the forced rebase satisfies |z| < |d| (no precision loss).
 * Bounded so that |Z|^p of the last point stays inside float range.
 */
inline double PowerMapReferenceEscapeRadius(double PixelBailout, double Power)
{
	const double Wanted = 4.0 * PixelBailout;
	const double FloatSafe = std::pow(10.0, 28.0 / (Power > 1.0 ? Power : 1.0));
	return Wanted < FloatSafe ? Wanted : (FloatSafe > 2.0 * PixelBailout ? FloatSafe : 2.0 * PixelBailout);
}

} // namespace FractalMath

#if defined(_MSC_VER) || defined(__clang__)
#pragma float_control(pop)
#endif
