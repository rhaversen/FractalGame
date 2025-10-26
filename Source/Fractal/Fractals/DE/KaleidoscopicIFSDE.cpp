// Copyright Epic Games, Inc. All Rights Reserved.

#include "KaleidoscopicIFSDE.h"

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

void FKaleidoscopicIFSDE::OctahedralFold(FVector &z)
{
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
}

void FKaleidoscopicIFSDE::SphereFold(FVector &z, double &dz, double MinRadius, double FixedRadius)
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

double FKaleidoscopicIFSDE::ComputeDistance(const FVector &WorldPos, const FFractalParameters &Params) const
{
	const FVector LocalPos = (WorldPos - Params.Center) / Params.Scale;
	const FVector c = LocalPos;

	FVector z = LocalPos;
	double dr = 1.0;
	const double scale = Params.Power;
	const double bailoutSquared = Params.Bailout * Params.Bailout;

	for (int32 iteration = 0; iteration < Params.Iterations; ++iteration)
	{
		OctahedralFold(z);
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
