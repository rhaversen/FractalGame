// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FractalTypes.generated.h"

USTRUCT(BlueprintType)
struct FFractalParameters
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal")
	FVector Center = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal")
	double Scale = 1000.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal")
	int32 Iterations = 50;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal")
	double Power = 8.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal")
	double Bailout = 50.0;

	FFractalParameters() = default;

	FFractalParameters(FVector InCenter, double InScale, int32 InIterations, double InPower, double InBailout)
		: Center(InCenter), Scale(InScale), Iterations(InIterations), Power(InPower), Bailout(InBailout)
	{
	}
};

USTRUCT(BlueprintType)
struct FRaymarchParameters
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal")
	int32 MaxSteps = 64;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal")
	float MaxDistance = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal")
	float Epsilon = 0.01f;

	FRaymarchParameters() = default;

	FRaymarchParameters(int32 InMaxSteps, float InMaxDistance, float InEpsilon)
		: MaxSteps(InMaxSteps), MaxDistance(InMaxDistance), Epsilon(InEpsilon)
	{
	}
};

class IFractalDistanceEstimator
{
public:
	virtual ~IFractalDistanceEstimator() = default;

	virtual double ComputeDistance(const FVector &WorldPos, const FFractalParameters &Params) const = 0;

	virtual FString GetName() const = 0;
};
