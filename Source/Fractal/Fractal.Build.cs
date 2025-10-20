// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class Fractal : ModuleRules
{
	public Fractal(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"ProceduralMeshComponent",
			"RenderCore",
			"RHI",
			"Projects"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		// Add subdirectories to include paths
		PublicIncludePaths.AddRange(new string[] {
			Path.Combine(ModuleDirectory, "Fractals"),
			Path.Combine(ModuleDirectory, "Fractals", "DE"),
			Path.Combine(ModuleDirectory, "Player"),
			Path.Combine(ModuleDirectory, "UI")
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
