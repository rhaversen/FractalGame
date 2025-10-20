// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "../FractalTypes.h"

class FBurningShipDE : public IFractalDistanceEstimator
{
public:
	virtual ~FBurningShipDE() = default;

	virtual double ComputeDistance(const FVector &WorldPos, const FFractalParameters &Params) const override;

	virtual FString GetName() const override { return TEXT("Burning Ship"); }
};
