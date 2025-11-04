#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "FractalDisplayActor.generated.h"

/**
 * Simple actor that renders a fractal to a render target and displays it
 */
UCLASS()
class FRACTALRENDERER_API AFractalDisplayActor : public AActor
{
	GENERATED_BODY()

public:
	AFractalDisplayActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	// Render target to draw fractal into
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal")
	UTextureRenderTarget2D* RenderTarget;

	// Auto-render each frame
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal")
	bool bAutoRender;

	// Render the fractal
	UFUNCTION(BlueprintCallable, Category = "Fractal")
	void RenderFractal();

private:
	bool bIsRendering;
};
