// Dumps FractalMath::GenerateReferenceOrbit (Mandelbulb, double-double) for the cases in test/orbit_cases.json-like argv,
// as JSON: used by orbit_compare.js to check that Web/src/lib.js is a 1:1 port.
#include "FractalMath/FractalFormulas.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace FractalMath;
int main(int argc, char** argv)
{
	// argv: power maxIterations cx_hi cx_lo cy_hi cy_lo cz_hi cz_lo
	FFormulaParams P; P.Power = atof(argv[1]); P.MaxIterations = atoi(argv[2]); P.Bailout = 10.0;
	FDDVec3 C(FDD(strtod(argv[3], nullptr), strtod(argv[4], nullptr)), FDD(strtod(argv[5], nullptr), strtod(argv[6], nullptr)), FDD(strtod(argv[7], nullptr), strtod(argv[8], nullptr)));
	const int Stride = FormulaOrbitStride<FMandelbulbFormula>();
	std::vector<float> Out((P.MaxIterations + 1) * Stride * 4);
	const int N = GenerateReferenceOrbit<FMandelbulbFormula, FDD>(C, P, Out.data());
	printf("{\"count\":%d,\"floats\":[", N);
	for (int I = 0; I < N * Stride * 4; I++) printf("%s%.9g", I ? "," : "", Out[I]);
	printf("]}\n");
}
