// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Fractals/FractalTypes.h"

class FSierpinskiDE : public IFractalDistanceEstimator
{
public:
	virtual ~FSierpinskiDE() = default;

	virtual double ComputeDistance(const FVector &WorldPos, const FFractalParameters &Params) const override;

	virtual FString GetName() const override { return TEXT("Sierpinski Tetrahedron"); }
};
