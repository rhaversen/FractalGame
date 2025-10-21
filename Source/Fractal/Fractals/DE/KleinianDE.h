// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "../FractalTypes.h"

class FKleinianDE : public IFractalDistanceEstimator
{
public:
	virtual ~FKleinianDE() = default;

	virtual double ComputeDistance(const FVector &WorldPos, const FFractalParameters &Params) const override;

	virtual FString GetName() const override { return TEXT("Kleinian"); }

private:
	static void SphereFold(FVector &z, double &dz, double MinRadius = 0.3, double FixedRadius = 1.0);
};
