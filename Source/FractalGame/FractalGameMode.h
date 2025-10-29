#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "FractalGameMode.generated.h"

UCLASS()
class FRACTALGAME_API AFractalGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    AFractalGameMode();

    /**
     * Sets the post-process material for perturbation rendering.
     * This must be called to connect the material to the subsystem.
     */
    UFUNCTION(BlueprintCallable, Category = "Fractals|Perturbation")
    void SetPerturbationMaterial(class UMaterialInstanceDynamic* MaterialInstance);

protected:
    virtual void BeginPlay() override;
};
