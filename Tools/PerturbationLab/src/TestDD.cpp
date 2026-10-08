// Validates FractalMath::FDD against __float128.
#include "QuadUtil.h"
#include <random>
#include <cstdio>
#include <functional>

using namespace FractalMath;

static int Failures = 0;

static void Report(const char* Name, double MaxRel, double Tol)
{
	bool Ok = MaxRel <= Tol;
	if (!Ok) Failures++;
	std::printf("  %-28s max rel err %.3e  (tol %.1e)  %s\n", Name, MaxRel, Tol, Ok ? "OK" : "FAIL");
}

static double Rel(quad Got, quad Want)
{
	quad D = QAbs(Got - Want);
	quad M = QAbs(Want);
	if (M == 0) return (double)D;
	return (double)(D / M);
}

int main()
{
	std::printf("DD self test: %s\n", DDSelfTest() ? "OK" : "FAIL");
	if (!DDSelfTest()) Failures++;

	// Constants
	quad PiQ = M_PIq;
	Report("const pi", Rel(ToQuad(DDConst::Pi), PiQ), 1e-32);
	Report("const 2pi", Rel(ToQuad(DDConst::TwoPi), 2 * PiQ), 1e-32);
	Report("const pi/2", Rel(ToQuad(DDConst::HalfPi), PiQ / 2), 1e-32);
	Report("const pi/4", Rel(ToQuad(DDConst::QuarterPi), PiQ / 4), 1e-32);
	Report("const 3pi/4", Rel(ToQuad(DDConst::ThreeQuarterPi), 3 * PiQ / 4), 1e-32);
	Report("const ln2", Rel(ToQuad(DDConst::Ln2), M_LN2q), 1e-32);

	std::mt19937_64 Rng(1234);
	std::uniform_real_distribution<double> U(-1.0, 1.0);
	auto RandDD = [&](double ExpLo, double ExpHi) {
		double E = ExpLo + (ExpHi - ExpLo) * (0.5 + 0.5 * U(Rng));
		quad Q = (quad)U(Rng) * powq(10, (quad)E) + (quad)U(Rng) * powq(10, (quad)E - 17);
		return ToDD(Q);
	};

	const int N = 20000;
	double MAdd = 0, MMul = 0, MDiv = 0, MSqrt = 0, MExp = 0, MLog = 0, MSin = 0, MCos = 0, MAtan = 0, MPow = 0, MPowI = 0;
	for (int I = 0; I < N; I++)
	{
		FDD A = RandDD(-5, 5), B = RandDD(-5, 5);
		quad QA = ToQuad(A), QB = ToQuad(B);
		// addition is only accurate relative to max(|a|,|b|)
		MAdd = std::max(MAdd, (double)(QAbs(ToQuad(A + B) - (QA + QB)) / fmaxq(QAbs(QA), QAbs(QB))));
		MMul = std::max(MMul, Rel(ToQuad(A * B), QA * QB));
		MDiv = std::max(MDiv, Rel(ToQuad(A / B), QA / QB));
		FDD PA = DDAbs(A);
		MSqrt = std::max(MSqrt, Rel(ToQuad(DDSqrt(PA)), sqrtq(ToQuad(PA))));
		FDD E = RandDD(-3, 2);
		MExp = std::max(MExp, Rel(ToQuad(DDExp(E)), expq(ToQuad(E))));
		MLog = std::max(MLog, (double)QAbs(ToQuad(DDLog(PA)) - logq(ToQuad(PA)))); // absolute near 1
		FDD T = RandDD(-8, 1.6); // up to ~40 rad (p*theta for p ~ 12)
		quad QT = ToQuad(T);
		FDD S, C;
		DDSinCos(T, S, C);
		// absolute error (sin near zero crossings is only abs-accurate in any implementation)
		MSin = std::max(MSin, (double)QAbs(ToQuad(S) - sinq(QT)));
		MCos = std::max(MCos, (double)QAbs(ToQuad(C) - cosq(QT)));
		FDD Y = RandDD(-12, 1), X = RandDD(-12, 1);
		MAtan = std::max(MAtan, Rel(ToQuad(DDAtan2(Y, X)), atan2q(ToQuad(Y), ToQuad(X))));
		double P = 2.0 + 10.0 * (0.5 + 0.5 * U(Rng));
		FDD Base = DDAbs(RandDD(-1, 0.5));
		MPow = std::max(MPow, Rel(ToQuad(DDPow(Base, P)), powq(ToQuad(Base), (quad)P)));
		int PI = 2 + (I % 15);
		MPowI = std::max(MPowI, Rel(ToQuad(DDPowInt(Base, PI)), powq(ToQuad(Base), (quad)PI)));
	}
	// small-argument relative accuracy of sin
	double MSinSmall = 0;
	for (int I = 0; I < N; I++)
	{
		FDD T = RandDD(-30, -2);
		MSinSmall = std::max(MSinSmall, Rel(ToQuad(DDSin(T)), sinq(ToQuad(T))));
	}
	Report("add (rel to max operand)", MAdd, 1e-31);
	Report("mul", MMul, 1e-31);
	Report("div", MDiv, 1e-31);
	Report("sqrt", MSqrt, 1e-31);
	Report("exp (abs x<=100)", MExp, 3e-30);
	Report("log (abs, x<=1e5)", MLog, 3e-30);
	Report("sin (abs, |x|<=40)", MSin, 1e-30);
	Report("cos (abs, |x|<=40)", MCos, 1e-30);
	Report("sin (rel, tiny x)", MSinSmall, 1e-31);
	Report("atan2", MAtan, 1e-30);
	Report("pow real", MPow, 1e-29);
	Report("pow int", MPowI, 1e-30);

	std::printf("%s\n", Failures ? "SOME TESTS FAILED" : "ALL DD TESTS PASSED");
	return Failures ? 1 : 0;
}
