// Copyright DreamMoon. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

// The editor target the test runner builds. Shared build environment: the engine modules already in
// Engine/Binaries are linked as they are, and only the host's modules and the plugins in Plugins/ are compiled.
public class DreamLocTestHostEditorTarget : TargetRules
{
	public DreamLocTestHostEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		BuildEnvironment = TargetBuildEnvironment.Shared;
		ExtraModuleNames.Add("DreamLocTestHost");
		ExtraModuleNames.Add("DreamLocTestHostEditor");
	}
}
