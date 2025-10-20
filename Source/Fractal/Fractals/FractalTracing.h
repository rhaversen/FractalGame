// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FractalTypes.h"

class FFractalTracing
{
public:
	static float RaymarchDirection(
		const FVector &StartPos,
		const FVector &Direction,
		const IFractalDistanceEstimator &DE,
		const FFractalParameters &FractalParams,
		const FRaymarchParameters &RaymarchParams);

	struct FDirectionalDistances
	{
		float Forward = 0.0f;
		float Back = 0.0f;
		float Right = 0.0f;
		float Left = 0.0f;
		float Up = 0.0f;
		float Down = 0.0f;
	};

	static FDirectionalDistances ComputeDirectionalDistances(
		const FVector &Position,
		const FRotator &Rotation,
		const IFractalDistanceEstimator &DE,
		const FFractalParameters &FractalParams,
		const FRaymarchParameters &RaymarchParams);
};
