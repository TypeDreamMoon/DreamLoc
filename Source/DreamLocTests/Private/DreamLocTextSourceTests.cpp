// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocTextSource.h"
#include "DreamLocTestUtil.h"
#include "Internationalization/LocalizedTextSourceTypes.h"
#include "Internationalization/TextLocalizationResource.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamLocTextSourceTestsLocal
{
	/** A plugin layer, the project and a mod, as three DLoc folders under one temporary root. */
	struct FLayeredFixture
	{
		FString Root;
		TArray<FDreamLocLayer> Layers;

		FString PluginDirectory() const { return Root / TEXT("Plugin/DLoc"); }
		FString ProjectDirectory() const { return Root / TEXT("Project/DLoc"); }
		FString ModDirectory() const { return Root / TEXT("Mod/DLoc"); }

		explicit FLayeredFixture(const TCHAR* InName)
		{
			Root = DreamLocTestUtil::MakeTempDirectory(InName);
			Layers.Add(FDreamLocLayer{ 0, TEXT("Plugin.Test"), PluginDirectory() });
			Layers.Add(FDreamLocLayer{ 1, TEXT("Project"), ProjectDirectory() });
			Layers.Add(FDreamLocLayer{ 2, TEXT("Mod"), ModDirectory() });
		}

		~FLayeredFixture()
		{
			DreamLocTestUtil::DeleteDirectory(Root);
		}

		void Write(const FString& InDirectory, const TCHAR* InCulture, const TCHAR* InFile, const FString& InBody) const
		{
			DreamLocTestUtil::WriteText(InDirectory / InCulture / InFile,
				FString::Printf(TEXT("dloc 1\nculture %s\nnamespace NS\n\n"), InCulture) + InBody);
		}
	};

	FString Entry(const TCHAR* InKey, const TCHAR* InSource, const TCHAR* InText = nullptr, const TCHAR* InPrevious = nullptr)
	{
		FString Body = FString::Printf(TEXT("%s {\n"), InKey);
		if (InPrevious != nullptr)
		{
			Body += FString::Printf(TEXT("    prev  \"%s\"\n"), InPrevious);
		}
		Body += FString::Printf(TEXT("    src   \"%s\"\n"), InSource);
		if (InText != nullptr)
		{
			Body += FString::Printf(TEXT("    text  \"%s\"\n"), InText);
		}
		return Body + TEXT("}\n\n");
	}

	const FTextLocalizationResource::FEntry* Find(const FTextLocalizationResource& InResource, const TCHAR* InKey, const TCHAR* InNamespace = TEXT("NS"))
	{
		return InResource.Entries.Find(FTextId(FTextKey(InNamespace), FTextKey(InKey)));
	}

	FString TextOf(const FTextLocalizationResource::FEntry* InEntry)
	{
		return InEntry != nullptr && InEntry->LocalizedString.IsValid() ? *InEntry->LocalizedString : FString(TEXT("<none>"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocPriorityFormulaTest, "DreamLoc.TextSource.Priority.Formula",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocPriorityFormulaTest::RunTest(const FString& Parameters)
{
	using Source = FDreamLocTextSource;
	TestEqual(TEXT("the most specific culture, the project"), Source::MakeEntryPriority(0, 1), -1000 + 62);
	TestEqual(TEXT("the second culture, a mod"), Source::MakeEntryPriority(1, 2), -1000 + 64 + 61);
	TestTrue(TEXT("same culture: a mod beats the project"), Source::MakeEntryPriority(0, 2) < Source::MakeEntryPriority(0, 1));
	TestTrue(TEXT("same culture: the project beats a plugin"), Source::MakeEntryPriority(0, 1) < Source::MakeEntryPriority(0, 0));
	TestTrue(TEXT("a more specific culture beats any layer of a less specific one"), Source::MakeEntryPriority(0, 0) < Source::MakeEntryPriority(1, 63));
	TestTrue(TEXT("ranks above the engine's own locres files, which start at 0"), Source::MakeEntryPriority(14, 0) < 0);
	TestTrue(TEXT("layers past 63 rank as 63"), Source::MakeEntryPriority(0, 100) == Source::MakeEntryPriority(0, 63));
	TestTrue(TEXT("a source-to-source entry ranks below every translation"), Source::MakeSourceEntryPriority(63) > Source::MakeEntryPriority(1000, 0));
	TestTrue(TEXT("and below the engine's native entries"), Source::MakeSourceEntryPriority(63) > 1000000);
	TestEqual(TEXT("the source priority is the highest there is"), FDreamLocTextSource().GetPriority(), (int32)ELocalizedTextSourcePriority::Highest);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocLayersTest, "DreamLoc.TextSource.Load.Layers",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocLayersTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocTextSourceTestsLocal;
	const FLayeredFixture Fixture(TEXT("Layers"));
	// K1: all three layers translate it in en: the mod's wins.
	// K2: the plugin has en-US, the project en: the more specific culture wins over the higher layer.
	// K3: the project lists it untranslated.
	Fixture.Write(Fixture.PluginDirectory(), TEXT("en"), TEXT("UI.dloc"), Entry(TEXT("K1"), TEXT("一"), TEXT("plugin")));
	Fixture.Write(Fixture.PluginDirectory(), TEXT("en-US"), TEXT("UI.dloc"), Entry(TEXT("K2"), TEXT("二"), TEXT("plugin en-US")));
	Fixture.Write(Fixture.ProjectDirectory(), TEXT("en"), TEXT("UI.dloc"),
		Entry(TEXT("K1"), TEXT("一"), TEXT("project")) + Entry(TEXT("K2"), TEXT("二"), TEXT("project en")) + Entry(TEXT("K3"), TEXT("三")));
	Fixture.Write(Fixture.ModDirectory(), TEXT("en"), TEXT("Sub/Deeper.dloc"), Entry(TEXT("K1"), TEXT("一"), TEXT("mod")));

	FDreamLocTextSource Source(Fixture.Layers, FDreamLocSettings());
	const TArray<FString> Cultures = { TEXT("en-US"), TEXT("en") };
	FTextLocalizationResource Resource;
	Source.LoadLocalizedResources(ELocalizationLoadFlags::Game, Cultures, Resource, Resource);

	const FTextLocalizationResource::FEntry* K1 = Find(Resource, TEXT("K1"));
	TestEqual(TEXT("K1: the mod's, from a nested folder"), TextOf(K1), FString(TEXT("mod")));
	if (K1 != nullptr)
	{
		TestEqual(TEXT("K1: second culture, mod layer"), K1->Priority, FDreamLocTextSource::MakeEntryPriority(1, 2));
		TestEqual(TEXT("K1: under its source's hash"), K1->SourceStringHash, FTextLocalizationResource::HashString(TEXT("一")));
		TestTrue(TEXT("K1: the file it came from"), K1->LocResID.ToString().EndsWith(TEXT("Mod/DLoc/en/Sub/Deeper.dloc")));
	}
	const FTextLocalizationResource::FEntry* K2 = Find(Resource, TEXT("K2"));
	TestEqual(TEXT("K2: the plugin's en-US over the project's en"), TextOf(K2), FString(TEXT("plugin en-US")));
	if (K2 != nullptr)
	{
		TestEqual(TEXT("K2: first culture, plugin layer"), K2->Priority, FDreamLocTextSource::MakeEntryPriority(0, 0));
	}
	const FTextLocalizationResource::FEntry* K3 = Find(Resource, TEXT("K3"));
	TestEqual(TEXT("K3: untranslated, so only its source"), TextOf(K3), FString(TEXT("三")));
	if (K3 != nullptr)
	{
		TestEqual(TEXT("K3: at the source-to-source rank"), K3->Priority, FDreamLocTextSource::MakeSourceEntryPriority(1));
	}
	TestEqual(TEXT("three texts and nothing else"), Resource.Entries.Num(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocStaleTest, "DreamLoc.TextSource.Load.StaleUsesPrevHash",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocStaleTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocTextSourceTestsLocal;
	const FLayeredFixture Fixture(TEXT("Stale"));
	Fixture.Write(Fixture.ProjectDirectory(), TEXT("en"), TEXT("UI.dloc"),
		Entry(TEXT("Hint"), TEXT("按 {Key} 返回上一页"), TEXT("Press {Key} to go back"), TEXT("按 {Key} 返回"))
		+ Entry(TEXT("Fresh"), TEXT("确定"), TEXT("OK")));

	FDreamLocTextSource Source(Fixture.Layers, FDreamLocSettings());
	const TArray<FString> Cultures = { TEXT("en") };
	FTextLocalizationResource Resource;
	Source.LoadLocalizedResources(ELocalizationLoadFlags::Game, Cultures, Resource, Resource);

	const FTextLocalizationResource::FEntry* Hint = Find(Resource, TEXT("Hint"));
	TestEqual(TEXT("the stale translation is still registered"), TextOf(Hint), FString(TEXT("Press {Key} to go back")));
	if (Hint != nullptr)
	{
		TestEqual(TEXT("under the hash of prev"), Hint->SourceStringHash, FTextLocalizationResource::HashString(TEXT("按 {Key} 返回")));
		TestNotEqual(TEXT("not the current source's, so the engine shows the source"), Hint->SourceStringHash,
			FTextLocalizationResource::HashString(TEXT("按 {Key} 返回上一页")));
	}
	const FTextLocalizationResource::FEntry* Fresh = Find(Resource, TEXT("Fresh"));
	if (TestNotNull(TEXT("a current translation"), Fresh))
	{
		TestEqual(TEXT("under the hash of src"), Fresh->SourceStringHash, FTextLocalizationResource::HashString(TEXT("确定")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocWhatLoadsTest, "DreamLoc.TextSource.Load.WhatLoads",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocWhatLoadsTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocTextSourceTestsLocal;
	const FLayeredFixture Fixture(TEXT("WhatLoads"));
	Fixture.Write(Fixture.ProjectDirectory(), TEXT("en"), TEXT("UI.dloc"),
		Entry(TEXT("Kept"), TEXT("留"), TEXT("kept"))
		+ TEXT("removed {\n    Gone {\n        src   \"去\"\n        text  \"gone\"\n    }\n}\n"));
	// A terms file and an underscore file are not translations; a file with an error is not loaded at all.
	DreamLocTestUtil::WriteText(Fixture.ProjectDirectory() / TEXT("en/_terms.dloc"), TEXT("dloc 1\nculture en\nterms\n\nterm \"留\" { text \"stay\" }\n"));
	DreamLocTestUtil::WriteText(Fixture.ProjectDirectory() / TEXT("en/_scratch.dloc"),
		TEXT("dloc 1\nnamespace NS\n\nScratch {\n    src   \"草\"\n    text  \"scratch\"\n}\n"));
	DreamLocTestUtil::WriteText(Fixture.ProjectDirectory() / TEXT("en/Broken.dloc"),
		TEXT("dloc 1\nnamespace NS\n\nBroken {\n    src   \"坏\"\n    text  \"broken\"\n    bogus \"x\"\n}\n"));
	// No src: nothing to check the translation against.
	DreamLocTestUtil::WriteText(Fixture.ProjectDirectory() / TEXT("en/NoSource.dloc"),
		TEXT("dloc 1\nnamespace NS\n\nOrphan {\n    text  \"orphan\"\n}\n"));
	// Another culture's folder is not read for en.
	Fixture.Write(Fixture.ProjectDirectory(), TEXT("ja"), TEXT("UI.dloc"), Entry(TEXT("Japanese"), TEXT("日"), TEXT("日本語")));

	FDreamLocTextSource Source(Fixture.Layers, FDreamLocSettings());
	const TArray<FString> Cultures = { TEXT("en") };

	FTextLocalizationResource EditorOnly;
	Source.LoadLocalizedResources(ELocalizationLoadFlags::Editor | ELocalizationLoadFlags::Engine | ELocalizationLoadFlags::Native
		| ELocalizationLoadFlags::Additional, Cultures, EditorOnly, EditorOnly);
	TestEqual(TEXT("an editor or engine load gets nothing"), EditorOnly.Entries.Num(), 0);

	FTextLocalizationResource Preview;
	Source.LoadLocalizedResources(ELocalizationLoadFlags::ForceLocalizedGame, Cultures, Preview, Preview);
	TestEqual(TEXT("the editor's forced game preview loads"), TextOf(Find(Preview, TEXT("Kept"))), FString(TEXT("kept")));

	FTextLocalizationResource Resource;
	Source.LoadLocalizedResources(ELocalizationLoadFlags::Game, Cultures, Resource, Resource);
	TestEqual(TEXT("the active entry"), TextOf(Find(Resource, TEXT("Kept"))), FString(TEXT("kept")));
	TestNull(TEXT("not the removed one"), Find(Resource, TEXT("Gone")));
	TestNull(TEXT("not a term"), Find(Resource, TEXT("留")));
	TestNull(TEXT("not an underscore file"), Find(Resource, TEXT("Scratch")));
	TestNull(TEXT("not a file with errors"), Find(Resource, TEXT("Broken")));
	TestNull(TEXT("not a translation without src"), Find(Resource, TEXT("Orphan")));
	TestNull(TEXT("not another culture"), Find(Resource, TEXT("Japanese")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocExtraFallbackTest, "DreamLoc.TextSource.Load.ExtraFallback",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocExtraFallbackTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocTextSourceTestsLocal;
	const FLayeredFixture Fixture(TEXT("Fallback"));
	Fixture.Write(Fixture.ProjectDirectory(), TEXT("pt-BR"), TEXT("UI.dloc"), Entry(TEXT("Brazil"), TEXT("巴"), TEXT("brasileiro")));
	Fixture.Write(Fixture.ProjectDirectory(), TEXT("pt"), TEXT("UI.dloc"), Entry(TEXT("Both"), TEXT("两"), TEXT("pt")));
	Fixture.Write(Fixture.ProjectDirectory(), TEXT("pt-BR"), TEXT("Both.dloc"), Entry(TEXT("Both"), TEXT("两"), TEXT("pt-BR")));
	const TArray<FString> Cultures = { TEXT("pt-PT"), TEXT("pt") };

	FTextLocalizationResource Without;
	FDreamLocTextSource Plain(Fixture.Layers, FDreamLocSettings());
	Plain.LoadLocalizedResources(ELocalizationLoadFlags::Game, Cultures, Without, Without);
	TestNull(TEXT("without a fallback, pt-BR is not read for pt-PT"), Find(Without, TEXT("Brazil")));

	FDreamLocSettings Settings;
	Settings.Fallbacks.Emplace(TEXT("pt-PT"), TEXT("pt-BR"));
	FDreamLocTextSource WithFallback(Fixture.Layers, Settings);
	FTextLocalizationResource With;
	WithFallback.LoadLocalizedResources(ELocalizationLoadFlags::Game, Cultures, With, With);
	const FTextLocalizationResource::FEntry* Brazil = Find(With, TEXT("Brazil"));
	TestEqual(TEXT("with pt-PT=pt-BR, it is"), TextOf(Brazil), FString(TEXT("brasileiro")));
	if (Brazil != nullptr)
	{
		TestEqual(TEXT("ranked after the engine's two cultures"), Brazil->Priority, FDreamLocTextSource::MakeEntryPriority(2, 1));
	}
	TestEqual(TEXT("and below every culture the engine asked for"), TextOf(Find(With, TEXT("Both"))), FString(TEXT("pt")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocRevertTest, "DreamLoc.TextSource.Load.RevertsWhatWasRemoved",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocRevertTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocTextSourceTestsLocal;
	const FLayeredFixture Fixture(TEXT("Revert"));
	Fixture.Write(Fixture.ProjectDirectory(), TEXT("en"), TEXT("UI.dloc"), Entry(TEXT("Title"), TEXT("标题"), TEXT("Title")));
	FDreamLocTextSource Source(Fixture.Layers, FDreamLocSettings());

	const TArray<FString> English = { TEXT("en") };
	FTextLocalizationResource First;
	Source.LoadLocalizedResources(ELocalizationLoadFlags::Game, English, First, First);
	TestEqual(TEXT("translated"), TextOf(Find(First, TEXT("Title"))), FString(TEXT("Title")));

	// The source language has no folder. The engine keeps "Title" in its table unless something overwrites it.
	const TArray<FString> SourceCulture = { TEXT("zh-Hans") };
	FTextLocalizationResource Back;
	Source.LoadLocalizedResources(ELocalizationLoadFlags::Game, SourceCulture, Back, Back);
	TestEqual(TEXT("back to the source language: the source"), TextOf(Find(Back, TEXT("Title"))), FString(TEXT("标题")));

	// The whole file gone: the text was seen, so it still gets its source.
	IFileManager::Get().Delete(*(Fixture.ProjectDirectory() / TEXT("en/UI.dloc")));
	FTextLocalizationResource Gone;
	Source.LoadLocalizedResources(ELocalizationLoadFlags::Game, English, Gone, Gone);
	const FTextLocalizationResource::FEntry* Title = Find(Gone, TEXT("Title"));
	TestEqual(TEXT("its file deleted: the source"), TextOf(Title), FString(TEXT("标题")));
	if (Title != nullptr)
	{
		TestEqual(TEXT("at the source-to-source rank"), Title->Priority, FDreamLocTextSource::MakeSourceEntryPriority(1));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocCulturesTest, "DreamLoc.TextSource.Cultures",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocCulturesTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocTextSourceTestsLocal;
	const FLayeredFixture Fixture(TEXT("Cultures"));
	Fixture.Write(Fixture.ProjectDirectory(), TEXT("en"), TEXT("UI.dloc"), Entry(TEXT("A"), TEXT("甲")));
	Fixture.Write(Fixture.ModDirectory(), TEXT("pl"), TEXT("UI.dloc"), Entry(TEXT("A"), TEXT("甲"), TEXT("A po polsku")));
	IFileManager::Get().MakeDirectory(*(Fixture.ProjectDirectory() / TEXT("NotACulture")), true);

	FDreamLocSettings Settings;
	Settings.SourceCulture = TEXT("ja");
	Settings.TargetCultures = { TEXT("ko") };
	FDreamLocTextSource Source(Fixture.Layers, Settings);

	FString Native;
	TestTrue(TEXT("speaks for the game's native culture"), Source.GetNativeCultureName(ELocalizedTextSourceCategory::Game, Native));
	TestEqual(TEXT("the settings' source culture"), Native, FString(TEXT("ja")));
	FString EngineNative;
	TestFalse(TEXT("not for the engine's"), Source.GetNativeCultureName(ELocalizedTextSourceCategory::Engine, EngineNative));
	TestFalse(TEXT("nor the editor's"), Source.GetNativeCultureName(ELocalizedTextSourceCategory::Editor, EngineNative));

	TSet<FString> Cultures;
	Source.GetLocalizedCultureNames(ELocalizationLoadFlags::Game, Cultures);
	TestTrue(TEXT("the source culture"), Cultures.Contains(TEXT("ja")));
	TestTrue(TEXT("a listed target"), Cultures.Contains(TEXT("ko")));
	TestTrue(TEXT("a folder of the project"), Cultures.Contains(TEXT("en")));
	TestTrue(TEXT("a language only a mod brings"), Cultures.Contains(TEXT("pl")));
	TestFalse(TEXT("not a folder that is no culture"), Cultures.Contains(TEXT("NotACulture")));

	TestTrue(TEXT("zh-Hans is a culture"), FDreamLocTextSource::IsKnownCulture(TEXT("zh-Hans")));
	TestTrue(TEXT("es-419 is a culture"), FDreamLocTextSource::IsKnownCulture(TEXT("es-419")));
	TestTrue(TEXT("whatever the case"), FDreamLocTextSource::IsKnownCulture(TEXT("EN")));
	TestFalse(TEXT("Drafts is not"), FDreamLocTextSource::IsKnownCulture(TEXT("Drafts")));

	TSet<FString> EditorCultures;
	Source.GetLocalizedCultureNames(ELocalizationLoadFlags::Editor, EditorCultures);
	TestEqual(TEXT("nothing for the editor's own text"), EditorCultures.Num(), 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
