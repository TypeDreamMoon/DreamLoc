// Copyright DreamMoon. All Rights Reserved.

using UnrealBuildTool;

// The editor side: watches the DLoc/ folders and reloads translations when a file changes, the console commands,
// and the commandlet the command line (Tools/dloc.ps1) runs.
public class DreamLocEditor : ModuleRules
{
	public DreamLocEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "DreamLoc" });
		PrivateDependencyModuleNames.AddRange(new string[] { "DirectoryWatcher", "Projects" });
	}
}
