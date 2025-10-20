// Copyright Epic Games, Inc. All Rights Reserved.

#include "MengerSpongeDE.h"

double FMengerSpongeDE::ComputeDistance(const FVector &WorldPos, const FFractalParameters &Params) const
{
	const FVector LocalPos = (WorldPos - Params.Center) / Params.Scale;

	FVector z = LocalPos;
	double scale = 1.0;

	for (int32 i = 0; i < Params.Iterations; i++)
	{
		z.X = FMath::Abs(z.X);
		z.Y = FMath::Abs(z.Y);
		z.Z = FMath::Abs(z.Z);

		if (z.X - z.Y < 0.0)
		{
			double temp = z.Y;
			z.Y = z.X;
			z.X = temp;
		}
		if (z.X - z.Z < 0.0)
		{
			double temp = z.Z;
			z.Z = z.X;
			z.X = temp;
		}
		if (z.Y - z.Z < 0.0)
		{
			double temp = z.Z;
			z.Z = z.Y;
			z.Y = temp;
		}

		z *= 3.0;
		scale *= 3.0;

		z.X -= 2.0;
		z.Y -= 2.0;

		if (z.Z > 1.0)
			z.Z -= 2.0;
	}

	const double dist = (z.Size() - 2.0) / scale;
	return dist * Params.Scale;
}
