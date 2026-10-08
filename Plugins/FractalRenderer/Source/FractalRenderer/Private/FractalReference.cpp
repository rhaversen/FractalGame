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
using FractalMath::GenerateReferenceOrbit;
using FractalMath::MarchReferenceRay;
using FractalMath::OffsetDD;
using FractalMath::ReferenceEscapeRadius;
using FractalMath::ToDoubleVec;

namespace
{
	double DistanceDD(const FDDVec3& A, const FDDVec3& B)
	{
		const FDVec3 D = ToDoubleVec(A - B);
		return FMath::Sqrt(D.X * D.X + D.Y * D.Y + D.Z * D.Z);
	}
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

void FFractalCameraMapping::SetFractalPositionOf(const FVector3d& WorldPoint, const FDDVec3& FractalPoint)
{
	Origin = FractalPoint;
	Anchor = WorldPoint;
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

void FFractalReferenceManager::Invalidate()
{
	FScopeLock Lock(&Mutex);
	Current.Reset();
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

FFractalReferenceManager::ENeed FFractalReferenceManager::EvaluateNeed(const FFractalReferenceData* Ref, const FFractalReferenceRequest& Request, double Now) const
{
	if (!Ref)
	{
		return ENeed::Sync;
	}
	if (Ref->Power != Request.Power || Ref->MaxIterations != Request.MaxIterations || Ref->Bailout != Request.Bailout)
	{
		return ENeed::Sync;
	}

	// Distances are compared in fractal units against how far the reference was from the camera when it
	// was made. Precision only degrades once the reference is ~100x farther away than the visible surface.
	const double Yardstick = FMath::Max3(Ref->YardstickFractal, Ref->CameraDistanceEstimate * Ref->ScaleAtCreation, Request.Scale * 1.0e-3);
	const double Distance = DistanceDD(Request.Camera, Ref->Center);
	if (Distance > 256.0 * Yardstick)
	{
		return ENeed::Sync;
	}

	if (Now - LastRequestTime < 0.05)
	{
		return ENeed::None;
	}
	const double Moved = DistanceDD(Request.Camera, LastRequest.Camera);
	const double Turned = FMath::Acos(FMath::Clamp(FVector3d::DotProduct(Request.Forward, LastRequest.Forward), -1.0, 1.0));
	const bool bChanged = Moved > 0.01 * Yardstick || Turned > 0.01 || Request.Scale != LastRequest.Scale;
	if (bChanged || Distance > 4.0 * Yardstick)
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
		TSharedPtr<const FFractalReferenceData, ESPMode::ThreadSafe> Data = Generate(Request, Version);
		Publish(Data);
		return GetCurrent();
	}

	TSharedRef<FThreadSafeCounter, ESPMode::ThreadSafe> Counter = JobsInFlight;
	UE::Tasks::Launch(UE_SOURCE_LOCATION, [this, Request, Version, Counter]()
	{
		Publish(Generate(Request, Version));
		Counter->Decrement();
	});
	return Snapshot;
}

TSharedPtr<const FFractalReferenceData, ESPMode::ThreadSafe> FFractalReferenceManager::Generate(const FFractalReferenceRequest& Request, uint64 Version)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FFractalReferenceManager::Generate);
	const double StartTime = FPlatformTime::Seconds();

	FReferenceMarchSettings Settings;
	Settings.Power = Request.Power;
	Settings.MaxIterations = Request.MaxIterations;
	Settings.Bailout = Request.Bailout;
	Settings.MaxSteps = Request.MaxRaySteps;
	Settings.MaxDistance = Request.MaxRayDistance;
	Settings.PixelRadiusPerUnitDistance = Request.PixelRadiusPerUnitDistance;

	const double Dir[3] = {Request.Forward.X, Request.Forward.Y, Request.Forward.Z};

	// The march only decides *where* along the centre ray the reference goes, so double precision is
	// enough while the zoom is shallow (and ~10x faster). The orbit itself is always double-double.
	const FReferenceMarchResult March = Request.Scale > 1.0e-9
		? MarchReferenceRay(ToDoubleVec(Request.Camera), Dir, Request.Scale, Settings)
		: MarchReferenceRay(Request.Camera, Dir, Request.Scale, Settings);

	TSharedPtr<FFractalReferenceData, ESPMode::ThreadSafe> Data = MakeShared<FFractalReferenceData, ESPMode::ThreadSafe>();
	Data->Center = OffsetDD(Request.Camera, Dir, March.ReferenceDistance * Request.Scale);
	Data->Power = Request.Power;
	Data->MaxIterations = Request.MaxIterations;
	Data->Bailout = Request.Bailout;
	Data->YardstickFractal = March.ReferenceDistance * Request.Scale;
	Data->CameraDistanceEstimate = March.CameraDistanceEstimate;
	Data->ScaleAtCreation = Request.Scale;
	Data->bHit = March.bHit;
	Data->bInside = March.bReferenceInside;
	Data->Version = Version;

	const int32 MaxIterations = FMath::Max(Request.MaxIterations, 2);
	Data->Orbit.SetNumUninitialized(MaxIterations + 1);
	// The orbit also carries the linear series skip (Jacobian products + validity radii), which lets each
	// sample start iterating where its perturbation stops being linear.
	const int32 Length = GenerateReferenceOrbit(Data->Center, Request.Power, MaxIterations,
		ReferenceEscapeRadius(Request.Bailout, Request.Power), Data->Orbit.GetData(),
		static_cast<FDDVec3*>(nullptr), Request.Bailout, FractalMath::DefaultSeriesTolerance);
	Data->Orbit.SetNum(Length);
	Data->GenerationMilliseconds = (FPlatformTime::Seconds() - StartTime) * 1000.0;

	UE_LOG(LogFractalReference, Verbose, TEXT("Reference v%llu: %d orbit points, hit=%d inside=%d, t=%.4g world, DE(camera)=%.4g world, %.2f ms"),
		Version, Length, Data->bHit ? 1 : 0, Data->bInside ? 1 : 0, March.ReferenceDistance, March.CameraDistanceEstimate, Data->GenerationMilliseconds);
	return Data;
}
