// Scalar and vector abstraction shared by every fractal formula, so the same templated code runs in
// double, double-double (FDD) and, in Tools/PerturbationLab, __float128 (see QuadUtil.h, which adds the
// quad overloads before this header is included). Depends only on <cmath>.

#pragma once

#include "DoubleDouble.h"

#include <cmath>

#if defined(_MSC_VER) || defined(__clang__)
#pragma float_control(precise, on, push)
#endif

namespace FractalMath
{

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
inline double RAbs(double A) { return std::fabs(A); }
inline FDD RAbs(const FDD& A) { return DDAbs(A); }

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

	T& operator[](int I) { return I == 0 ? X : (I == 1 ? Y : Z); }
	const T& operator[](int I) const { return I == 0 ? X : (I == 1 ? Y : Z); }
};

/** Quaternion-valued state (W is the real part). */
template <typename T>
struct TFractalVec4
{
	T X;
	T Y;
	T Z;
	T W;

	TFractalVec4() : X(0.0), Y(0.0), Z(0.0), W(0.0) {}
	TFractalVec4(const T& InX, const T& InY, const T& InZ, const T& InW) : X(InX), Y(InY), Z(InZ), W(InW) {}

	T& operator[](int I) { return I == 0 ? X : (I == 1 ? Y : (I == 2 ? Z : W)); }
	const T& operator[](int I) const { return I == 0 ? X : (I == 1 ? Y : (I == 2 ? Z : W)); }
};

using FDDVec3 = TFractalVec3<FDD>;
using FDVec3 = TFractalVec3<double>;

template <typename T>
inline TFractalVec3<T> operator+(const TFractalVec3<T>& A, const TFractalVec3<T>& B) { return TFractalVec3<T>(A.X + B.X, A.Y + B.Y, A.Z + B.Z); }
template <typename T>
inline TFractalVec3<T> operator-(const TFractalVec3<T>& A, const TFractalVec3<T>& B) { return TFractalVec3<T>(A.X - B.X, A.Y - B.Y, A.Z - B.Z); }
template <typename T>
inline TFractalVec3<T> operator*(const TFractalVec3<T>& A, double S) { return TFractalVec3<T>(A.X * S, A.Y * S, A.Z * S); }
template <typename T>
inline T Dot(const TFractalVec3<T>& A, const TFractalVec3<T>& B) { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }

template <typename T>
inline TFractalVec4<T> operator+(const TFractalVec4<T>& A, const TFractalVec4<T>& B) { return TFractalVec4<T>(A.X + B.X, A.Y + B.Y, A.Z + B.Z, A.W + B.W); }
template <typename T>
inline TFractalVec4<T> operator-(const TFractalVec4<T>& A, const TFractalVec4<T>& B) { return TFractalVec4<T>(A.X - B.X, A.Y - B.Y, A.Z - B.Z, A.W - B.W); }
template <typename T>
inline T Dot(const TFractalVec4<T>& A, const TFractalVec4<T>& B) { return A.X * B.X + A.Y * B.Y + A.Z * B.Z + A.W * B.W; }

inline FDVec3 ToDoubleVec(const FDDVec3& A) { return FDVec3(A.X.ToDouble(), A.Y.ToDouble(), A.Z.ToDouble()); }
inline FDVec3 ToDoubleVec(const FDVec3& A) { return A; }
inline FDDVec3 ToDDVec(const FDVec3& A) { return FDDVec3(FDD(A.X), FDD(A.Y), FDD(A.Z)); }
inline FDDVec3 ToDDVec(const FDDVec3& A) { return A; }

template <typename T>
inline TFractalVec3<double> ToDoubleState(const TFractalVec3<T>& A) { return TFractalVec3<double>(RToDouble(A.X), RToDouble(A.Y), RToDouble(A.Z)); }
template <typename T>
inline TFractalVec4<double> ToDoubleState(const TFractalVec4<T>& A) { return TFractalVec4<double>(RToDouble(A.X), RToDouble(A.Y), RToDouble(A.Z), RToDouble(A.W)); }

/** Base + Dir * Distance with the product rounded to double-double (Dir and Distance are doubles). */
inline FDDVec3 OffsetDD(const FDDVec3& Base, const double Dir[3], double Distance)
{
	return FDDVec3(Base.X + DDTwoProd(Dir[0], Distance), Base.Y + DDTwoProd(Dir[1], Distance), Base.Z + DDTwoProd(Dir[2], Distance));
}
inline FDVec3 OffsetDD(const FDVec3& Base, const double Dir[3], double Distance)
{
	return FDVec3(Base.X + Dir[0] * Distance, Base.Y + Dir[1] * Distance, Base.Z + Dir[2] * Distance);
}

/** Integer powers 2..64 use exact trig-free complex formulations (faster and more accurate in dd). */
inline bool IsIntegerPower(double Power)
{
	return Power >= 2.0 && Power <= 64.0 && Power == std::floor(Power);
}

inline float ClampToFloatRange(double V)
{
	constexpr double Limit = 1.0e30;
	return static_cast<float>(V > Limit ? Limit : (V < -Limit ? -Limit : V));
}

} // namespace FractalMath

#if defined(_MSC_VER) || defined(__clang__)
#pragma float_control(pop)
#endif
