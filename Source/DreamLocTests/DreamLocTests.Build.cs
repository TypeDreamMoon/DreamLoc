// Copyright DreamMoon. All Rights Reserved.

using UnrealBuildTool;

// Automation tests. An editor module so that the live tests can use the game localization preview, which only the
// editor has. The translation tests run fake services on 127.0.0.1 with the engine's HTTP server: nothing leaves the
// machine.
public class DreamLocTests : ModuleRules
{
	public DreamLocTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "DreamLoc", "DreamLocEditor", "DreamLocTranslate", "Localization", "Projects",
			"HTTP", "HTTPServer", "Json", "Sockets", "Slate", "SlateCore", "InputCore",
		});
	}
}
