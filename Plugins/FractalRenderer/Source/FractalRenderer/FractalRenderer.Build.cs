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
			"Projects"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Renderer"
		});

		if (Target.bBuildEditor == true)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
	}
}
