// Copyright Epic Games, Inc. All Rights Reserved.

#include "SierpinskiDE.h"

double FSierpinskiDE::ComputeDistance(const FVector &WorldPos, const FFractalParameters &Params) const
{
	const FVector LocalPos = (WorldPos - Params.Center) / Params.Scale;

	FVector z = LocalPos;
	double scale = 2.0;
	double d = 0.0;

	for (int32 i = 0; i < Params.Iterations; i++)
	{
		if (z.X + z.Y < 0.0)
		{
			double temp = -z.Y;
			z.Y = -z.X;
			z.X = temp;
		}
		if (z.X + z.Z < 0.0)
		{
			double temp = -z.Z;
			z.Z = -z.X;
			z.X = temp;
		}
		if (z.Y + z.Z < 0.0)
		{
			double temp = -z.Z;
			z.Z = -z.Y;
			z.Y = temp;
		}

		z.X = z.X * scale - 1.0 * (scale - 1.0);
		z.Y = z.Y * scale - 1.0 * (scale - 1.0);
		z.Z = z.Z * scale;

		if (z.Z > 0.5 * (scale - 1.0))
		{
			z.Z -= (scale - 1.0);
		}
	}

	const double dist = (z.Size() - 1.0) * FMath::Pow(scale, static_cast<double>(-Params.Iterations));
	return dist * Params.Scale;
}
