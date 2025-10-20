// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractalTracing.h"

float FFractalTracing::RaymarchDirection(
	const FVector &StartPos,
	const FVector &Direction,
	const IFractalDistanceEstimator &DE,
	const FFractalParameters &FractalParams,
	const FRaymarchParameters &RaymarchParams)
{
	FVector CurrentPos = StartPos;
	float TotalDistance = 0.0f;
	int32 Steps = 0;

	while (Steps < RaymarchParams.MaxSteps && TotalDistance < RaymarchParams.MaxDistance)
	{
		const double Distance = DE.ComputeDistance(CurrentPos, FractalParams);

		if (Distance < RaymarchParams.Epsilon)
		{
			return TotalDistance;
		}

		const float StepSize = static_cast<float>(Distance);
		CurrentPos += Direction * StepSize;
		TotalDistance += StepSize;
		Steps++;
	}

	return TotalDistance;
}

FFractalTracing::FDirectionalDistances FFractalTracing::ComputeDirectionalDistances(
	const FVector &Position,
	const FRotator &Rotation,
	const IFractalDistanceEstimator &DE,
	const FFractalParameters &FractalParams,
	const FRaymarchParameters &RaymarchParams)
{
	const FVector ForwardDir = Rotation.RotateVector(FVector::ForwardVector);
	const FVector RightDir = Rotation.RotateVector(FVector::RightVector);
	const FVector UpDir = Rotation.RotateVector(FVector::UpVector);

	FDirectionalDistances Distances;
	Distances.Forward = RaymarchDirection(Position, ForwardDir, DE, FractalParams, RaymarchParams);
	Distances.Back = RaymarchDirection(Position, -ForwardDir, DE, FractalParams, RaymarchParams);
	Distances.Right = RaymarchDirection(Position, RightDir, DE, FractalParams, RaymarchParams);
	Distances.Left = RaymarchDirection(Position, -RightDir, DE, FractalParams, RaymarchParams);
	Distances.Up = RaymarchDirection(Position, UpDir, DE, FractalParams, RaymarchParams);
	Distances.Down = RaymarchDirection(Position, -UpDir, DE, FractalParams, RaymarchParams);

	return Distances;
}
