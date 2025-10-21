// Copyright Epic Games, Inc. All Rights Reserved.

#include "KleinianDE.h"

void FKleinianDE::SphereFold(FVector &z, double &dz, double MinRadius, double FixedRadius)
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

double FKleinianDE::ComputeDistance(const FVector &WorldPos, const FFractalParameters &Params) const
{
	const FVector LocalPos = (WorldPos - Params.Center) / Params.Scale;
	const FVector c = LocalPos;

	FVector z = LocalPos;
	double dr = 1.0;
	const double Scale = 1.0 + (Params.Power - 1.0) * 0.1;
	const double Bailout = Params.Bailout;

	for (int32 i = 0; i < Params.Iterations; i++)
	{
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
