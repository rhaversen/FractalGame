#include "MandelbrotPerturbationPostProcessActor.h"

#include "FractalGameMode.h"
#include "MandelbrotPerturbationSubsystem.h"

#include "Components/PostProcessComponent.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "Engine/GameInstance.h"

AMandelbrotPerturbationPostProcessActor::AMandelbrotPerturbationPostProcessActor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = true;

    USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    RootComponent = SceneRoot;

    PostProcessComponent = CreateDefaultSubobject<UPostProcessComponent>(TEXT("PostProcessComponent"));
    PostProcessComponent->SetupAttachment(SceneRoot);
    PostProcessComponent->bUnbound = true;
    PostProcessComponent->bEnabled = true;
    PostProcessComponent->Priority = 10.0f; // Slightly higher priority to ensure the material renders last.

    BlendWeight = 1.0f;
    CameraPanScale = 0.001f;
    CameraZoomScale = 0.0f;
    IterationCount = 2048;
    bHasCachedCameraState = false;
    InitialViewLocation = FVector::ZeroVector;
    InitialViewportCenter = FVector::ZeroVector;
    LastPushedCenter = FVector(BIG_NUMBER, BIG_NUMBER, BIG_NUMBER);
    LastPushedPower = -1.0;
}

void AMandelbrotPerturbationPostProcessActor::BeginPlay()
{
    Super::BeginPlay();
    InitialiseMaterialBinding();
}

void AMandelbrotPerturbationPostProcessActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    UpdateViewportFromCamera();
}

#if WITH_EDITOR
void AMandelbrotPerturbationPostProcessActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    const FName PropertyName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;
    if (PropertyName == GET_MEMBER_NAME_CHECKED(AMandelbrotPerturbationPostProcessActor, PerturbationMaterial) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AMandelbrotPerturbationPostProcessActor, BlendWeight))
    {
        InitialiseMaterialBinding();
    }
}
#endif

void AMandelbrotPerturbationPostProcessActor::InitialiseMaterialBinding()
{
    if (!PostProcessComponent)
    {
        UE_LOG(LogTemp, Warning, TEXT("MandelbrotPerturbationPostProcessActor: Missing PostProcessComponent."));
        return;
    }

    // Remove previous blendables so we can rebuild from scratch.
    PostProcessComponent->Settings.WeightedBlendables.Array.Empty();
    DynamicMaterialInstance = nullptr;

    if (!PerturbationMaterial)
    {
        UE_LOG(LogTemp, Warning, TEXT("MandelbrotPerturbationPostProcessActor: PerturbationMaterial is not assigned."));
        bHasCachedCameraState = false;
        return;
    }

    DynamicMaterialInstance = UMaterialInstanceDynamic::Create(PerturbationMaterial, this);
    if (!DynamicMaterialInstance)
    {
        UE_LOG(LogTemp, Warning, TEXT("MandelbrotPerturbationPostProcessActor: Failed to create dynamic material instance."));
        return;
    }

    PostProcessComponent->Settings.AddBlendable(DynamicMaterialInstance, BlendWeight);
    PostProcessComponent->BlendWeight = BlendWeight;

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    UGameInstance* GameInstance = World->GetGameInstance();
    if (!GameInstance)
    {
        UE_LOG(LogTemp, Warning, TEXT("MandelbrotPerturbationPostProcessActor: No GameInstance available."));
        return;
    }

    // Prefer the explicit helper on the FractalGameMode, but fall back to talking to the subsystem directly.
    if (AFractalGameMode* FractalGameMode = World->GetAuthGameMode<AFractalGameMode>())
    {
        FractalGameMode->SetPerturbationMaterial(DynamicMaterialInstance);
    }
    else if (UMandelbrotPerturbationSubsystem* Subsystem = GameInstance->GetSubsystem<UMandelbrotPerturbationSubsystem>())
    {
        Subsystem->SetTargetMaterial(DynamicMaterialInstance);
        Subsystem->ForceRebuild();
    }

    bHasCachedCameraState = false;
}

void AMandelbrotPerturbationPostProcessActor::UpdateViewportFromCamera()
{
    if (!PostProcessComponent || !DynamicMaterialInstance)
    {
        return;
    }

    if (IterationCount <= 0)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    UGameInstance* GameInstance = World->GetGameInstance();
    if (!GameInstance)
    {
        return;
    }

    UMandelbrotPerturbationSubsystem* Subsystem = GameInstance->GetSubsystem<UMandelbrotPerturbationSubsystem>();
    if (!Subsystem)
    {
        return;
    }

    APlayerController* PlayerController = World->GetFirstPlayerController();
    if (!PlayerController)
    {
        return;
    }

    FVector ViewLocation;
    FRotator ViewRotation;
    PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);

    if (!bHasCachedCameraState)
    {
        InitialViewLocation = ViewLocation;
        InitialViewportCenter = Subsystem->GetViewportCenter();
        LastPushedCenter = FVector(BIG_NUMBER, BIG_NUMBER, BIG_NUMBER);
        LastPushedPower = -1.0;
        bHasCachedCameraState = true;
    }

    // For 3D Mandelbulb, use camera position directly scaled by CameraPanScale
    const FVector ViewDelta = ViewLocation - InitialViewLocation;
    FVector NewCenter = InitialViewportCenter + (ViewDelta * static_cast<double>(CameraPanScale));

    double NewPower = Subsystem->GetPower();

    const bool bCenterChanged = FVector::DistSquared(NewCenter, LastPushedCenter) > KINDA_SMALL_NUMBER;
    const bool bPowerChanged = !FMath::IsNearlyEqual(static_cast<float>(NewPower), static_cast<float>(LastPushedPower), KINDA_SMALL_NUMBER);

    if (!bCenterChanged && !bPowerChanged)
    {
        return;
    }

    Subsystem->SetViewportParameters(NewCenter, NewPower, IterationCount);

    LastPushedCenter = NewCenter;
    LastPushedPower = NewPower;
}
