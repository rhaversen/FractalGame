// Copyright Epic Games, Inc. All Rights Reserved.

#include "MandelboxDE.h"

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
	const double Scale = Params.Power;
	const double Bailout = Params.Bailout;

	for (int32 i = 0; i < Params.Iterations; i++)
	{
		BoxFold(z);
		SphereFold(z, dr);

		z = z * Scale + c;
		dr = dr * FMath::Abs(Scale) + 1.0;

		if (z.SizeSquared() > Bailout * Bailout)
			break;
	}

	const double r = z.Size();
	const double DE = r / FMath::Abs(dr);
	return DE * Params.Scale;
}
