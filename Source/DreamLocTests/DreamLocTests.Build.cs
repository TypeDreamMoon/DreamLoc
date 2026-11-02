// Copyright DreamMoon. All Rights Reserved.

using UnrealBuildTool;

// Automation tests. An editor module so that the live tests can use the game localization preview, which only the
// editor has.
public class DreamLocTests : ModuleRules
{
	public DreamLocTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "DreamLoc", "DreamLocEditor", "Localization" });
	}
}
