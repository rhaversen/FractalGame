#include "FractalGameMode.h"

#include "FractalPawn.h"
#include "UI/FractalHUD.h"

AFractalGameMode::AFractalGameMode()
{
    DefaultPawnClass = AFractalPawn::StaticClass();
    HUDClass = AFractalHUD::StaticClass();
}
