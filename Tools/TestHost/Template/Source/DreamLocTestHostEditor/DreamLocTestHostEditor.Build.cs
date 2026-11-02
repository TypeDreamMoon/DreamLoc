// Copyright DreamMoon. All Rights Reserved.

using UnrealBuildTool;

// The end-to-end check: a .dui compiled by DreamGUI, a UMG text block and a C++ text, all translated by DreamLoc in
// the editor's game preview. An editor module of the host's, so DreamLoc itself never depends on DreamGUI.
public class DreamLocTestHostEditor : ModuleRules
{
	public DreamLocTestHostEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UnrealEd",
			"UMG",
			"UMGEditor",
			"AssetRegistry",
			"DreamGUI",
			"DreamGUIEditor",
			"DreamLoc",
			"DreamLocEditor",
			"DreamLocTestHost",
		});
	}
}
