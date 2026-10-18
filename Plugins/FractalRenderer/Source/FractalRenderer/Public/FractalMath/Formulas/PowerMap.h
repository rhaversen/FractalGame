// The Mandelbulb power map g_p shared by the power-map formulas (Mandelbulb, Burning Ship, Julia Set):
// evaluation in any precision, the GPU reference block consumed by Shaders/FractalPowerMap.ush, and the
// series-skip helpers.
//
// Conventions (identical to the shaders):
//   r = |z|,  theta = atan2(sqrt(x^2+y^2), z) in [0, pi]  (angle from +Z),  phi = atan2(y, x)  (0 on the axis)
//   g_p(z) = r^p (sin(p theta) cos(p phi), sin(p theta) sin(p phi), cos(p theta))

#pragma once

#include <limits>

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
 * Jacobian of g_p at Z (double). With the unit vectors e_r, e_theta, e_phi at Z and e'_r, e'_theta, e'_phi at the multiplied
 * angles (p theta, p phi):
 *     J = p r^(p-1) [ e'_r e_r^T + e'_theta e_theta^T + (sin(p theta) / sin(theta)) e'_phi e_phi^T ]
 * The map stretches the azimuth by sin(p theta) / sin(theta) less (or more) than the other two directions. The scalar
 * running derivative dr' = p r^(p-1) dr ignores that, and over the hundreds of iterations of an orbit next to the surface
 * its error compounds: the classic estimate comes out 1e2-1e8 times too small (or, near the polar axis, too large) at depth.
 * Returns false at the origin.
 */
inline bool PowerMapJacobian(const TFractalVec3<double>& Z, double Power, double OutJ[3][3])
{
	const double Rho = std::sqrt(Z.X * Z.X + Z.Y * Z.Y);
	const double R = std::sqrt(Rho * Rho + Z.Z * Z.Z);
	if (!(R > 0.0))
	{
		return false;
	}
	const double CT = Z.Z / R, ST = Rho / R;
	const double CP = Rho > 0.0 ? Z.X / Rho : 1.0, SP = Rho > 0.0 ? Z.Y / Rho : 0.0;
	double CPT, SPT, CPP, SPP;
	if (IsIntegerPower(Power))
	{
		// (cos theta + i sin theta)^p and (cos phi + i sin phi)^p
		double ARe = CT, AIm = ST, BRe = CP, BIm = SP, TRe = 1.0, TIm = 0.0, QRe = 1.0, QIm = 0.0;
		for (int E = static_cast<int>(Power); E > 0; E >>= 1)
		{
			if (E & 1)
			{
				const double NT = TRe * ARe - TIm * AIm; TIm = TRe * AIm + TIm * ARe; TRe = NT;
				const double NQ = QRe * BRe - QIm * BIm; QIm = QRe * BIm + QIm * BRe; QRe = NQ;
			}
			const double NA = ARe * ARe - AIm * AIm; AIm = 2.0 * ARe * AIm; ARe = NA;
			const double NB = BRe * BRe - BIm * BIm; BIm = 2.0 * BRe * BIm; BRe = NB;
		}
		CPT = TRe; SPT = TIm; CPP = QRe; SPP = QIm;
	}
	else
	{
		const double Theta = std::atan2(Rho, Z.Z), Phi = Rho > 0.0 ? std::atan2(Z.Y, Z.X) : 0.0;
		CPT = std::cos(Power * Theta); SPT = std::sin(Power * Theta);
		CPP = std::cos(Power * Phi); SPP = std::sin(Power * Phi);
	}
	const double K = Power * std::pow(R, Power - 1.0);
	const double Az = ST > 1.0e-300 ? SPT / ST : Power * (CT >= 0.0 ? 1.0 : -1.0);
	const double Er[3] = {ST * CP, ST * SP, CT}, Et[3] = {CT * CP, CT * SP, -ST}, Ep[3] = {-SP, CP, 0.0};
	const double Er2[3] = {SPT * CPP, SPT * SPP, CPT}, Et2[3] = {CPT * CPP, CPT * SPP, -SPT}, Ep2[3] = {-SPP, CPP, 0.0};
	for (int I = 0; I < 3; ++I)
	{
		for (int J = 0; J < 3; ++J)
		{
			OutJ[I][J] = K * (Er2[I] * Er[J] + Et2[I] * Et[J] + Az * Ep2[I] * Ep[J]);
		}
	}
	return true;
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

/**
 * Radius of a sphere around the origin that contains the whole set of z -> |z|^p + c, where |c| <= CMagnitude (the
 * point itself for Mandelbrot-type maps, the constant for Julia maps; Burning Ship and the quaternion map obey the
 * same bound). Beyond r = max(1, (1 + CMagnitude)^(1/(p-1)))  (and r >= |c| for the Mandelbrot type, hence the 2)
 * |z_{n+1}| > |z_n|, so the orbit escapes. No bound (infinity) for p close to 1.
 */
inline double PowerMapBoundingRadius(double Power, double CMagnitude, bool bAddsPosition)
{
	if (Power < 1.05)
	{
		return std::numeric_limits<double>::infinity();
	}
	const double Growth = bAddsPosition ? 2.0 : 1.0 + CMagnitude;
	const double R = std::pow(Growth, 1.0 / (Power - 1.0));
	return R > 1.0 ? R : 1.0;
}

} // namespace FractalMath

#if defined(_MSC_VER) || defined(__clang__)
#pragma float_control(pop)
#endif
