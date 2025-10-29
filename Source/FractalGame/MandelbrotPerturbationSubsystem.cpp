#include "MandelbrotPerturbationSubsystem.h"

#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Rendering/Texture2DResource.h"
#include "RHI.h"


DEFINE_LOG_CATEGORY_STATIC(LogMandelbrotPerturbation, Log, All);

namespace
{
	constexpr EPixelFormat OrbitTextureFormat = PF_A32B32G32R32F;
	const FName NameViewportCenter(TEXT("ViewportCenter"));
	const FName NamePower(TEXT("Power"));
	const FName NameMaxIterations(TEXT("MaxIterations"));
	const FName NameOrbitTexture(TEXT("OrbitTexture"));
}

void UMandelbrotPerturbationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ViewportCenter = FVector::ZeroVector;
	Power = 8.0;
	CachedOrbitLength = 0;
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

void UMandelbrotPerturbationSubsystem::SetViewportParameters(const FVector& Center, double InPower, int32 MaxIterations)
{
	ViewportCenter = Center;
	Power = InPower;

	const int32 ClampedIterations = FMath::Clamp(MaxIterations, 1, MaxSupportedIterations);
	if (ClampedIterations != CachedOrbitLength)
	{
		bOrbitDirty = true;
		CachedOrbitLength = ClampedIterations;
	}

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

void UMandelbrotPerturbationSubsystem::EnsureOrbitTexture(int32 DesiredLength)
{
	DesiredLength = FMath::Clamp(DesiredLength, 1, MaxSupportedIterations);

	if (OrbitTexture &&
		OrbitTexture->GetSizeX() == DesiredLength &&
		OrbitTexture->GetSizeY() == OrbitTextureRows)
	{
		return;
	}

	OrbitTexture = UTexture2D::CreateTransient(DesiredLength, OrbitTextureRows, OrbitTextureFormat);
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
	if (!bOrbitDirty && OrbitTexture &&
		OrbitTexture->GetSizeX() == OrbitLength &&
		OrbitTexture->GetSizeY() == OrbitTextureRows)
	{
		return;
	}

	OrbitLength = FMath::Clamp(OrbitLength, 1, MaxSupportedIterations);

	EnsureOrbitTexture(OrbitLength);

	if (!OrbitTexture)
	{
		return;
	}

	const int32 TextureWidth = OrbitLength;
	const int32 TextureHeight = OrbitTextureRows;
	TArray<FVector4f> OrbitData;
	OrbitData.SetNum(TextureWidth * TextureHeight);

	// Use double precision for the reference orbit center
	const double CRefX = static_cast<double>(ViewportCenter.X);
	const double CRefY = static_cast<double>(ViewportCenter.Y);
	const double CRefZ = static_cast<double>(ViewportCenter.Z);

	// Mandelbulb iteration with Jacobian tracking
	double zx = 0.0;
	double zy = 0.0;
	double zz = 0.0;
	double dr = 1.0;

	// Jacobian matrix (3x3) stored as columns
	double J00 = 1.0, J01 = 0.0, J02 = 0.0;
	double J10 = 0.0, J11 = 1.0, J12 = 0.0;
	double J20 = 0.0, J21 = 0.0, J22 = 1.0;

	for (int32 Iter = 0; Iter < OrbitLength; ++Iter)
	{
		// Store current state
		const FVector4f ReferenceSample(
			static_cast<float>(zx),
			static_cast<float>(zy),
			static_cast<float>(zz),
			0.0f);
		
		const FVector4f Column0(
			static_cast<float>(J00),
			static_cast<float>(J10),
			static_cast<float>(J20),
			0.0f);
		
		const FVector4f Column1(
			static_cast<float>(J01),
			static_cast<float>(J11),
			static_cast<float>(J21),
			0.0f);
		
		const FVector4f Column2(
			static_cast<float>(J02),
			static_cast<float>(J12),
			static_cast<float>(J22),
			0.0f);

		const int32 BaseIndex = Iter;
		OrbitData[BaseIndex] = ReferenceSample;
		OrbitData[TextureWidth + BaseIndex] = Column0;
		OrbitData[(2 * TextureWidth) + BaseIndex] = Column1;
		OrbitData[(3 * TextureWidth) + BaseIndex] = Column2;

		// Compute next iteration using Mandelbulb formula
		const double r = FMath::Sqrt(zx * zx + zy * zy + zz * zz);

		if (r > 10.0) // Bailout
		{
			// Fill remaining iterations with last valid state
			for (int32 RemIter = Iter + 1; RemIter < OrbitLength; ++RemIter)
			{
				const int32 RemIndex = RemIter;
				OrbitData[RemIndex] = ReferenceSample;
				OrbitData[TextureWidth + RemIndex] = Column0;
				OrbitData[(2 * TextureWidth) + RemIndex] = Column1;
				OrbitData[(3 * TextureWidth) + RemIndex] = Column2;
			}
			break;
		}

		// Spherical coordinates
		const double theta = FMath::Acos(FMath::Clamp(zz / r, -1.0, 1.0));
		const double phi = FMath::Atan2(zy, zx);

		// Update derivative magnitude used in distance estimator
		dr = FMath::Pow(r, Power - 1.0) * Power * dr + 1.0;

		// New position
		const double zr = FMath::Pow(r, Power);
		const double newTheta = theta * Power;
		const double newPhi = phi * Power;

		const double sinTheta = FMath::Sin(newTheta);
		const double cosTheta = FMath::Cos(newTheta);
		const double sinPhi = FMath::Sin(newPhi);
		const double cosPhi = FMath::Cos(newPhi);

		const double newZx = zr * sinTheta * cosPhi;
		const double newZy = zr * sinTheta * sinPhi;
		const double newZz = zr * cosTheta;
		
		// Update Jacobian (simplified - for full accuracy would need chain rule through spherical transform)
		// For now, use finite difference approximation or analytical derivative
		// This is a simplified Jacobian that assumes local linearity
		const double scale = Power * FMath::Pow(r, Power - 1.0);

		double newJ00 = J00 * scale;
		double newJ01 = J01 * scale;
		double newJ02 = J02 * scale;
		double newJ10 = J10 * scale;
		double newJ11 = J11 * scale;
		double newJ12 = J12 * scale;
		double newJ20 = J20 * scale;
		double newJ21 = J21 * scale;
		double newJ22 = J22 * scale;

		zx = newZx + CRefX;
		zy = newZy + CRefY;
		zz = newZz + CRefZ;
		
		J00 = newJ00;
		J01 = newJ01;
		J02 = newJ02;
		J10 = newJ10;
		J11 = newJ11;
		J12 = newJ12;
		J20 = newJ20;
		J21 = newJ21;
		J22 = newJ22;
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
	const int32 Height = OrbitTexture->GetSizeY();
	if (Width * Height != OrbitData.Num())
	{
		UE_LOG(LogMandelbrotPerturbation, Warning, TEXT("Orbit data length %d does not match texture dimensions %dx%d"), OrbitData.Num(), Width, Height);
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
		static_cast<float>(ViewportCenter.Z),
		0.0f));

	TargetMaterial->SetScalarParameterValue(NamePower, static_cast<float>(Power));
	TargetMaterial->SetScalarParameterValue(NameMaxIterations, static_cast<float>(CachedOrbitLength));

	if (OrbitTexture)
	{
		TargetMaterial->SetTextureParameterValue(NameOrbitTexture, OrbitTexture);
	}
}
