// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class FractalGame : ModuleRules
{
	public FractalGame(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"RHI",
			"FractalRenderer",
		});
	}
}
