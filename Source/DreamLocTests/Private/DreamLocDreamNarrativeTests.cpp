// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocExchange.h"
#include "DreamLocExtract.h"
#include "DreamLocFile.h"
#include "DreamLocSync.h"
#include "DreamLocTestUtil.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamLocDreamNarrativeTestsLocal
{
	using FFiles = TMap<FString, TMap<FString, FString>>;

	/** The manifest DreamNarrative wrote for a real .dna (a lighthouse keeper's first night): the plugin's Tests/DreamNarrative. */
	bool LoadSample(FDreamLocExtractManifest& OutManifest, FString& OutError)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DreamLoc"));
		const FString Path = Plugin.IsValid() ? FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / TEXT("Tests/DreamNarrative/Lighthouse.dna.json")) : FString();
		return DreamLocExtract::LoadFile(Path, OutManifest, OutError);
	}

	const FDreamLocExtractEntry* FindExtracted(const FDreamLocExtractManifest& InManifest, const TCHAR* InKey)
	{
		for (const FDreamLocExtractNamespace& Namespace : InManifest.Namespaces)
		{
			if (const FDreamLocExtractEntry* Entry = Namespace.Entries.FindByPredicate([InKey](const FDreamLocExtractEntry& In) { return In.Key == InKey; }))
			{
				return Entry;
			}
		}
		return nullptr;
	}

	FDreamLocExtractEntry* FindExtracted(FDreamLocExtractManifest& InManifest, const TCHAR* InKey)
	{
		return const_cast<FDreamLocExtractEntry*>(FindExtracted(static_cast<const FDreamLocExtractManifest&>(InManifest), InKey));
	}

	FString ContextOf(const FDreamLocExtractEntry* InEntry, const TCHAR* InName)
	{
		const FDreamLocExtractContext* Item = InEntry != nullptr ? InEntry->Context.FindByPredicate([InName](const FDreamLocExtractContext& In) { return In.Name == InName; }) : nullptr;
		return Item != nullptr ? Item->Describe() : FString(TEXT("<none>"));
	}

	FDreamLocSyncOutput Sync(const FDreamLocExtractManifest& InManifest, FFiles& InOutFiles)
	{
		FDreamLocSyncInput Input;
		Input.Manifests = { InManifest };
		Input.Cultures = { TEXT("en") };
		Input.Files = InOutFiles;
		const FString Source = InManifest.Source;
		Input.SourceExists = [Source](const FString& InSource) { return InSource == Source; };
		FDreamLocSyncOutput Output = DreamLocSync::Run(Input);
		for (const TPair<FString, TMap<FString, TOptional<FString>>>& Culture : Output.Changes)
		{
			for (const TPair<FString, TOptional<FString>>& Change : Culture.Value)
			{
				if (Change.Value.IsSet())
				{
					InOutFiles.FindOrAdd(Culture.Key).Add(Change.Key, Change.Value.GetValue());
				}
				else
				{
					InOutFiles.FindOrAdd(Culture.Key).Remove(Change.Key);
				}
			}
		}
		return Output;
	}

	void Apply(const FDreamLocSyncOutput& InOutput, FFiles& InOutFiles)
	{
		for (const TPair<FString, TMap<FString, TOptional<FString>>>& Culture : InOutput.Changes)
		{
			for (const TPair<FString, TOptional<FString>>& Change : Culture.Value)
			{
				InOutFiles.FindOrAdd(Culture.Key).Add(Change.Key, Change.Value.Get(FString()));
			}
		}
	}

	/** Sets a translation in the .dloc text, as a translator would. */
	FString Translate(const FString& InFile, TMap<FString, FString> InTexts)
	{
		FDreamLocDocument Document;
		TArray<FDreamLocDiagnostic> Diagnostics;
		DreamLocFile::Parse(InFile, Document, Diagnostics);
		for (FDreamLocItem& Item : Document.Items)
		{
			if (const FString* Text = Item.Kind == EDreamLocItemKind::Entry ? InTexts.Find(Item.Entry.Key) : nullptr)
			{
				Item.Entry.Set(EDreamLocField::Text, *Text);
			}
		}
		return DreamLocFile::Write(Document);
	}

	int32 CountCode(const FDreamLocSyncOutput& InOutput, int32 InCode)
	{
		int32 Count = 0;
		for (const FDreamLocReportLine& Line : InOutput.Messages)
		{
			Count += Line.Diagnostic.Code == InCode ? 1 : 0;
		}
		return Count;
	}

	const TCHAR* const DlocPath = TEXT("DNarrative/DLocSample/灯塔.dloc");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocDreamNarrativeSampleTest, "DreamLoc.DreamNarrative.ASampleSyncsExportsItsContextAndWarnsOfShiftedKeys",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocDreamNarrativeSampleTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocDreamNarrativeTestsLocal;
	FDreamLocExtractManifest Manifest;
	FString Error;
	if (!TestTrue(TEXT("the sample reads"), LoadSample(Manifest, Error)))
	{
		AddInfo(Error);
		return false;
	}
	TestEqual(TEXT("21 texts"), Manifest.NumEntries(), 21);
	const FDreamLocExtractEntry* First = FindExtracted(Manifest, TEXT("老周_初见.1"));
	TestTrue(TEXT("a line without an #id is numbered by its position"), First != nullptr && !First->bStable);
	TestTrue(TEXT("one with an #id is not"), FindExtracted(Manifest, TEXT("老周_灯坏")) != nullptr && FindExtracted(Manifest, TEXT("老周_灯坏"))->bStable);
	TestEqual(TEXT("its context: the speaker"), ContextOf(First, TEXT("speaker")), FString(TEXT("老周")));
	TestEqual(TEXT("how long it may take"), ContextOf(First, TEXT("for")), FString(TEXT("2.5")));
	TestEqual(TEXT("what each argument is"), ContextOf(FindExtracted(Manifest, TEXT("老周_初见.4")), TEXT("args")), FString(TEXT("{0} 找到的零件")));
	FDreamLocExtractManifest Again;
	TestTrue(TEXT("written and read again"), DreamLocExtract::Parse(DreamLocExtract::Write(Manifest), Again, Error));
	TestEqual(TEXT("the manifest keeps stable and context"), DreamLocExtract::Write(Again), DreamLocExtract::Write(Manifest));
	TestEqual(TEXT("its .dloc mirrors the .dna"), DreamLocExtract::GetDlocPath(Manifest), FString(DlocPath));

	// Synced, and two lines translated: one numbered by its position, one with an #id.
	FFiles Files;
	FDreamLocSyncOutput Output = Sync(Manifest, Files);
	TestEqual(TEXT("every text gets an entry"), Output.Added, 21);
	Files[TEXT("en")][DlocPath] = Translate(Files[TEXT("en")][DlocPath], { { TEXT("老周_初见.1"), TEXT("So you're the new keeper?") }, { TEXT("老周_灯坏"), TEXT("The lamp's been out three days.") } });

	// A line put in before the first: DreamNarrative numbers again, so .1 now has another text. The #id line was edited.
	FindExtracted(Manifest, TEXT("老周_初见.1"))->Source = TEXT("喂。");
	FindExtracted(Manifest, TEXT("老周_灯坏"))->Source = TEXT("灯坏了整整三天了。");
	Output = Sync(Manifest, Files);
	TestEqual(TEXT("both translations are stale"), Output.Outdated, 2);
	TestEqual(TEXT("only the one numbered by its position is a possible shift"), CountCode(Output, DreamLocCode::PositionalKeyChanged), 1);
	const FDreamLocReportLine* Shift = Output.Messages.FindByPredicate([](const FDreamLocReportLine& In) { return In.Diagnostic.Code == DreamLocCode::PositionalKeyChanged; });
	TestTrue(TEXT("about the line, against the .dna"), Shift != nullptr && Shift->Diagnostic.Message.Contains(TEXT("'老周_初见.1'")) && Shift->File == Manifest.Source);
	Output = Sync(Manifest, Files);
	TestEqual(TEXT("said again while the translation stays stale"), CountCode(Output, DreamLocCode::PositionalKeyChanged), 1);

	// Exported: each entry has what DreamNarrative knows about it.
	TArray<FDreamLocReportLine> Messages;
	DreamLocExchange::FExportOptions Export;
	Export.Culture = TEXT("en");
	Export.Only = { TEXT("all") };
	Export.Manifests = { Manifest };
	const FDreamLocExchangeBatch Batch = DreamLocExchange::Collect(Files, Export, TEXT("zh-Hans"), Messages);
	const FDreamLocExchangeEntry* Fourth = Batch.Entries.FindByPredicate([](const FDreamLocExchangeEntry& In) { return In.Key == TEXT("老周_初见.4"); });
	TestTrue(TEXT("the kind and the context come along"), Fourth != nullptr && Fourth->Kind == TEXT("line") && Fourth->Context.Num() == 5);
	const FString Json = DreamLocExchange::Write(Batch, EDreamLocExchangeFormat::Json);
	TestTrue(TEXT("JSON: the context as the manifest has it"), Json.Contains(TEXT("\"kind\": \"line\", \"context\": { \"speaker\": \"老周\", \"speakerDisplay\": \"老周头\", \"dialogue\": \"老周_初见\", \"previous\": \"灯室里缺三个零件。\", \"args\": [\"找到的零件\"] }")));
	const FString Po = DreamLocExchange::Write(Batch, EDreamLocExchangeFormat::Po);
	TestTrue(TEXT("PO: in a comment"), Po.Contains(TEXT("#. dloc-context: speaker: 老周; speakerDisplay: 老周头; dialogue: 老周_初见; previous: 灯室里缺三个零件。; args: {0} 找到的零件\n")));
	const FString Xliff = DreamLocExchange::Write(Batch, EDreamLocExchangeFormat::Xliff);
	TestTrue(TEXT("XLIFF: as contexts"), Xliff.Contains(TEXT("<context context-type=\"x-speaker\">老周</context>")) && Xliff.Contains(TEXT("<context context-type=\"x-kind\">line</context>")));
	for (const EDreamLocExchangeFormat Format : { EDreamLocExchangeFormat::Json, EDreamLocExchangeFormat::Xliff, EDreamLocExchangeFormat::Po, EDreamLocExchangeFormat::Csv })
	{
		FDreamLocExchangeBatch Read;
		DreamLocExchange::Read(DreamLocExchange::Write(Batch, Format), Format, Read, Error);
		const FDreamLocExchangeEntry* Back = Read.Entries.FindByPredicate([](const FDreamLocExchangeEntry& In) { return In.Key == TEXT("老周_初见.4"); });
		TestTrue(FString::Printf(TEXT("%s: the context is not read back as a note"), DreamLocExchange::ExtensionOf(Format)), Back != nullptr && Back->Note.IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocDreamNarrativeCsvTest, "DreamLoc.DreamNarrative.TheOldLocExportCsvImports",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocDreamNarrativeCsvTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocDreamNarrativeTestsLocal;
	FDreamLocExtractManifest Manifest;
	FString Error;
	if (!TestTrue(TEXT("the sample reads"), LoadSample(Manifest, Error)))
	{
		return false;
	}
	FFiles Files;
	Sync(Manifest, Files);

	// What `dnc loc-export` wrote and a translator filled in: one language, no culture in it, its own columns, a
	// translation over two lines, one made from an older source, one of a line that is gone.
	const FString Csv = FString(TEXT("\xFEFF"))
		+ TEXT("Namespace,Key,Source,Translation,Kind,Character,Dialogue,Previous,Tags,File,Line\r\n")
		+ TEXT("DNA/灯塔,老周_初见.1,你就是新来的守夜人?,\"So you're\nthe new keeper?\",line,老周,老周_初见,,皱眉,DNarrative/DLocSample/灯塔.dna,7\r\n")
		+ TEXT("DNA/灯塔,老周_灯坏,灯坏了三天了。,\"The lamp's been out three days.\",line,老周,老周_初见,你就是新来的守夜人?,,DNarrative/DLocSample/灯塔.dna,8\r\n")
		+ TEXT("DNA/灯塔,quest.修灯.title,修灯塔,\"Fix the lamp\",quest,,,,,DNarrative/DLocSample/灯塔.dna,20\r\n")
		+ TEXT("DNA/灯塔,老周_初见.c1,我去修。,\"I'll fix it.\",choice,,老周_初见,灯坏了三天了。,,DNarrative/DLocSample/灯塔.dna,9\r\n")
		+ TEXT("DNA/灯塔,老周_删掉的,删掉的一句,\"A cut line\",line,老周,老周_初见,,,DNarrative/DLocSample/灯塔.dna,30\r\n");
	FDreamLocExchangeBatch Batch;
	TestTrue(TEXT("reads as CSV"), DreamLocExchange::Read(Csv, EDreamLocExchangeFormat::Csv, Batch, Error));
	TestEqual(TEXT("five rows"), Batch.Entries.Num(), 5);
	TestTrue(TEXT("and names no culture"), Batch.Culture.IsEmpty());

	const FDreamLocSyncOutput Refused = DreamLocExchange::Import(Batch, Files, DreamLocExchange::FImportOptions());
	TestTrue(TEXT("without -Culture it is refused"), Refused.bBlocked && CountCode(Refused, DreamLocCode::ImportUnreadable) == 1);

	DreamLocExchange::FImportOptions Options;
	Options.Culture = TEXT("en");
	const FDreamLocSyncOutput Output = DreamLocExchange::Import(Batch, Files, Options);
	TestEqual(TEXT("four into the .dloc"), Output.Added, 4);
	TestEqual(TEXT("one of them stale"), Output.Outdated, 1);
	TestEqual(TEXT("the line that is gone is listed"), CountCode(Output, DreamLocCode::ImportUnknownKey), 1);
	FFiles After = Files;
	Apply(Output, After);
	const FString& File = After[TEXT("en")][DlocPath];
	TestTrue(TEXT("the translation over two lines"), File.Contains(TEXT("text  \"So you're\\nthe new keeper?\"")));
	TestTrue(TEXT("the one made from an older source has its prev"), File.Contains(TEXT("prev  \"修灯塔\"")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
