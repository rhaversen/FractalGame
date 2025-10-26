// Copyright Epic Games, Inc. All Rights Reserved.

#include "MengerSpongeDE.h"

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

double FMengerSpongeDE::ComputeDistance(const FVector &WorldPos, const FFractalParameters &Params) const
{
	const FVector LocalPos = (WorldPos - Params.Center) / Params.Scale;

	FVector z = LocalPos;
	double dr = 1.0;
	const double scale = Params.Power;
	const double bailoutSquared = Params.Bailout * Params.Bailout;

	for (int32 iteration = 0; iteration < Params.Iterations; ++iteration)
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
		z = z * scale - FVector(1.0, 1.0, 1.0) * (scale - 1.0);
		dr *= scale;

		if (z.SizeSquared() > bailoutSquared)
		{
			break;
		}
	}

	const double distance = ComputeIFSDistance(z, dr);
	return distance * Params.Scale;
}
