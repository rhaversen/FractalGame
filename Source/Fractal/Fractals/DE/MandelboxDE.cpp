// Copyright Epic Games, Inc. All Rights Reserved.

#include "MandelboxDE.h"

namespace
{
constexpr double kDerivativeEpsilon = 1e-6;

double ComputeIFSDistance(const FVector &Z, double Derivative)
{
	// Guard against tiny derivatives so the distance estimate remains well behaved.
	const double SafeDerivative = FMath::Max(FMath::Abs(Derivative), kDerivativeEpsilon);
	return FMath::Max(Z.Size() / SafeDerivative, 0.0);
}
}

void FMandelboxDE::BoxFold(FVector &z, double FoldingLimit)
{
	z.X = FMath::Clamp(z.X, -FoldingLimit, FoldingLimit) * 2.0 - z.X;
	z.Y = FMath::Clamp(z.Y, -FoldingLimit, FoldingLimit) * 2.0 - z.Y;
	z.Z = FMath::Clamp(z.Z, -FoldingLimit, FoldingLimit) * 2.0 - z.Z;
}

void FMandelboxDE::SphereFold(FVector &z, double &dz, double MinRadius, double FixedRadius)
{
	const double r2 = z.SizeSquared();
	const double MinRadius2 = MinRadius * MinRadius;
	const double FixedRadius2 = FixedRadius * FixedRadius;

	if (r2 < MinRadius2)
	{
		const double temp = FixedRadius2 / MinRadius2;
		z *= temp;
		dz *= temp;
	}
	else if (r2 < FixedRadius2)
	{
		const double temp = FixedRadius2 / r2;
		z *= temp;
		dz *= temp;
	}
}

double FMandelboxDE::ComputeDistance(const FVector &WorldPos, const FFractalParameters &Params) const
{
	const FVector LocalPos = (WorldPos - Params.Center) / Params.Scale;
	const FVector c = LocalPos;

	FVector z = LocalPos;
	double dr = 1.0;
	const double scale = Params.Power;
	const double bailoutSquared = Params.Bailout * Params.Bailout;

	for (int32 iteration = 0; iteration < Params.Iterations; ++iteration)
	{
		BoxFold(z);
		SphereFold(z, dr);

		z = z * scale + c;
		dr = dr * FMath::Abs(scale) + 1.0;

		if (z.SizeSquared() > bailoutSquared)
		{
			break;
		}
	}

	const double distance = ComputeIFSDistance(z, dr);
	return distance * Params.Scale;
}
