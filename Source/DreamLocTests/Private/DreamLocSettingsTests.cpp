// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocSettings.h"
#include "DreamLocTestUtil.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocSettingsParseTest, "DreamLoc.Settings.Parse",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocSettingsParseTest::RunTest(const FString& Parameters)
{
	{
		TArray<FDreamLocDiagnostic> Diagnostics;
		const FDreamLocSettings Defaults = FDreamLocSettings::Parse(FString(), Diagnostics);
		TestEqual(TEXT("the source is Chinese by default"), Defaults.SourceCulture, FString(TEXT("zh-Hans")));
		TestEqual(TEXT("no targets by default"), Defaults.TargetCultures.Num(), 0);
		TestEqual(TEXT("no extra fallbacks by default"), Defaults.Fallbacks.Num(), 0);
	}

	const FString Text = TEXT(R"ini(; DreamLoc settings
[DreamLoc]
SourceCulture=zh-Hans
TargetCultures=en, ja ; two to start with
+TargetCultures=ko
# a comment of the other kind

[Fallback]
pt-PT=pt-BR         ; 欧洲葡语缺翻译时先用巴西葡语
es-ES=es-419
)ini");
	TArray<FDreamLocDiagnostic> Diagnostics;
	const FDreamLocSettings Settings = FDreamLocSettings::Parse(Text, Diagnostics);
	TestEqual(TEXT("nothing to report"), DreamLocTestUtil::Describe(Diagnostics), FString());
	TestEqual(TEXT("source"), Settings.SourceCulture, FString(TEXT("zh-Hans")));
	TestEqual(TEXT("targets, the list and the added one"), FString::Join(Settings.TargetCultures, TEXT(",")), FString(TEXT("en,ja,ko")));
	TestEqual(TEXT("two fallbacks"), Settings.Fallbacks.Num(), 2);
	if (Settings.Fallbacks.Num() == 2)
	{
		TestEqual(TEXT("first fallback from"), Settings.Fallbacks[0].Key, FString(TEXT("pt-PT")));
		TestEqual(TEXT("first fallback to, its comment stripped"), Settings.Fallbacks[0].Value, FString(TEXT("pt-BR")));
	}

	TArray<FDreamLocDiagnostic> Bad;
	FDreamLocSettings::Parse(TEXT("[DreamLoc]\nSourceLanguage=en\nnot a line\n[Other]\n"), Bad);
	TestEqual(TEXT("an unknown key, a line that is not key=value, an unknown section"), DreamLocTestUtil::Describe(Bad),
		FString(TEXT("DLOC1016@2:1 DLOC1015@3:1 DLOC1016@4:1")));

	{
		TArray<FDreamLocDiagnostic> GatherDiagnostics;
		const FDreamLocSettings Defaults;
		TestEqual(TEXT("the gather searches Source by default"), FString::Join(Defaults.GatherSourcePaths, TEXT(",")), FString(TEXT("Source")));
		const FDreamLocSettings Gather = FDreamLocSettings::Parse(TEXT("[Gather]\nSourcePaths=Source/Game\n+SourcePaths=Plugins/Shop/Source\n+ExcludePaths=Content/Tests/*\nWhat=1\n"),
			GatherDiagnostics);
		TestEqual(TEXT("a list key replaces the default, + adds to it"), FString::Join(Gather.GatherSourcePaths, TEXT(",")),
			FString(TEXT("Source/Game,Plugins/Shop/Source")));
		TestEqual(TEXT("+ alone adds to the default"), Gather.GatherExcludePaths.Num(), 4);
		TestEqual(TEXT("an unknown [Gather] key"), DreamLocTestUtil::Describe(GatherDiagnostics), FString(TEXT("DLOC1016@5:1")));
	}
	{
		TArray<FDreamLocDiagnostic> PublishDiagnostics;
		TestFalse(TEXT("Source by default"), FDreamLocSettings().bPublishNative);
		TestTrue(TEXT("Native"), FDreamLocSettings::Parse(TEXT("[Publish]\nMode=Native\n"), PublishDiagnostics).bPublishNative);
		FDreamLocSettings::Parse(TEXT("[Publish]\nMode=Loose\nHow=1\n"), PublishDiagnostics);
		TestEqual(TEXT("a mode that is none, a key that is not read"), DreamLocTestUtil::Describe(PublishDiagnostics), FString(TEXT("DLOC1015@2:1 DLOC1016@3:1")));
	}
	{
		// A plugin's editor UI: the editor-only texts, the meta-data, and when its baked targets load.
		const FDreamLocSettings Defaults;
		TestFalse(TEXT("no editor-only texts by default"), Defaults.bGatherEditorOnlyData);
		TestFalse(TEXT("no meta-data by default"), Defaults.bGatherMetaData);
		TestEqual(TEXT("a plugin's targets load in the editor by default"), Defaults.PublishLoadingPolicy, FString(TEXT("Editor")));
		TArray<FDreamLocDiagnostic> PluginDiagnostics;
		const FDreamLocSettings Plugin = FDreamLocSettings::Parse(TEXT("[Gather]\nEditorOnlyData=true\nMetaData=True\n[Publish]\nLoadingPolicy=always\n"), PluginDiagnostics);
		TestEqual(TEXT("nothing to report"), DreamLocTestUtil::Describe(PluginDiagnostics), FString());
		TestTrue(TEXT("EditorOnlyData"), Plugin.bGatherEditorOnlyData);
		TestTrue(TEXT("MetaData, whatever its case"), Plugin.bGatherMetaData);
		TestEqual(TEXT("LoadingPolicy, in the engine's spelling"), Plugin.PublishLoadingPolicy, FString(TEXT("Always")));
		TArray<FDreamLocDiagnostic> BadDiagnostics;
		const FDreamLocSettings Wrong = FDreamLocSettings::Parse(TEXT("[Gather]\nMetaData=yes\n[Publish]\nLoadingPolicy=Never\n"), BadDiagnostics);
		TestEqual(TEXT("not a boolean, not a policy DreamLoc bakes"), DreamLocTestUtil::Describe(BadDiagnostics), FString(TEXT("DLOC1015@2:1 DLOC1015@4:1")));
		TestFalse(TEXT("the default kept"), Wrong.bGatherMetaData);
		TestEqual(TEXT("the default policy kept"), Wrong.PublishLoadingPolicy, FString(TEXT("Editor")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocSettingsFallbackTest, "DreamLoc.Settings.ExpandFallbacks",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocSettingsFallbackTest::RunTest(const FString& Parameters)
{
	const TArray<FString> Portuguese = { TEXT("pt-PT"), TEXT("pt") };
	const TArray<FString> English = { TEXT("en-US"), TEXT("en") };
	FDreamLocSettings Settings;
	TestEqual(TEXT("no fallbacks: the engine's list as it is"),
		FString::Join(Settings.ExpandFallbacks(Portuguese), TEXT(",")), FString(TEXT("pt-PT,pt")));

	Settings.Fallbacks.Emplace(TEXT("pt-PT"), TEXT("pt-BR"));
	Settings.Fallbacks.Emplace(TEXT("pt-BR"), TEXT("es-419"));
	Settings.Fallbacks.Emplace(TEXT("pt"), TEXT("pt-PT"));
	TestEqual(TEXT("after the engine's list, a fallback's own fallbacks after it, nothing twice"),
		FString::Join(Settings.ExpandFallbacks(Portuguese), TEXT(",")), FString(TEXT("pt-PT,pt,pt-BR,es-419")));
	TestEqual(TEXT("a culture with no fallback gains nothing"),
		FString::Join(Settings.ExpandFallbacks(English), TEXT(",")), FString(TEXT("en-US,en")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
