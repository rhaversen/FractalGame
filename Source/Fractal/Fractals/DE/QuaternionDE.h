// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "../FractalTypes.h"

class FQuaternionDE : public IFractalDistanceEstimator
{
public:
	virtual ~FQuaternionDE() = default;

	virtual double ComputeDistance(const FVector &WorldPos, const FFractalParameters &Params) const override;

	virtual FString GetName() const override { return TEXT("Quaternion"); }
};
