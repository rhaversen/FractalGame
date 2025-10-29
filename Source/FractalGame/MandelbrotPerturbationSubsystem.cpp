#include "MandelbrotPerturbationSubsystem.h"

#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Rendering/Texture2DResource.h"
#include "RHI.h"

#include <complex>

DEFINE_LOG_CATEGORY_STATIC(LogMandelbrotPerturbation, Log, All);

namespace
{
	constexpr EPixelFormat OrbitTextureFormat = PF_A32B32G32R32F;
	const FName NameViewportCenter(TEXT("ViewportCenter"));
	const FName NameViewportScaleAspect(TEXT("ViewportScaleAspect"));
	const FName NameReferenceHi(TEXT("ReferenceHi"));
	const FName NameReferenceLo(TEXT("ReferenceLo"));
	const FName NameMaxIterations(TEXT("MaxIterations"));
	const FName NameOrbitTexture(TEXT("OrbitTexture"));
}

void UMandelbrotPerturbationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ViewportCenter = FVector2D::ZeroVector;
	ViewportScale = 3.0;
	ViewportAspect = 1.0;
	CachedOrbitLength = 0;
	ReferenceCenterX = 0.0L;
	ReferenceCenterY = 0.0L;
	ReferenceCenterHi = FVector2D::ZeroVector;
	ReferenceCenterLo = FVector2D::ZeroVector;
	bOrbitDirty = true;
}

void UMandelbrotPerturbationSubsystem::Deinitialize()
{
	TargetMaterial = nullptr;
	OrbitTexture = nullptr;
	Super::Deinitialize();
}

void UMandelbrotPerturbationSubsystem::SetTargetMaterial(UMaterialInstanceDynamic* InMaterial)
{
	TargetMaterial = InMaterial;
	if (TargetMaterial.IsValid())
	{
		PushParametersToMaterial();
	}
}

void UMandelbrotPerturbationSubsystem::SetViewportParameters(const FVector2D& Center, double Scale, double Aspect, int32 MaxIterations)
{
	ViewportCenter = Center;
	ViewportScale = Scale;
	ViewportAspect = Aspect > 0.0 ? Aspect : 1.0;

	const int32 ClampedIterations = FMath::Clamp(MaxIterations, 1, MaxSupportedIterations);
	if (ClampedIterations != CachedOrbitLength)
	{
		bOrbitDirty = true;
		CachedOrbitLength = ClampedIterations;
	}

	ReferenceCenterX = static_cast<long double>(Center.X);
	ReferenceCenterY = static_cast<long double>(Center.Y);
	UpdateReferenceSplit();

	BuildOrbit(CachedOrbitLength);
	PushParametersToMaterial();
}

void UMandelbrotPerturbationSubsystem::ForceRebuild()
{
	bOrbitDirty = true;
	CachedOrbitLength = FMath::Max(CachedOrbitLength, 1);
	BuildOrbit(CachedOrbitLength);
	PushParametersToMaterial();
}

FVector4 UMandelbrotPerturbationSubsystem::GetViewportVector() const
{
	return FVector4(
		static_cast<float>(ViewportCenter.X),
		static_cast<float>(ViewportCenter.Y),
		static_cast<float>(ViewportScale),
		static_cast<float>(ViewportAspect));
}

FVector4 UMandelbrotPerturbationSubsystem::GetReferenceVector() const
{
	return FVector4(
		static_cast<float>(ReferenceCenterHi.X),
		static_cast<float>(ReferenceCenterHi.Y),
		static_cast<float>(ReferenceCenterLo.X),
		static_cast<float>(ReferenceCenterLo.Y));
}

void UMandelbrotPerturbationSubsystem::EnsureOrbitTexture(int32 DesiredLength)
{
	DesiredLength = FMath::Clamp(DesiredLength, 1, MaxSupportedIterations);

	if (OrbitTexture && OrbitTexture->GetSizeX() == DesiredLength)
	{
		return;
	}

	OrbitTexture = UTexture2D::CreateTransient(DesiredLength, 1, OrbitTextureFormat);
	if (!OrbitTexture)
	{
		UE_LOG(LogMandelbrotPerturbation, Error, TEXT("Failed to allocate orbit texture with length %d"), DesiredLength);
		return;
	}

	OrbitTexture->MipGenSettings = TMGS_NoMipmaps;
	OrbitTexture->CompressionSettings = TC_HDR;
	OrbitTexture->SRGB = false;
	OrbitTexture->Filter = TF_Nearest;
	OrbitTexture->AddressX = TA_Clamp;
	OrbitTexture->AddressY = TA_Clamp;
	OrbitTexture->UpdateResource();
}

void UMandelbrotPerturbationSubsystem::BuildOrbit(int32 OrbitLength)
{
	if (!bOrbitDirty && OrbitTexture)
	{
		return;
	}

	OrbitLength = FMath::Clamp(OrbitLength, 1, MaxSupportedIterations);

	EnsureOrbitTexture(OrbitLength);

	if (!OrbitTexture)
	{
		return;
	}

	TArray<FVector4f> OrbitData;
	OrbitData.SetNum(OrbitLength);

	std::complex<long double> CRef(ReferenceCenterX, ReferenceCenterY);
	std::complex<long double> Z(0.0L, 0.0L);
	std::complex<long double> DZ(0.0L, 0.0L);

	for (int32 Iter = 0; Iter < OrbitLength; ++Iter)
	{
		OrbitData[Iter] = FVector4f(
			static_cast<float>(Z.real()),
			static_cast<float>(Z.imag()),
			static_cast<float>(DZ.real()),
			static_cast<float>(DZ.imag()));

		DZ = (2.0L * Z * DZ) + std::complex<long double>(1.0L, 0.0L);
		Z = (Z * Z) + CRef;
	}

	UploadOrbitData(OrbitData);
	bOrbitDirty = false;
}

void UMandelbrotPerturbationSubsystem::UploadOrbitData(const TArray<FVector4f>& OrbitData)
{
	if (!OrbitTexture || OrbitData.Num() == 0)
	{
		return;
	}

	const int32 Width = OrbitTexture->GetSizeX();
	if (Width != OrbitData.Num())
	{
		UE_LOG(LogMandelbrotPerturbation, Warning, TEXT("Orbit data length %d does not match texture width %d"), OrbitData.Num(), Width);
		return;
	}

	// Use platform data for safer texture updates
	FTexture2DMipMap& Mip = OrbitTexture->GetPlatformData()->Mips[0];
	void* MipData = Mip.BulkData.Lock(LOCK_READ_WRITE);
	
	const SIZE_T DataSize = static_cast<SIZE_T>(OrbitData.Num()) * sizeof(FVector4f);
	FMemory::Memcpy(MipData, OrbitData.GetData(), DataSize);
	
	Mip.BulkData.Unlock();
	
	// Update the texture resource on the render thread
	OrbitTexture->UpdateResource();
}

void UMandelbrotPerturbationSubsystem::PushParametersToMaterial()
{
	if (!TargetMaterial.IsValid())
	{
		return;
	}

	TargetMaterial->SetVectorParameterValue(NameViewportCenter, FLinearColor(
		static_cast<float>(ViewportCenter.X),
		static_cast<float>(ViewportCenter.Y),
		0.0f,
		0.0f));

	TargetMaterial->SetVectorParameterValue(NameViewportScaleAspect, FLinearColor(
		static_cast<float>(ViewportScale),
		static_cast<float>(ViewportAspect),
		0.0f,
		0.0f));

	TargetMaterial->SetVectorParameterValue(NameReferenceHi, FLinearColor(
		static_cast<float>(ReferenceCenterHi.X),
		static_cast<float>(ReferenceCenterHi.Y),
		0.0f,
		0.0f));

	TargetMaterial->SetVectorParameterValue(NameReferenceLo, FLinearColor(
		static_cast<float>(ReferenceCenterLo.X),
		static_cast<float>(ReferenceCenterLo.Y),
		0.0f,
		0.0f));

	TargetMaterial->SetScalarParameterValue(NameMaxIterations, static_cast<float>(CachedOrbitLength));

	if (OrbitTexture)
	{
		TargetMaterial->SetTextureParameterValue(NameOrbitTexture, OrbitTexture);
	}
}

void UMandelbrotPerturbationSubsystem::UpdateReferenceSplit()
{
	const double CenterX = static_cast<double>(ReferenceCenterX);
	const double CenterY = static_cast<double>(ReferenceCenterY);

	const float HiX = static_cast<float>(CenterX);
	const float HiY = static_cast<float>(CenterY);

	const float LoX = static_cast<float>(CenterX - static_cast<double>(HiX));
	const float LoY = static_cast<float>(CenterY - static_cast<double>(HiY));

	ReferenceCenterHi = FVector2D(HiX, HiY);
	ReferenceCenterLo = FVector2D(LoX, LoY);
}
