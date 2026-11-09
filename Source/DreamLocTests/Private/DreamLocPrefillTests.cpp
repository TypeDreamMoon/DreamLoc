// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocEngine.h"
#include "DreamLocFile.h"
#include "DreamLocSync.h"
#include "DreamLocTestUtil.h"
#include "Internationalization/TextLocalizationResource.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamLocPrefillTestsLocal
{
	struct FText3
	{
		const TCHAR* Namespace;
		const TCHAR* Key;
		const TCHAR* Source;
		const TCHAR* Translation;
		/** The source the translation was made from, when it is another (a stale one). */
		const TCHAR* MadeFrom = nullptr;
	};

	/** A target as the engine ships one: <target>/en/<target>.locres and <target>/zh-Hans/<target>.locres. */
	void WriteTarget(const FString& InDirectory, const TCHAR* InTarget, TConstArrayView<FText3> InTexts)
	{
		FTextLocalizationResource Native;
		FTextLocalizationResource Chinese;
		for (const FText3& Text : InTexts)
		{
			Native.AddEntry(FTextKey(Text.Namespace), FTextKey(Text.Key), FTextLocalizationResource::HashString(Text.Source), Text.Source, 0);
			Chinese.AddEntry(FTextKey(Text.Namespace), FTextKey(Text.Key), FTextLocalizationResource::HashString(Text.MadeFrom != nullptr ? Text.MadeFrom : Text.Source),
				Text.Translation, 0);
		}
		const FString Base = InDirectory / InTarget;
		IFileManager::Get().MakeDirectory(*(Base / TEXT("en")), /*Tree*/ true);
		IFileManager::Get().MakeDirectory(*(Base / TEXT("zh-Hans")), /*Tree*/ true);
		Native.SaveToFile(Base / TEXT("en") / (FString(InTarget) + TEXT(".locres")));
		Chinese.SaveToFile(Base / TEXT("zh-Hans") / (FString(InTarget) + TEXT(".locres")));
	}

	void WriteEngine(const FString& InDirectory)
	{
		WriteTarget(InDirectory, TEXT("PropertyNames"), {
			{ TEXT("UObjectDisplayNames"), TEXT("Widget:Visibility"), TEXT("Visibility"), TEXT("可见性") },
			{ TEXT("UObjectDisplayNames"), TEXT("Slot:Visibility"), TEXT("Visibility"), TEXT("可见性") },
			{ TEXT("UObjectDisplayNames"), TEXT("Light:Visibility"), TEXT("Visibility"), TEXT("可见度") },
			{ TEXT("UObjectDisplayNames"), TEXT("Brush:Clear"), TEXT("Clear"), TEXT("透明") },
			// A stale translation: made from another text.
			{ TEXT("UObjectDisplayNames"), TEXT("Widget:Opacity"), TEXT("Render Opacity"), TEXT("旧的"), TEXT("Opacity") } });
		WriteTarget(InDirectory, TEXT("Editor"), {
			{ TEXT("UnrealEd"), TEXT("ClearAll"), TEXT("Clear"), TEXT("清除") },
			{ TEXT("UnrealEd"), TEXT("ClearLog"), TEXT("Clear"), TEXT("清除") } });
	}

	const TCHAR* const PanelFile = TEXT(R"(dloc 1
culture zh-Hans
namespace UObjectDisplayNames

"Panel:Visibility" {
    src   "Visibility"
}

"Panel:Clear" {
    src   "Clear"
}

"Panel:Opacity" {
    src   "Render Opacity"
}

"Panel:Mine" {
    src   "Visibility"
    text  "显示"
    state done
}

namespace PanelEditor

ClearButton {
    src   "Clear"
}

Unknown {
    src   "Something the engine never says"
}

removed {
    namespace UObjectDisplayNames

    "Old:Visibility" {
        src   "Visibility"
    }
}
)");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocPrefillReadTest, "DreamLoc.Prefill.TheEnginesTranslationsBySourceTheMostFrequentOne",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocPrefillReadTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocPrefillTestsLocal;
	const FString Work = DreamLocTestUtil::MakeTempDirectory(TEXT("PrefillRead"));
	ON_SCOPE_EXIT { DreamLocTestUtil::DeleteDirectory(Work); };
	WriteEngine(Work);

	TArray<FDreamLocReportLine> Messages;
	const DreamLocEngine::FEngineTranslations Read = DreamLocEngine::ReadEngineTranslations(Work, { TEXT("PropertyNames"), TEXT("Editor"), TEXT("Category") },
		TEXT("en"), TEXT("zh-Hans"), Messages);
	TestEqual(TEXT("six translations; the stale one is none"), Read.Pairs, 6);
	TestEqual(TEXT("the most frequent of a source's"), Read.BySource.FindRef(TEXT("Visibility")), FString(TEXT("可见性")));
	TestEqual(TEXT("over every target: Clear is 清除 twice, 透明 once"), Read.BySource.FindRef(TEXT("Clear")), FString(TEXT("清除")));
	TestEqual(TEXT("and within its namespace"), Read.ByNamespaceAndSource.FindRef(FString(TEXT("UObjectDisplayNames")) + TEXT("\x1F") + TEXT("Clear")), FString(TEXT("透明")));
	TestFalse(TEXT("a stale translation is not read"), Read.BySource.Contains(TEXT("Render Opacity")));
	TestEqual(TEXT("a target with no files: one warning"), Messages.Num(), 1);

	// The engine as it is installed: its own Chinese.
	TArray<FDreamLocReportLine> EngineMessages;
	const DreamLocEngine::FEngineTranslations Engine = DreamLocEngine::ReadEngineTranslations(FString(), DreamLocEngine::GetDefaultPrefillTargets(),
		TEXT("en"), TEXT("zh-Hans"), EngineMessages);
	AddInfo(FString::Printf(TEXT("the engine's zh-Hans: %d translation(s) of %d source text(s)"), Engine.Pairs, Engine.BySource.Num()));
	TestTrue(TEXT("the engine ships Chinese for its editor"), Engine.Pairs > 50000);
	TestFalse(TEXT("Visibility among it"), Engine.BySource.FindRef(TEXT("Visibility")).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocPrefillApplyTest, "DreamLoc.Prefill.OnlyEntriesWithoutATranslationMarkedForReview",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocPrefillApplyTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocPrefillTestsLocal;
	const FString Work = DreamLocTestUtil::MakeTempDirectory(TEXT("PrefillApply"));
	ON_SCOPE_EXIT { DreamLocTestUtil::DeleteDirectory(Work); };
	WriteEngine(Work / TEXT("Engine"));
	const FString DLoc = Work / TEXT("Plugin/DLoc");
	DreamLocTestUtil::WriteText(DLoc / TEXT("dloc.ini"), TEXT("[DreamLoc]\nSourceCulture=en\nTargetCultures=zh-Hans\n"));
	IFileManager::Get().MakeDirectory(*(DLoc / TEXT("zh-Hans/Source")), /*Tree*/ true);
	DreamLocTestUtil::WriteText(DLoc / TEXT("zh-Hans/Source/Panel.h.dloc"), PanelFile);
	DreamLocTestUtil::WriteText(DLoc / TEXT("zh-Hans/_terms.dloc"), TEXT("dloc 1\nculture zh-Hans\nterms\n\nterm \"Visibility\" {\n    text  \"可见性\"\n}\n"));

	DreamLocEngine::FPrefillOptions Options;
	Options.DLocDirectory = DLoc;
	Options.LocalizationDirectory = Work / TEXT("Engine");
	Options.Targets = { TEXT("PropertyNames"), TEXT("Editor") };
	Options.bDryRun = true;
	const FDreamLocSyncOutput Dry = DreamLocEngine::Prefill(Options);
	TestFalse(TEXT("the dry run is not blocked"), Dry.bBlocked);
	TestEqual(TEXT("it counts three: Visibility, Clear twice; not the stale source, not the unknown one, not the reviewed one"), Dry.Added, 3);
	TestTrue(TEXT("and says so"), Dry.Messages.ContainsByPredicate([](const FDreamLocReportLine& InLine)
	{
		return InLine.Diagnostic.Code == DreamLocCode::Prefilled && InLine.Diagnostic.Message.Contains(TEXT("3 of 5 untranslated entr(ies) to fill"));
	}));
	FString Before;
	FFileHelper::LoadFileToString(Before, *(DLoc / TEXT("zh-Hans/Source/Panel.h.dloc")));
	TestEqual(TEXT("it writes nothing"), Before, FString(PanelFile));

	Options.bDryRun = false;
	const FDreamLocSyncOutput Done = DreamLocEngine::Prefill(Options);
	TestEqual(TEXT("three filled"), Done.Added, 3);
	FDreamLocDocument Document;
	TArray<FDreamLocDiagnostic> Diagnostics;
	TestTrue(TEXT("the file reads"), DreamLocFile::LoadFile(DLoc / TEXT("zh-Hans/Source/Panel.h.dloc"), Document, Diagnostics));
	TMap<FString, const FDreamLocEntry*> ByKey;
	for (const FDreamLocItem& Item : Document.Items)
	{
		if (Item.Kind == EDreamLocItemKind::Entry)
		{
			ByKey.Add(Item.Entry.Key, &Item.Entry);
		}
	}
	auto TextOf = [&ByKey](const TCHAR* InKey)
	{
		const FDreamLocEntry* const* Entry = ByKey.Find(InKey);
		const FString* Text = Entry != nullptr ? (*Entry)->Find(EDreamLocField::Text) : nullptr;
		return Text != nullptr ? *Text : FString(TEXT("<none>"));
	};
	TestEqual(TEXT("the most frequent translation"), TextOf(TEXT("Panel:Visibility")), FString(TEXT("可见性")));
	TestEqual(TEXT("a display name takes the display names' translation"), TextOf(TEXT("Panel:Clear")), FString(TEXT("透明")));
	TestEqual(TEXT("a LOCTEXT, with no namespace of its own in the engine, the most frequent of all"), TextOf(TEXT("ClearButton")), FString(TEXT("清除")));
	TestEqual(TEXT("a reviewed translation stays"), TextOf(TEXT("Panel:Mine")), FString(TEXT("显示")));
	TestEqual(TEXT("a source the engine never translated stays empty"), TextOf(TEXT("Unknown")), FString(TEXT("<none>")));
	TestEqual(TEXT("so does one it translated from another text"), TextOf(TEXT("Panel:Opacity")), FString(TEXT("<none>")));
	const FDreamLocEntry* const* Visibility = ByKey.Find(TEXT("Panel:Visibility"));
	if (TestNotNull(TEXT("the filled entry"), Visibility))
	{
		TestEqual(TEXT("for review: a draft"), int32((*Visibility)->GetState()), int32(EDreamLocState::Draft));
		TestEqual(TEXT("marked, above its text"), FString::Join((*Visibility)->Fields[(int32)EDreamLocField::Text].Comments, TEXT("|")),
			FString(TEXT("// from the engine's zh-Hans")));
	}
	FString After;
	FFileHelper::LoadFileToString(After, *(DLoc / TEXT("zh-Hans/Source/Panel.h.dloc")));
	const FString Filled = DreamLocFile::QuoteWordIfNeeded(TEXT("Panel:Visibility"), /*bInIsKey*/ true)
		+ TEXT(" {\n    src   \"Visibility\"\n    // from the engine's zh-Hans\n    text  \"可见性\"\n    state draft\n}");
	TestTrue(TEXT("as the file has it"), After.Contains(Filled));
	const FString Removed = TEXT("    ") + DreamLocFile::QuoteWordIfNeeded(TEXT("Old:Visibility"), /*bInIsKey*/ true) + TEXT(" {\n        src   \"Visibility\"\n    }");
	TestTrue(TEXT("the removed block as it was"), After.Contains(Removed));
	if (!After.Contains(Filled))
	{
		AddInfo(After);
	}
	FString Terms;
	FFileHelper::LoadFileToString(Terms, *(DLoc / TEXT("zh-Hans/_terms.dloc")));
	TestFalse(TEXT("the terms file is not an entries file"), Terms.Contains(TEXT("// from")));

	// Again: nothing left to fill.
	TestEqual(TEXT("a second run fills nothing"), DreamLocEngine::Prefill(Options).Added, 0);

	// A DLoc folder whose source is Chinese: the engine's English-to-Chinese is no translation of its texts.
	DreamLocTestUtil::WriteText(DLoc / TEXT("dloc.ini"), TEXT("[DreamLoc]\nSourceCulture=zh-Hans\nTargetCultures=en\n"));
	TestTrue(TEXT("another source culture: blocked"), DreamLocEngine::Prefill(Options).bBlocked);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
