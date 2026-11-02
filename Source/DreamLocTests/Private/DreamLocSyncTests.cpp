// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocEngine.h"
#include "DreamLocExtract.h"
#include "DreamLocFile.h"
#include "DreamLocSync.h"
#include "DreamLocTestUtil.h"
#include "LocTextHelper.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * sync works on texts in memory: manifests in, the .dloc files' texts in, the new texts out. Each test brings the
 * files to a state with one sync, changes what the sources say, syncs again, and reads what the files hold now. Every
 * result is synced once more and must not change: sync of its own output is a no-op.
 */
namespace DreamLocSyncTestsLocal
{
	using FFiles = TMap<FString, TMap<FString, FString>>;

	const TCHAR* const SettingsDui = TEXT("DUI/UI/Settings.dui");
	const TCHAR* const SettingsDloc = TEXT("DUI/UI/Settings.dloc");
	const TCHAR* const SettingsNamespace = TEXT("/Game/UI/WBP_Settings");

	FDreamLocExtractEntry Text(const TCHAR* InKey, const TCHAR* InSource, const TCHAR* InAt = TEXT(""), const TCHAR* InRenamedFrom = TEXT(""))
	{
		FDreamLocExtractEntry Entry;
		Entry.Key = InKey;
		Entry.Source = InSource;
		Entry.At = InAt;
		Entry.RenamedFrom = InRenamedFrom;
		return Entry;
	}

	FDreamLocExtractManifest Manifest(const TCHAR* InSource, const TCHAR* InNamespace, TArray<FDreamLocExtractEntry> InEntries,
		const TCHAR* InProvider = TEXT("DreamGUI"))
	{
		FDreamLocExtractManifest Result;
		Result.Provider = InProvider;
		Result.Source = InSource;
		FDreamLocExtractNamespace& Namespace = Result.Namespaces.AddDefaulted_GetRef();
		Namespace.Namespace = InNamespace;
		Namespace.Entries = MoveTemp(InEntries);
		return Result;
	}

	FDreamLocSyncInput MakeInput(TArray<FDreamLocExtractManifest> InManifests, const FFiles& InFiles, TArray<FString> InExistingSources = {},
		TArray<FString> InCultures = { TEXT("en") })
	{
		FDreamLocSyncInput Input;
		for (const FDreamLocExtractManifest& Manifest : InManifests)
		{
			InExistingSources.AddUnique(Manifest.Source);
		}
		Input.Manifests = MoveTemp(InManifests);
		Input.Files = InFiles;
		Input.Cultures = MoveTemp(InCultures);
		Input.SourceExists = [InExistingSources](const FString& InSource) { return InExistingSources.Contains(InSource); };
		return Input;
	}

	/** The files as they are after the run's changes. */
	FFiles Apply(const FFiles& InFiles, const FDreamLocSyncOutput& InOutput)
	{
		FFiles Files = InFiles;
		for (const TPair<FString, TMap<FString, TOptional<FString>>>& Culture : InOutput.Changes)
		{
			TMap<FString, FString>& CultureFiles = Files.FindOrAdd(Culture.Key);
			for (const TPair<FString, TOptional<FString>>& Change : Culture.Value)
			{
				if (Change.Value.IsSet())
				{
					CultureFiles.Add(Change.Key, Change.Value.GetValue());
				}
				else
				{
					CultureFiles.Remove(Change.Key);
				}
			}
		}
		return Files;
	}

	/** Syncs, applies, and checks that a second sync of the result changes nothing. */
	FFiles Sync(FAutomationTestBase& InTest, const FString& InStep, FDreamLocSyncInput InInput, FDreamLocSyncOutput* OutOutput = nullptr)
	{
		const FDreamLocSyncOutput Output = DreamLocSync::Run(InInput);
		InTest.TestFalse(InStep + TEXT(": not blocked"), Output.bBlocked);
		FFiles Files = Apply(InInput.Files, Output);
		InInput.Files = Files;
		const FDreamLocSyncOutput Again = DreamLocSync::Run(InInput);
		InTest.TestEqual(InStep + TEXT(": syncing the result again changes nothing"), Again.Changes.Num(), 0);
		if (Again.Changes.Num() > 0)
		{
			for (const TPair<FString, TMap<FString, TOptional<FString>>>& Culture : Again.Changes)
			{
				for (const TPair<FString, TOptional<FString>>& Change : Culture.Value)
				{
					InTest.AddInfo(FString::Printf(TEXT("%s: changes again: %s/%s"), *InStep, *Culture.Key, *Change.Key));
				}
			}
		}
		if (OutOutput != nullptr)
		{
			*OutOutput = Output;
		}
		return Files;
	}

	struct FFound
	{
		bool bFound = false;
		FDreamLocEntry Entry;
	};

	FFound Find(const FFiles& InFiles, const FString& InPath, const FString& InNamespace, const FString& InKey, bool bInRemoved = false,
		const FString& InCulture = TEXT("en"))
	{
		FFound Found;
		const TMap<FString, FString>* Culture = InFiles.Find(InCulture);
		const FString* Text = Culture != nullptr ? Culture->Find(InPath) : nullptr;
		if (Text == nullptr)
		{
			return Found;
		}
		FDreamLocDocument Document;
		TArray<FDreamLocDiagnostic> Diagnostics;
		DreamLocFile::Parse(*Text, Document, Diagnostics);
		Document.ForEachEntry([&](const FString& InEntryNamespace, const FDreamLocEntry& InEntry, bool bRemoved)
		{
			if (!Found.bFound && bRemoved == bInRemoved && InEntryNamespace == InNamespace && InEntry.Key == InKey)
			{
				Found.bFound = true;
				Found.Entry = InEntry;
			}
		}, /*bInIncludeRemoved*/ true);
		return Found;
	}

	FString Field(const FFound& InFound, EDreamLocField InField)
	{
		if (!InFound.bFound)
		{
			return TEXT("<no entry>");
		}
		const FString* Value = InFound.Entry.Find(InField);
		return Value != nullptr ? *Value : FString(TEXT("<none>"));
	}

	/** Gives an entry a translation, as a translator would. */
	void Translate(FFiles& InOutFiles, const FString& InPath, const FString& InNamespace, const FString& InKey, const FString& InText,
		const TCHAR* InState = nullptr, const FString& InCulture = TEXT("en"))
	{
		FString& Text = InOutFiles.FindOrAdd(InCulture).FindOrAdd(InPath);
		FDreamLocDocument Document;
		TArray<FDreamLocDiagnostic> Diagnostics;
		DreamLocFile::Parse(Text, Document, Diagnostics);
		FString Namespace;
		for (FDreamLocItem& Item : Document.Items)
		{
			if (Item.Kind == EDreamLocItemKind::Namespace)
			{
				Namespace = Item.Namespace;
			}
			else if (Item.Kind == EDreamLocItemKind::Entry && Namespace == InNamespace && Item.Entry.Key == InKey)
			{
				Item.Entry.Set(EDreamLocField::Text, InText);
				if (InState != nullptr)
				{
					Item.Entry.Set(EDreamLocField::State, InState);
				}
			}
		}
		Text = DreamLocFile::Write(Document);
	}

	bool HasCode(const FDreamLocSyncOutput& InOutput, int32 InCode)
	{
		return InOutput.Messages.ContainsByPredicate([InCode](const FDreamLocReportLine& InLine) { return InLine.Diagnostic.Code == InCode; });
	}

	FString Codes(const FDreamLocSyncOutput& InOutput)
	{
		TArray<FString> Names;
		for (const FDreamLocReportLine& Line : InOutput.Messages)
		{
			Names.Add(Line.Diagnostic.GetCodeName());
		}
		return FString::Join(Names, TEXT(" "));
	}

	bool HasFile(const FFiles& InFiles, const FString& InPath, const FString& InCulture = TEXT("en"))
	{
		const TMap<FString, FString>* Culture = InFiles.Find(InCulture);
		return Culture != nullptr && Culture->Contains(InPath);
	}

	FString FileText(const FFiles& InFiles, const FString& InPath, const FString& InCulture = TEXT("en"))
	{
		const TMap<FString, FString>* Culture = InFiles.Find(InCulture);
		const FString* Text = Culture != nullptr ? Culture->Find(InPath) : nullptr;
		return Text != nullptr ? *Text : FString();
	}

	/** A settings screen with two texts, synced and translated. */
	FFiles MakeTranslatedSettings(FAutomationTestBase& InTest)
	{
		FFiles Files = Sync(InTest, TEXT("the first sync"), MakeInput({ Manifest(SettingsDui, SettingsNamespace,
			{ Text(TEXT("Title.Text"), TEXT("设置"), TEXT("Title")), Text(TEXT("Hint.Text"), TEXT("按任意键继续"), TEXT("Footer/Hint")) }) }, {}));
		Translate(Files, SettingsDloc, SettingsNamespace, TEXT("Title.Text"), TEXT("Settings"), TEXT("done"));
		Translate(Files, SettingsDloc, SettingsNamespace, TEXT("Hint.Text"), TEXT("Press any key"));
		return Files;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocSyncNewKeysTest, "DreamLoc.Sync.NewKeysGetTheirSourceAndNoText",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocSyncNewKeysTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocSyncTestsLocal;
	FDreamLocExtractEntry ApplyText = Text(TEXT("Apply.Text"), TEXT("应用"), TEXT("Footer/Apply"));
	ApplyText.Note = TEXT("底部按钮");
	ApplyText.Max = 16;
	FDreamLocSyncOutput Output;
	const FFiles Files = Sync(*this, TEXT("new keys"), MakeInput({ Manifest(SettingsDui, SettingsNamespace,
		{ Text(TEXT("Title.Text"), TEXT("设置"), TEXT("Title")), ApplyText }) }, {}, {}, { TEXT("en"), TEXT("ja") }), &Output);

	TestEqual(TEXT("two texts in two cultures"), Output.Added, 4);
	TestEqual(TEXT("one file per culture"), Output.NumWrites(), 2);
	const FFound Title = Find(Files, SettingsDloc, SettingsNamespace, TEXT("Title.Text"));
	TestEqual(TEXT("the source text"), Field(Title, EDreamLocField::Src), FString(TEXT("设置")));
	TestEqual(TEXT("where it comes from"), Field(Title, EDreamLocField::At), FString(TEXT("DUI/UI/Settings.dui#Title")));
	TestEqual(TEXT("no translation yet"), Field(Title, EDreamLocField::Text), FString(TEXT("<none>")));
	const FFound Found = Find(Files, SettingsDloc, SettingsNamespace, TEXT("Apply.Text"));
	TestEqual(TEXT("the note"), Field(Found, EDreamLocField::Note), FString(TEXT("底部按钮")));
	TestEqual(TEXT("the length limit"), Field(Found, EDreamLocField::Max), FString(TEXT("16")));
	TestTrue(TEXT("Japanese has its own file"), Find(Files, SettingsDloc, SettingsNamespace, TEXT("Title.Text"), false, TEXT("ja")).bFound);
	TestTrue(TEXT("the Japanese file says so"), FileText(Files, SettingsDloc, TEXT("ja")).Contains(TEXT("culture ja")));
	const FString English = FileText(Files, SettingsDloc);
	TestTrue(TEXT("the entries in the source's order"), English.Find(TEXT("Title.Text")) < English.Find(TEXT("Apply.Text")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocSyncChangedSourceTest, "DreamLoc.Sync.AChangedSourceKeepsTheTranslationAndTheEarliestPrev",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocSyncChangedSourceTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocSyncTestsLocal;
	FFiles Files = MakeTranslatedSettings(*this);
	auto Settings = [](const TCHAR* InTitle, const TCHAR* InHint)
	{
		return Manifest(SettingsDui, SettingsNamespace, { Text(TEXT("Title.Text"), InTitle, TEXT("Title")), Text(TEXT("Hint.Text"), InHint, TEXT("Footer/Hint")) });
	};

	FDreamLocSyncOutput Output;
	Files = Sync(*this, TEXT("the title changed"), MakeInput({ Settings(TEXT("设置页"), TEXT("按任意键继续")) }, Files), &Output);
	FFound Title = Find(Files, SettingsDloc, SettingsNamespace, TEXT("Title.Text"));
	TestEqual(TEXT("the new source"), Field(Title, EDreamLocField::Src), FString(TEXT("设置页")));
	TestEqual(TEXT("the old source as prev"), Field(Title, EDreamLocField::Prev), FString(TEXT("设置")));
	TestEqual(TEXT("the translation stays"), Field(Title, EDreamLocField::Text), FString(TEXT("Settings")));
	TestEqual(TEXT("so does its state"), Field(Title, EDreamLocField::State), FString(TEXT("done")));
	TestEqual(TEXT("one outdated"), Output.Outdated, 1);

	Files = Sync(*this, TEXT("the title changed again"), MakeInput({ Settings(TEXT("设置页面"), TEXT("按任意键继续")) }, Files));
	Title = Find(Files, SettingsDloc, SettingsNamespace, TEXT("Title.Text"));
	TestEqual(TEXT("the prev is still what the translation was made from"), Field(Title, EDreamLocField::Prev), FString(TEXT("设置")));

	Files = Sync(*this, TEXT("the title changed back"), MakeInput({ Settings(TEXT("设置"), TEXT("按任意键继续")) }, Files));
	Title = Find(Files, SettingsDloc, SettingsNamespace, TEXT("Title.Text"));
	TestEqual(TEXT("the source the translation was made from again: no prev"), Field(Title, EDreamLocField::Prev), FString(TEXT("<none>")));

	// An entry with no translation has nothing to be stale against.
	Files = Sync(*this, TEXT("a text without a translation changed"), MakeInput({ Manifest(SettingsDui, SettingsNamespace,
		{ Text(TEXT("Title.Text"), TEXT("设置"), TEXT("Title")), Text(TEXT("Hint.Text"), TEXT("按任意键继续"), TEXT("Footer/Hint")),
		  Text(TEXT("Apply.Text"), TEXT("应用"), TEXT("Footer/Apply")) }) }, Files));
	Files = Sync(*this, TEXT("its source changed"), MakeInput({ Manifest(SettingsDui, SettingsNamespace,
		{ Text(TEXT("Title.Text"), TEXT("设置"), TEXT("Title")), Text(TEXT("Hint.Text"), TEXT("按任意键继续"), TEXT("Footer/Hint")),
		  Text(TEXT("Apply.Text"), TEXT("应用更改"), TEXT("Footer/Apply")) }) }, Files));
	const FFound ApplyEntry = Find(Files, SettingsDloc, SettingsNamespace, TEXT("Apply.Text"));
	TestEqual(TEXT("the new source"), Field(ApplyEntry, EDreamLocField::Src), FString(TEXT("应用更改")));
	TestEqual(TEXT("and no prev"), Field(ApplyEntry, EDreamLocField::Prev), FString(TEXT("<none>")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocSyncMachineLinesTest, "DreamLoc.Sync.MachineLinesAreRewrittenAndPeoplesLinesKept",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocSyncMachineLinesTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocSyncTestsLocal;
	FFiles Files;
	Files.FindOrAdd(TEXT("en")).Add(SettingsDloc, TEXT(R"(// The settings screen.
dloc 1
culture en
namespace /Game/UI/WBP_Settings

// 标题，别太长
Title.Text {
    at    DUI/UI/Settings.dui#Old
    note  "旧说明"
    max   8
    src   "设置"
    // 校对过
    text  "Settings"
    state done
}
)"));
	FDreamLocExtractEntry Title = Text(TEXT("Title.Text"), TEXT("设置"), TEXT("Header/Title"));
	Title.Note = TEXT("新说明");
	Files = Sync(*this, TEXT("the node moved and its note changed"), MakeInput({ Manifest(SettingsDui, SettingsNamespace, { Title }) }, Files));
	const FFound Found = Find(Files, SettingsDloc, SettingsNamespace, TEXT("Title.Text"));
	TestEqual(TEXT("at follows the source"), Field(Found, EDreamLocField::At), FString(TEXT("DUI/UI/Settings.dui#Header/Title")));
	TestEqual(TEXT("note follows the source"), Field(Found, EDreamLocField::Note), FString(TEXT("新说明")));
	TestEqual(TEXT("a max the source no longer has goes"), Field(Found, EDreamLocField::Max), FString(TEXT("<none>")));
	TestEqual(TEXT("the translation stays"), Field(Found, EDreamLocField::Text), FString(TEXT("Settings")));
	TestEqual(TEXT("the state stays"), Field(Found, EDreamLocField::State), FString(TEXT("done")));
	const FString Text = FileText(Files, SettingsDloc);
	TestTrue(TEXT("the file's comment stays"), Text.StartsWith(TEXT("// The settings screen.\n")));
	TestTrue(TEXT("the entry's comment stays"), Text.Contains(TEXT("// 标题，别太长\nTitle.Text {")));
	TestTrue(TEXT("the comment on the translation stays"), Text.Contains(TEXT("// 校对过")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocSyncRemovedTest, "DreamLoc.Sync.VanishedKeysGoToTheRemovedBlockAndComeBackFromIt",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocSyncRemovedTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocSyncTestsLocal;
	FFiles Files = MakeTranslatedSettings(*this);
	FDreamLocSyncOutput Output;
	Files = Sync(*this, TEXT("the hint is gone"), MakeInput({ Manifest(SettingsDui, SettingsNamespace,
		{ Text(TEXT("Title.Text"), TEXT("设置"), TEXT("Title")) }) }, Files), &Output);
	TestFalse(TEXT("the hint is not live"), Find(Files, SettingsDloc, SettingsNamespace, TEXT("Hint.Text")).bFound);
	TestEqual(TEXT("it is in the removed block with its translation"),
		Field(Find(Files, SettingsDloc, SettingsNamespace, TEXT("Hint.Text"), /*bInRemoved*/ true), EDreamLocField::Text), FString(TEXT("Press any key")));
	TestEqual(TEXT("one removed"), Output.Removed, 1);
	TestTrue(TEXT("and said so"), HasCode(Output, DreamLocCode::MovedToRemoved));

	Files = Sync(*this, TEXT("the hint is back"), MakeInput({ Manifest(SettingsDui, SettingsNamespace,
		{ Text(TEXT("Title.Text"), TEXT("设置"), TEXT("Title")), Text(TEXT("Hint.Text"), TEXT("按任意键继续"), TEXT("Footer/Hint")) }) }, Files), &Output);
	TestEqual(TEXT("live again with its translation"),
		Field(Find(Files, SettingsDloc, SettingsNamespace, TEXT("Hint.Text")), EDreamLocField::Text), FString(TEXT("Press any key")));
	TestFalse(TEXT("no removed block left"), FileText(Files, SettingsDloc).Contains(TEXT("removed {")));
	TestEqual(TEXT("one revived"), Output.Revived, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocSyncDeletedSourceTest, "DreamLoc.Sync.ADeletedSourceKeepsItsTranslationsInRemovedAndAnUncompiledOneIsLeftAlone",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocSyncDeletedSourceTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocSyncTestsLocal;
	const FFiles Translated = MakeTranslatedSettings(*this);

	FDreamLocSyncOutput Output;
	const FFiles Deleted = Sync(*this, TEXT("the source is deleted"), MakeInput({}, Translated), &Output);
	TestTrue(TEXT("the file stays"), HasFile(Deleted, SettingsDloc));
	TestEqual(TEXT("its translations are in the removed block"),
		Field(Find(Deleted, SettingsDloc, SettingsNamespace, TEXT("Title.Text"), /*bInRemoved*/ true), EDreamLocField::Text), FString(TEXT("Settings")));
	TestFalse(TEXT("none is live"), Find(Deleted, SettingsDloc, SettingsNamespace, TEXT("Title.Text")).bFound);

	const FDreamLocSyncOutput NotCompiled = DreamLocSync::Run(MakeInput({}, Translated, { SettingsDui }));
	TestEqual(TEXT("a source that exists but has no manifest: nothing changes"), NotCompiled.Changes.Num(), 0);
	TestTrue(TEXT("and sync says why"), HasCode(NotCompiled, DreamLocCode::SourceNotExtracted));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocSyncHandWrittenTest, "DreamLoc.Sync.AHandWrittenTranslationMovesToItsFileWhenAManifestHasItsKey",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocSyncHandWrittenTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocSyncTestsLocal;
	FFiles Files;
	Files.FindOrAdd(TEXT("en")).Add(TEXT("Code/Hand.dloc"), TEXT(R"(dloc 1
culture en
namespace Game

Quit {
    src   "退出"
    text  "Quit"
}

Other {
    src   "其他"
    text  "Other"
}
)"));
	FDreamLocSyncOutput Output;
	Files = Sync(*this, TEXT("the gather found Quit"), MakeInput({ Manifest(TEXT("Source/Game/Private/Menu.cpp"), TEXT("Game"),
		{ Text(TEXT("Quit"), TEXT("退出")) }, DreamLocExtract::EngineProvider) }, Files), &Output);
	const FString MenuDloc = TEXT("Engine/Source/Game/Private/Menu.dloc");
	const FFound Quit = Find(Files, MenuDloc, TEXT("Game"), TEXT("Quit"));
	TestEqual(TEXT("the translation is in the source's file"), Field(Quit, EDreamLocField::Text), FString(TEXT("Quit")));
	TestEqual(TEXT("with where it comes from"), Field(Quit, EDreamLocField::At), FString(TEXT("Source/Game/Private/Menu.cpp")));
	TestFalse(TEXT("and not in the hand-written file any more"), Find(Files, TEXT("Code/Hand.dloc"), TEXT("Game"), TEXT("Quit")).bFound);
	TestEqual(TEXT("the hand-written file keeps the rest"), Field(Find(Files, TEXT("Code/Hand.dloc"), TEXT("Game"), TEXT("Other")), EDreamLocField::Text),
		FString(TEXT("Other")));
	TestEqual(TEXT("one moved"), Output.Moved, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocRenameExactTest, "DreamLoc.Rename.ARenameTheToolKnowsMovesTheTranslation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocRenameExactTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocSyncTestsLocal;
	FFiles Files = MakeTranslatedSettings(*this);
	FDreamLocSyncOutput Output;
	Files = Sync(*this, TEXT("Title became Heading, and its text changed"), MakeInput({ Manifest(SettingsDui, SettingsNamespace,
		{ Text(TEXT("Heading.Text"), TEXT("设置界面"), TEXT("Heading"), TEXT("Title.Text")), Text(TEXT("Hint.Text"), TEXT("按任意键继续"), TEXT("Footer/Hint")) }) },
		Files), &Output);
	const FFound Heading = Find(Files, SettingsDloc, SettingsNamespace, TEXT("Heading.Text"));
	TestEqual(TEXT("the translation moved"), Field(Heading, EDreamLocField::Text), FString(TEXT("Settings")));
	TestEqual(TEXT("with the text it was made from"), Field(Heading, EDreamLocField::Prev), FString(TEXT("设置")));
	TestFalse(TEXT("the old key is gone"), Find(Files, SettingsDloc, SettingsNamespace, TEXT("Title.Text")).bFound);
	TestFalse(TEXT("and not kept as removed"), Find(Files, SettingsDloc, SettingsNamespace, TEXT("Title.Text"), true).bFound);
	TestEqual(TEXT("one migrated"), Output.Migrated, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocRenameInferredTest, "DreamLoc.Rename.TheSameTextOneToOneIsInferredAndSeveralAreOnlyReported",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocRenameInferredTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocSyncTestsLocal;
	FFiles Files = MakeTranslatedSettings(*this);
	FDreamLocSyncOutput Output;
	Files = Sync(*this, TEXT("Hint became Tip, with no rename clause"), MakeInput({ Manifest(SettingsDui, SettingsNamespace,
		{ Text(TEXT("Title.Text"), TEXT("设置"), TEXT("Title")), Text(TEXT("Tip.Text"), TEXT("按任意键继续"), TEXT("Footer/Tip")) }) }, Files), &Output);
	TestEqual(TEXT("the translation moved"), Field(Find(Files, SettingsDloc, SettingsNamespace, TEXT("Tip.Text")), EDreamLocField::Text),
		FString(TEXT("Press any key")));
	TestTrue(TEXT("and sync said why"), HasCode(Output, DreamLocCode::RenameInferred));
	TestFalse(TEXT("nothing in removed"), FileText(Files, SettingsDloc).Contains(TEXT("removed {")));

	// Two new keys with the old text: which one is it?
	Files = Sync(*this, TEXT("Tip became two texts"), MakeInput({ Manifest(SettingsDui, SettingsNamespace,
		{ Text(TEXT("Title.Text"), TEXT("设置"), TEXT("Title")), Text(TEXT("TipA.Text"), TEXT("按任意键继续"), TEXT("Footer/TipA")),
		  Text(TEXT("TipB.Text"), TEXT("按任意键继续"), TEXT("Footer/TipB")) }) }, Files), &Output);
	TestTrue(TEXT("a warning with the candidates"), HasCode(Output, DreamLocCode::RenameCandidates));
	TestEqual(TEXT("neither took the translation"), Field(Find(Files, SettingsDloc, SettingsNamespace, TEXT("TipA.Text")), EDreamLocField::Text),
		FString(TEXT("<none>")));
	TestEqual(TEXT("it waits in removed"), Field(Find(Files, SettingsDloc, SettingsNamespace, TEXT("Tip.Text"), true), EDreamLocField::Text),
		FString(TEXT("Press any key")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocRenameSimilarTest, "DreamLoc.Rename.ASimilarTextIsOnlyAHint",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocRenameSimilarTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocSyncTestsLocal;
	FFiles Files = MakeTranslatedSettings(*this);
	FDreamLocSyncOutput Output;
	Files = Sync(*this, TEXT("Hint became Tip and its text changed a little"), MakeInput({ Manifest(SettingsDui, SettingsNamespace,
		{ Text(TEXT("Title.Text"), TEXT("设置"), TEXT("Title")), Text(TEXT("Tip.Text"), TEXT("按任意键继续游戏"), TEXT("Footer/Tip")) }) }, Files), &Output);
	TestTrue(TEXT("a warning naming the likely one"), Output.Messages.ContainsByPredicate([](const FDreamLocReportLine& InLine)
	{
		return InLine.Diagnostic.Code == DreamLocCode::RenameCandidates && InLine.Diagnostic.Message.Contains(TEXT("'Tip.Text'"));
	}));
	TestEqual(TEXT("nothing moved"), Field(Find(Files, SettingsDloc, SettingsNamespace, TEXT("Tip.Text")), EDreamLocField::Text), FString(TEXT("<none>")));
	TestTrue(TEXT("the old one is in removed"), Find(Files, SettingsDloc, SettingsNamespace, TEXT("Hint.Text"), true).bFound);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocRenameNamespaceTest, "DreamLoc.Rename.ANamespaceThatMovedTakesItsTranslationsWhenMostOfItMatches",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocRenameNamespaceTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocSyncTestsLocal;
	const TCHAR* const Keys[] = { TEXT("A.Text"), TEXT("B.Text"), TEXT("C.Text"), TEXT("D.Text"), TEXT("E.Text") };
	const TCHAR* const Sources[] = { TEXT("一"), TEXT("二"), TEXT("三"), TEXT("四"), TEXT("五") };
	auto Screen = [&Keys, &Sources](const TCHAR* InNamespace, int32 InChanged)
	{
		TArray<FDreamLocExtractEntry> Entries;
		for (int32 Index = 0; Index < 5; ++Index)
		{
			Entries.Add(Text(Keys[Index], Index < InChanged ? *FString::Printf(TEXT("%s改"), Sources[Index]) : Sources[Index]));
		}
		return Manifest(SettingsDui, InNamespace, MoveTemp(Entries));
	};
	FFiles Start = Sync(*this, TEXT("five texts"), MakeInput({ Screen(TEXT("/Game/UI/WBP_A"), 0) }, {}));
	for (int32 Index = 0; Index < 5; ++Index)
	{
		Translate(Start, SettingsDloc, TEXT("/Game/UI/WBP_A"), Keys[Index], FString::Printf(TEXT("T%d"), Index));
	}

	FDreamLocSyncOutput Output;
	FFiles Files = Sync(*this, TEXT("the class line changed, one text too"), MakeInput({ Screen(TEXT("/Game/UI/WBP_B"), 1) }, Start), &Output);
	TestTrue(TEXT("the namespace moved"), HasCode(Output, DreamLocCode::NamespaceMoved));
	TestEqual(TEXT("all five translations with it"), Output.Migrated, 5);
	TestEqual(TEXT("an unchanged one"), Field(Find(Files, SettingsDloc, TEXT("/Game/UI/WBP_B"), TEXT("E.Text")), EDreamLocField::Text), FString(TEXT("T4")));
	const FFound Changed = Find(Files, SettingsDloc, TEXT("/Game/UI/WBP_B"), TEXT("A.Text"));
	TestEqual(TEXT("the changed one, with its translation"), Field(Changed, EDreamLocField::Text), FString(TEXT("T0")));
	TestEqual(TEXT("and a prev"), Field(Changed, EDreamLocField::Prev), FString(TEXT("一")));
	TestFalse(TEXT("the old namespace is gone"), FileText(Files, SettingsDloc).Contains(TEXT("/Game/UI/WBP_A")));

	Files = Sync(*this, TEXT("the class line changed, three texts too"), MakeInput({ Screen(TEXT("/Game/UI/WBP_C"), 3) }, Start), &Output);
	TestTrue(TEXT("only a warning: 2 of 5 is too few"), HasCode(Output, DreamLocCode::RenameCandidates) && !HasCode(Output, DreamLocCode::NamespaceMoved));
	TestEqual(TEXT("nothing moved"), Field(Find(Files, SettingsDloc, TEXT("/Game/UI/WBP_C"), TEXT("E.Text")), EDreamLocField::Text), FString(TEXT("<none>")));
	TestTrue(TEXT("the old ones wait in removed"), Find(Files, SettingsDloc, TEXT("/Game/UI/WBP_A"), TEXT("E.Text"), true).bFound);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocRenameMovedFileTest, "DreamLoc.Rename.AMovedSourceTakesItsTranslationsToItsNewFile",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocRenameMovedFileTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocSyncTestsLocal;
	const FFiles Start = MakeTranslatedSettings(*this);
	const TCHAR* const MovedDui = TEXT("DUI/Menus/Settings.dui");
	const FString MovedDloc = TEXT("DUI/Menus/Settings.dloc");

	FDreamLocSyncOutput Output;
	FFiles Files = Sync(*this, TEXT("moved, with a class line"), MakeInput({ Manifest(MovedDui, SettingsNamespace,
		{ Text(TEXT("Title.Text"), TEXT("设置"), TEXT("Title")), Text(TEXT("Hint.Text"), TEXT("按任意键继续"), TEXT("Footer/Hint")) }) }, Start), &Output);
	TestFalse(TEXT("the old file is gone"), HasFile(Files, SettingsDloc));
	const FFound Title = Find(Files, MovedDloc, SettingsNamespace, TEXT("Title.Text"));
	TestEqual(TEXT("the translation is in the new one"), Field(Title, EDreamLocField::Text), FString(TEXT("Settings")));
	TestEqual(TEXT("pointing at the new source"), Field(Title, EDreamLocField::At), FString(TEXT("DUI/Menus/Settings.dui#Title")));
	TestEqual(TEXT("two moved"), Output.Moved, 2);

	// Without a class line the namespace is the source's path, so it changes with the move.
	FFiles NoClass = Sync(*this, TEXT("a .dui without a class line"), MakeInput({ Manifest(SettingsDui, TEXT("I:/Project/DUI/UI/Settings.dui"),
		{ Text(TEXT("Title.Text"), TEXT("设置"), TEXT("Title")), Text(TEXT("Hint.Text"), TEXT("按任意键继续"), TEXT("Footer/Hint")) }) }, {}));
	Translate(NoClass, SettingsDloc, TEXT("I:/Project/DUI/UI/Settings.dui"), TEXT("Title.Text"), TEXT("Settings"));
	NoClass = Sync(*this, TEXT("moved, without a class line"), MakeInput({ Manifest(MovedDui, TEXT("I:/Project/DUI/Menus/Settings.dui"),
		{ Text(TEXT("Title.Text"), TEXT("设置"), TEXT("Title")), Text(TEXT("Hint.Text"), TEXT("按任意键继续"), TEXT("Footer/Hint")) }) }, NoClass), &Output);
	TestFalse(TEXT("the old file is gone"), HasFile(NoClass, SettingsDloc));
	TestEqual(TEXT("the translation followed the namespace and the file"),
		Field(Find(NoClass, MovedDloc, TEXT("I:/Project/DUI/Menus/Settings.dui"), TEXT("Title.Text")), EDreamLocField::Text), FString(TEXT("Settings")));
	TestTrue(TEXT("as a namespace move"), HasCode(Output, DreamLocCode::NamespaceMoved));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocSyncEngineMergeTest, "DreamLoc.Sync.TheEngineGathersOtherToolsTextsOnceAndConflictsAreReported",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocSyncEngineMergeTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocSyncTestsLocal;
	const FDreamLocExtractManifest Dui = Manifest(SettingsDui, SettingsNamespace, { Text(TEXT("Title.Text"), TEXT("设置"), TEXT("Title")) });
	FDreamLocExtractManifest Asset = Manifest(TEXT("Content/UI/WBP_Settings.uasset"), SettingsNamespace,
		{ Text(TEXT("Title.Text"), TEXT("设置"), TEXT("WBP_Settings:WidgetTree.Title")) }, DreamLocExtract::EngineProvider);
	Asset.FindOrAddNamespace(TEXT("Game")).Entries.Add(Text(TEXT("Extra"), TEXT("额外")));

	FDreamLocSyncOutput Output;
	// The engine's manifest first, as the files list them: the order must not matter.
	const FFiles Files = Sync(*this, TEXT("the asset DreamGUI made, gathered"), MakeInput({ Asset, Dui }, {}), &Output);
	TestTrue(TEXT("the text is in the .dui's file"), Find(Files, SettingsDloc, SettingsNamespace, TEXT("Title.Text")).bFound);
	TestFalse(TEXT("and not twice"), Find(Files, TEXT("Engine/Content/UI/WBP_Settings.dloc"), SettingsNamespace, TEXT("Title.Text")).bFound);
	TestTrue(TEXT("the asset's own text is in the asset's file"), Find(Files, TEXT("Engine/Content/UI/WBP_Settings.dloc"), TEXT("Game"), TEXT("Extra")).bFound);
	TestFalse(TEXT("no conflict"), HasCode(Output, DreamLocCode::SourceConflict));

	Asset.Namespaces[0].Entries[0].Source = TEXT("设置！");
	const FDreamLocSyncOutput Conflict = DreamLocSync::Run(MakeInput({ Asset, Dui }, {}));
	TestTrue(TEXT("a different text under the same key is reported"), HasCode(Conflict, DreamLocCode::SourceConflict));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocSyncGatherCompleteTest, "DreamLoc.Sync.AfterAGatherAFileWithoutTextsHasNone",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocSyncGatherCompleteTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocSyncTestsLocal;
	const FString OldDloc = TEXT("Engine/Source/Game/Private/Old.dloc");
	FFiles Files;
	Files.FindOrAdd(TEXT("en")).Add(OldDloc, TEXT("dloc 1\nculture en\nnamespace Game\n\nGone {\n    at    Source/Game/Private/Old.cpp\n    src   \"走了\"\n    text  \"Gone\"\n}\n"));

	const FDreamLocSyncOutput NotGathered = DreamLocSync::Run(MakeInput({}, Files, { TEXT("Source/Game/Private/Old.cpp") }));
	TestEqual(TEXT("never gathered: left alone"), NotGathered.Changes.Num(), 0);

	FDreamLocSyncInput Input = MakeInput({}, Files, { TEXT("Source/Game/Private/Old.cpp") });
	Input.CompleteProviders.Add(DreamLocExtract::EngineProvider);
	const FFiles After = Sync(*this, TEXT("gathered, and the file has no texts"), Input);
	TestTrue(TEXT("the text is in removed"), Find(After, OldDloc, TEXT("Game"), TEXT("Gone"), true).bFound);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocSyncBlockedTest, "DreamLoc.Sync.AFileThatDoesNotReadStopsEverything",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocSyncBlockedTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocSyncTestsLocal;
	FFiles Files;
	Files.FindOrAdd(TEXT("en")).Add(TEXT("DUI/Bad.dloc"), TEXT("dloc 1\nculture en\nnamespace X\n\nK {\n    src   \"a\"\n    text\n}\n"));
	const FDreamLocSyncOutput Output = DreamLocSync::Run(MakeInput({ Manifest(SettingsDui, SettingsNamespace, { Text(TEXT("Title.Text"), TEXT("设置")) }) }, Files));
	TestTrue(TEXT("blocked"), Output.bBlocked);
	TestEqual(TEXT("nothing to write"), Output.Changes.Num(), 0);
	TestTrue(TEXT("the file's error and why nothing was written"), HasCode(Output, DreamLocCode::SyncBlocked) && Output.NumErrors() >= 2);
	AddInfo(Codes(Output));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocMigrateTest, "DreamLoc.Migrate.MovesATranslationSyncLeftInRemoved",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocMigrateTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocSyncTestsLocal;
	FFiles Files = MakeTranslatedSettings(*this);
	// The hint became Tip with a slightly different text: sync only hints at it.
	Files = Sync(*this, TEXT("the hint became Tip"), MakeInput({ Manifest(SettingsDui, SettingsNamespace,
		{ Text(TEXT("Title.Text"), TEXT("设置"), TEXT("Title")), Text(TEXT("Tip.Text"), TEXT("按任意键继续游戏"), TEXT("Footer/Tip")) }) }, Files));

	DreamLocSync::FMigrateRequest Request;
	Request.FromKey = TEXT("Hint.Text");
	Request.ToKey = TEXT("Tip.Text");
	const FDreamLocSyncOutput Output = DreamLocSync::Migrate(Files, Request);
	TestEqual(TEXT("no errors"), Output.NumErrors(), 0);
	Files = Apply(Files, Output);
	const FFound Tip = Find(Files, SettingsDloc, SettingsNamespace, TEXT("Tip.Text"));
	TestEqual(TEXT("the translation moved"), Field(Tip, EDreamLocField::Text), FString(TEXT("Press any key")));
	TestEqual(TEXT("with the text it was made from"), Field(Tip, EDreamLocField::Prev), FString(TEXT("按任意键继续")));
	TestFalse(TEXT("the removed block is empty, so gone"), FileText(Files, SettingsDloc).Contains(TEXT("removed {")));

	Request.FromKey = TEXT("Nothing.Text");
	TestTrue(TEXT("an unknown key is an error"), HasCode(DreamLocSync::Migrate(Files, Request), DreamLocCode::MigrateNotFound));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocExtractTest, "DreamLoc.Extract.ManifestsReadAndWriteInBothShapes",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocExtractTest::RunTest(const FString& Parameters)
{
	const FString One = TEXT(R"({
  "version": 1,
  "provider": "DreamGUI",
  "source": "DUI/UI/Settings.dui",
  "namespace": "/Game/UI/WBP_Settings",
  "entries": [
    { "key": "Title.Text", "src": "设置", "at": "Title", "kind": "DreamText" },
    { "key": "Apply.Text", "src": "应用 \"{Count}\"", "at": "Footer/Apply", "note": "底部按钮", "max": 16, "renamedFrom": "Ok.Text" }
  ]
}
)");
	FDreamLocExtractManifest Manifest;
	FString Error;
	TestTrue(TEXT("one namespace reads"), DreamLocExtract::Parse(One, Manifest, Error));
	TestEqual(TEXT("its texts"), Manifest.NumEntries(), 2);
	if (Manifest.NumEntries() == 2)
	{
		const FDreamLocExtractEntry& Apply = Manifest.Namespaces[0].Entries[1];
		TestEqual(TEXT("an escaped quote"), Apply.Source, FString(TEXT("应用 \"{Count}\"")));
		TestEqual(TEXT("max"), Apply.Max, 16);
		TestEqual(TEXT("renamedFrom"), Apply.RenamedFrom, FString(TEXT("Ok.Text")));
	}
	TestEqual(TEXT("written back, the same bytes"), DreamLocExtract::Write(Manifest), One);
	TestEqual(TEXT("its .dloc"), DreamLocExtract::GetDlocPath(Manifest), FString(TEXT("DUI/UI/Settings.dloc")));

	Manifest.Provider = DreamLocExtract::EngineProvider;
	Manifest.FindOrAddNamespace(TEXT("Game")).Entries.Add(FDreamLocExtractEntry{ TEXT("Quit"), TEXT("退出") });
	FDreamLocExtractManifest Again;
	TestTrue(TEXT("several namespaces read"), DreamLocExtract::Parse(DreamLocExtract::Write(Manifest), Again, Error));
	TestEqual(TEXT("both namespaces"), Again.Namespaces.Num(), 2);
	TestEqual(TEXT("and stay the same"), DreamLocExtract::Write(Again), DreamLocExtract::Write(Manifest));
	TestEqual(TEXT("an engine manifest's .dloc"), DreamLocExtract::GetDlocPath(Again), FString(TEXT("Engine/DUI/UI/Settings.dloc")));

	TestFalse(TEXT("not JSON"), DreamLocExtract::Parse(TEXT("{ nope"), Manifest, Error));
	TestFalse(TEXT("no source"), DreamLocExtract::Parse(TEXT("{ \"version\": 1, \"provider\": \"X\" }"), Manifest, Error));
	TestEqual(TEXT("at, source"), DreamLocExtract::GetAtSource(TEXT("DUI/A.dui#Title")), FString(TEXT("DUI/A.dui")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocImportTest, "DreamLoc.Import.AnEngineTargetsTranslationsReachTheDlocFiles",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocImportTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocSyncTestsLocal;
	const FString Project = DreamLocTestUtil::MakeTempDirectory(TEXT("Import"));
	ON_SCOPE_EXIT { DreamLocTestUtil::DeleteDirectory(Project); };

	// A localization target as the dashboard leaves it: a manifest and an English archive.
	{
		FLocTextHelper Helper(Project / TEXT("Content/Localization/Game"), TEXT("Game.manifest"), TEXT("Game.archive"), TEXT("zh-Hans"), { TEXT("en") }, nullptr);
		FText Error;
		TestTrue(TEXT("a new target"), Helper.LoadAll(ELocTextHelperLoadFlags::Create, &Error));
		const TPair<const TCHAR*, const TCHAR*> Texts[] = { { TEXT("Quit"), TEXT("退出") }, { TEXT("Start"), TEXT("开始游戏") },
			{ TEXT("Done"), TEXT("完成") }, { TEXT("Missing"), TEXT("缺的") } };
		for (const TPair<const TCHAR*, const TCHAR*>& Pair : Texts)
		{
			FManifestContext Context;
			Context.Key = Pair.Key;
			Context.SourceLocation = TEXT("Source/Game/Private/Menu.cpp(1)");
			Helper.AddSourceText(FLocKey(TEXT("Game")), FLocItem(Pair.Value), Context);
		}
		Helper.AddTranslation(TEXT("en"), FLocKey(TEXT("Game")), FLocKey(TEXT("Quit")), nullptr, FLocItem(TEXT("退出")), FLocItem(TEXT("Quit")), false);
		// Translated before the source text changed.
		Helper.AddTranslation(TEXT("en"), FLocKey(TEXT("Game")), FLocKey(TEXT("Start")), nullptr, FLocItem(TEXT("开始")), FLocItem(TEXT("Start")), false);
		Helper.AddTranslation(TEXT("en"), FLocKey(TEXT("Game")), FLocKey(TEXT("Done")), nullptr, FLocItem(TEXT("完成")), FLocItem(TEXT("Finished")), false);
		Helper.AddTranslation(TEXT("en"), FLocKey(TEXT("Game")), FLocKey(TEXT("Missing")), nullptr, FLocItem(TEXT("缺的")), FLocItem(TEXT("Missing")), false);
		TestTrue(TEXT("saved"), Helper.SaveAll(&Error));
	}

	TMap<FString, TArray<DreamLocEngine::FImportedText>> Imported;
	TArray<FDreamLocReportLine> Messages;
	TestTrue(TEXT("the target reads"), DreamLocEngine::ReadTarget(Project, TEXT("Game"), TEXT("zh-Hans"), Imported, Messages));
	TestEqual(TEXT("four English translations"), Imported.Contains(TEXT("en")) ? Imported[TEXT("en")].Num() : 0, 4);

	const FString MenuDloc = TEXT("Engine/Source/Game/Private/Menu.dloc");
	FFiles Files = Sync(*this, TEXT("the gathered texts"), MakeInput({ Manifest(TEXT("Source/Game/Private/Menu.cpp"), TEXT("Game"),
		{ Text(TEXT("Quit"), TEXT("退出")), Text(TEXT("Start"), TEXT("开始游戏")), Text(TEXT("Done"), TEXT("完成")) }, DreamLocExtract::EngineProvider) }, {}));
	Translate(Files, MenuDloc, TEXT("Game"), TEXT("Done"), TEXT("Complete"), TEXT("done"));

	FDreamLocSyncOutput Output = DreamLocEngine::ApplyImport(Imported, Files, TEXT("Game"), /*bInForce*/ false);
	FFiles After = Apply(Files, Output);
	TestEqual(TEXT("a translation into its entry"), Field(Find(After, MenuDloc, TEXT("Game"), TEXT("Quit")), EDreamLocField::Text), FString(TEXT("Quit")));
	const FFound Start = Find(After, MenuDloc, TEXT("Game"), TEXT("Start"));
	TestEqual(TEXT("one made from an older text"), Field(Start, EDreamLocField::Text), FString(TEXT("Start")));
	TestEqual(TEXT("comes as stale"), Field(Start, EDreamLocField::Prev), FString(TEXT("开始")));
	TestEqual(TEXT("a reviewed one is kept"), Field(Find(After, MenuDloc, TEXT("Game"), TEXT("Done")), EDreamLocField::Text), FString(TEXT("Complete")));
	TestTrue(TEXT("and sync says so"), HasCode(Output, DreamLocCode::ImportKept));
	const FString ImportedDloc = TEXT("Engine/Imported/Game.dloc");
	TestEqual(TEXT("a key no file has waits in the import file"), Field(Find(After, ImportedDloc, TEXT("Game"), TEXT("Missing")), EDreamLocField::Text),
		FString(TEXT("Missing")));

	Output = DreamLocEngine::ApplyImport(Imported, Files, TEXT("Game"), /*bInForce*/ true);
	TestEqual(TEXT("-Force replaces a reviewed one"), Field(Find(Apply(Files, Output), MenuDloc, TEXT("Game"), TEXT("Done")), EDreamLocField::Text),
		FString(TEXT("Finished")));

	// The next gather finds the missing key in a file: sync moves it there.
	After = Sync(*this, TEXT("the key is found"), MakeInput({ Manifest(TEXT("Source/Game/Private/Menu.cpp"), TEXT("Game"),
		{ Text(TEXT("Quit"), TEXT("退出")), Text(TEXT("Start"), TEXT("开始游戏")), Text(TEXT("Done"), TEXT("完成")), Text(TEXT("Missing"), TEXT("缺的")) },
		DreamLocExtract::EngineProvider) }, After));
	TestEqual(TEXT("it is in its file"), Field(Find(After, MenuDloc, TEXT("Game"), TEXT("Missing")), EDreamLocField::Text), FString(TEXT("Missing")));
	TestFalse(TEXT("the import file is empty, so gone"), HasFile(After, ImportedDloc));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
