#include "FractalReference.h"
#include "Tasks/Task.h"
#include "Misc/ScopeLock.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

DEFINE_LOG_CATEGORY_STATIC(LogFractalReference, Log, All);

// Targeted using-declarations (not a using-directive: Unreal's unity builds share one translation unit).
using FractalMath::DDTwoProd;
using FractalMath::FDDVec3;
using FractalMath::FDVec3;
using FractalMath::FReferenceMarchResult;
using FractalMath::FReferenceMarchSettings;
using FractalMath::OffsetDD;
using FractalMath::ToDoubleVec;

namespace
{
	// Regenerate in the background once the reference is this many times farther from the camera than the
	// surface is; build one inline when it is hopelessly far (the usable ratio is ~1e3, see MarchReferenceFan).
	constexpr double AsyncDistanceRatio = 50.0;
	constexpr double SyncDistanceRatio = 5000.0;
	// Never treat the surface as closer than this many world units (a camera inside the set has distance 0).
	constexpr double MinCameraDistanceWorld = 1.0e-3;
}

// ---------------------------------------------------------------------------------------------
// FFractalCameraMapping
// ---------------------------------------------------------------------------------------------

FDDVec3 FFractalCameraMapping::WorldToFractal(const FVector3d& World) const
{
	const FVector3d Local = World - Anchor;
	return FDDVec3(Origin.X + DDTwoProd(Local.X, Scale), Origin.Y + DDTwoProd(Local.Y, Scale), Origin.Z + DDTwoProd(Local.Z, Scale));
}

void FFractalCameraMapping::ZoomAround(const FVector3d& WorldPoint, double Factor)
{
	Origin = WorldToFractal(WorldPoint);
	Anchor = WorldPoint;
	Scale *= Factor;
}

// ---------------------------------------------------------------------------------------------
// FFractalReferenceManager
// ---------------------------------------------------------------------------------------------

FFractalReferenceManager::FFractalReferenceManager()
	: JobsInFlight(MakeShared<FThreadSafeCounter, ESPMode::ThreadSafe>())
{
}

FFractalReferenceManager::~FFractalReferenceManager()
{
	// Worker jobs publish into this object; wait for them (they take milliseconds).
	while (JobsInFlight->GetValue() > 0)
	{
		FPlatformProcess::Sleep(0.001f);
	}
}

TSharedPtr<const FFractalReferenceData, ESPMode::ThreadSafe> FFractalReferenceManager::GetCurrent() const
{
	FScopeLock Lock(&Mutex);
	return Current;
}

void FFractalReferenceManager::Publish(const TSharedPtr<const FFractalReferenceData, ESPMode::ThreadSafe>& Data)
{
	FScopeLock Lock(&Mutex);
	// Never replace a newer reference with an older one (async job finishing after a synchronous rebuild).
	if (Data.IsValid() && (!Current.IsValid() || Data->Version > Current->Version))
	{
		Current = Data;
	}
}

double FFractalReferenceManager::DistanceWorld(const FDDVec3& A, const FDDVec3& B, double Scale)
{
	const FDVec3 D = ToDoubleVec(A - B);
	return FMath::Sqrt(D.X * D.X + D.Y * D.Y + D.Z * D.Z) / Scale;
}

FFractalReferenceManager::ENeed FFractalReferenceManager::EvaluateNeed(const FFractalReferenceData* Ref, const FFractalReferenceRequest& Request, double Now) const
{
	if (!Ref)
	{
		return ENeed::Sync;
	}
	if (Ref->Formula != Request.Formula || Ref->Params.Power != Request.Params.Power || Ref->Params.Bailout != Request.Params.Bailout)
	{
		return ENeed::Sync;
	}

	const double SurfaceDistance = FMath::Max(Request.CameraDistanceWorld, MinCameraDistanceWorld);
	const double ReferenceRatio = DistanceWorld(Request.Camera, Ref->Center, Request.Scale) / SurfaceDistance;
	if (ReferenceRatio > SyncDistanceRatio)
	{
		return ENeed::Sync;
	}

	if (Now - LastRequestTime < 0.05)
	{
		return ENeed::None;
	}
	const double Moved = DistanceWorld(Request.Camera, LastRequest.Camera, Request.Scale);
	const double Turned = FMath::Acos(FMath::Clamp(FVector3d::DotProduct(Request.Forward, LastRequest.Forward), -1.0, 1.0));
	// The nearest visible surface changes as the camera moves or turns; a stale reference is only a problem
	// once it is far (relative to the surface distance), but refreshing is cheap and keeps it at the nearest hit.
	if (ReferenceRatio > AsyncDistanceRatio || Ref->Params.MaxIterations != Request.Params.MaxIterations || Moved > 0.01 * SurfaceDistance || Turned > 0.01 || Request.Scale != LastRequest.Scale)
	{
		return ENeed::Async;
	}
	return ENeed::None;
}

TSharedPtr<const FFractalReferenceData, ESPMode::ThreadSafe> FFractalReferenceManager::Update(const FFractalReferenceRequest& Request)
{
	const double Now = FPlatformTime::Seconds();
	TSharedPtr<const FFractalReferenceData, ESPMode::ThreadSafe> Snapshot;
	ENeed Need = ENeed::None;
	uint64 Version = 0;
	{
		FScopeLock Lock(&Mutex);
		Snapshot = Current;
		Need = EvaluateNeed(Snapshot.Get(), Request, Now);
		if (Need == ENeed::Async && JobsInFlight->GetValue() > 0)
		{
			Need = ENeed::None; // one job at a time; the next frame will ask again
		}
		if (Need == ENeed::None)
		{
			return Snapshot;
		}
		LastRequest = Request;
		LastRequestTime = Now;
		Version = NextVersion++;
		if (Need == ENeed::Async)
		{
			JobsInFlight->Increment();
		}
	}

	if (Need == ENeed::Sync)
	{
		TSharedPtr<const FFractalReferenceData, ESPMode::ThreadSafe> Data = Generate(Request, Version, true);
		Publish(Data);
		return GetCurrent();
	}

	TSharedRef<FThreadSafeCounter, ESPMode::ThreadSafe> Counter = JobsInFlight;
	UE::Tasks::Launch(UE_SOURCE_LOCATION, [this, Request, Version, Counter]()
	{
		Publish(Generate(Request, Version, false));
		Counter->Decrement();
	});
	return Snapshot;
}

TSharedPtr<const FFractalReferenceData, ESPMode::ThreadSafe> FFractalReferenceManager::Generate(const FFractalReferenceRequest& Request, uint64 Version, bool bQuick)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FFractalReferenceManager::Generate);
	const double StartTime = FPlatformTime::Seconds();

	FReferenceMarchSettings Settings;
	Settings.Params = Request.Params;
	Settings.MaxSteps = Request.MaxRaySteps;
	Settings.MaxDistance = Request.MaxRayDistance;
	Settings.PixelRadiusPerUnitDistance = Request.PixelRadiusPerUnitDistance;

	// Rays across the view place the reference at the nearest visible surface (centre ray first). The march only
	// decides *where* the reference goes, so double precision is enough while the zoom is shallow (and ~10x
	// faster). The orbit itself is always double-double.
	constexpr int MaxDirections = FractalMath::ReferenceFanCount;
	double Dirs[MaxDirections][3];
	int DirectionCount = FMath::Min(Request.Directions.Num(), bQuick ? FractalMath::ReferenceFanQuickCount : MaxDirections);
	for (int32 I = 0; I < DirectionCount; ++I)
	{
		Dirs[I][0] = Request.Directions[I].X;
		Dirs[I][1] = Request.Directions[I].Y;
		Dirs[I][2] = Request.Directions[I].Z;
	}
	if (DirectionCount == 0)
	{
		Dirs[0][0] = Request.Forward.X;
		Dirs[0][1] = Request.Forward.Y;
		Dirs[0][2] = Request.Forward.Z;
		DirectionCount = 1;
	}

	const FReferenceMarchResult March = Request.Scale > 1.0e-9
		? FractalMath::MarchReferenceFan(Request.Formula, ToDoubleVec(Request.Camera), Dirs, DirectionCount, Request.Scale, Settings)
		: FractalMath::MarchReferenceFan(Request.Formula, Request.Camera, Dirs, DirectionCount, Request.Scale, Settings);

	TSharedPtr<FFractalReferenceData, ESPMode::ThreadSafe> Data = MakeShared<FFractalReferenceData, ESPMode::ThreadSafe>();
	Data->Center = OffsetDD(Request.Camera, Dirs[March.DirectionIndex], March.ReferenceDistance * Request.Scale);
	Data->Formula = Request.Formula;
	Data->Params = Request.Params;
	Data->bHit = March.bHit;
	Data->bInside = March.bReferenceInside;
	Data->Version = Version;

	// The orbit also carries the linear series skip (Jacobian products + validity radii), which lets each
	// sample start iterating where its perturbation stops being linear.
	const int32 Stride = FractalMath::FormulaOrbitStride(Request.Formula);
	Data->OrbitStride = Stride;
	Data->Orbit.SetNumUninitialized((FMath::Max(Request.Params.MaxIterations, 1) + 1) * Stride);
	static_assert(sizeof(FVector4f) == 4 * sizeof(float), "orbit points are packed float4s");
	const int32 Length = FractalMath::GenerateReferenceOrbit(Request.Formula, Data->Center, Request.Params,
		reinterpret_cast<float*>(Data->Orbit.GetData()), FractalMath::DefaultSeriesTolerance);
	Data->OrbitLength = Length;
	Data->Orbit.SetNum(Length * Stride);
	Data->GenerationMilliseconds = (FPlatformTime::Seconds() - StartTime) * 1000.0;

	UE_LOG(LogFractalReference, Verbose, TEXT("Reference v%llu: %d orbit points, hit=%d inside=%d, t=%.4g world, DE(camera)=%.4g world, %.2f ms"),
		Version, Length, Data->bHit ? 1 : 0, Data->bInside ? 1 : 0, March.ReferenceDistance, March.CameraDistanceEstimate, Data->GenerationMilliseconds);
	return Data;
}
