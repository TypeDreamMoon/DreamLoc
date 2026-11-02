// Copyright DreamMoon. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

// The game target. Nothing builds it -- the tests use the editor binary -- but a code project needs one.
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
