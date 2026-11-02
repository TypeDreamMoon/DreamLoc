// Copyright DreamMoon. All Rights Reserved.

#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/TextBlock.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamTextUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "DreamLocEngine.h"
#include "DreamLocExtract.h"
#include "DreamLocFile.h"
#include "DreamLocModule.h"
#include "DreamLocSync.h"
#include "DreamLocTestHostTexts.h"
#include "DreamLocTextSource.h"
#include "DreamWidgetBlueprint.h"
#include "HAL/FileManager.h"
#include "Internationalization/TextLocalizationManager.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "WidgetBlueprint.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * The second stage's checks, through the real tools: DreamGUI compiles a .dui and writes its manifest, the engine's
 * gather reads C++ and assets, DreamLoc syncs both into .dloc files in a folder of the test's own, and the engine's
 * FText table shows the translations under the game preview. Nothing of the host's own DLoc/ is touched.
 */
namespace DreamLocSyncAcceptanceLocal
{
	FString TempDirectory(const TCHAR* InName)
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

	bool WriteText(const FString& InPath, const FString& InText)
	{
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(InPath), /*Tree*/ true);
		return FFileHelper::SaveStringToFile(InText, *InPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	/** Compiles a .dui the way the editor does: a .dui-backed widget blueprint. Its manifest is written as it compiles. */
	UDreamWidgetBlueprint* CompileDui(FAutomationTestBase& InTest, const FString& InAbsolutePath, TArray<TStrongObjectPtr<UObject>>& InOutKeep,
		UPackage* InPackage = nullptr, const TCHAR* InName = nullptr)
	{
		const FString Name = InName != nullptr ? FString(InName) : FString::Printf(TEXT("WBP_Sync_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
		UPackage* Package = InPackage != nullptr ? InPackage : CreatePackage(*(TEXT("/Temp/DreamLocTestHost/") + Name));
		UDreamWidgetBlueprint* Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
			UDreamTextUserWidget::StaticClass(), Package, FName(*Name), BPTYPE_Normal,
			UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
		UDreamTextUserWidget* Defaults = Blueprint != nullptr && Blueprint->GeneratedClass != nullptr
			? Cast<UDreamTextUserWidget>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
		if (Defaults == nullptr)
		{
			InTest.AddError(TEXT("no .dui-backed blueprint"));
			return nullptr;
		}
		InOutKeep.Emplace(Blueprint);
		Defaults->SourceFile.FilePath = InAbsolutePath;
		FCompilerResultsLog Results;
		FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
		InTest.TestEqual(FString::Printf(TEXT("%s compiles"), *FPaths::GetCleanFilename(InAbsolutePath)), Results.NumErrors, 0);
		return Blueprint;
	}

	const FText* FindDuiText(const UDreamWidgetBlueprint* InBlueprint, const TCHAR* InNode)
	{
		const FText* Found = nullptr;
		if (InBlueprint != nullptr && IsValid(InBlueprint->WidgetTree))
		{
			InBlueprint->WidgetTree->ForEachWidget([&Found, InNode](UDreamWidget* InWidget)
			{
				if (Found == nullptr && InWidget->GetDisplayName() == InNode)
				{
					if (const UDreamText* Text = Cast<UDreamText>(InWidget->GetVisual()))
					{
						Found = &Text->GetText();
					}
				}
			});
		}
		return Found;
	}

	struct FEntry
	{
		bool bFound = false;
		FString Text;
		FString Prev;
	};

	FEntry ReadEntry(const FString& InPath, const FString& InNamespace, const FString& InKey, bool bInRemoved = false)
	{
		FEntry Result;
		FDreamLocDocument Document;
		TArray<FDreamLocDiagnostic> Diagnostics;
		if (!DreamLocFile::LoadFile(InPath, Document, Diagnostics))
		{
			return Result;
		}
		Document.ForEachEntry([&](const FString& InEntryNamespace, const FDreamLocEntry& InEntry, bool bRemoved)
		{
			if (!Result.bFound && bRemoved == bInRemoved && InEntryNamespace == InNamespace && InEntry.Key == InKey)
			{
				Result.bFound = true;
				Result.Text = InEntry.Find(EDreamLocField::Text) != nullptr ? *InEntry.Find(EDreamLocField::Text) : FString();
				Result.Prev = InEntry.Find(EDreamLocField::Prev) != nullptr ? *InEntry.Find(EDreamLocField::Prev) : FString();
			}
		}, /*bInIncludeRemoved*/ true);
		return Result;
	}

	/** Gives the entries of a .dloc translations, by key, as a translator would. */
	bool Translate(const FString& InPath, const TMap<FString, FString>& InTexts)
	{
		FDreamLocDocument Document;
		TArray<FDreamLocDiagnostic> Diagnostics;
		if (!DreamLocFile::LoadFile(InPath, Document, Diagnostics))
		{
			return false;
		}
		for (FDreamLocItem& Item : Document.Items)
		{
			if (Item.Kind == EDreamLocItemKind::Entry)
			{
				if (const FString* Text = InTexts.Find(Item.Entry.Key))
				{
					Item.Entry.Set(EDreamLocField::Text, *Text);
				}
			}
		}
		return DreamLocFile::SaveFile(InPath, Document);
	}

	bool Has(const FDreamLocSyncOutput& InOutput, int32 InCode)
	{
		return InOutput.Messages.ContainsByPredicate([InCode](const FDreamLocReportLine& InLine) { return InLine.Diagnostic.Code == InCode; });
	}

	void Report(FAutomationTestBase& InTest, const FString& InStep, const FDreamLocSyncOutput& InOutput)
	{
		InTest.AddInfo(FString::Printf(TEXT("%s: %d written, %d deleted; %d added, %d migrated, %d moved, %d removed"), *InStep, InOutput.NumWrites(),
			InOutput.NumDeletes(), InOutput.Added, InOutput.Migrated, InOutput.Moved, InOutput.Removed));
		for (const FDreamLocReportLine& Line : InOutput.Messages)
		{
			InTest.AddInfo(TEXT("    ") + Line.Format());
		}
		InTest.TestEqual(InStep + TEXT(": no errors"), InOutput.NumErrors(), 0);
	}

	/** Mounts a test's DLoc folder over the project's and reloads, then waits until InCheck holds or 20 seconds pass. */
	class FPreviewAndWait : public IAutomationLatentCommand
	{
	public:
		FPreviewAndWait(FAutomationTestBase* InTest, FString InStep, TFunction<void()> InAction, TFunction<bool(FString&)> InCheck)
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

	void MountLayer(const FString& InDLocDirectory)
	{
		if (const TSharedPtr<FDreamLocTextSource> Source = FDreamLocModule::Get().GetTextSource())
		{
			FDreamLocLayer Layer;
			Layer.Index = 2;
			Layer.Name = TEXT("DreamLocHost.Test");
			Layer.Directory = InDLocDirectory;
			Source->MountLayer(Layer);
		}
	}

	void UnmountLayer(const FString& InDLocDirectory)
	{
		if (const TSharedPtr<FDreamLocTextSource> Source = FDreamLocModule::Get().GetTextSource())
		{
			Source->UnmountLayer(InDLocDirectory);
		}
	}

	bool SaveAsset(UObject* InAsset)
	{
		UPackage* Package = InAsset->GetOutermost();
		FAssetRegistryModule::AssetCreated(InAsset);
		Package->MarkPackageDirty();
		const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		return UPackage::SavePackage(Package, InAsset, *Filename, Args);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocRenameAcceptanceTest, "DreamLocHost.Acceptance.RenamesClassLinesAndMovesKeepTheTranslations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLocRenameAcceptanceTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocSyncAcceptanceLocal;
	if (FTextLocalizationManager::Get().IsGameLocalizationPreviewEnabled())
	{
		AddError(TEXT("a game localization preview is already on; this test turns it on and off itself"));
		return false;
	}

	const FString Project = ProjectDirectory();
	const FString Tag = FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(12);
	const FString Folder = FString::Printf(TEXT("DUI/Acceptance/Sync_%s"), *Tag);
	const FString Work = TempDirectory(TEXT("Rename"));
	const FString DLoc = Work / TEXT("DLoc");
	TSharedRef<TArray<TStrongObjectPtr<UObject>>> Keep = MakeShared<TArray<TStrongObjectPtr<UObject>>>();
	auto Cleanup = [Project, Folder, Work]()
	{
		IFileManager::Get().DeleteDirectory(*(Project / Folder), false, true);
		IFileManager::Get().DeleteDirectory(*(DreamLocExtract::GetExtractDirectory(Project) / TEXT("DreamGUI") / Folder), false, true);
		IFileManager::Get().DeleteDirectory(*Work, false, true);
	};

	auto Sync = [this, Project, Folder, DLoc](const FString& InStep)
	{
		DreamLocSync::FDirectoryOptions Options;
		Options.OwnerDirectory = Project;
		Options.DLocDirectory = DLoc;
		Options.Cultures = { TEXT("en") };
		Options.SourceFilter = { Folder + TEXT("/") };
		const FDreamLocSyncOutput Output = DreamLocSync::SyncDirectory(Options);
		Report(*this, InStep, Output);
		return Output;
	};
	auto Dui = [](const TCHAR* InClassLine, const TCHAR* InTitle, const TCHAR* InHint)
	{
		return FString::Printf(TEXT("%s\nWidget Root {\n    %s {\n        Text = \"设置\"\n    }\n    %s {\n        Text = \"按任意键继续\"\n    }\n")
			TEXT("    Widget Footer {\n        Text Apply {\n            Text = \"应用\"\n        }\n    }\n}\n"), InClassLine, InTitle, InHint);
	};
	const FString ClassLine = FString::Printf(TEXT("class /Game/Acceptance/Sync_%s/WBP_Panel"), *Tag);
	const FString Namespace = FString::Printf(TEXT("/Game/Acceptance/Sync_%s/WBP_Panel"), *Tag);
	const FString Namespace2 = Namespace + TEXT("2");
	const FString Panel = Project / Folder / TEXT("Panel.dui");
	const FString PanelDloc = DLoc / TEXT("en") / Folder / TEXT("Panel.dloc");

	// The first compile and sync, then a translator's pass.
	WriteText(Panel, Dui(*ClassLine, TEXT("Text Title"), TEXT("Text Hint")));
	CompileDui(*this, Panel, *Keep);
	Sync(TEXT("the first sync"));
	TestTrue(TEXT("the .dloc mirrors the .dui"), IFileManager::Get().FileExists(*PanelDloc));
	TestTrue(TEXT("translated"), Translate(PanelDloc, { { TEXT("Title.Text"), TEXT("Settings") }, { TEXT("Hint.Text"), TEXT("Press any key") },
		{ TEXT("Apply.Text"), TEXT("Apply") } }));

	// A node id changed with a rename clause.
	WriteText(Panel, Dui(*ClassLine, TEXT("Text Heading (was: Title)"), TEXT("Text Hint")));
	CompileDui(*this, Panel, *Keep);
	FDreamLocSyncOutput Output = Sync(TEXT("Title renamed Heading (was: Title)"));
	TestEqual(TEXT("Heading has Title's translation"), ReadEntry(PanelDloc, Namespace, TEXT("Heading.Text")).Text, FString(TEXT("Settings")));
	TestFalse(TEXT("Title is not kept as removed"), ReadEntry(PanelDloc, Namespace, TEXT("Title.Text"), true).bFound);

	// A node id changed with nothing said: the same text, one to one.
	WriteText(Panel, Dui(*ClassLine, TEXT("Text Heading"), TEXT("Text Tip")));
	CompileDui(*this, Panel, *Keep);
	Output = Sync(TEXT("Hint renamed Tip, no clause"));
	TestEqual(TEXT("Tip has Hint's translation"), ReadEntry(PanelDloc, Namespace, TEXT("Tip.Text")).Text, FString(TEXT("Press any key")));
	TestTrue(TEXT("inferred, and said so"), Has(Output, DreamLocCode::RenameInferred));

	// The class line changed: a new namespace for every text.
	const FString ClassLine2 = ClassLine + TEXT("2");
	WriteText(Panel, Dui(*ClassLine2, TEXT("Text Heading"), TEXT("Text Tip")));
	CompileDui(*this, Panel, *Keep);
	Output = Sync(TEXT("the class line changed"));
	TestTrue(TEXT("the namespace moved"), Has(Output, DreamLocCode::NamespaceMoved));
	TestEqual(TEXT("Heading under the new namespace"), ReadEntry(PanelDloc, Namespace2, TEXT("Heading.Text")).Text, FString(TEXT("Settings")));
	TestEqual(TEXT("Apply too"), ReadEntry(PanelDloc, Namespace2, TEXT("Apply.Text")).Text, FString(TEXT("Apply")));

	// The file moved.
	const FString Moved = Project / Folder / TEXT("Moved/Panel.dui");
	const FString MovedDloc = DLoc / TEXT("en") / Folder / TEXT("Moved/Panel.dloc");
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Moved), /*Tree*/ true);
	IFileManager::Get().Move(*Moved, *Panel);
	CompileDui(*this, Moved, *Keep);
	Output = Sync(TEXT("the file moved"));
	TestFalse(TEXT("the old .dloc is gone"), IFileManager::Get().FileExists(*PanelDloc));
	TestEqual(TEXT("the new .dloc has the translations"), ReadEntry(MovedDloc, Namespace2, TEXT("Tip.Text")).Text, FString(TEXT("Press any key")));

	// No class line: the namespace is the source's path, and it changes when the file moves.
	WriteText(Moved, Dui(TEXT("// no class line"), TEXT("Text Heading"), TEXT("Text Tip")));
	CompileDui(*this, Moved, *Keep);
	Output = Sync(TEXT("the class line is gone"));
	TestTrue(TEXT("the namespace moved to the file's path"), Has(Output, DreamLocCode::NamespaceMoved));
	const FString Moved2 = Project / Folder / TEXT("Again/Panel.dui");
	const FString Moved2Dloc = DLoc / TEXT("en") / Folder / TEXT("Again/Panel.dloc");
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Moved2), /*Tree*/ true);
	IFileManager::Get().Move(*Moved2, *Moved);
	UDreamWidgetBlueprint* Last = CompileDui(*this, Moved2, *Keep);
	Output = Sync(TEXT("moved again, without a class line"));
	TestTrue(TEXT("the namespace followed the path"), Has(Output, DreamLocCode::NamespaceMoved));
	FDreamLocDocument Final;
	TArray<FDreamLocDiagnostic> Diagnostics;
	DreamLocFile::LoadFile(Moved2Dloc, Final, Diagnostics);
	FString FinalNamespace;
	Final.ForEachEntry([&FinalNamespace](const FString& InNamespace, const FDreamLocEntry&, bool) { FinalNamespace = InNamespace; });
	AddInfo(FString::Printf(TEXT("without a class line the namespace is '%s'"), *FinalNamespace));
	TestEqual(TEXT("every translation made it"), ReadEntry(Moved2Dloc, FinalNamespace, TEXT("Heading.Text")).Text, FString(TEXT("Settings")));
	TestEqual(TEXT("every translation made it"), ReadEntry(Moved2Dloc, FinalNamespace, TEXT("Apply.Text")).Text, FString(TEXT("Apply")));

	// And the engine shows them: the last compiled widget, under the game preview in English.
	const FText* Heading = FindDuiText(Last, TEXT("Heading"));
	if (!TestNotNull(TEXT("the Heading text of the last compile"), Heading))
	{
		Cleanup();
		return false;
	}
	const FText HeadingText = *Heading;
	ADD_LATENT_AUTOMATION_COMMAND(FPreviewAndWait(this, TEXT("the game preview in English"),
		[DLoc]() { MountLayer(DLoc); FTextLocalizationManager::Get().EnableGameLocalizationPreview(TEXT("en")); },
		[HeadingText](FString& OutShown) { OutShown = HeadingText.ToString(); return OutShown == TEXT("Settings"); }));
	ADD_LATENT_AUTOMATION_COMMAND(FPreviewAndWait(this, TEXT("the preview turned off"),
		[DLoc]() { FTextLocalizationManager::Get().DisableGameLocalizationPreview(); UnmountLayer(DLoc); },
		[HeadingText](FString& OutShown) { OutShown = HeadingText.ToString(); return OutShown == TEXT("设置"); }));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Cleanup, Keep]() { Keep->Reset(); Cleanup(); return true; }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocGatherAcceptanceTest, "DreamLocHost.Acceptance.CppAndAssetTextsAreGatheredSyncedAndTranslated",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLocGatherAcceptanceTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocSyncAcceptanceLocal;
	if (FTextLocalizationManager::Get().IsGameLocalizationPreviewEnabled())
	{
		AddError(TEXT("a game localization preview is already on; this test turns it on and off itself"));
		return false;
	}
	const FString Project = ProjectDirectory();
	const FString Work = TempDirectory(TEXT("Gather"));
	const FString DLoc = Work / TEXT("DLoc");
	const FString Extract = Work / TEXT("extract");
	TSharedRef<TArray<TStrongObjectPtr<UObject>>> Keep = MakeShared<TArray<TStrongObjectPtr<UObject>>>();

	// Two assets for the gather to find, made once and kept: a UMG text block, and the DreamGUI class LocDemo.dui compiles to.
	const TCHAR* const UmgPath = TEXT("/Game/Acceptance/Gather/WBP_GatherUmg");
	const TCHAR* const DuiPath = TEXT("/Game/Acceptance/Gather/WBP_GatherDui");
	if (!FPackageName::DoesPackageExist(UmgPath))
	{
		UPackage* Package = CreatePackage(UmgPath);
		UWidgetBlueprint* Blueprint = Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(UUserWidget::StaticClass(), Package,
			TEXT("WBP_GatherUmg"), BPTYPE_Normal, UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass()));
		UCanvasPanel* Canvas = Blueprint->WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Canvas"));
		Blueprint->WidgetTree->RootWidget = Canvas;
		UTextBlock* Start = Blueprint->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Start"));
		Start->SetText(FText::AsLocalizable_Advanced(TEXT("DreamLocTestHost.Gather"), TEXT("Start"), TEXT("开始游戏")));
		Canvas->AddChildToCanvas(Start);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		TestTrue(TEXT("the UMG asset is saved"), SaveAsset(Blueprint));
	}
	const FString LocDemo = Project / TEXT("DUI/Acceptance/LocDemo.dui");
	if (!FPackageName::DoesPackageExist(DuiPath))
	{
		UDreamWidgetBlueprint* Blueprint = CompileDui(*this, LocDemo, *Keep, CreatePackage(DuiPath), TEXT("WBP_GatherDui"));
		TestTrue(TEXT("the DreamGUI asset is saved"), Blueprint != nullptr && SaveAsset(Blueprint));
	}
	else
	{
		// Its manifest, as a compile writes it.
		CompileDui(*this, LocDemo, *Keep);
	}

	DreamLocEngine::FGatherOptions Options;
	Options.ProjectDirectory = Project;
	// All of Source: the default excludes leave the editor modules out, this test's own module among them.
	Options.SourcePaths = { TEXT("Source") };
	Options.ContentPaths = { TEXT("Content/Acceptance/Gather") };
	Options.ExcludePaths = FDreamLocSettings().GatherExcludePaths;
	Options.ExtractDirectory = Extract;
	Options.WorkDirectory = Work / TEXT("gather");
	const double GatherStart = FPlatformTime::Seconds();
	const DreamLocEngine::FGatherResult Gathered = DreamLocEngine::Gather(Options);
	AddInfo(FString::Printf(TEXT("the gather: %d text(s) in %d file(s), %.1f s"), Gathered.Texts, Gathered.Sources, FPlatformTime::Seconds() - GatherStart));
	for (const FDreamLocReportLine& Line : Gathered.Messages)
	{
		AddInfo(TEXT("    ") + Line.Format());
	}
	if (!TestTrue(TEXT("the gather finished"), Gathered.bSucceeded))
	{
		return false;
	}

	TArray<FDreamLocExtractManifest> Manifests;
	FDreamLocExtractManifest Code;
	FDreamLocExtractManifest Umg;
	FDreamLocExtractManifest DuiAsset;
	for (const FString& Path : DreamLocExtract::FindManifestFiles(Extract))
	{
		FDreamLocExtractManifest Manifest;
		FString Error;
		if (TestTrue(FString::Printf(TEXT("%s reads"), *Path), DreamLocExtract::LoadFile(Path, Manifest, Error)))
		{
			AddInfo(FString::Printf(TEXT("gathered %s: %d text(s)"), *Manifest.Source, Manifest.NumEntries()));
			Code = Manifest.Source.EndsWith(TEXT("DreamLocTestHostTexts.cpp")) ? Manifest : Code;
			Umg = Manifest.Source.EndsWith(TEXT("WBP_GatherUmg.uasset")) ? Manifest : Umg;
			DuiAsset = Manifest.Source.EndsWith(TEXT("WBP_GatherDui.uasset")) ? Manifest : DuiAsset;
			Manifests.Add(MoveTemp(Manifest));
		}
	}
	TestFalse(TEXT("nothing from an editor module"), Manifests.ContainsByPredicate([](const FDreamLocExtractManifest& InManifest)
	{
		return InManifest.Source.StartsWith(TEXT("Source/DreamLocTestHostEditor/"));
	}));
	TestEqual(TEXT("the C++ file's manifest"), Code.Source, FString(TEXT("Source/DreamLocTestHost/Private/DreamLocTestHostTexts.cpp")));
	TestTrue(TEXT("with its LOCTEXT"), Code.Namespaces.Num() == 1 && Code.Namespaces[0].Namespace == TEXT("DreamLocTestHost.Game")
		&& Code.Namespaces[0].Entries.Num() == 1 && Code.Namespaces[0].Entries[0].Key == TEXT("Continue") && Code.Namespaces[0].Entries[0].Source == TEXT("继续游戏"));
	TestTrue(TEXT("the UMG text block's text"), Umg.Namespaces.ContainsByPredicate([](const FDreamLocExtractNamespace& InNamespace)
	{
		return InNamespace.Namespace == TEXT("DreamLocTestHost.Gather") && InNamespace.Entries.ContainsByPredicate(
			[](const FDreamLocExtractEntry& InEntry) { return InEntry.Key == TEXT("Start") && InEntry.Source == TEXT("开始游戏"); });
	}));

	// What the gather saw in the DreamGUI asset, against what DreamGUI's own manifest says.
	FDreamLocExtractManifest DuiManifest;
	FString Error;
	TestTrue(TEXT("LocDemo.dui's manifest"), DreamLocExtract::LoadFile(DreamLocExtract::GetExtractDirectory(Project) / TEXT("DreamGUI/DUI/Acceptance/LocDemo.dui.json"),
		DuiManifest, Error));
	int32 Same = 0;
	int32 Other = 0;
	for (const FDreamLocExtractNamespace& Namespace : DuiAsset.Namespaces)
	{
		for (const FDreamLocExtractEntry& Entry : Namespace.Entries)
		{
			const bool bInManifest = DuiManifest.Namespaces.ContainsByPredicate([&](const FDreamLocExtractNamespace& InNamespace)
			{
				return InNamespace.Namespace == Namespace.Namespace && InNamespace.Entries.ContainsByPredicate([&](const FDreamLocExtractEntry& InEntry)
				{
					return InEntry.Key == Entry.Key && InEntry.Source == Entry.Source;
				});
			});
			(bInManifest ? Same : Other) += 1;
			AddInfo(FString::Printf(TEXT("the DreamGUI asset has %s / %s \"%s\" at %s: %s"), *Namespace.Namespace, *Entry.Key, *Entry.Source, *Entry.At,
				bInManifest ? TEXT("the same as DreamGUI's manifest") : TEXT("not in DreamGUI's manifest")));
		}
	}
	AddInfo(FString::Printf(TEXT("the DreamGUI asset: %d text(s) as in DreamGUI's manifest, %d other"), Same, Other));
	Manifests.Add(DuiManifest);

	FDreamLocSyncInput Input;
	Input.Manifests = Manifests;
	Input.Cultures = { TEXT("en") };
	Input.CompleteProviders.Add(DreamLocExtract::EngineProvider);
	Input.SourceExists = [Project](const FString& InSource) { return IFileManager::Get().FileExists(*(Project / InSource)); };
	Input.Files = DreamLocSync::ReadFiles(DLoc, Input.Cultures);
	Input.DLocName = DLoc;
	FDreamLocSyncOutput Output = DreamLocSync::Run(Input);
	Report(*this, TEXT("the sync"), Output);
	TestFalse(TEXT("no source conflict between DreamGUI's manifest and the gathered asset"), Has(Output, DreamLocCode::SourceConflict));
	TArray<FDreamLocReportLine> Written;
	DreamLocSync::ApplyChanges(DLoc, Output, Written);

	const FString CodeDloc = DLoc / TEXT("en/Engine/Source/DreamLocTestHost/Private/DreamLocTestHostTexts.dloc");
	const FString UmgDloc = DLoc / TEXT("en/Engine/Content/Acceptance/Gather/WBP_GatherUmg.dloc");
	TestTrue(TEXT("the LOCTEXT is in a .dloc"), ReadEntry(CodeDloc, TEXT("DreamLocTestHost.Game"), TEXT("Continue")).bFound);
	TestTrue(TEXT("so is the UMG text"), ReadEntry(UmgDloc, TEXT("DreamLocTestHost.Gather"), TEXT("Start")).bFound);
	TestTrue(TEXT("the .dui's texts are in the .dui's file"), ReadEntry(DLoc / TEXT("en/DUI/Acceptance/LocDemo.dloc"), DuiManifest.Namespaces[0].Namespace,
		TEXT("Title.Text")).bFound);
	TestFalse(TEXT("and not again in the asset's"), ReadEntry(DLoc / TEXT("en/Engine/Content/Acceptance/Gather/WBP_GatherDui.dloc"),
		DuiManifest.Namespaces[0].Namespace, TEXT("Title.Text")).bFound);
	TestTrue(TEXT("translated"), Translate(CodeDloc, { { TEXT("Continue"), TEXT("Continue") } }) && Translate(UmgDloc, { { TEXT("Start"), TEXT("Start Game") } }));

	UWidgetBlueprint* UmgBlueprint = LoadObject<UWidgetBlueprint>(nullptr, TEXT("/Game/Acceptance/Gather/WBP_GatherUmg.WBP_GatherUmg"));
	UTextBlock* StartBlock = UmgBlueprint != nullptr && UmgBlueprint->WidgetTree != nullptr
		? Cast<UTextBlock>(UmgBlueprint->WidgetTree->FindWidget(TEXT("Start"))) : nullptr;
	if (!TestNotNull(TEXT("the UMG text block"), StartBlock))
	{
		return false;
	}
	Keep->Emplace(UmgBlueprint);
	const FText Continue = DreamLocTestHostTexts::GetContinue();
	TWeakObjectPtr<UTextBlock> WeakStart = StartBlock;
	auto Shown = [Continue, WeakStart]() { return FString::Printf(TEXT("code '%s', umg '%s'"), *Continue.ToString(),
		WeakStart.IsValid() ? *WeakStart->GetText().ToString() : TEXT("<gone>")); };
	ADD_LATENT_AUTOMATION_COMMAND(FPreviewAndWait(this, TEXT("the game preview in English"),
		[DLoc]() { MountLayer(DLoc); FTextLocalizationManager::Get().EnableGameLocalizationPreview(TEXT("en")); },
		[Shown](FString& OutShown) { OutShown = Shown(); return OutShown == TEXT("code 'Continue', umg 'Start Game'"); }));
	ADD_LATENT_AUTOMATION_COMMAND(FPreviewAndWait(this, TEXT("the preview turned off"),
		[DLoc]() { FTextLocalizationManager::Get().DisableGameLocalizationPreview(); UnmountLayer(DLoc); },
		[Shown](FString& OutShown) { OutShown = Shown(); return OutShown == TEXT("code '继续游戏', umg '开始游戏'"); }));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Keep, Work]() { Keep->Reset(); IFileManager::Get().DeleteDirectory(*Work, false, true); return true; }));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
