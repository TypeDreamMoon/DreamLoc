// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocPublish.h"
#include "DreamLocSync.h"
#include "DreamLocTestUtil.h"
#include "DreamLocTextSource.h"
#include "Internationalization/LocalizedTextSourceTypes.h"
#include "Internationalization/TextLocalizationResource.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamLocPublishTestsLocal
{
	void WriteFile(const FString& InPath, const FString& InText)
	{
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(InPath), /*Tree*/ true);
		DreamLocTestUtil::WriteText(InPath, InText);
	}

	/** One namespace, three texts: translated, stale, untranslated. */
	FString MenuFile(const TCHAR* InCulture, const TCHAR* InStart, const TCHAR* InQuit)
	{
		FString Text = FString::Printf(TEXT("dloc 1\nculture %s\nnamespace Game.Menu\n\n"), InCulture);
		if (InStart != nullptr)
		{
			Text += FString::Printf(TEXT("Start {\n    src   \"开始\"\n    text  \"%s\"\n}\n\n"), InStart);
		}
		if (InQuit != nullptr)
		{
			Text += FString::Printf(TEXT("Quit {\n    prev  \"退出游戏\"\n    src   \"退出\"\n    text  \"%s\"\n}\n\n"), InQuit);
		}
		return Text + TEXT("Options {\n    src   \"选项\"\n}\n");
	}

	const FTextLocalizationResource::FEntry* FindEntry(const FTextLocalizationResource& InResource, const TCHAR* InKey)
	{
		return InResource.Entries.Find(FTextId(FTextKey(TEXT("Game.Menu")), FTextKey(InKey)));
	}

	FString TextOf(const FTextLocalizationResource& InResource, const TCHAR* InKey)
	{
		const FTextLocalizationResource::FEntry* Entry = FindEntry(InResource, InKey);
		return Entry != nullptr && Entry->LocalizedString.IsValid() ? *Entry->LocalizedString : FString(TEXT("<none>"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocBakeTest, "DreamLoc.Bake.ALocresPerCultureAndTheSourceCulturesOwn",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocBakeTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocPublishTestsLocal;
	const FString Work = DreamLocTestUtil::MakeTempDirectory(TEXT("Bake"));
	ON_SCOPE_EXIT { DreamLocTestUtil::DeleteDirectory(Work); };
	WriteFile(Work / TEXT("Plugin/en/Menu.dloc"), MenuFile(TEXT("en"), TEXT("Begin"), nullptr));
	WriteFile(Work / TEXT("Project/en/Menu.dloc"), MenuFile(TEXT("en"), TEXT("Start"), TEXT("Quit")));
	WriteFile(Work / TEXT("Project/pt-BR/Menu.dloc"), MenuFile(TEXT("pt-BR"), TEXT("Iniciar"), nullptr));
	WriteFile(Work / TEXT("Project/pt-PT/Menu.dloc"), TEXT("dloc 1\nculture pt-PT\nnamespace Game.Menu\n\nOptions {\n    src   \"选项\"\n    text  \"Opções\"\n}\n"));

	DreamLocPublish::FBakeOptions Options;
	Options.OutputDirectory = Work / TEXT("Baked");
	Options.Layers = { FDreamLocLayer{ 0, TEXT("Plugin.X"), Work / TEXT("Plugin") }, FDreamLocLayer{ 1, TEXT("Project"), Work / TEXT("Project") } };
	FDreamLocSettings Settings;
	Settings.TargetCultures = { TEXT("en"), TEXT("pt-PT") };
	Settings.Fallbacks.Emplace(TEXT("pt-PT"), TEXT("pt-BR"));
	Options.Settings = Settings;
	Options.bCheckConfig = false;
	const DreamLocPublish::FBakeResult Result = DreamLocPublish::Bake(Options);
	TestTrue(TEXT("baked"), Result.bSucceeded);
	TestEqual(TEXT("two cultures, the source's file and the meta file"), Result.Files.Num(), 4);

	FTextLocalizationResource English;
	TestTrue(TEXT("the English file reads"), English.LoadFromFile(Work / TEXT("Baked/en/DreamLoc.locres"), 0));
	TestEqual(TEXT("the project's translation over the plugin's"), TextOf(English, TEXT("Start")), FString(TEXT("Start")));
	const FTextLocalizationResource::FEntry* Quit = FindEntry(English, TEXT("Quit"));
	TestTrue(TEXT("a stale translation is under the hash of its prev"), Quit != nullptr && Quit->SourceStringHash == FTextLocalizationResource::HashString(TEXT("退出游戏")));
	TestEqual(TEXT("no untranslated text"), TextOf(English, TEXT("Options")), FString(TEXT("<none>")));

	FTextLocalizationResource Portuguese;
	Portuguese.LoadFromFile(Work / TEXT("Baked/pt-PT/DreamLoc.locres"), 0);
	TestEqual(TEXT("pt-PT's own"), TextOf(Portuguese, TEXT("Options")), FString(TEXT("Opções")));
	TestEqual(TEXT("and pt-BR's for what pt-PT lacks, as dloc.ini says"), TextOf(Portuguese, TEXT("Start")), FString(TEXT("Iniciar")));

	FTextLocalizationResource Native;
	Native.LoadFromFile(Work / TEXT("Baked/zh-Hans/DreamLoc.locres"), 0);
	TestEqual(TEXT("the source culture's file maps every text to its source"), TextOf(Native, TEXT("Options")), FString(TEXT("选项")));
	TestEqual(TEXT("every text"), Native.Entries.Num(), 3);

	FTextLocalizationMetaDataResource MetaData;
	TestTrue(TEXT("the meta file reads"), MetaData.LoadFromFile(Work / TEXT("Baked/DreamLoc.locmeta")));
	TestEqual(TEXT("it names the source culture"), MetaData.NativeCulture, FString(TEXT("zh-Hans")));
	TestEqual(TEXT("and its file"), MetaData.NativeLocRes, FString(TEXT("zh-Hans/DreamLoc.locres")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocBakedLayerTest, "DreamLoc.Bake.APackagedGameReadsTheBakedFilesAndModsGoOverThem",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocBakedLayerTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocPublishTestsLocal;
	const FString Work = DreamLocTestUtil::MakeTempDirectory(TEXT("BakedLayer"));
	ON_SCOPE_EXIT { DreamLocTestUtil::DeleteDirectory(Work); };
	WriteFile(Work / TEXT("Project/en/Menu.dloc"), MenuFile(TEXT("en"), TEXT("Start"), TEXT("Quit")));
	DreamLocPublish::FBakeOptions Options;
	Options.OutputDirectory = Work / TEXT("Baked");
	Options.Layers = { FDreamLocLayer{ 1, TEXT("Project"), Work / TEXT("Project") } };
	FDreamLocSettings Settings;
	Settings.TargetCultures = { TEXT("en") };
	Options.Settings = Settings;
	Options.bCheckConfig = false;
	TestTrue(TEXT("baked"), DreamLocPublish::Bake(Options).bSucceeded);

	// A mod: Polish of its own, and an English text over the project's.
	WriteFile(Work / TEXT("Mod/pl/Menu.dloc"), MenuFile(TEXT("pl"), TEXT("Rozpocznij"), nullptr));
	WriteFile(Work / TEXT("Mod/en/Menu.dloc"), TEXT("dloc 1\nculture en\nnamespace Game.Menu\n\nOptions {\n    src   \"选项\"\n    text  \"Settings\"\n}\n"));

	FDreamLocLayer Baked{ 1, TEXT("Project"), Work / TEXT("Baked") };
	Baked.bBaked = true;
	FDreamLocTextSource Source({ Baked, FDreamLocLayer{ 2, TEXT("Mod.Test"), Work / TEXT("Mod") } }, Settings);

	FTextLocalizationResource English;
	Source.LoadLocalizedResources(ELocalizationLoadFlags::Game, TArray<FString>{ TEXT("en") }, English, English);
	TestEqual(TEXT("the baked translation"), TextOf(English, TEXT("Start")), FString(TEXT("Start")));
	TestEqual(TEXT("the mod's, where the project has none"), TextOf(English, TEXT("Options")), FString(TEXT("Settings")));
	const FTextLocalizationResource::FEntry* Quit = FindEntry(English, TEXT("Quit"));
	TestTrue(TEXT("a baked stale translation stays stale"), Quit != nullptr && Quit->SourceStringHash == FTextLocalizationResource::HashString(TEXT("退出游戏")));

	FTextLocalizationResource Polish;
	Source.LoadLocalizedResources(ELocalizationLoadFlags::Game, TArray<FString>{ TEXT("pl") }, Polish, Polish);
	TestEqual(TEXT("the mod's language"), TextOf(Polish, TEXT("Start")), FString(TEXT("Rozpocznij")));
	const FTextLocalizationResource::FEntry* Back = FindEntry(Polish, TEXT("Quit"));
	TestTrue(TEXT("and every baked text maps to its source below it, for going back to the source language"),
		Back != nullptr && Back->LocalizedString.IsValid() && *Back->LocalizedString == TEXT("退出") && Back->Priority >= FDreamLocTextSource::MakeSourceEntryPriority(63));

	TSet<FString> Cultures;
	Source.GetLocalizedCultureNames(ELocalizationLoadFlags::Game, Cultures);
	TestTrue(TEXT("the baked cultures and the mod's are offered"), Cultures.Contains(TEXT("en")) && Cultures.Contains(TEXT("zh-Hans")) && Cultures.Contains(TEXT("pl")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocPruneTest, "DreamLoc.Prune.DeletesRemovedBlocksAndTheFilesLeftEmpty",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocPruneTest::RunTest(const FString& Parameters)
{
	TMap<FString, TMap<FString, FString>> Files;
	Files.FindOrAdd(TEXT("en")).Add(TEXT("Live.dloc"), TEXT("dloc 1\nculture en\nnamespace N\n\nA {\n    src   \"一\"\n}\n\nremoved {\n    namespace N\n    B {\n        src   \"二\"\n    }\n}\n"));
	Files.FindOrAdd(TEXT("en")).Add(TEXT("Gone.dloc"), TEXT("dloc 1\nculture en\nnamespace N\n\nremoved {\n    namespace N\n    C {\n        src   \"三\"\n    }\n}\n"));
	Files.FindOrAdd(TEXT("en")).Add(TEXT("Clean.dloc"), TEXT("dloc 1\nculture en\nnamespace N\n\nD {\n    src   \"四\"\n}\n"));
	const FDreamLocSyncOutput Output = DreamLocPublish::Prune(Files);
	TestEqual(TEXT("two removed entries"), Output.Removed, 2);
	const TMap<FString, TOptional<FString>>* Changes = Output.Changes.Find(TEXT("en"));
	TestTrue(TEXT("the live file loses its removed block"), Changes != nullptr && Changes->Contains(TEXT("Live.dloc")) && (*Changes)[TEXT("Live.dloc")].IsSet()
		&& !(*Changes)[TEXT("Live.dloc")].GetValue().Contains(TEXT("removed")));
	TestTrue(TEXT("the file with nothing else goes"), Changes != nullptr && Changes->Contains(TEXT("Gone.dloc")) && !(*Changes)[TEXT("Gone.dloc")].IsSet());
	TestTrue(TEXT("a file without one is left alone"), Changes != nullptr && !Changes->Contains(TEXT("Clean.dloc")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocFreezeTest, "DreamLoc.Freeze.AShippedKeyThatIsGoneStopsSyncAndARenameDoesNot",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocFreezeTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocPublishTestsLocal;
	const FString Work = DreamLocTestUtil::MakeTempDirectory(TEXT("Freeze"));
	ON_SCOPE_EXIT { DreamLocTestUtil::DeleteDirectory(Work); };
	TMap<FString, TMap<FString, FString>> Files;
	Files.FindOrAdd(TEXT("en")).Add(TEXT("DUI/Menu.dloc"), TEXT("dloc 1\nculture en\nnamespace /Game/WBP_Menu\n\nTitle.Text {\n    at    DUI/Menu.dui#Title\n    src   \"菜单\"\n    text  \"Menu\"\n}\n"));
	int32 Keys = 0;
	TestTrue(TEXT("frozen"), DreamLocPublish::Freeze(Work, Files, Keys));
	TestEqual(TEXT("one key"), Keys, 1);
	const TSet<FString> Frozen = DreamLocPublish::ReadFrozen(Work);
	TestTrue(TEXT("read back"), Frozen.Contains(TEXT("/Game/WBP_Menu\x01Title.Text")));

	auto Input = [&Files, &Frozen](FDreamLocExtractEntry InEntry)
	{
		FDreamLocSyncInput Result;
		FDreamLocExtractManifest Manifest;
		Manifest.Provider = TEXT("DreamGUI");
		Manifest.Source = TEXT("DUI/Menu.dui");
		Manifest.FindOrAddNamespace(TEXT("/Game/WBP_Menu")).Entries.Add(MoveTemp(InEntry));
		Result.Manifests.Add(Manifest);
		Result.Files = Files;
		Result.Cultures = { TEXT("en") };
		Result.SourceExists = [](const FString&) { return true; };
		Result.Frozen = Frozen;
		return Result;
	};
	FDreamLocExtractEntry Other;
	Other.Key = TEXT("Heading.Text");
	Other.Source = TEXT("目录");
	const FDreamLocSyncOutput Gone = DreamLocSync::Run(Input(Other));
	TestTrue(TEXT("the frozen key is gone: blocked"), Gone.bBlocked && Gone.Messages.ContainsByPredicate([](const FDreamLocReportLine& InLine)
	{
		return InLine.Diagnostic.Code == DreamLocCode::FrozenKeyGone;
	}));
	TestEqual(TEXT("and nothing written"), Gone.Changes.Num(), 0);

	FDreamLocExtractEntry Renamed;
	Renamed.Key = TEXT("Heading.Text");
	Renamed.Source = TEXT("菜单");
	Renamed.RenamedFrom = TEXT("Title.Text");
	TestFalse(TEXT("renamed: its translation lives on, so it is fine"), DreamLocSync::Run(Input(Renamed)).bBlocked);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
