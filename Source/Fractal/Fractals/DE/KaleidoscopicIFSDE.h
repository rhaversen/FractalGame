// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "../FractalTypes.h"

class FKaleidoscopicIFSDE : public IFractalDistanceEstimator
{
public:
	virtual ~FKaleidoscopicIFSDE() = default;

	virtual double ComputeDistance(const FVector &WorldPos, const FFractalParameters &Params) const override;

	virtual FString GetName() const override { return TEXT("Kaleidoscopic IFS"); }

private:
	static void OctahedralFold(FVector &z);
	static void SphereFold(FVector &z, double &dz, double MinRadius = 0.5, double FixedRadius = 1.0);
};
