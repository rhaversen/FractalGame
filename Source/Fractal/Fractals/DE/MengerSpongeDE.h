// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Fractals/FractalTypes.h"

class FMengerSpongeDE : public IFractalDistanceEstimator
{
public:
	virtual ~FMengerSpongeDE() = default;

	virtual double ComputeDistance(const FVector &WorldPos, const FFractalParameters &Params) const override;

	virtual FString GetName() const override { return TEXT("Menger Sponge"); }
};
