// Copyright Epic Games, Inc. All Rights Reserved.

#include "KaleidoscopicIFSDE.h"

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
	const double Scale = Params.Power;
	const double Bailout = Params.Bailout;

	for (int32 i = 0; i < Params.Iterations; i++)
	{
		OctahedralFold(z);
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
