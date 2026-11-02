// Copyright DreamMoon. All Rights Reserved.

#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamTextUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Dom/JsonObject.h"
#include "DreamLocCheck.h"
#include "DreamLocExchange.h"
#include "DreamLocFile.h"
#include "DreamLocModule.h"
#include "DreamLocSync.h"
#include "DreamLocTestHostTexts.h"
#include "DreamLocTextSource.h"
#include "DreamWidgetBlueprint.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Internationalization/TextLocalizationManager.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamLocExchangeAcceptanceLocal
{
	FString Folder(const TCHAR* InName)
	{
		FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("DreamLocTests")
			/ FString::Printf(TEXT("%s-%s"), InName, *FGuid::NewGuid().ToString(EGuidFormats::Short)));
		FPaths::NormalizeDirectoryName(Directory);
		IFileManager::Get().MakeDirectory(*Directory, /*Tree*/ true);
		return Directory;
	}

	FString ProjectDirectory()
	{
		FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
		FPaths::NormalizeDirectoryName(Directory);
		return Directory;
	}

	/**
	 * What an AI sent back for the screen: written here by hand, as no model is called. Two mistakes in it on purpose,
	 * of the kinds an AI makes -- an argument dropped, a term not used -- for the check to catch and the review to fix.
	 */
	const TMap<FString, FString> AiTranslation = {
		{ TEXT("Title.Text"), TEXT("Settings") },
		{ TEXT("Volume.Text"), TEXT("Volume") },
		{ TEXT("Saved.Text"), TEXT("{Count} {Count}|plural(one=save,other=saves) stored") },
		{ TEXT("Warning.Text"), TEXT("<b>Note</b>: restart the game after changing the language") },
		{ TEXT("Implant.Text"), TEXT("Implant settings") },
		{ TEXT("Credits.Text"), TEXT("Made by DreamMoon") },
		{ TEXT("Apply.Text"), TEXT("Apply") },
		{ TEXT("Back.Text"), TEXT("Back") },
	};

	/** The reviewer's corrections. */
	const TMap<FString, FString> Review = {
		{ TEXT("Volume.Text"), TEXT("Volume: {Value}%") },
		{ TEXT("Implant.Text"), TEXT("Cyberware settings") },
	};

	/** The AI's part: fill in `text` of every entry of the exported JSON, and leave the rest as it is. */
	FString FillIn(const FString& InJson, int32& OutFilled)
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InJson);
		OutFilled = 0;
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			return FString();
		}
		for (const TSharedPtr<FJsonValue>& Value : Root->GetArrayField(TEXT("entries")))
		{
			const TSharedPtr<FJsonObject> Entry = Value->AsObject();
			if (const FString* Text = AiTranslation.Find(Entry->GetStringField(TEXT("key"))))
			{
				Entry->SetStringField(TEXT("text"), *Text);
				++OutFilled;
			}
		}
		FString Out;
		FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<>::Create(&Out));
		return Out;
	}

	TArray<FString> Codes(const FDreamLocCheckResult& InResult)
	{
		TArray<FString> Out;
		for (const FDreamLocReportLine& Line : InResult.Lines)
		{
			if (Line.Diagnostic.Severity != EDreamLocSeverity::Info)
			{
				Out.Add(Line.Diagnostic.GetCodeName());
			}
		}
		Out.Sort();
		return Out;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocAiRoundTripTest, "DreamLocHost.Acceptance.AScreenGoesThroughExportAiTranslationImportCheckAndReview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLocAiRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocExchangeAcceptanceLocal;
	if (FTextLocalizationManager::Get().IsGameLocalizationPreviewEnabled())
	{
		AddError(TEXT("a game localization preview is already on; this test turns it on and off itself"));
		return false;
	}
	const FString Project = ProjectDirectory();
	const FString Work = Folder(TEXT("AiRoundTrip"));
	const FString DLoc = Work / TEXT("DLoc");
	const FString Screen = Project / TEXT("DUI/Acceptance/Settings.dui");
	const FString ScreenDloc = TEXT("DUI/Acceptance/Settings.dloc");
	FFileHelper::SaveStringToFile(TEXT("dloc 1\nculture en\nterms\n\nterm \"义体\" { text \"Cyberware\" }\nkeep \"DreamMoon\"\n"), *(DLoc / TEXT("en/_terms.dloc")),
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

	// The screen compiled the way the editor compiles it, which writes its manifest, and synced into the test's DLoc.
	const FString Name = FString::Printf(TEXT("WBP_Settings_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	UDreamWidgetBlueprint* Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(UDreamTextUserWidget::StaticClass(),
		CreatePackage(*(TEXT("/Temp/DreamLocTestHost/") + Name)), FName(*Name), BPTYPE_Normal, UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
	UDreamTextUserWidget* Defaults = Blueprint != nullptr ? Cast<UDreamTextUserWidget>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
	if (!TestNotNull(TEXT("a .dui-backed blueprint"), Defaults))
	{
		return false;
	}
	TSharedRef<TStrongObjectPtr<UDreamWidgetBlueprint>> Keep = MakeShared<TStrongObjectPtr<UDreamWidgetBlueprint>>(Blueprint);
	Defaults->SourceFile.FilePath = Screen;
	FCompilerResultsLog Results;
	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
	TestEqual(TEXT("the screen compiles"), Results.NumErrors, 0);
	DreamLocSync::FDirectoryOptions SyncOptions;
	SyncOptions.OwnerDirectory = Project;
	SyncOptions.DLocDirectory = DLoc;
	SyncOptions.Cultures = { TEXT("en") };
	SyncOptions.SourceFilter = { TEXT("DUI/Acceptance/Settings.dui") };
	const FDreamLocSyncOutput Synced = DreamLocSync::SyncDirectory(SyncOptions);
	TestEqual(TEXT("eight texts to translate"), Synced.Added, 8);

	// 1. Export what has no translation, for the AI.
	TArray<FDreamLocReportLine> Messages;
	DreamLocExchange::FExportOptions Export;
	Export.Culture = TEXT("en");
	Export.Files = { ScreenDloc };
	const FDreamLocExchangeBatch Batch = DreamLocExchange::Collect(DreamLocSync::ReadFiles(DLoc, { TEXT("en") }), Export, TEXT("zh-Hans"), Messages);
	TestEqual(TEXT("the batch has the whole screen"), Batch.Entries.Num(), 8);
	const FDreamLocExchangeEntry* Implant = Batch.Entries.FindByPredicate([](const FDreamLocExchangeEntry& InEntry) { return InEntry.Key == TEXT("Implant.Text"); });
	TestTrue(TEXT("with the term the source uses"), Implant != nullptr && Implant->Terms.Num() == 1 && Implant->Terms[0].Value == TEXT("Cyberware"));
	TestTrue(TEXT("and the texts around it"), Implant != nullptr && Implant->Context.Num() == 4);
	const FString BatchPath = Work / TEXT("en-batch.json");
	FFileHelper::SaveStringToFile(DreamLocExchange::Write(Batch, EDreamLocExchangeFormat::Json), *BatchPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

	// 2. The AI translates: the file goes out and comes back with every text filled in.
	FString Json;
	FFileHelper::LoadFileToString(Json, *BatchPath);
	int32 Filled = 0;
	const FString Answered = FillIn(Json, Filled);
	TestEqual(TEXT("the AI filled in every entry"), Filled, 8);
	FFileHelper::SaveStringToFile(Answered, *BatchPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

	// 3. Import, as machine translations.
	FDreamLocExchangeBatch Back;
	FString Error;
	FFileHelper::LoadFileToString(Json, *BatchPath);
	TestTrue(TEXT("the answer reads"), DreamLocExchange::Read(Json, EDreamLocExchangeFormat::Json, Back, Error));
	DreamLocExchange::FImportOptions Import;
	Import.State = TEXT("machine");
	FDreamLocSyncOutput Imported = DreamLocExchange::Import(Back, DreamLocSync::ReadFiles(DLoc, { TEXT("en") }), Import, DLoc);
	TestEqual(TEXT("eight translations in"), Imported.Added, 8);
	DreamLocSync::ApplyChanges(DLoc, Imported, Imported.Messages);

	// 4. Check: the two mistakes, and nothing else.
	FDreamLocCheckOptions CheckOptions;
	CheckOptions.Roots.Add(FDreamLocCheckRoot{ DLoc, 1 });
	FDreamLocCheckResult Checked = DreamLocCheck::Run(CheckOptions);
	for (const FDreamLocReportLine& Line : Checked.Lines)
	{
		AddInfo(TEXT("check: ") + Line.Format());
	}
	TestEqual(TEXT("the check finds the dropped argument and the term not used"), FString::Join(Codes(Checked), TEXT(" ")), FString(TEXT("DLOC2001 DLOC2007")));

	// 5. Review: fix those two, and mark the screen reviewed.
	FDreamLocDocument Document;
	TArray<FDreamLocDiagnostic> Diagnostics;
	DreamLocFile::LoadFile(DLoc / TEXT("en") / ScreenDloc, Document, Diagnostics);
	for (FDreamLocItem& Item : Document.Items)
	{
		if (Item.Kind == EDreamLocItemKind::Entry)
		{
			if (const FString* Fixed = Review.Find(Item.Entry.Key))
			{
				Item.Entry.Set(EDreamLocField::Text, *Fixed);
			}
			Item.Entry.Set(EDreamLocField::State, TEXT("done"));
		}
	}
	DreamLocFile::SaveFile(DLoc / TEXT("en") / ScreenDloc, Document);
	Checked = DreamLocCheck::Run(CheckOptions);
	TestEqual(TEXT("reviewed, the check is clean"), FString::Join(Codes(Checked), TEXT(" ")), FString());

	// 6. And the game shows it.
	const FText* Title = nullptr;
	const FText* Volume = nullptr;
	Blueprint->WidgetTree->ForEachWidget([&Title, &Volume](UDreamWidget* InWidget)
	{
		if (const UDreamText* Text = Cast<UDreamText>(InWidget->GetVisual()))
		{
			Title = InWidget->GetDisplayName() == TEXT("Title") ? &Text->GetText() : Title;
			Volume = InWidget->GetDisplayName() == TEXT("Volume") ? &Text->GetText() : Volume;
		}
	});
	if (!TestTrue(TEXT("the compiled texts"), Title != nullptr && Volume != nullptr))
	{
		return false;
	}
	const FText TitleText = *Title;
	const FText VolumeText = *Volume;
	auto Mount = [DLoc](bool bInMount)
	{
		if (const TSharedPtr<FDreamLocTextSource> Source = FDreamLocModule::Get().GetTextSource())
		{
			if (bInMount)
			{
				Source->MountLayer(FDreamLocLayer{ 2, TEXT("DreamLocHost.Test"), DLoc });
			}
			else
			{
				Source->UnmountLayer(DLoc);
			}
		}
	};
	ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(this, TEXT("the game preview in English"),
		[Mount]() { Mount(true); FTextLocalizationManager::Get().EnableGameLocalizationPreview(TEXT("en")); },
		[TitleText, VolumeText](FString& OutShown)
		{
			OutShown = FString::Printf(TEXT("'%s' / '%s'"), *TitleText.ToString(), *VolumeText.ToString());
			return TitleText.ToString() == TEXT("Settings") && VolumeText.ToString() == TEXT("Volume: {Value}%");
		}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(this, TEXT("the preview turned off"),
		[Mount]() { FTextLocalizationManager::Get().DisableGameLocalizationPreview(); Mount(false); },
		[TitleText](FString& OutShown) { OutShown = TitleText.ToString(); return OutShown == TEXT("设置"); }));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Keep, Work]() { Keep->Reset(); IFileManager::Get().DeleteDirectory(*Work, false, true); return true; }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocModMountTest, "DreamLocHost.Acceptance.AModMountedWhileRunningBringsItsLanguage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLocModMountTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocExchangeAcceptanceLocal;
	if (FTextLocalizationManager::Get().IsGameLocalizationPreviewEnabled())
	{
		AddError(TEXT("a game localization preview is already on; this test turns it on and off itself"));
		return false;
	}
	// The test mod of the host's TestMods/: no content, no code, Polish in its DLoc/ folder.
	const FString Mod = ProjectDirectory() / TEXT("TestMods/DreamLocTestMod/DreamLocTestMod.uplugin");
	if (!TestTrue(TEXT("the test mod is there"), IFileManager::Get().FileExists(*Mod)))
	{
		return false;
	}
	if (!IPluginManager::Get().FindPlugin(TEXT("DreamLocTestMod")).IsValid())
	{
		FText Reason;
		TestTrue(TEXT("added"), IPluginManager::Get().AddToPluginsList(Mod, &Reason));
		IPluginManager::Get().MountNewlyCreatedPlugin(TEXT("DreamLocTestMod"));
	}
	bool bLayer = false;
	for (const FDreamLocLayer& Layer : FDreamLocModule::Get().GetTextSource()->GetLayers())
	{
		bLayer |= Layer.Name == TEXT("Mod.DreamLocTestMod") && Layer.Index >= 2;
		AddInfo(FString::Printf(TEXT("layer %d %s"), Layer.Index, *Layer.Name));
	}
	TestTrue(TEXT("the mod is a layer over the project"), bLayer);
	TestTrue(TEXT("Polish is in the game's list"), FTextLocalizationManager::Get().GetLocalizedCultureNames(ELocalizationLoadFlags::Game).Contains(TEXT("pl")));

	const FText Continue = DreamLocTestHostTexts::GetContinue();
	ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(this, TEXT("the game preview in Polish"),
		[]() { FTextLocalizationManager::Get().EnableGameLocalizationPreview(TEXT("pl")); },
		[Continue](FString& OutShown) { OutShown = Continue.ToString(); return OutShown == TEXT("Kontynuuj"); }));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(this, TEXT("the preview turned off"),
		[]() { FTextLocalizationManager::Get().DisableGameLocalizationPreview(); },
		[Continue](FString& OutShown) { OutShown = Continue.ToString(); return OutShown == TEXT("继续游戏"); }));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
