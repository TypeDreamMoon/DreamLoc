// Copyright DreamMoon. All Rights Reserved.

using UnrealBuildTool;

// The host's own module: a code project needs a primary game module, and the code project is what gives the host
// an editor target that builds the plugins in Plugins/. It also holds the texts the gather finds in C++, and the
// probe a packaged build of the host runs (-DreamLocProbe=<file>) to show which translations it gets.
public class DreamLocTestHost : ModuleRules
{
	public DreamLocTestHost(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
		PrivateDependencyModuleNames.AddRange(new string[] { "UMG", "Json", "Projects", "DreamLoc" });
	}
}
