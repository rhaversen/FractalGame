// Double-double ("dd") arithmetic: an unevaluated sum Hi + Lo of two doubles, giving ~106 bits
// (~32 decimal digits) of precision with the exponent range of a double.
//
// Used on the CPU for everything whose absolute precision must exceed a double: the fractal-space
// camera position, the perturbation reference point C_ref and the reference orbit Z_n.
//
// Algorithms follow the QD library (Hida, Li, Bailey, "Library for Double-Double and Quad-Double
// Arithmetic", 2007). Everything here is header-only and depends on <cmath> only, so the same code
// is compiled by Unreal (FractalRenderer plugin) and by the standalone test lab (Tools/PerturbationLab).
//
// IMPORTANT: error-free transformations (TwoSum, TwoProd) break under value-changing floating point
// optimisations ("fast math", re-association). The plugin's Build.cs requests precise FP semantics and
// MSVC is additionally pinned to precise mode for this header. FractalMath::DDSelfTest() verifies at
// runtime that the arithmetic survived the compiler.

#pragma once

#include <cmath>
#include <cstdint>

#if defined(_MSC_VER) || defined(__clang__)
#pragma float_control(precise, on, push)
#endif

namespace FractalMath
{

struct FDD
{
	double Hi;
	double Lo;

	constexpr FDD() : Hi(0.0), Lo(0.0) {}
	constexpr FDD(double InHi) : Hi(InHi), Lo(0.0) {}
	constexpr FDD(double InHi, double InLo) : Hi(InHi), Lo(InLo) {}

	double ToDouble() const { return Hi + Lo; }
	float ToFloat() const { return static_cast<float>(Hi + Lo); }
};

// ---------------------------------------------------------------------------------------------
// Error-free transformations
// ---------------------------------------------------------------------------------------------

/** s + e == a + b exactly, |e| <= ulp(s)/2. */
inline FDD DDTwoSum(double A, double B)
{
	const double S = A + B;
	const double BB = S - A;
	const double E = (A - (S - BB)) + (B - BB);
	return FDD(S, E);
}

/** Like DDTwoSum but requires |A| >= |B|. */
inline FDD DDQuickTwoSum(double A, double B)
{
	const double S = A + B;
	const double E = B - (S - A);
	return FDD(S, E);
}

/** Veltkamp split of A into two 26-bit halves (exact, works with or without FMA contraction). */
inline void DDSplit(double A, double& OutHi, double& OutLo)
{
	constexpr double Splitter = 134217729.0; // 2^27 + 1
	constexpr double SplitThreshold = 6.69692879491417e+299; // 2^996
	if (A > SplitThreshold || A < -SplitThreshold)
	{
		const double Scaled = A * 3.7252902984619140625e-09; // 2^-28
		const double T = Splitter * Scaled;
		OutHi = T - (T - Scaled);
		OutLo = Scaled - OutHi;
		OutHi *= 268435456.0; // 2^28
		OutLo *= 268435456.0;
	}
	else
	{
		const double T = Splitter * A;
		OutHi = T - (T - A);
		OutLo = A - OutHi;
	}
}

/** p + e == a * b exactly (Dekker). */
inline FDD DDTwoProd(double A, double B)
{
	const double P = A * B;
	double AH, AL, BH, BL;
	DDSplit(A, AH, AL);
	DDSplit(B, BH, BL);
	const double E = ((AH * BH - P) + AH * BL + AL * BH) + AL * BL;
	return FDD(P, E);
}

inline FDD DDTwoSqr(double A)
{
	const double P = A * A;
	double AH, AL;
	DDSplit(A, AH, AL);
	const double E = ((AH * AH - P) + 2.0 * AH * AL) + AL * AL;
	return FDD(P, E);
}

// ---------------------------------------------------------------------------------------------
// Basic arithmetic
// ---------------------------------------------------------------------------------------------

inline FDD operator-(const FDD& A) { return FDD(-A.Hi, -A.Lo); }

/** Accurate ("IEEE style") addition. */
inline FDD operator+(const FDD& A, const FDD& B)
{
	FDD S = DDTwoSum(A.Hi, B.Hi);
	const FDD T = DDTwoSum(A.Lo, B.Lo);
	S.Lo += T.Hi;
	S = DDQuickTwoSum(S.Hi, S.Lo);
	S.Lo += T.Lo;
	return DDQuickTwoSum(S.Hi, S.Lo);
}

inline FDD operator+(const FDD& A, double B)
{
	FDD S = DDTwoSum(A.Hi, B);
	S.Lo += A.Lo;
	return DDQuickTwoSum(S.Hi, S.Lo);
}

inline FDD operator+(double A, const FDD& B) { return B + A; }
inline FDD operator-(const FDD& A, const FDD& B) { return A + (-B); }
inline FDD operator-(const FDD& A, double B) { return A + (-B); }
inline FDD operator-(double A, const FDD& B) { return (-B) + A; }

inline FDD operator*(const FDD& A, const FDD& B)
{
	FDD P = DDTwoProd(A.Hi, B.Hi);
	P.Lo += (A.Hi * B.Lo + A.Lo * B.Hi);
	return DDQuickTwoSum(P.Hi, P.Lo);
}

inline FDD operator*(const FDD& A, double B)
{
	FDD P = DDTwoProd(A.Hi, B);
	P.Lo += A.Lo * B;
	return DDQuickTwoSum(P.Hi, P.Lo);
}

inline FDD operator*(double A, const FDD& B) { return B * A; }

inline FDD DDSqr(const FDD& A)
{
	FDD P = DDTwoSqr(A.Hi);
	P.Lo += 2.0 * A.Hi * A.Lo;
	P.Lo += A.Lo * A.Lo;
	return DDQuickTwoSum(P.Hi, P.Lo);
}

/** Multiplication by an exact power of two. */
inline FDD DDMulPow2(const FDD& A, double B) { return FDD(A.Hi * B, A.Lo * B); }

inline FDD operator/(const FDD& A, double B)
{
	const double Q1 = A.Hi / B;
	FDD P = DDTwoProd(Q1, B);
	FDD S = DDTwoSum(A.Hi, -P.Hi);
	S.Lo -= P.Lo;
	S.Lo += A.Lo;
	const double Q2 = (S.Hi + S.Lo) / B;
	return DDQuickTwoSum(Q1, Q2);
}

/** Accurate division (three quotient terms). */
inline FDD operator/(const FDD& A, const FDD& B)
{
	const double Q1 = A.Hi / B.Hi;
	FDD R = A - B * Q1;
	const double Q2 = R.Hi / B.Hi;
	R = R - B * Q2;
	const double Q3 = R.Hi / B.Hi;
	const FDD Q = DDQuickTwoSum(Q1, Q2);
	return Q + Q3;
}

inline FDD operator/(double A, const FDD& B) { return FDD(A) / B; }

inline FDD& operator+=(FDD& A, const FDD& B) { A = A + B; return A; }
inline FDD& operator-=(FDD& A, const FDD& B) { A = A - B; return A; }
inline FDD& operator*=(FDD& A, const FDD& B) { A = A * B; return A; }
inline FDD& operator/=(FDD& A, const FDD& B) { A = A / B; return A; }

inline bool operator<(const FDD& A, const FDD& B) { return A.Hi < B.Hi || (A.Hi == B.Hi && A.Lo < B.Lo); }
inline bool operator>(const FDD& A, const FDD& B) { return B < A; }
inline bool operator<=(const FDD& A, const FDD& B) { return !(B < A); }
inline bool operator>=(const FDD& A, const FDD& B) { return !(A < B); }
inline bool operator==(const FDD& A, const FDD& B) { return A.Hi == B.Hi && A.Lo == B.Lo; }
inline bool operator!=(const FDD& A, const FDD& B) { return !(A == B); }

inline FDD DDAbs(const FDD& A) { return A.Hi < 0.0 ? -A : A; }

inline FDD DDLdexp(const FDD& A, int Exp) { return FDD(std::ldexp(A.Hi, Exp), std::ldexp(A.Lo, Exp)); }

/** Round to nearest integer (half away from zero). */
inline FDD DDNint(const FDD& A)
{
	double HiInt = std::floor(A.Hi + 0.5);
	double LoInt = 0.0;
	if (HiInt == A.Hi)
	{
		// Hi is already an integer: round Lo.
		LoInt = std::floor(A.Lo + 0.5);
		const FDD R = DDQuickTwoSum(HiInt, LoInt);
		return R;
	}
	if (std::fabs(HiInt - A.Hi) == 0.5 && A.Lo < 0.0)
	{
		HiInt -= 1.0;
	}
	return FDD(HiInt, LoInt);
}

inline FDD DDSqrt(const FDD& A)
{
	if (A.Hi <= 0.0)
	{
		return FDD(0.0);
	}
	const double X = 1.0 / std::sqrt(A.Hi);
	const double AX = A.Hi * X;
	return DDTwoSum(AX, (A - DDTwoSqr(AX)).Hi * (X * 0.5));
}

// ---------------------------------------------------------------------------------------------
// Constants (values from the QD library, verified against __float128 in the test lab)
// ---------------------------------------------------------------------------------------------

namespace DDConst
{
	constexpr FDD Pi(3.141592653589793116e+00, 1.224646799147353207e-16);
	constexpr FDD TwoPi(6.283185307179586232e+00, 2.449293598294706414e-16);
	constexpr FDD HalfPi(1.570796326794896558e+00, 6.123233995736766036e-17);
	constexpr FDD QuarterPi(7.853981633974482790e-01, 3.061616997868383018e-17);
	constexpr FDD ThreeQuarterPi(2.356194490192344837e+00, 9.1848509936051484375e-17);
	constexpr FDD Ln2(6.931471805599452862e-01, 2.319046813846299558e-17);
	constexpr double Eps = 4.93038065763132e-32; // 2^-104
	constexpr double Huge = 1.7976931348623157e308; // DBL_MAX
}

// ---------------------------------------------------------------------------------------------
// Transcendental functions
// ---------------------------------------------------------------------------------------------

inline FDD DDExp(const FDD& A)
{
	constexpr double InvK = 1.0 / 512.0;

	if (A.Hi <= -709.0)
	{
		return FDD(0.0);
	}
	if (A.Hi >= 709.0)
	{
		return FDD(DDConst::Huge); // overflow (not representable)
	}
	if (A.Hi == 0.0 && A.Lo == 0.0)
	{
		return FDD(1.0);
	}

	const double M = std::floor(A.Hi / DDConst::Ln2.Hi + 0.5);
	const FDD R = DDMulPow2(A - DDConst::Ln2 * M, InvK);

	// Taylor series of expm1(R): |R| <= ln2/1024, so 1/n! terms beyond n=10 are below dd epsilon.
	FDD P = DDSqr(R);
	FDD S = R + DDMulPow2(P, 0.5);
	P = P * R;
	double Factorial = 6.0;
	FDD T = P / Factorial;
	int Index = 3;
	do
	{
		S = S + T;
		P = P * R;
		++Index;
		Factorial *= static_cast<double>(Index);
		T = P / Factorial;
	} while (std::fabs(T.Hi) > InvK * DDConst::Eps && Index < 12);
	S = S + T;

	// Undo the 1/512 reduction: expm1(2x) = 2 expm1(x) + expm1(x)^2, applied 9 times.
	for (int Squaring = 0; Squaring < 9; ++Squaring)
	{
		S = DDMulPow2(S, 2.0) + DDSqr(S);
	}
	S = S + 1.0;
	return DDLdexp(S, static_cast<int>(M));
}

inline FDD DDLog(const FDD& A)
{
	if (A.Hi <= 0.0)
	{
		return FDD(-DDConst::Huge); // log of a non-positive number: -infinity, without relying on IEEE specials
	}
	if (A.Hi == 1.0 && A.Lo == 0.0)
	{
		return FDD(0.0);
	}
	// One Newton step on f(x) = exp(x) - a doubles the ~53 correct bits of the double log.
	FDD X(std::log(A.Hi));
	X = X + A * DDExp(-X) - 1.0;
	return X;
}

namespace DDDetail
{
	/** sin and cos of |T| <= pi/4 by Taylor series. */
	inline void SinCosTaylor(const FDD& T, FDD& OutSin, FDD& OutCos)
	{
		if (T.Hi == 0.0 && T.Lo == 0.0)
		{
			OutSin = FDD(0.0);
			OutCos = FDD(1.0);
			return;
		}

		const FDD X2 = -DDSqr(T);
		const double Threshold = 0.5 * std::fabs(T.Hi) * DDConst::Eps;

		// sin
		FDD Sum = T;
		FDD Term = T;
		double N = 1.0;
		for (int Iter = 0; Iter < 30; ++Iter)
		{
			Term = Term * X2 / ((N + 1.0) * (N + 2.0));
			N += 2.0;
			Sum = Sum + Term;
			if (std::fabs(Term.Hi) <= Threshold)
			{
				break;
			}
		}
		OutSin = Sum;

		// cos
		Sum = FDD(1.0);
		Term = FDD(1.0);
		N = 0.0;
		for (int Iter = 0; Iter < 30; ++Iter)
		{
			Term = Term * X2 / ((N + 1.0) * (N + 2.0));
			N += 2.0;
			Sum = Sum + Term;
			if (std::fabs(Term.Hi) <= 0.5 * DDConst::Eps)
			{
				break;
			}
		}
		OutCos = Sum;
	}
}

inline void DDSinCos(const FDD& A, FDD& OutSin, FDD& OutCos)
{
	if (A.Hi == 0.0 && A.Lo == 0.0)
	{
		OutSin = FDD(0.0);
		OutCos = FDD(1.0);
		return;
	}

	// Reduce modulo 2pi, then modulo pi/2.
	const FDD Z = DDNint(A / DDConst::TwoPi);
	const FDD R = A - DDConst::TwoPi * Z;

	const double Q = std::floor(R.Hi / DDConst::HalfPi.Hi + 0.5);
	const FDD T = R - DDConst::HalfPi * Q;
	const int J = static_cast<int>(Q);

	FDD S, C;
	DDDetail::SinCosTaylor(T, S, C);

	switch (((J % 4) + 4) % 4)
	{
	case 0: OutSin = S; OutCos = C; break;
	case 1: OutSin = C; OutCos = -S; break;
	case 2: OutSin = -S; OutCos = -C; break;
	default: OutSin = -C; OutCos = S; break;
	}
}

inline FDD DDSin(const FDD& A) { FDD S, C; DDSinCos(A, S, C); return S; }
inline FDD DDCos(const FDD& A) { FDD S, C; DDSinCos(A, S, C); return C; }

/** atan2 with the same conventions as std::atan2 (atan2(0, 0) == 0, result in [-pi, pi]). */
inline FDD DDAtan2(const FDD& Y, const FDD& X)
{
	if (X.Hi == 0.0)
	{
		if (Y.Hi == 0.0)
		{
			return FDD(0.0);
		}
		return Y.Hi > 0.0 ? DDConst::HalfPi : -DDConst::HalfPi;
	}
	if (Y.Hi == 0.0)
	{
		return X.Hi > 0.0 ? FDD(0.0) : DDConst::Pi;
	}
	if (X == Y)
	{
		return Y.Hi > 0.0 ? DDConst::QuarterPi : -DDConst::ThreeQuarterPi;
	}
	if (X == -Y)
	{
		return Y.Hi > 0.0 ? DDConst::ThreeQuarterPi : -DDConst::QuarterPi;
	}

	const FDD R = DDSqrt(DDSqr(X) + DDSqr(Y));
	const FDD XX = X / R;
	const FDD YY = Y / R;

	// Newton step from the double precision estimate.
	FDD Z(std::atan2(Y.ToDouble(), X.ToDouble()));
	FDD SinZ, CosZ;
	DDSinCos(Z, SinZ, CosZ);
	if (std::fabs(XX.Hi) > std::fabs(YY.Hi))
	{
		Z = Z + (YY - SinZ) / CosZ;
	}
	else
	{
		Z = Z - (XX - CosZ) / SinZ;
	}
	return Z;
}

/** A^P for A >= 0 and real P. */
inline FDD DDPow(const FDD& A, double P)
{
	if (A.Hi <= 0.0)
	{
		return FDD(0.0);
	}
	return DDExp(DDLog(A) * P);
}

/** A^N for integer N >= 0 by binary exponentiation. */
inline FDD DDPowInt(const FDD& A, int N)
{
	FDD Result(1.0);
	FDD Base = A;
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
			Base = DDSqr(Base);
		}
	}
	return Result;
}

/**
 * Checks at runtime that the compiler preserved the error-free transformations.
 * Returns false if double-double arithmetic is broken (e.g. compiled with fast-math).
 */
inline bool DDSelfTest()
{
	// 1 + 2^-60 is not representable in a double; dd must keep the tail.
	volatile double Tiny = 8.673617379884035e-19; // 2^-60
	const FDD Sum = FDD(1.0) + static_cast<double>(Tiny);
	if (Sum.Hi != 1.0 || Sum.Lo != static_cast<double>(Tiny))
	{
		return false;
	}
	// (1 + 2^-30)^2 = 1 + 2^-29 + 2^-60: the 2^-60 term lives in Lo.
	volatile double A = 1.0 + 9.313225746154785e-10; // 1 + 2^-30
	const FDD Sq = DDTwoProd(A, A);
	if (Sq.Lo != static_cast<double>(Tiny))
	{
		return false;
	}
	// sqrt(2)^2 == 2 to dd precision.
	const FDD Two = DDSqr(DDSqrt(FDD(2.0)));
	const double Err = std::fabs((Two - 2.0).ToDouble());
	return Err < 1e-30;
}

} // namespace FractalMath

#if defined(_MSC_VER) || defined(__clang__)
#pragma float_control(pop)
#endif
