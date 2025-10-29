#include "FractalGameMode.h"

#include "FractalPawn.h"

AFractalGameMode::AFractalGameMode()
{
    DefaultPawnClass = AFractalPawn::StaticClass();
}
