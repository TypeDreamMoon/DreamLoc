// Copyright DreamMoon. All Rights Reserved.

#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Core/DreamTextUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "DreamWidgetBlueprint.h"
#include "HAL/IConsoleManager.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "WidgetBlueprint.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"

DEFINE_LOG_CATEGORY_STATIC(LogDreamLocTestHost, Log, All);

namespace DreamLocTestHostLocal
{
	bool Save(UObject* InAsset)
	{
		UPackage* Package = InAsset->GetOutermost();
		FAssetRegistryModule::AssetCreated(InAsset);
		Package->MarkPackageDirty();
		const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		const bool bSaved = UPackage::SavePackage(Package, InAsset, *Filename, Args);
		UE_LOG(LogDreamLocTestHost, Display, TEXT("%s %s"), bSaved ? TEXT("Saved") : TEXT("Could not save"), *Filename);
		return bSaved;
	}

	/**
	 * Two assets to look at the first stage with by eye: /Game/Acceptance/WBP_LocDemo, the DreamGUI class compiled from
	 * DUI/Acceptance/LocDemo.dui, and /Game/Acceptance/WBP_UmgDemo, a UMG widget with one text block whose text is
	 * DreamLocTestHost.UMG / Start, which DLoc/en/Code/DreamLocTestHost.dloc translates. Open both, run
	 * `DreamLoc.Preview en`, and both read English; edit a .dloc and save, and they follow.
	 * `DreamLocHost.MakeDemoAssets open` opens both editors too.
	 */
	void MakeDemoAssets(const TArray<FString>& InArgs)
	{
		if (FindPackage(nullptr, TEXT("/Game/Acceptance/WBP_LocDemo")) == nullptr && !FPackageName::DoesPackageExist(TEXT("/Game/Acceptance/WBP_LocDemo")))
		{
			UPackage* Package = CreatePackage(TEXT("/Game/Acceptance/WBP_LocDemo"));
			UDreamWidgetBlueprint* Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
				UDreamTextUserWidget::StaticClass(), Package, TEXT("WBP_LocDemo"), BPTYPE_Normal,
				UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
			if (UDreamTextUserWidget* Defaults = Blueprint != nullptr && Blueprint->GeneratedClass != nullptr
				? Cast<UDreamTextUserWidget>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr)
			{
				Defaults->SourceFile.FilePath = TEXT("Acceptance/LocDemo.dui");
				FKismetEditorUtilities::CompileBlueprint(Blueprint);
				Save(Blueprint);
			}
		}
		else
		{
			UE_LOG(LogDreamLocTestHost, Display, TEXT("/Game/Acceptance/WBP_LocDemo is already there."));
		}

		if (FindPackage(nullptr, TEXT("/Game/Acceptance/WBP_UmgDemo")) == nullptr && !FPackageName::DoesPackageExist(TEXT("/Game/Acceptance/WBP_UmgDemo")))
		{
			UPackage* Package = CreatePackage(TEXT("/Game/Acceptance/WBP_UmgDemo"));
			UWidgetBlueprint* Blueprint = Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
				UUserWidget::StaticClass(), Package, TEXT("WBP_UmgDemo"), BPTYPE_Normal,
				UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass()));
			if (Blueprint != nullptr && Blueprint->WidgetTree != nullptr)
			{
				UCanvasPanel* Canvas = Blueprint->WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Canvas"));
				Blueprint->WidgetTree->RootWidget = Canvas;
				UTextBlock* Start = Blueprint->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Start"));
				Start->SetText(FText::AsLocalizable_Advanced(TEXT("DreamLocTestHost.UMG"), TEXT("Start"), TEXT("开始游戏")));
				if (UCanvasPanelSlot* StartSlot = Canvas->AddChildToCanvas(Start))
				{
					StartSlot->SetPosition(FVector2D(100.0, 100.0));
					StartSlot->SetAutoSize(true);
				}
				FKismetEditorUtilities::CompileBlueprint(Blueprint);
				Save(Blueprint);
			}
		}
		else
		{
			UE_LOG(LogDreamLocTestHost, Display, TEXT("/Game/Acceptance/WBP_UmgDemo is already there."));
		}

		if (InArgs.ContainsByPredicate([](const FString& InArg) { return InArg.Equals(TEXT("open"), ESearchCase::IgnoreCase); }) && GEditor != nullptr)
		{
			UAssetEditorSubsystem* AssetEditors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
			for (const TCHAR* Path : { TEXT("/Game/Acceptance/WBP_LocDemo.WBP_LocDemo"), TEXT("/Game/Acceptance/WBP_UmgDemo.WBP_UmgDemo") })
			{
				if (UObject* Asset = LoadObject<UObject>(nullptr, Path); Asset != nullptr && AssetEditors != nullptr)
				{
					AssetEditors->OpenEditorForAsset(Asset);
				}
			}
		}
	}
}

class FDreamLocTestHostEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		Command = IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("DreamLocHost.MakeDemoAssets"),
			TEXT("Make /Game/Acceptance/WBP_LocDemo (DreamGUI, from DUI/Acceptance/LocDemo.dui) and /Game/Acceptance/WBP_UmgDemo (UMG), to look at the translations by eye. 'open' opens their editors."),
			FConsoleCommandWithArgsDelegate::CreateStatic(&DreamLocTestHostLocal::MakeDemoAssets),
			ECVF_Default);
	}

	virtual void ShutdownModule() override
	{
		if (Command != nullptr)
		{
			IConsoleManager::Get().UnregisterConsoleObject(Command);
			Command = nullptr;
		}
	}

private:
	IConsoleObject* Command = nullptr;
};

IMPLEMENT_MODULE(FDreamLocTestHostEditorModule, DreamLocTestHostEditor);
