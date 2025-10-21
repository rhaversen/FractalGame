// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "../FractalTypes.h"

class FApollonianDE : public IFractalDistanceEstimator
{
public:
	virtual ~FApollonianDE() = default;

	virtual double ComputeDistance(const FVector &WorldPos, const FFractalParameters &Params) const override;

	virtual FString GetName() const override { return TEXT("Apollonian"); }
};
