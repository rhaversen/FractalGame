// Copyright Epic Games, Inc. All Rights Reserved.

#include "JuliaSetDE.h"

double FJuliaSetDE::ComputeDistance(const FVector &WorldPos, const FFractalParameters &Params) const
{
	const FVector LocalPos = (WorldPos - Params.Center) / Params.Scale;

	FVector z = LocalPos;
	double dr = 1.0;
	double r = 0.0;
	const double Power = Params.Power;
	const double Bailout = Params.Bailout;

	const FVector c = FVector(-0.8, 0.156, 0.0);

	for (int32 i = 0; i < Params.Iterations; i++)
	{
		r = z.Size();
		if (r > Bailout)
			break;

		const double theta = FMath::Acos(z.Z / r);
		const double phi = FMath::Atan2(z.Y, z.X);
		dr = FMath::Pow(r, Power - 1.0) * Power * dr;

		const double zr = FMath::Pow(r, Power);
		const double newTheta = theta * Power;
		const double newPhi = phi * Power;

		const double sinTheta = FMath::Sin(newTheta);
		z = FVector(
				sinTheta * FMath::Cos(newPhi),
				sinTheta * FMath::Sin(newPhi),
				FMath::Cos(newTheta)) *
			zr;
		z += c;
	}

	const double DE = 0.5 * FMath::Loge(r) * r / dr;
	return DE * Params.Scale;
}
