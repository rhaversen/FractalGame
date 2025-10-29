#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "FractalHUD.generated.h"

UCLASS()
class FRACTALGAME_API AFractalHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
};
