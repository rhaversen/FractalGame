#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "HAL/ThreadSafeCounter.h"
#include "Templates/SharedPointer.h"
#include "FractalMath/FractalFormulas.h"

/**
 * Maps world space to fractal space: Fractal = Origin + (World - Anchor) * Scale.
 * Origin is double-double so the camera can sit arbitrarily deep inside the fractal while Unreal keeps
 * working with ordinary world coordinates near the anchor. Zooming changes Scale around a world point by
 * moving the anchor there (see ZoomAround), so the camera never jumps.
 */
struct FRACTALRENDERER_API FFractalCameraMapping
{
	FractalMath::FDDVec3 Origin;
	FVector3d Anchor = FVector3d::ZeroVector;
	double Scale = 1.0e-3; // fractal units per world unit (cm)

	FractalMath::FDDVec3 WorldToFractal(const FVector3d& World) const;

	/** Multiplies Scale by Factor while keeping the fractal position of WorldPoint fixed. */
	void ZoomAround(const FVector3d& WorldPoint, double Factor);

	/** Re-anchors so that WorldPoint maps exactly to FractalPoint (Scale unchanged). */
	void SetFractalPositionOf(const FVector3d& WorldPoint, const FractalMath::FDDVec3& FractalPoint);
};

/**
 * Iterations per sample for a given pixel footprint (fractal units). Folding fractals expand by ~|s| per
 * iteration, so a point one footprint away from the surface needs ~log(Bailout / footprint) / log|s|
 * iterations to escape; deep zooms with small scale factors need more than BaseIterations. Rounded up to
 * multiples of 50 (so that the reference orbit, whose length depends on it, is only rebuilt occasionally)
 * and capped at 1000. Power fractals return BaseIterations.
 */
FRACTALRENDERER_API int32 ComputeIterationBudget(FractalMath::EFractalFormula Formula, double Power, int32 BaseIterations, double Bailout, double FootprintFractal);

/** An immutable reference point + orbit, shared between the game, render and worker threads. */
struct FRACTALRENDERER_API FFractalReferenceData
{
	FractalMath::FDDVec3 Center;                   // C_ref (fractal space, double-double)
	TArray<FVector4f> Orbit;                       // GPU layout, OrbitStride float4 per point (FractalReferenceOrbit.h)
	int32 OrbitStride = 0;
	int32 OrbitLength = 0;                         // points (Orbit.Num() / OrbitStride)
	FractalMath::EFractalFormula Formula = FractalMath::EFractalFormula::Mandelbulb;
	FractalMath::FFormulaParams Params;
	double YardstickFractal = 0.0;                 // camera-to-reference distance at creation (fractal units)
	double CameraDistanceEstimate = 0.0;           // DE at the camera at creation (world units)
	double ScaleAtCreation = 0.0;
	bool bHit = false;
	bool bInside = false;
	uint64 Version = 0;
	double GenerationMilliseconds = 0.0;
};

/** What the renderer knows about the view this frame. */
struct FRACTALRENDERER_API FFractalReferenceRequest
{
	FractalMath::FDDVec3 Camera;  // camera position in fractal space
	FVector3d Forward = FVector3d::ForwardVector; // unit view direction (world == fractal axes)
	double Scale = 1.0;
	double PixelRadiusPerUnitDistance = 0.001;
	FractalMath::EFractalFormula Formula = FractalMath::EFractalFormula::Mandelbulb;
	FractalMath::FFormulaParams Params;
	int32 MaxRaySteps = 150;
	double MaxRayDistance = 1.0e6; // world units
};

/**
 * Owns the perturbation reference. The reference point is found by marching the view's centre ray
 * (FractalMath::MarchReferenceRay, in double-double once the zoom is deep) and its orbit is computed in
 * double-double for the requested formula.
 * Regeneration runs on a worker thread so the renderer never waits, except when no usable reference
 * exists (first frame, formula change, or a teleport far beyond the old reference), where it runs inline
 * (~0.2-2 ms). Because the renderer recomputes (camera - C_ref) every frame in double-double, a slightly
 * stale reference is still exact; it only has to stay close enough for float precision.
 */
class FRACTALRENDERER_API FFractalReferenceManager
{
public:
	FFractalReferenceManager();
	~FFractalReferenceManager();

	/** Thread-safe. Returns the reference to render with (may be null) and schedules a refresh if needed. */
	TSharedPtr<const FFractalReferenceData, ESPMode::ThreadSafe> Update(const FFractalReferenceRequest& Request);

	/** Latest published reference (may be null). */
	TSharedPtr<const FFractalReferenceData, ESPMode::ThreadSafe> GetCurrent() const;

	/** Builds a reference synchronously (also used by the worker). */
	static TSharedPtr<const FFractalReferenceData, ESPMode::ThreadSafe> Generate(const FFractalReferenceRequest& Request, uint64 Version);

private:
	enum class ENeed : uint8 { None, Async, Sync };
	ENeed EvaluateNeed(const FFractalReferenceData* Current, const FFractalReferenceRequest& Request, double Now) const;
	void Publish(const TSharedPtr<const FFractalReferenceData, ESPMode::ThreadSafe>& Data);

	mutable FCriticalSection Mutex;
	TSharedPtr<const FFractalReferenceData, ESPMode::ThreadSafe> Current;
	FFractalReferenceRequest LastRequest;
	double LastRequestTime = -1.0e30;
	uint64 NextVersion = 1;
	TSharedRef<FThreadSafeCounter, ESPMode::ThreadSafe> JobsInFlight;
};
