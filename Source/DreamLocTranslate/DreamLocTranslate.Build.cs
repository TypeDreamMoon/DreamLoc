// Copyright DreamMoon. All Rights Reserved.

using UnrealBuildTool;

// Translation in the editor: large models (OpenAI-compatible chat completions, local or in the cloud) and machine
// translation services fill in a culture's untranslated and stale entries, the checks look at each, and what passes
// is written back as `state machine`. The flow is apart from the window, so a command line can run it later.
// Editor only: never in a packaged game.
public class DreamLocTranslate : ModuleRules
{
	public DreamLocTranslate(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "DreamLoc", "DreamLocEditor", "Slate", "SlateCore" });
		PrivateDependencyModuleNames.AddRange(new string[] { "ApplicationCore", "DirectoryWatcher", "Engine", "HTTP", "Json", "Projects", "InputCore", "Settings", "UnrealEd", "ToolMenus" });
	}
}
