#include "FractalDisplayActor.h"
#include "PerturbationShader.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "FractalControlSubsystem.h"
#include "Engine/GameInstance.h"

AFractalDisplayActor::AFractalDisplayActor()
{
	PrimaryActorTick.bCanEverTick = true;

	bAutoRender = true;
	bIsRendering = false;

	// Create a plane mesh to display the fractal
	UStaticMeshComponent* PlaneMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DisplayPlane"));
	RootComponent = PlaneMesh;

	// Try to find the default plane mesh
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMeshAsset(TEXT("/Engine/BasicShapes/Plane"));
	if (PlaneMeshAsset.Succeeded())
	{
		PlaneMesh->SetStaticMesh(PlaneMeshAsset.Object);
	}
}

void AFractalDisplayActor::BeginPlay()
{
	Super::BeginPlay();

	// Create render target if not set
	if (!RenderTarget)
	{
		RenderTarget = NewObject<UTextureRenderTarget2D>(this);
		RenderTarget->InitAutoFormat(1024, 1024);
		RenderTarget->UpdateResourceImmediate(true);
		
		UE_LOG(LogTemp, Warning, TEXT("FractalDisplayActor: Created default 1024x1024 render target"));
	}

	// Create dynamic material to display the render target
	UStaticMeshComponent* PlaneMesh = Cast<UStaticMeshComponent>(RootComponent);
	if (PlaneMesh && RenderTarget)
	{
		UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/DefaultMaterial"));
		if (BaseMaterial)
		{
			UMaterialInstanceDynamic* DynMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
			// Note: For a proper display, you'd create a custom material with a Texture parameter
			// For now, this just shows the structure
			PlaneMesh->SetMaterial(0, DynMaterial);
		}
	}

	// Initial render
	if (bAutoRender)
	{
		RenderFractal();
	}
}

void AFractalDisplayActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Auto-render each frame if enabled
	if (bAutoRender && !bIsRendering)
	{
		RenderFractal();
	}
}

void AFractalDisplayActor::RenderFractal()
{
	if (!RenderTarget)
	{
		UE_LOG(LogTemp, Error, TEXT("FractalDisplayActor: No render target set!"));
		return;
	}

	if (bIsRendering)
	{
		return; // Already rendering
	}

	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		UE_LOG(LogTemp, Error, TEXT("FractalDisplayActor: Could not get GameInstance!"));
		return;
	}

	UFractalControlSubsystem* FractalControl = GameInstance->GetSubsystem<UFractalControlSubsystem>();
	if (!FractalControl)
	{
		return;
	}
	
	const FFractalParameter& FractalParameters = FractalControl->GetFractalParameters();
	if (!FractalParameters.bEnabled)
	{
		return;
	}

	bIsRendering = true;

	// Setup parameters
	FPerturbationShaderDispatchParams Params(1, 1, 1);
	Params.ApplyFractalParameters(FractalParameters);
	Params.OutputRenderTarget = RenderTarget;

	// Dispatch the shader
	FPerturbationShaderInterface::Dispatch(Params, [this]()
	{
		bIsRendering = false;
		UE_LOG(LogTemp, Log, TEXT("FractalDisplayActor: Fractal rendered!"));
	});
}
