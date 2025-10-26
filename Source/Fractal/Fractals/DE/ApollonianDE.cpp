// Copyright Epic Games, Inc. All Rights Reserved.

#include "ApollonianDE.h"

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

double FApollonianDE::ComputeDistance(const FVector &WorldPos, const FFractalParameters &Params) const
{
	const FVector LocalPos = (WorldPos - Params.Center) / Params.Scale;

	FVector z = LocalPos;
	double dr = 1.0;
	const double scale = 1.0 + (Params.Power - 1.0) * 0.15;
	const double bailoutSquared = Params.Bailout * Params.Bailout;

	const FVector center1(1.0, 1.0, 1.0);
	const FVector center2(1.0, -1.0, -1.0);
	const FVector center3(-1.0, 1.0, -1.0);
	const FVector center4(-1.0, -1.0, 1.0);
	const double radius = 1.0;

	for (int32 iteration = 0; iteration < Params.Iterations; ++iteration)
	{
		const FVector diff1 = z - center1;
		const FVector diff2 = z - center2;
		const FVector diff3 = z - center3;
		const FVector diff4 = z - center4;

		const double d1 = diff1.SizeSquared();
		const double d2 = diff2.SizeSquared();
		const double d3 = diff3.SizeSquared();
		const double d4 = diff4.SizeSquared();

		FVector closest = center1;
		double closestDistance = d1;

		if (d2 < closestDistance)
		{
			closest = center2;
			closestDistance = d2;
		}
		if (d3 < closestDistance)
		{
			closest = center3;
			closestDistance = d3;
		}
		if (d4 < closestDistance)
		{
			closest = center4;
			closestDistance = d4;
		}

		const FVector diff = z - closest;
		const double radiusSquared = radius * radius;
		const double invRadius = FMath::Max(diff.SizeSquared(), 0.01);
		const double k = radiusSquared / invRadius;
		z = closest + diff * k;
		dr *= k;

		z *= scale;
		dr *= scale;

		if (z.SizeSquared() > bailoutSquared)
		{
			break;
		}
	}

	const double distance = ComputeIFSDistance(z, dr);
	return distance * Params.Scale;
}
