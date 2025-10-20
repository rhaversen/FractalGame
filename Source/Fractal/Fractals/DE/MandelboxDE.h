// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "../FractalTypes.h"

class FMandelboxDE : public IFractalDistanceEstimator
{
public:
	virtual ~FMandelboxDE() = default;

	virtual double ComputeDistance(const FVector &WorldPos, const FFractalParameters &Params) const override;

	virtual FString GetName() const override { return TEXT("Mandelbox"); }

private:
	static void BoxFold(FVector &z, double FoldingLimit = 1.0);
	static void SphereFold(FVector &z, double &dz, double MinRadius = 0.5, double FixedRadius = 1.0);
};
