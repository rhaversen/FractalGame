// Copyright Epic Games, Inc. All Rights Reserved.

#include "QuaternionDE.h"

double FQuaternionDE::ComputeDistance(const FVector &WorldPos, const FFractalParameters &Params) const
{
	const FVector LocalPos = (WorldPos - Params.Center) / Params.Scale;

	FVector4 z(LocalPos.X, LocalPos.Y, LocalPos.Z, 0.0);
	const FVector4 c(LocalPos.X, LocalPos.Y, LocalPos.Z, 0.0);
	double dr = 1.0;
	const double Power = Params.Power;
	const double Bailout = Params.Bailout;

	for (int32 i = 0; i < Params.Iterations; i++)
	{
		const double r = FMath::Sqrt(z.X * z.X + z.Y * z.Y + z.Z * z.Z + z.W * z.W);
		if (r > Bailout)
			break;

		// Quaternion power transformation
		const double theta = FMath::Acos(z.W / r) * Power;
		const double phi = FMath::Atan2(z.Y, z.X) * Power;
		const double zr = FMath::Pow(r, Power);

		const double sinTheta = FMath::Sin(theta);
		z = FVector4(
			zr * sinTheta * FMath::Cos(phi),
			zr * sinTheta * FMath::Sin(phi),
			zr * FMath::Cos(theta),
			0.0
		);

		z += c;
		dr = FMath::Pow(r, Power - 1.0) * Power * dr + 1.0;
	}

	const double r = FMath::Sqrt(z.X * z.X + z.Y * z.Y + z.Z * z.Z + z.W * z.W);
	const double DE = 0.5 * FMath::Loge(r) * r / dr;
	return DE * Params.Scale;
}
