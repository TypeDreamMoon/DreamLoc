// Copyright DreamMoon. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

// The game target: what Tools/TestHost/Test-Package.ps1 packages, Development and Shipping, to check the baked
// translations and a mounted mod in a packaged game. The tests use the editor binary.
public class DreamLocTestHostTarget : TargetRules
{
	public DreamLocTestHostTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("DreamLocTestHost");
	}
}
