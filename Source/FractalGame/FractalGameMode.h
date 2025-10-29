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

protected:
    virtual void BeginPlay() override;
};
