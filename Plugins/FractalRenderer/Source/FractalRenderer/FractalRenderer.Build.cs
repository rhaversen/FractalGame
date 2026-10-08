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

		// Double-double arithmetic (FractalMath/DoubleDouble.h) needs exact IEEE semantics: request precise
		// floating point where UnrealBuildTool supports it (looked up by reflection so older engines still build).
		var FPSemanticsField = GetType().GetField("FPSemantics");
		var FPSemanticsProperty = GetType().GetProperty("FPSemantics");
		if (FPSemanticsField != null && FPSemanticsField.FieldType.IsEnum)
		{
			FPSemanticsField.SetValue(this, System.Enum.Parse(FPSemanticsField.FieldType, "Precise"));
		}
		else if (FPSemanticsProperty != null && FPSemanticsProperty.CanWrite && FPSemanticsProperty.PropertyType.IsEnum)
		{
			FPSemanticsProperty.SetValue(this, System.Enum.Parse(FPSemanticsProperty.PropertyType, "Precise"));
		}
	}
}
