// Copyright Epic Games, Inc. All Rights Reserved.

#include "ApollonianDE.h"

double FApollonianDE::ComputeDistance(const FVector &WorldPos, const FFractalParameters &Params) const
{
	const FVector LocalPos = (WorldPos - Params.Center) / Params.Scale;

	FVector z = LocalPos;
	double dr = 1.0;
	const double Scale = 1.0 + (Params.Power - 1.0) * 0.15;
	const double Bailout = Params.Bailout;

	const FVector center1(1.0, 1.0, 1.0);
	const FVector center2(1.0, -1.0, -1.0);
	const FVector center3(-1.0, 1.0, -1.0);
	const FVector center4(-1.0, -1.0, 1.0);
	const double radius = 1.0;

	for (int32 i = 0; i < Params.Iterations; i++)
	{
		FVector diff1 = z - center1;
		FVector diff2 = z - center2;
		FVector diff3 = z - center3;
		FVector diff4 = z - center4;

		double d1 = diff1.SizeSquared();
		double d2 = diff2.SizeSquared();
		double d3 = diff3.SizeSquared();
		double d4 = diff4.SizeSquared();

		FVector closest = center1;
		double closestD = d1;

		if (d2 < closestD) { closest = center2; closestD = d2; }
		if (d3 < closestD) { closest = center3; closestD = d3; }
		if (d4 < closestD) { closest = center4; closestD = d4; }

		FVector diff = z - closest;
		double r2 = diff.SizeSquared();
		double k = radius * radius / FMath::Max(r2, 0.01);
		z = closest + diff * k;
		dr *= k;

		z *= Scale;
		dr *= Scale;

		if (z.SizeSquared() > Bailout * Bailout)
			break;
	}

	const double r = z.Size();
	const double DE = r / FMath::Abs(dr);
	return DE * Params.Scale;
}
