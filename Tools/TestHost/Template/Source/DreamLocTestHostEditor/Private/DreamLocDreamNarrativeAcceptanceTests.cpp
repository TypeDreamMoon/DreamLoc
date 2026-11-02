// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocCheck.h"
#include "DreamLocExchange.h"
#include "DreamLocFile.h"
#include "DreamLocModule.h"
#include "DreamLocSync.h"
#include "DreamLocTextSource.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Internationalization/TextLocalizationManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamLocDreamNarrativeAcceptanceLocal
{
	/** The chapter's file, as DreamNarrative compiles it from DNarrative/ into /Game/Narrative/ (by its base name). */
	const TCHAR* const Source = TEXT("DNarrative/DLocSample/灯塔.dna");
	const TCHAR* const Namespace = TEXT("DNA/灯塔");
	const TCHAR* const AssetPath = TEXT("/Game/Narrative/灯塔");

	/**
	 * What a translator wrote into the old CSV: by hand, for this test, no model involved. Every text of the chapter --
	 * its lines and choices, the quest, the archive, the stage text, the names.
	 */
	const TMap<FString, FString> English = {
		{ TEXT("character.老周"), TEXT("Old Zhou") },
		{ TEXT("character.小雾"), TEXT("Mist") },
		{ TEXT("character.守夜人"), TEXT("Night Keeper") },
		{ TEXT("chapter.序章"), TEXT("Prologue") },
		{ TEXT("quest.修灯.title"), TEXT("Fix the Lighthouse") },
		{ TEXT("quest.修灯.journal.accepted"), TEXT("The lamp room is missing three parts.") },
		{ TEXT("quest.修灯.journal.going"), TEXT("{0} to go.") },
		{ TEXT("quest.修灯.objective1"), TEXT("Parts {0}/3") },
		{ TEXT("quest.修灯.点灯"), TEXT("Light the lighthouse") },
		{ TEXT("archive.守夜日志.title"), TEXT("Night Watch Log") },
		{ TEXT("archive.守夜日志.entry0"), TEXT("First night: the lamp is broken.") },
		{ TEXT("archive.守夜日志.灯亮那夜"), TEXT("The lamp is lit.") },
		{ TEXT("stage.灯亮.灯亮"), TEXT("The lamp is lit") },
		{ TEXT("stage.灯亮.text1"), TEXT("A ship at sea") },
		{ TEXT("老周_初见.1"), TEXT("So you're the new keeper?") },
		{ TEXT("老周_灯坏"), TEXT("The lamp's been out for three days.") },
		{ TEXT("老周_初见.c1"), TEXT("I'll fix it.") },
		{ TEXT("老周_初见.3"), TEXT("Three parts are missing in the lamp room.") },
		{ TEXT("老周_初见.c2"), TEXT("Let me rest a while first.") },
		{ TEXT("老周_初见.4"), TEXT("You've got {0} parts.") },
		{ TEXT("小雾_无线电.1"), TEXT("I can see the light!") },
	};

	/** RFC 4180, as `dnc loc-export` writes it: rows of fields, quotes doubled, a quoted field may hold a line break. */
	TArray<TArray<FString>> ReadRows(const FString& InText)
	{
		TArray<TArray<FString>> Rows;
		TArray<FString> Row;
		FString Field;
		bool bQuoted = false;
		for (int32 Index = InText.Len() > 0 && InText[0] == TEXT('\xFEFF') ? 1 : 0; Index < InText.Len(); ++Index)
		{
			const TCHAR Char = InText[Index];
			if (bQuoted)
			{
				if (Char == TEXT('"') && Index + 1 < InText.Len() && InText[Index + 1] == TEXT('"'))
				{
					Field.AppendChar(TEXT('"'));
					++Index;
				}
				else if (Char == TEXT('"'))
				{
					bQuoted = false;
				}
				else
				{
					Field.AppendChar(Char);
				}
			}
			else if (Char == TEXT('"'))
			{
				bQuoted = true;
			}
			else if (Char == TEXT(','))
			{
				Row.Add(MoveTemp(Field));
				Field.Reset();
			}
			else if (Char == TEXT('\n') || Char == TEXT('\r'))
			{
				if (Char == TEXT('\r') && Index + 1 < InText.Len() && InText[Index + 1] == TEXT('\n'))
				{
					++Index;
				}
				Row.Add(MoveTemp(Field));
				Field.Reset();
				Rows.Add(MoveTemp(Row));
				Row.Reset();
			}
			else
			{
				Field.AppendChar(Char);
			}
		}
		if (!Field.IsEmpty() || Row.Num() > 0)
		{
			Row.Add(MoveTemp(Field));
			Rows.Add(MoveTemp(Row));
		}
		return Rows;
	}

	FString WriteRows(const TArray<TArray<FString>>& InRows)
	{
		FString Out = TEXT("\xFEFF");
		for (const TArray<FString>& Row : InRows)
		{
			TArray<FString> Fields;
			for (const FString& Field : Row)
			{
				const bool bQuote = Field.Contains(TEXT(",")) || Field.Contains(TEXT("\"")) || Field.Contains(TEXT("\n")) || Field.Contains(TEXT("\r"));
				Fields.Add(bQuote ? TEXT("\"") + Field.Replace(TEXT("\""), TEXT("\"\"")) + TEXT("\"") : Field);
			}
			Out += FString::Join(Fields, TEXT(",")) + TEXT("\r\n");
		}
		return Out;
	}

	/** Every FText the compiled chapter holds under its namespace, by key: what the game shows. */
	TMap<FString, FText> TextsOf(UPackage* InPackage)
	{
		TMap<FString, FText> Texts;
		ForEachObjectWithPackage(InPackage, [&Texts](UObject* InObject)
		{
			for (TPropertyValueIterator<FTextProperty> It(InObject->GetClass(), InObject); It; ++It)
			{
				const FText& Text = *static_cast<const FText*>(It.Value());
				const TOptional<FString> TextNamespace = FTextInspector::GetNamespace(Text);
				const TOptional<FString> Key = FTextInspector::GetKey(Text);
				if (TextNamespace.IsSet() && Key.IsSet() && TextNamespace.GetValue() == Namespace)
				{
					Texts.Add(Key.GetValue(), Text);
				}
			}
			return true;
		});
		return Texts;
	}

	class FWaitFor : public IAutomationLatentCommand
	{
	public:
		FWaitFor(FAutomationTestBase* InTest, FString InStep, TFunction<void()> InAction, TFunction<bool(FString&)> InCheck)
			: Test(InTest), Step(MoveTemp(InStep)), Action(MoveTemp(InAction)), Check(MoveTemp(InCheck))
		{
		}

		virtual bool Update() override
		{
			if (StartTime < 0.0)
			{
				StartTime = FPlatformTime::Seconds();
				Action();
				return false;
			}
			FString Shown;
			if (Check(Shown))
			{
				Test->AddInfo(FString::Printf(TEXT("%s: %s after %.2f s"), *Step, *Shown, FPlatformTime::Seconds() - StartTime));
				return true;
			}
			if (FPlatformTime::Seconds() - StartTime > 20.0)
			{
				Test->AddError(FString::Printf(TEXT("%s: still %s after 20 s"), *Step, *Shown));
				return true;
			}
			return false;
		}

	private:
		FAutomationTestBase* Test;
		FString Step;
		TFunction<void()> Action;
		TFunction<bool(FString&)> Check;
		double StartTime = -1.0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocDreamNarrativeChapterTest, "DreamLocHost.Acceptance.DreamNarrative.AChapterIsTranslatedThroughDLocAndTheOldCsvLosesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLocDreamNarrativeChapterTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocDreamNarrativeAcceptanceLocal;
	if (!IPluginManager::Get().FindPlugin(TEXT("DreamNarrative")).IsValid())
	{
		AddInfo(TEXT("DreamNarrative is not in this host; the host made with it checks this"));
		return true;
	}
	if (FTextLocalizationManager::Get().IsGameLocalizationPreviewEnabled())
	{
		AddError(TEXT("a game localization preview is already on; this test turns it on and off itself"));
		return false;
	}
	FString Project = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	FPaths::NormalizeDirectoryName(Project);
	const FString Manifest = Project / TEXT("Intermediate/DLoc/extract/DreamNarrative") / (FString(Source) + TEXT(".json"));
	const FString Exported = Project / TEXT("Saved/DreamNarrative/Translations.csv");
	if (!TestTrue(TEXT("dnc build wrote the chapter's manifest"), IFileManager::Get().FileExists(*Manifest))
		|| !TestTrue(TEXT("dnc loc-export wrote the old CSV"), IFileManager::Get().FileExists(*Exported)))
	{
		AddInfo(TEXT("first: pwsh -File Plugins/DreamNarrative/Tools/dnc.ps1 build, then loc-export, with -Project this host"));
		return false;
	}
	FString Work = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("DreamLocTests") / (TEXT("DreamNarrative-") + FGuid::NewGuid().ToString(EGuidFormats::Short)));
	FPaths::NormalizeDirectoryName(Work);
	const FString DLoc = Work / TEXT("DLoc");

	// 1. The chapter's texts into the .dloc files, from the manifest DreamNarrative wrote as it compiled.
	DreamLocSync::FDirectoryOptions SyncOptions;
	SyncOptions.OwnerDirectory = Project;
	SyncOptions.DLocDirectory = DLoc;
	SyncOptions.Cultures = { TEXT("en") };
	SyncOptions.SourceFilter = { Source };
	const FDreamLocSyncOutput Synced = DreamLocSync::SyncDirectory(SyncOptions);
	TestEqual(TEXT("every text of the chapter gets an entry"), Synced.Added, English.Num());

	// 2. The old CSV as a translator sends it back: dnc loc-export's own file, its Translation column filled in.
	FString Csv;
	FFileHelper::LoadFileToString(Csv, *Exported);
	TArray<TArray<FString>> Rows = ReadRows(Csv);
	const int32 KeyColumn = Rows.Num() > 0 ? Rows[0].IndexOfByKey(TEXT("Key")) : INDEX_NONE;
	const int32 TranslationColumn = Rows.Num() > 0 ? Rows[0].IndexOfByKey(TEXT("Translation")) : INDEX_NONE;
	if (!TestTrue(TEXT("the CSV has its Key and Translation columns"), KeyColumn != INDEX_NONE && TranslationColumn != INDEX_NONE))
	{
		return false;
	}
	int32 Filled = 0;
	for (int32 Row = 1; Row < Rows.Num(); ++Row)
	{
		if (const FString* Text = Rows[Row].IsValidIndex(KeyColumn) ? English.Find(Rows[Row][KeyColumn]) : nullptr)
		{
			Rows[Row].SetNum(FMath::Max(Rows[Row].Num(), TranslationColumn + 1));
			Rows[Row][TranslationColumn] = *Text;
			++Filled;
		}
	}
	TestEqual(TEXT("the CSV has every text of the chapter, once"), Filled, English.Num());
	TestEqual(TEXT("and nothing else"), Rows.Num() - 1, English.Num());
	const FString Translated = Work / TEXT("Translations.en.csv");
	FFileHelper::SaveStringToFile(WriteRows(Rows), *Translated, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

	// 3. Imported, as dloc import -In <file> -Culture en does: the CSV names no culture.
	FString Text;
	FFileHelper::LoadFileToString(Text, *Translated);
	FDreamLocExchangeBatch Batch;
	FString Error;
	TestTrue(TEXT("the CSV reads"), DreamLocExchange::Read(Text, EDreamLocExchangeFormat::Csv, Batch, Error));
	DreamLocExchange::FImportOptions Import;
	Import.Culture = TEXT("en");
	FDreamLocSyncOutput Imported = DreamLocExchange::Import(Batch, DreamLocSync::ReadFiles(DLoc, { TEXT("en") }), Import, DLoc);
	for (const FDreamLocReportLine& Line : Imported.Messages)
	{
		AddInfo(TEXT("import: ") + Line.Format());
	}
	TestEqual(TEXT("every translation in"), Imported.Added, English.Num());
	TestEqual(TEXT("none stale"), Imported.Outdated, 0);
	TestFalse(TEXT("none left out"), Imported.Messages.ContainsByPredicate([](const FDreamLocReportLine& In) { return In.Diagnostic.Code == DreamLocCode::ImportUnknownKey; }));
	DreamLocSync::ApplyChanges(DLoc, Imported, Imported.Messages);

	// 4. Nothing lost: each .dloc entry has the translation the CSV had for it.
	TMap<FString, FString> InFiles;
	for (const TPair<FString, FString>& File : DreamLocSync::ReadFiles(DLoc, { TEXT("en") }).FindRef(TEXT("en")))
	{
		FDreamLocDocument Document;
		TArray<FDreamLocDiagnostic> Diagnostics;
		DreamLocFile::Parse(File.Value, Document, Diagnostics);
		Document.ForEachEntry([&InFiles](const FString& InNamespace, const FDreamLocEntry& InEntry, bool)
		{
			if (InNamespace == Namespace && InEntry.Find(EDreamLocField::Text) != nullptr)
			{
				InFiles.Add(InEntry.Key, *InEntry.Find(EDreamLocField::Text));
			}
		});
	}
	int32 Same = 0;
	for (const TPair<FString, FString>& Pair : English)
	{
		Same += InFiles.FindRef(Pair.Key) == Pair.Value ? 1 : 0;
	}
	TestEqual(TEXT("every translation is in the .dloc as the CSV had it"), Same, English.Num());

	// 5. Checked: {0} and the rest hold.
	FDreamLocCheckOptions CheckOptions;
	CheckOptions.Roots.Add(FDreamLocCheckRoot{ DLoc, 1 });
	const FDreamLocCheckResult Checked = DreamLocCheck::Run(CheckOptions);
	for (const FDreamLocReportLine& Line : Checked.Lines)
	{
		if (Line.Diagnostic.Severity != EDreamLocSeverity::Info)
		{
			AddInfo(TEXT("check: ") + Line.Format());
		}
	}
	TestEqual(TEXT("check: no errors"), Checked.NumErrors(), 0);
	TestEqual(TEXT("check: no warnings"), Checked.NumWarnings(), 0);

	// 6. And the game: the chapter as DreamNarrative compiled it shows the translations under the English preview.
	UPackage* Package = LoadPackage(nullptr, AssetPath, LOAD_None);
	if (!TestNotNull(TEXT("the compiled chapter"), Package))
	{
		return false;
	}
	const TMap<FString, FText> Texts = TextsOf(Package);
	TestEqual(TEXT("its texts are the CSV's"), Texts.Num(), English.Num());
	const FText Line = Texts.FindRef(TEXT("老周_灯坏"));
	const FText Choice = Texts.FindRef(TEXT("老周_初见.c1"));
	auto Mount = [DLoc](bool bInMount)
	{
		if (const TSharedPtr<FDreamLocTextSource> TextSource = FDreamLocModule::Get().GetTextSource())
		{
			if (bInMount)
			{
				TextSource->MountLayer(FDreamLocLayer{ 2, TEXT("DreamLocHost.DreamNarrative"), DLoc });
			}
			else
			{
				TextSource->UnmountLayer(DLoc);
			}
		}
	};
	ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(this, TEXT("the English preview"),
		[Mount]() { Mount(true); FTextLocalizationManager::Get().EnableGameLocalizationPreview(TEXT("en")); },
		[Texts](FString& OutShown)
		{
			int32 Right = 0;
			for (const TPair<FString, FText>& Pair : Texts)
			{
				Right += Pair.Value.ToString() == English.FindRef(Pair.Key) ? 1 : 0;
			}
			OutShown = FString::Printf(TEXT("%d of %d texts in English ('%s' / '%s')"), Right, Texts.Num(),
				*Texts.FindRef(TEXT("老周_灯坏")).ToString(), *Texts.FindRef(TEXT("老周_初见.c1")).ToString());
			return Right == English.Num();
		}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(this, TEXT("the preview turned off"),
		[Mount]() { FTextLocalizationManager::Get().DisableGameLocalizationPreview(); Mount(false); },
		[Line, Choice](FString& OutShown)
		{
			OutShown = FString::Printf(TEXT("'%s' / '%s'"), *Line.ToString(), *Choice.ToString());
			return Line.ToString() == TEXT("灯坏了三天了。") && Choice.ToString() == TEXT("我去修。");
		}));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Work]() { IFileManager::Get().DeleteDirectory(*Work, false, true); return true; }));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
