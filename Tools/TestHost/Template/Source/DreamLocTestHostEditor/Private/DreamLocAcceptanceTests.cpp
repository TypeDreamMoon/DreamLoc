// Copyright DreamMoon. All Rights Reserved.

#include "Components/TextBlock.h"
#include "Core/DreamTextUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "DreamWidgetBlueprint.h"
#include "HAL/FileManager.h"
#include "Internationalization/TextLocalizationManager.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * The end-to-end check of the first stage: the host's DUI/Acceptance/LocDemo.dui (written in Chinese) compiled by
 * DreamGUI, a UMG text block and a C++ text, with English in the host's DLoc/en/ folder. In the editor's game preview
 * in English all three read English; a .dloc saved while the preview is on changes them without a restart; the
 * preview turned off puts the Chinese back.
 *
 * What it reads is the FText each of them holds -- the text a DreamText lays out and a text block draws. That the
 * DreamGUI widget lays itself out again when the table changes in the same culture is DreamGUI's own test
 * (DreamGUI.Manager.NewTranslationsInTheSameCultureReachCultureListenersOnce).
 */

namespace DreamLocAcceptanceLocal
{
	struct FState
	{
		FText DuiTitle;
		FText DuiApply;
		FText Code;
		TStrongObjectPtr<UTextBlock> Umg;
		FString DlocPath;
		FString DlocOriginal;
	};

	FString Describe(const FState& InState)
	{
		return FString::Printf(TEXT("dui '%s' / '%s', umg '%s', code '%s'"), *InState.DuiTitle.ToString(), *InState.DuiApply.ToString(),
			InState.Umg.IsValid() ? *InState.Umg->GetText().ToString() : TEXT("<none>"), *InState.Code.ToString());
	}

	/** Runs InAction once, then waits until InCheck holds or 15 seconds pass. */
	class FActAndWait : public IAutomationLatentCommand
	{
	public:
		FActAndWait(FAutomationTestBase* InTest, TSharedRef<FState> InState, FString InStep, TFunction<void()> InAction, TFunction<bool(const FState&)> InCheck)
			: Test(InTest), State(InState), Step(MoveTemp(InStep)), Action(MoveTemp(InAction)), Check(MoveTemp(InCheck))
		{
		}

		virtual bool Update() override
		{
			if (StartTime < 0.0)
			{
				StartTime = FPlatformTime::Seconds();
				if (Action)
				{
					Action();
				}
				return false;
			}
			if (Check(*State))
			{
				Test->AddInfo(FString::Printf(TEXT("%s: %s after %.2f s"), *Step, *Describe(*State), FPlatformTime::Seconds() - StartTime));
				return true;
			}
			if (FPlatformTime::Seconds() - StartTime > 15.0)
			{
				Test->AddError(FString::Printf(TEXT("%s: still %s after 15 s"), *Step, *Describe(*State)));
				return true;
			}
			return false;
		}

	private:
		FAutomationTestBase* Test;
		TSharedRef<FState> State;
		FString Step;
		TFunction<void()> Action;
		TFunction<bool(const FState&)> Check;
		double StartTime = -1.0;
	};

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

	bool AllEnglish(const FState& InState, const TCHAR* InTitle)
	{
		return InState.DuiTitle.ToString() == InTitle && InState.DuiApply.ToString() == TEXT("Apply")
			&& InState.Umg->GetText().ToString() == TEXT("Start Game") && InState.Code.ToString() == TEXT("Quit");
	}

	bool AllChinese(const FState& InState)
	{
		return InState.DuiTitle.ToString() == TEXT("设置") && InState.DuiApply.ToString() == TEXT("应用")
			&& InState.Umg->GetText().ToString() == TEXT("开始游戏") && InState.Code.ToString() == TEXT("退出");
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocAcceptanceTest, "DreamLocHost.Acceptance.DreamGUIUMGAndCodeFollowTheDLocFiles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLocAcceptanceTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocAcceptanceLocal;
	FTextLocalizationManager& Manager = FTextLocalizationManager::Get();
	if (Manager.IsGameLocalizationPreviewEnabled())
	{
		AddError(TEXT("a game localization preview is already on; this test turns it on and off itself"));
		return false;
	}

	TSharedRef<FState> State = MakeShared<FState>();
	State->DlocPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("DLoc/en/DUI/Acceptance/LocDemo.dloc"));
	if (!TestTrue(TEXT("the host has its English .dloc"), FFileHelper::LoadFileToString(State->DlocOriginal, *State->DlocPath)))
	{
		return false;
	}

	// The .dui compiled the way the editor compiles it: a .dui-backed widget blueprint.
	// A name of its own per run: a second run in the same editor would find the first one's blueprint.
	const FString Name = FString::Printf(TEXT("WBP_LocDemo_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Short));
	UPackage* Package = CreatePackage(*(TEXT("/Temp/DreamLocTestHost/") + Name));
	UDreamWidgetBlueprint* Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
		UDreamTextUserWidget::StaticClass(), Package, FName(*Name), BPTYPE_Normal,
		UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
	UDreamTextUserWidget* Defaults = Blueprint != nullptr && Blueprint->GeneratedClass != nullptr
		? Cast<UDreamTextUserWidget>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
	if (!TestNotNull(TEXT("a .dui-backed blueprint"), Defaults))
	{
		return false;
	}
	Defaults->SourceFile.FilePath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("DUI/Acceptance/LocDemo.dui"));
	FCompilerResultsLog Results;
	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
	TestEqual(TEXT("the .dui compiles"), Results.NumErrors, 0);
	const FText* Title = FindDuiText(Blueprint, TEXT("Title"));
	const FText* Apply = FindDuiText(Blueprint, TEXT("Apply"));
	if (!TestNotNull(TEXT("the Title text"), Title) || !TestNotNull(TEXT("the Apply text"), Apply))
	{
		return false;
	}
	State->DuiTitle = *Title;
	State->DuiApply = *Apply;

	State->Umg.Reset(NewObject<UTextBlock>(GetTransientPackage()));
	State->Umg->SetText(FText::AsLocalizable_Advanced(TEXT("DreamLocTestHost.UMG"), TEXT("Start"), TEXT("开始游戏")));
	State->Code = NSLOCTEXT("DreamLocTestHost", "Quit", "退出");

	TestTrue(TEXT("without a preview the editor shows the Chinese source"), AllChinese(*State));
	if (!AllChinese(*State))
	{
		AddInfo(Describe(*State));
	}

	ADD_LATENT_AUTOMATION_COMMAND(FActAndWait(this, State, TEXT("the game preview in English"),
		[]() { FTextLocalizationManager::Get().EnableGameLocalizationPreview(TEXT("en")); },
		[](const FState& InState) { return AllEnglish(InState, TEXT("Settings")); }));

	ADD_LATENT_AUTOMATION_COMMAND(FActAndWait(this, State, TEXT("a translation changed in the .dloc and saved"),
		[State]() { FFileHelper::SaveStringToFile(State->DlocOriginal.Replace(TEXT("\"Settings\""), TEXT("\"Options\"")), *State->DlocPath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM); },
		[](const FState& InState) { return AllEnglish(InState, TEXT("Options")); }));

	ADD_LATENT_AUTOMATION_COMMAND(FActAndWait(this, State, TEXT("the .dloc put back"),
		[State]() { FFileHelper::SaveStringToFile(State->DlocOriginal, *State->DlocPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM); },
		[](const FState& InState) { return AllEnglish(InState, TEXT("Settings")); }));

	ADD_LATENT_AUTOMATION_COMMAND(FActAndWait(this, State, TEXT("the preview turned off"),
		[]() { FTextLocalizationManager::Get().DisableGameLocalizationPreview(); },
		[](const FState& InState) { return AllChinese(InState); }));

	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([State]()
	{
		// Whatever happened above, the host's file is left as it was.
		FString Now;
		if (!FFileHelper::LoadFileToString(Now, *State->DlocPath) || Now != State->DlocOriginal)
		{
			FFileHelper::SaveStringToFile(State->DlocOriginal, *State->DlocPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		}
		State->Umg.Reset();
		return true;
	}));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
