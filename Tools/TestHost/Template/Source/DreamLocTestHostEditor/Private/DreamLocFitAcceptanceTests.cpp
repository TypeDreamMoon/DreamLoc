// Copyright DreamMoon. All Rights Reserved.

#include "Core/DreamTextUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "DreamLocCheck.h"
#include "DreamLocModule.h"
#include "DreamLocTextSource.h"
#include "DreamWidgetBlueprint.h"
#include "HAL/FileManager.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamLocFitAcceptanceLocal
{
	/** One .dloc of the fit screen for a culture: Apply and Hint, as sync would write them, with these translations. */
	FString FitFile(const TCHAR* InCulture, const TCHAR* InApply, const TCHAR* InHint)
	{
		return FString::Printf(TEXT("dloc 1\nculture %s\nnamespace /Game/Acceptance/WBP_Fit\n\n")
			TEXT("Apply.Text {\n    at    DUI/Acceptance/Fit.dui#Apply\n    note  \"底部按钮，点了立刻生效\"\n    max   16\n    src   \"应用更改\"\n    text  \"%s\"\n}\n\n")
			TEXT("Hint.Text {\n    at    DUI/Acceptance/Fit.dui#Hint\n    src   \"按任意键继续\"\n    text  \"%s\"\n}\n"), InCulture, InApply, InHint);
	}

	TArray<FString> MessagesWith(const FCompilerResultsLog& InResults, const TCHAR* InCode)
	{
		TArray<FString> Found;
		for (const TSharedRef<FTokenizedMessage>& Message : InResults.Messages)
		{
			const FString Text = Message->ToText().ToString();
			if (Text.Contains(InCode))
			{
				Found.Add(Text);
			}
		}
		return Found;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocFitTest, "DreamLocHost.Acceptance.ATranslationThatDoesNotFitIsSaidByTheCompileAndByCheck",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLocFitTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocFitAcceptanceLocal;
	FString Project = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	FPaths::NormalizeDirectoryName(Project);
	FString Work = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("DreamLocTests") / (TEXT("Fit-") + FGuid::NewGuid().ToString(EGuidFormats::Short)));
	FPaths::NormalizeDirectoryName(Work);
	const FString DLoc = Work / TEXT("DLoc");
	ON_SCOPE_EXIT { IFileManager::Get().DeleteDirectory(*Work, false, true); };

	// A German translation four times the box, Japanese with a character no font here has (U+13000), English that fits.
	FFileHelper::SaveStringToFile(FitFile(TEXT("de"), TEXT("Alle Änderungen übernehmen und dauerhaft speichern"), TEXT("Weiter")),
		*(DLoc / TEXT("de/DUI/Acceptance/Fit.dloc")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	FFileHelper::SaveStringToFile(FitFile(TEXT("ja"), TEXT("適用"), TEXT("キーを押して\U00013000")),
		*(DLoc / TEXT("ja/DUI/Acceptance/Fit.dloc")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	FFileHelper::SaveStringToFile(FitFile(TEXT("en"), TEXT("Apply"), TEXT("Press any key")),
		*(DLoc / TEXT("en/DUI/Acceptance/Fit.dloc")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

	// The translations as a layer the editor has, as a mod's would be, while the screen compiles.
	const TSharedPtr<FDreamLocTextSource> Source = FDreamLocModule::Get().GetTextSource();
	if (!TestTrue(TEXT("DreamLoc's text source"), Source.IsValid()))
	{
		return false;
	}
	Source->MountLayer(FDreamLocLayer{ 2, TEXT("DreamLocHost.Fit"), DLoc });
	ON_SCOPE_EXIT { Source->UnmountLayer(DLoc); };

	const FString Name = FString::Printf(TEXT("WBP_Fit_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	UDreamWidgetBlueprint* Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(UDreamTextUserWidget::StaticClass(),
		CreatePackage(*(TEXT("/Temp/DreamLocTestHost/") + Name)), FName(*Name), BPTYPE_Normal, UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
	UDreamTextUserWidget* Defaults = Blueprint != nullptr ? Cast<UDreamTextUserWidget>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
	if (!TestNotNull(TEXT("a .dui-backed blueprint"), Defaults))
	{
		return false;
	}
	TStrongObjectPtr<UDreamWidgetBlueprint> Keep(Blueprint);
	Defaults->SourceFile.FilePath = Project / TEXT("DUI/Acceptance/Fit.dui");
	FCompilerResultsLog Results;
	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
	TestEqual(TEXT("the screen compiles"), Results.NumErrors, 0);

	// 1. The compile, which is what the designer runs as the screen is edited.
	const TArray<FString> TooLong = MessagesWith(Results, TEXT("DUI6024"));
	for (const FString& Message : TooLong)
	{
		AddInfo(TEXT("compile: ") + Message);
	}
	TestTrue(TEXT("the compile says the German one does not fit"), TooLong.Num() == 1 && TooLong[0].Contains(TEXT("the de translation of 'Apply'")));
	const TArray<FString> Missing = MessagesWith(Results, TEXT("DUI6025"));
	for (const FString& Message : Missing)
	{
		AddInfo(TEXT("compile: ") + Message);
	}
	TestTrue(TEXT("and that the Japanese one has a character the font has not"), Missing.ContainsByPredicate([](const FString& In)
	{
		return In.Contains(TEXT("U+13000")) && In.Contains(TEXT("in the ja translation"));
	}));

	// 2. dloc check, which asks DreamGUI to lay the same translations out.
	FDreamLocCheckOptions Options;
	Options.Roots.Add(FDreamLocCheckRoot{ DLoc, 2 });
	TArray<FString> Codes;
	for (const FDreamLocReportLine& Line : DreamLocCheck::Run(Options).Lines)
	{
		if (Line.Diagnostic.Code >= 4000)
		{
			Codes.Add(FString::Printf(TEXT("%s %s"), *Line.Diagnostic.GetCodeName(), *FPaths::GetCleanFilename(FPaths::GetPath(FPaths::GetPath(FPaths::GetPath(Line.File))))));
			AddInfo(TEXT("check: ") + Line.Format());
		}
	}
	Codes.Sort();
	TestEqual(TEXT("check: the German one does not fit, the Japanese one lacks a glyph, English is fine"), FString::Join(Codes, TEXT(", ")),
		FString(TEXT("DLOC4001 de, DLOC4002 ja")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
