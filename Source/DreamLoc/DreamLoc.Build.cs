// Copyright DreamMoon. All Rights Reserved.

using UnrealBuildTool;

// The run time: the .dloc reader and writer, dloc.ini, and the text source that hands translations to the engine.
// Built into every configuration, Shipping included -- a mod's .dloc files are read by this module in a packaged game.
public class DreamLoc : ModuleRules
{
	public DreamLoc(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core" });
		PrivateDependencyModuleNames.AddRange(new string[] { "Projects" });
	}
}
