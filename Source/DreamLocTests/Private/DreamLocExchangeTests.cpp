// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocExchange.h"
#include "DreamLocFile.h"
#include "DreamLocTestUtil.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamLocExchangeTestsLocal
{
	using FFiles = TMap<FString, TMap<FString, FString>>;

	/** A screen's worth of texts, with everything a format can trip on: quotes, commas, line breaks, tags, blanks, Chinese. */
	const TCHAR* const ScreenFile = TEXT(R"(dloc 1
culture en
namespace /Game/UI/WBP_Settings

Title.Text {
    at    DUI/UI/Settings.dui#Title
    note  "页面标题, 简短"
    max   16
    src   "设置"
    text  "Settings"
    state done
}

Quote.Text {
    at    DUI/UI/Settings.dui#Quote
    src   "他说：\"好，走吧。\""
    text  "He said: \"Fine, let's go.\""
    state draft
}

Lines.Text {
    at    DUI/UI/Settings.dui#Lines
    src   " 第一行\n第二行 & <b>粗</b> "
    text  " First line\nsecond & <b>bold</b> "
    state machine
}

Count.Text {
    at    DUI/UI/Settings.dui#Count
    src   "已保存 {Count} 个, 共 {Total} 个"
    text  "{Count} of {Total} saved"
}

Hint.Text {
    at    DUI/UI/Settings.dui#Hint
    prev  "按 {Key} 返回"
    src   "按 {Key} 返回上一页"
    text  "Press {Key} to go back"
    state done
}

Implant.Text {
    at    DUI/UI/Settings.dui#Implant
    src   "义体由 DreamMoon 提供"
}
)");

	FFiles MakeFiles()
	{
		FFiles Files;
		Files.FindOrAdd(TEXT("en")).Add(TEXT("DUI/UI/Settings.dloc"), ScreenFile);
		Files.FindOrAdd(TEXT("en")).Add(TEXT("_terms.dloc"), TEXT("dloc 1\nculture en\nterms\n\nterm \"义体\" { text \"Cyberware\" }\nkeep \"DreamMoon\"\n"));
		Files.FindOrAdd(TEXT("ja")).Add(TEXT("DUI/UI/Settings.dloc"), TEXT("dloc 1\nculture ja\nnamespace /Game/UI/WBP_Settings\n\nTitle.Text {\n    src   \"设置\"\n    text  \"設定\"\n}\n"));
		return Files;
	}

	/** The same files with every translation and state taken out: what importing everything back must restore. */
	FFiles Stripped(const FFiles& InFiles)
	{
		FFiles Files = InFiles;
		for (TPair<FString, TMap<FString, FString>>& Culture : Files)
		{
			for (TPair<FString, FString>& File : Culture.Value)
			{
				FDreamLocDocument Document;
				TArray<FDreamLocDiagnostic> Diagnostics;
				DreamLocFile::Parse(File.Value, Document, Diagnostics);
				for (FDreamLocItem& Item : Document.Items)
				{
					if (Item.Kind == EDreamLocItemKind::Entry && !Item.Entry.Has(EDreamLocField::Prev))
					{
						Item.Entry.Remove(EDreamLocField::Text);
						Item.Entry.Remove(EDreamLocField::State);
					}
				}
				File.Value = DreamLocFile::Write(Document);
			}
		}
		return Files;
	}

	const FDreamLocExchangeEntry* FindEntry(const FDreamLocExchangeBatch& InBatch, const TCHAR* InKey)
	{
		return InBatch.Entries.FindByPredicate([InKey](const FDreamLocExchangeEntry& InEntry) { return InEntry.Key == InKey; });
	}

	FString Describe(const FDreamLocExchangeEntry& InEntry)
	{
		return FString::Printf(TEXT("%s|%s|%s|%s|%s"), *InEntry.Namespace, *InEntry.Key, *InEntry.Source, *InEntry.Text, *InEntry.State);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocExportTest, "DreamLoc.Exchange.AnExportCarriesWhatATranslatorNeeds",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocExportTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocExchangeTestsLocal;
	TArray<FDreamLocReportLine> Messages;
	DreamLocExchange::FExportOptions Options;
	Options.Culture = TEXT("en");
	Options.References = { TEXT("ja") };
	FDreamLocExchangeBatch Batch = DreamLocExchange::Collect(MakeFiles(), Options, TEXT("zh-Hans"), Messages);
	TArray<FString> Keys;
	for (const FDreamLocExchangeEntry& Entry : Batch.Entries)
	{
		Keys.Add(Entry.Key);
	}
	TestEqual(TEXT("by default what has no translation and what is stale"), FString::Join(Keys, TEXT(",")), FString(TEXT("Hint.Text,Implant.Text")));
	const FDreamLocExchangeEntry* Hint = FindEntry(Batch, TEXT("Hint.Text"));
	TestTrue(TEXT("a stale entry has no text to fill, and the old source and translation beside it"),
		Hint != nullptr && Hint->Text.IsEmpty() && Hint->Prev == TEXT("按 {Key} 返回") && Hint->PreviousText == TEXT("Press {Key} to go back"));
	const FDreamLocExchangeEntry* Implant = FindEntry(Batch, TEXT("Implant.Text"));
	TestTrue(TEXT("the terms the source uses"), Implant != nullptr && Implant->Terms.Num() == 1 && Implant->Terms[0].Value == TEXT("Cyberware"));
	TestTrue(TEXT("and the words kept"), Implant != nullptr && Implant->Keeps.Num() == 1 && Implant->Keeps[0] == TEXT("DreamMoon"));
	TestTrue(TEXT("the texts around it on the screen"), Implant != nullptr && Implant->Context.Num() == 2 && Implant->Context[1] == TEXT("按 {Key} 返回上一页"));

	Options.Only = { TEXT("all") };
	Batch = DreamLocExchange::Collect(MakeFiles(), Options, TEXT("zh-Hans"), Messages);
	const FDreamLocExchangeEntry* Title = FindEntry(Batch, TEXT("Title.Text"));
	TestTrue(TEXT("-Only all: everything, with its note, max and where"), Batch.Entries.Num() == 6 && Title != nullptr && Title->Note == TEXT("页面标题, 简短")
		&& Title->Max == 16 && Title->At == TEXT("DUI/UI/Settings.dui#Title"));
	TestTrue(TEXT("-Reference ja: Japanese beside it"), Title != nullptr && Title->References.Num() == 1 && Title->References[0].Value == TEXT("設定"));

	Options.Only = { TEXT("done") };
	TestEqual(TEXT("-Only done"), DreamLocExchange::Collect(MakeFiles(), Options, TEXT("zh-Hans"), Messages).Entries.Num(), 1);
	Options.Only = { TEXT("all") };
	Options.Files = { TEXT("DUI/Other") };
	TestEqual(TEXT("-Files: another screen has nothing"), DreamLocExchange::Collect(MakeFiles(), Options, TEXT("zh-Hans"), Messages).Entries.Num(), 0);

	const FString Json = DreamLocExchange::Write(DreamLocExchange::Collect(MakeFiles(), DreamLocExchange::FExportOptions{ TEXT("en") }, TEXT("zh-Hans"), Messages),
		EDreamLocExchangeFormat::Json);
	TestTrue(TEXT("the JSON carries its instructions for an AI"), Json.Contains(TEXT("\"instructions\": \"Translate each entry")));
	TestTrue(TEXT("and the culture's terms"), Json.Contains(TEXT("\"terms\": [{ \"term\": \"义体\", \"text\": \"Cyberware\" }]")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocExchangeRoundTripTest, "DreamLoc.Exchange.EveryFormatComesBackAsItWent",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocExchangeRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocExchangeTestsLocal;
	const FFiles Files = MakeFiles();
	TArray<FDreamLocReportLine> Messages;
	DreamLocExchange::FExportOptions Options;
	Options.Culture = TEXT("en");
	Options.Only = { TEXT("all") };
	const FDreamLocExchangeBatch Batch = DreamLocExchange::Collect(Files, Options, TEXT("zh-Hans"), Messages);
	const FFiles Empty = Stripped(Files);

	for (const EDreamLocExchangeFormat Format : { EDreamLocExchangeFormat::Json, EDreamLocExchangeFormat::Xliff, EDreamLocExchangeFormat::Po, EDreamLocExchangeFormat::Csv })
	{
		const FString Name = DreamLocExchange::ExtensionOf(Format);
		const FString Text = DreamLocExchange::Write(Batch, Format);
		FDreamLocExchangeBatch Read;
		FString Error;
		if (!TestTrue(FString::Printf(TEXT("%s reads back"), *Name), DreamLocExchange::Read(Text, Format, Read, Error)))
		{
			AddInfo(Error);
			continue;
		}
		TestEqual(FString::Printf(TEXT("%s: the cultures"), *Name), Read.SourceCulture + TEXT(">") + Read.Culture, FString(TEXT("zh-Hans>en")));
		TestEqual(FString::Printf(TEXT("%s: every entry"), *Name), Read.Entries.Num(), Batch.Entries.Num());
		for (int32 Index = 0; Index < FMath::Min(Read.Entries.Num(), Batch.Entries.Num()); ++Index)
		{
			TestEqual(FString::Printf(TEXT("%s: %s"), *Name, *Batch.Entries[Index].Key), Describe(Read.Entries[Index]), Describe(Batch.Entries[Index]));
		}
		// Imported into the files without their translations, the translations are as they were.
		const FDreamLocSyncOutput Output = DreamLocExchange::Import(Read, Empty, DreamLocExchange::FImportOptions());
		FString After = Empty[TEXT("en")][TEXT("DUI/UI/Settings.dloc")];
		if (const TMap<FString, TOptional<FString>>* Changes = Output.Changes.Find(TEXT("en")))
		{
			if (const TOptional<FString>* Changed = Changes->Find(TEXT("DUI/UI/Settings.dloc")); Changed != nullptr && Changed->IsSet())
			{
				After = Changed->GetValue();
			}
		}
		TestEqual(FString::Printf(TEXT("%s: imported back, the file is as it was"), *Name), After, Files[TEXT("en")][TEXT("DUI/UI/Settings.dloc")]);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocExchangeImportTest, "DreamLoc.Exchange.AnImportKeepsReviewedTranslationsAndMarksStaleOnes",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocExchangeImportTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocExchangeTestsLocal;
	const FFiles Files = MakeFiles();
	FDreamLocExchangeBatch Batch;
	Batch.Culture = TEXT("en");
	auto Add = [&Batch](const TCHAR* InKey, const TCHAR* InSource, const TCHAR* InText)
	{
		FDreamLocExchangeEntry& Entry = Batch.Entries.AddDefaulted_GetRef();
		Entry.Namespace = TEXT("/Game/UI/WBP_Settings");
		Entry.Key = InKey;
		Entry.Source = InSource;
		Entry.Text = InText;
	};
	Add(TEXT("Title.Text"), TEXT("设置"), TEXT("Options"));
	Add(TEXT("Implant.Text"), TEXT("义体由 DreamMoon 提供"), TEXT("Cyberware by DreamMoon"));
	Add(TEXT("Count.Text"), TEXT("已保存 {Count} 个"), TEXT("{Count} saved"));
	Add(TEXT("Nowhere.Text"), TEXT("无"), TEXT("None"));

	DreamLocExchange::FImportOptions Options;
	Options.State = TEXT("machine");
	FDreamLocSyncOutput Output = DreamLocExchange::Import(Batch, Files, Options);
	const FString After = Output.Changes[TEXT("en")][TEXT("DUI/UI/Settings.dloc")].GetValue();
	TestTrue(TEXT("a reviewed translation is kept"), After.Contains(TEXT("text  \"Settings\"")) && !After.Contains(TEXT("\"Options\"")));
	TestTrue(TEXT("a new one comes in with the state asked for"), After.Contains(TEXT("text  \"Cyberware by DreamMoon\"\n    state machine")));
	TestTrue(TEXT("one made for a source text that has changed since comes in stale"), After.Contains(TEXT("prev  \"已保存 {Count} 个\"")));
	TestTrue(TEXT("a key no file has is reported"), Output.Messages.ContainsByPredicate([](const FDreamLocReportLine& InLine) { return InLine.Diagnostic.Code == DreamLocCode::ImportUnknownKey; }));
	TestTrue(TEXT("and so is what was kept"), Output.Messages.ContainsByPredicate([](const FDreamLocReportLine& InLine) { return InLine.Diagnostic.Code == DreamLocCode::ImportKept; }));

	Options.bForce = true;
	Output = DreamLocExchange::Import(Batch, Files, Options);
	TestTrue(TEXT("-Force replaces a reviewed one"), Output.Changes[TEXT("en")][TEXT("DUI/UI/Settings.dloc")].GetValue().Contains(TEXT("text  \"Options\"")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
