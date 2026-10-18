// Quaternion Julia set (GPU: Shaders/FormulaQuaternion.ush, power map in Shaders/FractalQuaternion.ush).
//
// The state q = (X, Y, Z, W) = X + Y i + Z j + W k has its REAL part in X. The visible slice is (real, i, j) with
// k = 0:  z_1 = (position, 0),  z_{n+1} = z_n^p + c_J,  |z| > Bailout tested before each step,
// dr' = p |z|^(p-1) dr (Julia type: dr = |dz_n / dz_1|, no "+1"),  DE = 0.5 r ln r / dr.
//
// Power. Write q = a + v (a real, v imaginary), s = |v|, n = v / s, r = |q|, theta = atan2(s, a) in [0, pi].
// 1 and n span a copy of the complex numbers, so with zeta = a + i s
//     q^p = r^p (cos(p theta) + n sin(p theta)) = Re(zeta^p) + K v,      K = Im(zeta^p) / s.
// For integer p this is the quaternion product q q ... q, and Re(zeta^p), K are polynomials in a and s^2:
//     zeta^m = R_m + i s I_m,   (R, I) (R', I') = (R R' - s^2 I I', R I' + I R'),   zeta = (a, 1)
// (smooth everywhere, no square root or division). Non-integer p use the principal power,
// K = r^(p-1) sin(p theta) / sin(theta). It is continuous except on the negative real axis (s = 0, a < 0), where
// n is undefined and q^p := (r^p cos(p pi), 0, 0, 0).
//
// c_J = (0.264, 0.542, 0, 0.542) was chosen by rendering (Tools/PerturbationLab). The 4D set is invariant under
// rotations of the imaginary space about Im c_J, and its critical orbit 0 -> c_J -> ... stays in the plane of 1 and
// Im c_J, where it is the orbit of c' = Re c_J + i |Im c_J| = 0.264 + 0.7665 i under z^p + c'. For p = 5, c' lies in a
// period-3 component of the Multibrot set (centre 0.2639 + 0.7670 i), so the default power gives a connected set
// whose slice is a cage of interlocked shells with windows; Im c_J is tilted 45 degrees out of the visible slice so
// that no symmetry axis lies in view. Powers 2 .. 8 give related shell cages (for p = 2, 3, 4, 6 c' is outside the
// Multibrot set and the shells break up into dust).
//
// GPU payload (3 float4, decoded by FP_DecodeQuaternionRef), computed in double from the float-rounded reference Q~
// so that the GPU sees self-consistent coefficients:
//   Q~ (real, i, j, k) | R, 1/R, S = |imag(Q~)|, R^p | cos(p theta), sin(p theta), K, 0

#pragma once

#include "PowerMap.h"

#if defined(_MSC_VER) || defined(__clang__)
#pragma float_control(precise, on, push)
#endif

namespace FractalMath
{

namespace QuaternionPowerMap
{
	/** (Re(zeta^N), Im(zeta^N) / s) for zeta = A + i s, given S2 = s^2 and N >= 1 (binary powering on (R, I) pairs). */
	template <typename T>
	inline void ComplexPowParts(const T& A, const T& S2, int N, T& OutRe, T& OutK)
	{
		T BaseRe = A;
		T BaseK(1.0);
		T Re(1.0);
		T K(0.0);
		for (int E = N; E > 0; E >>= 1)
		{
			if (E & 1)
			{
				const T NewRe = Re * BaseRe - S2 * (K * BaseK);
				K = Re * BaseK + K * BaseRe;
				Re = NewRe;
			}
			if (E > 1)
			{
				const T NewRe = BaseRe * BaseRe - S2 * (BaseK * BaseK);
				BaseK = (BaseRe * BaseK) * 2.0;
				BaseRe = NewRe;
			}
		}
		OutRe = Re;
		OutK = K;
	}

	/** q^p (any precision). */
	template <typename T>
	inline TFractalVec4<T> Power(const TFractalVec4<T>& Q, double P)
	{
		const T S2 = Q.Y * Q.Y + Q.Z * Q.Z + Q.W * Q.W;
		T Re, K;
		if (IsIntegerPower(P))
		{
			ComplexPowParts(Q.X, S2, static_cast<int>(P), Re, K);
		}
		else
		{
			const T R2 = Q.X * Q.X + S2;
			if (!(RToDouble(R2) > 0.0))
			{
				return TFractalVec4<T>();
			}
			const T S = RSqrt(S2);
			const T RPowP = RPow(RSqrt(R2), P);
			T SinPT, CosPT;
			RSinCos(RAtan2(S, Q.X) * P, SinPT, CosPT);
			Re = RPowP * CosPT;
			K = RToDouble(S) > 0.0 ? RPowP * SinPT / S : T(0.0);
		}
		return TFractalVec4<T>(Re, K * Q.Y, K * Q.Z, K * Q.W);
	}

	/**
	 * cos(p theta), sin(p theta), r^p and K = Im(zeta^p) / s at (A, S2 = s^2) in double. Integer powers use the
	 * normalised polynomial form, so sin(p theta) and K keep relative accuracy next to the real axis and are exact
	 * on it; for non-integer powers K -> p r^(p-1) on the positive real axis and K := 0 on the negative one.
	 */
	inline void PolarParts(double A, double S2, double P, double& OutCosPT, double& OutSinPT, double& OutRPow, double& OutK)
	{
		const double S = std::sqrt(S2);
		const double R = std::sqrt(A * A + S2);
		OutCosPT = 1.0;
		OutSinPT = 0.0;
		OutRPow = 0.0;
		OutK = 0.0;
		if (!(R > 0.0))
		{
			return;
		}
		OutRPow = std::pow(R, P);
		if (IsIntegerPower(P))
		{
			double Re, KN;
			ComplexPowParts(A / R, S2 / (R * R), static_cast<int>(P), Re, KN);
			OutCosPT = Re;
			OutSinPT = (S / R) * KN;
			OutK = OutRPow / R * KN;
		}
		else
		{
			const double Theta = std::atan2(S, A);
			OutCosPT = std::cos(P * Theta);
			OutSinPT = std::sin(P * Theta);
			OutK = S > 0.0 ? OutRPow * OutSinPT / S : (A > 0.0 ? P * OutRPow / R : 0.0);
		}
	}
} // namespace QuaternionPowerMap

struct FQuaternionFormula
{
	static constexpr EFractalFormula Type = EFractalFormula::Quaternion;
	static constexpr EFractalFamily Family = EFractalFamily::EscapeTime;
	static constexpr bool bAddsC = false;
	static constexpr int StateDim = 4;
	static constexpr int PayloadFloat4 = 3;
	template <typename T>
	using TState = TFractalVec4<T>;

	// c_J = (real, i, j, k), rounded to float so that the CPU and the GPU iterate exactly the same set.
	static constexpr double CX = static_cast<double>(0.264f);
	static constexpr double CY = static_cast<double>(0.542f);
	static constexpr double CZ = static_cast<double>(0.0f);
	static constexpr double CW = static_cast<double>(0.542f);

	template <typename T>
	static TState<T> Start(const TFractalVec3<T>& Position) { return TState<T>(Position.X, Position.Y, Position.Z, T(0.0)); }

	template <typename T>
	static TState<T> Step(const TState<T>& Z, const TFractalVec3<T>& /*C*/, const FFormulaParams& Params)
	{
		return QuaternionPowerMap::Power(Z, Params.Power) + TState<T>(T(CX), T(CY), T(CZ), T(CW));
	}

	static double DerivativeStep(const TState<double>& Z, double DR, const FFormulaParams& Params)
	{
		const double R = std::sqrt(Dot(Z, Z));
		return Params.Power * (R > 0.0 ? std::pow(R, Params.Power - 1.0) : 0.0) * DR;
	}

	/**
	 * Like ClampToFloatRange, but up to 1e37: with powers up to 25 the last orbit point (|Z|^p + c after
	 * |Z| <= ReferenceEscapeRadius = 20) and R^p reach ~3e32, which must not be clipped. NaN (inf * 0 in the
	 * never-used coefficients of that last point) becomes 0.
	 */
	static float ToFloatWide(double V)
	{
		constexpr double Limit = 1.0e37;
		return V == V ? static_cast<float>(V > Limit ? Limit : (V < -Limit ? -Limit : V)) : 0.0f;
	}

	template <typename T>
	static void Pack(const TState<T>& Z, const FFormulaParams& Params, float* Out)
	{
		for (int I = 0; I < 4; ++I)
		{
			Out[I] = ToFloatWide(RToDouble(Z[I]));
		}
		const double A = Out[0];
		const double S2 = static_cast<double>(Out[1]) * Out[1] + static_cast<double>(Out[2]) * Out[2] + static_cast<double>(Out[3]) * Out[3];
		const double R = std::sqrt(A * A + S2);
		double CosPT, SinPT, RPowP, K;
		QuaternionPowerMap::PolarParts(A, S2, Params.Power, CosPT, SinPT, RPowP, K);
		Out[4] = ToFloatWide(R);
		Out[5] = R > 0.0 ? ToFloatWide(1.0 / R) : 0.0f;
		Out[6] = ToFloatWide(std::sqrt(S2));
		Out[7] = ToFloatWide(RPowP);
		Out[8] = static_cast<float>(CosPT);
		Out[9] = static_cast<float>(SinPT);
		Out[10] = ToFloatWide(K);
		Out[11] = 0.0f;
	}

	/**
	 * Analytic Jacobian of q -> q^p: with F(zeta) = zeta^p, F' = p zeta^(p-1),
	 *     J = [[Re F', -Im F' n^T], [Im F' n, K (I - n n^T) + Re F' n n^T]]
	 * (the complex derivative in the (1, n) plane, K across it; J = F'(a) I on the real axis).
	 * Linear validity: (Q + d)^p expands into words of quaternion products with |word| = |Q|^(p-k) |d|^k, so for
	 * integer p the nonlinear remainder is at most (r + |d|)^p - r^p - p r^(p-1) |d| <= (e - 2) p |d| / r * p r^(p-1) |d|
	 * for p |d| <= r: |d| <= Tolerance r / p keeps it below Tolerance ||J|| |d|. Non-integer powers are singular on the
	 * negative real axis (the direction n has curvature 1 / s there), so r is replaced by the distance to it (s for
	 * a < 0) and the bound halved; this also keeps the pixel away from the singular line. 0 on the line itself.
	 */
	static double SeriesStep(const TState<double>& Z, const FFormulaParams& Params, double Tolerance, double OutJ[4][4])
	{
		const double P = Params.Power;
		const double A = Z.X;
		const double S2 = Z.Y * Z.Y + Z.Z * Z.Z + Z.W * Z.W;
		const double S = std::sqrt(S2);
		const double R = std::sqrt(A * A + S2);
		const bool bInteger = IsIntegerPower(P);
		const double Distance = (bInteger || A >= 0.0) ? R : S;
		if (!(Distance > 0.0))
		{
			return 0.0;
		}
		double DerivRe, DerivImOverS, K;
		if (bInteger)
		{
			const int N = static_cast<int>(P);
			double RePM1, KPM1, ReP;
			QuaternionPowerMap::ComplexPowParts(A, S2, N - 1, RePM1, KPM1);
			QuaternionPowerMap::ComplexPowParts(A, S2, N, ReP, K);
			DerivRe = P * RePM1;
			DerivImOverS = P * KPM1;
		}
		else
		{
			const double Theta = std::atan2(S, A);
			const double RPowPM1 = std::pow(R, P - 1.0);
			DerivRe = P * RPowPM1 * std::cos((P - 1.0) * Theta);
			DerivImOverS = S > 0.0 ? P * RPowPM1 * std::sin((P - 1.0) * Theta) / S : 0.0;
			K = S > 0.0 ? RPowPM1 * R * std::sin(P * Theta) / S : P * RPowPM1;
		}
		const double V[3] = {Z.Y, Z.Z, Z.W};
		const double Radial = S2 > 0.0 ? (DerivRe - K) / S2 : 0.0;
		OutJ[0][0] = DerivRe;
		for (int I = 0; I < 3; ++I)
		{
			OutJ[0][I + 1] = -DerivImOverS * V[I];
			OutJ[I + 1][0] = DerivImOverS * V[I];
			for (int J = 0; J < 3; ++J)
			{
				OutJ[I + 1][J + 1] = (I == J ? K : 0.0) + Radial * V[I] * V[J];
			}
		}
		return Tolerance * Distance / (bInteger ? P : 2.0 * (P > 1.0 ? P : 1.0));
	}

	static double ReferenceEscapeRadius(const FFormulaParams& Params)
	{
		return PowerMapReferenceEscapeRadius(Params.Bailout, Params.Power);
	}

	static double BoundingRadius(const FFormulaParams& Params)
	{
		return PowerMapBoundingRadius(Params.Power, std::sqrt(CX * CX + CY * CY + CZ * CZ + CW * CW), false);
	}
};

} // namespace FractalMath

#if defined(_MSC_VER) || defined(__clang__)
#pragma float_control(pop)
#endif
