using UnrealBuildTool;

public class FractalRenderer : ModuleRules
{
	public FractalRenderer(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateIncludePaths.AddRange(new string[]
		{
			"Runtime/Renderer/Private",
			"FractalRenderer/Private"
		});

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"RenderCore",
			"RHI",
			"Projects",
			"Renderer"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// ... add private dependencies that you statically link with here ...
		});

		if (Target.bBuildEditor == true)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
	}
}
