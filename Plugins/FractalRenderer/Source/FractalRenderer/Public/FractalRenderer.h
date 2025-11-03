#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FFractalSceneViewExtension;

class FFractalRendererModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Get the scene view extension instance */
	TSharedPtr<FFractalSceneViewExtension, ESPMode::ThreadSafe> GetSceneViewExtension() const { return SceneViewExtension; }

private:
	TSharedPtr<FFractalSceneViewExtension, ESPMode::ThreadSafe> SceneViewExtension;
	FDelegateHandle PostEngineInitHandle;

	void RegisterSceneViewExtension();
};
