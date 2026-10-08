// __float128 (113-bit mantissa, ~34 digits) helpers used as ground truth by the lab.
#pragma once
#include <quadmath.h>
#include <cstdio>
#include <string>
#include "FractalMath/DoubleDouble.h"

using quad = __float128;

// Scalar overloads so the FractalMath templates (formulas, DistanceEstimate, ...) also run in quad.
// They must be declared before FractalScalar.h is included (no ADL for builtin types).
namespace FractalMath
{
inline double RToDouble(quad A) { return (double)A; }
inline quad RSqrt(quad A) { return A > 0 ? sqrtq(A) : (quad)0; }
inline quad RAtan2(quad Y, quad X) { return atan2q(Y, X); }
inline void RSinCos(quad A, quad& S, quad& C) { S = sinq(A); C = cosq(A); }
inline quad RPow(quad A, double P) { return A > 0 ? powq(A, (quad)P) : (quad)0; }
inline quad RLog(quad A) { return logq(A); }
inline quad RAbs(quad A) { return A < 0 ? -A : A; }
}

inline quad ToQuad(const FractalMath::FDD& A) { return (quad)A.Hi + (quad)A.Lo; }
inline FractalMath::FDD ToDD(quad Q)
{
	double Hi = (double)Q;
	double Lo = (double)(Q - (quad)Hi);
	return FractalMath::FDD(Hi, Lo);
}
inline std::string QStr(quad Q, int Digits = 36)
{
	char Buf[128];
	quadmath_snprintf(Buf, sizeof(Buf), "%.*Qe", Digits, Q);
	return Buf;
}
inline quad QAbs(quad Q) { return Q < 0 ? -Q : Q; }

struct QVec3 { quad X, Y, Z; };
inline QVec3 operator+(const QVec3& A, const QVec3& B) { return {A.X + B.X, A.Y + B.Y, A.Z + B.Z}; }
inline QVec3 operator-(const QVec3& A, const QVec3& B) { return {A.X - B.X, A.Y - B.Y, A.Z - B.Z}; }
inline QVec3 operator*(const QVec3& A, quad S) { return {A.X * S, A.Y * S, A.Z * S}; }
inline quad QDot(const QVec3& A, const QVec3& B) { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
inline quad QLen(const QVec3& A) { return sqrtq(QDot(A, A)); }

/** Reference trigonometric Mandelbulb power map in quad precision (theta from +z, phi = atan2(y,x)). */
inline QVec3 QMandelbulbPow(const QVec3& W, quad P)
{
	quad R = QLen(W);
	if (R == 0) return {0, 0, 0};
	quad Rho = sqrtq(W.X * W.X + W.Y * W.Y);
	quad Theta = atan2q(Rho, W.Z);
	quad Phi = atan2q(W.Y, W.X);
	quad Rp = powq(R, P);
	quad ST = sinq(P * Theta), CT = cosq(P * Theta);
	return {Rp * ST * cosq(P * Phi), Rp * ST * sinq(P * Phi), Rp * CT};
}
