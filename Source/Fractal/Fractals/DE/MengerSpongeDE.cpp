// Copyright Epic Games, Inc. All Rights Reserved.

#include "MengerSpongeDE.h"

double FMengerSpongeDE::ComputeDistance(const FVector &WorldPos, const FFractalParameters &Params) const
{
	const FVector LocalPos = (WorldPos - Params.Center) / Params.Scale;

	FVector z = LocalPos;
	double dr = 1.0;
	const double Scale = Params.Power;
	const double Bailout = Params.Bailout;

	for (int32 i = 0; i < Params.Iterations; i++)
	{
		// Octahedral fold (abs + sort)
		z.X = FMath::Abs(z.X);
		z.Y = FMath::Abs(z.Y);
		z.Z = FMath::Abs(z.Z);

		// Sort components in descending order
		if (z.X < z.Y)
		{
			double temp = z.Y;
			z.Y = z.X;
			z.X = temp;
		}
		if (z.X < z.Z)
		{
			double temp = z.Z;
			z.Z = z.X;
			z.X = temp;
		}
		if (z.Y < z.Z)
		{
			double temp = z.Z;
			z.Z = z.Y;
			z.Y = temp;
		}

		// Scale and translate (Inverted Menger transformation)
		z = z * Scale - FVector(1.0, 1.0, 1.0) * (Scale - 1.0);
		dr *= Scale;

		if (z.SizeSquared() > Bailout * Bailout)
			break;
	}

	const double r = z.Size();
	const double DE = r / FMath::Abs(dr);
	return DE * Params.Scale;
}
