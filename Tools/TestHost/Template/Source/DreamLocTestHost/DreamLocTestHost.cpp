// Copyright DreamMoon. All Rights Reserved.

#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "DreamLocModule.h"
#include "DreamLocTestHostTexts.h"
#include "DreamLocTextSource.h"
#include "HAL/PlatformMisc.h"
#include "Interfaces/IPluginManager.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/TextLocalizationManager.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UObjectGlobals.h"

/*
 * The packaged check: started with -DreamLocProbe=<file>, the game reads its texts in each language, mounts the test
 * mod named by -DreamLocMod=<.uplugin> (whose DLoc/ brings Polish), reads them in Polish, writes what it saw to the
 * file as JSON and quits. A Shipping game has no log and no console, so a file is the only way to say anything;
 * Tools/TestHost/Test-Package.ps1 reads it.
 */
namespace DreamLocTestHostProbe
{
	FString ReadUmgText()
	{
		const UWidgetBlueprintGeneratedClass* Class = Cast<UWidgetBlueprintGeneratedClass>(
			LoadObject<UClass>(nullptr, TEXT("/Game/Acceptance/Gather/WBP_GatherUmg.WBP_GatherUmg_C")));
		const UWidgetTree* Tree = Class != nullptr ? Class->GetWidgetTreeArchetype() : nullptr;
		const UTextBlock* Text = Tree != nullptr ? Cast<UTextBlock>(Tree->FindWidget(TEXT("Start"))) : nullptr;
		return Text != nullptr ? Text->GetText().ToString() : TEXT("<no WBP_GatherUmg>");
	}

	TSharedRef<FJsonObject> ReadTexts(const FString& InStep)
	{
		FTextLocalizationManager::Get().WaitForAsyncTasks();
		TSharedRef<FJsonObject> Step = MakeShared<FJsonObject>();
		Step->SetStringField(TEXT("step"), InStep);
		Step->SetStringField(TEXT("culture"), FInternationalization::Get().GetCurrentLanguage()->GetName());
		Step->SetStringField(TEXT("code"), DreamLocTestHostTexts::GetContinue().ToString());
		Step->SetStringField(TEXT("umg"), ReadUmgText());
		TArray<TSharedPtr<FJsonValue>> Cultures;
		TArray<FString> Names = FTextLocalizationManager::Get().GetLocalizedCultureNames(ELocalizationLoadFlags::Game);
		Names.Sort();
		for (const FString& Name : Names)
		{
			Cultures.Add(MakeShared<FJsonValueString>(Name));
		}
		Step->SetArrayField(TEXT("cultures"), Cultures);
		TArray<TSharedPtr<FJsonValue>> Layers;
		if (FDreamLocModule::IsAvailable() && FDreamLocModule::Get().GetTextSource().IsValid())
		{
			for (const FDreamLocLayer& Layer : FDreamLocModule::Get().GetTextSource()->GetLayers())
			{
				Layers.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%d %s%s"), Layer.Index, *Layer.Name, Layer.bBaked ? TEXT(" (baked)") : TEXT(""))));
			}
		}
		Step->SetArrayField(TEXT("layers"), Layers);
		return Step;
	}

	class FProbe
	{
	public:
		FProbe(FString InOutput, FString InMod) : Output(MoveTemp(InOutput)), Mod(MoveTemp(InMod)) {}

		/** One step a few frames apart: the engine settles between a culture change and the next read. */
		bool Tick(float)
		{
			if (++Frame < 10 || (Frame % 5) != 0)
			{
				return true;
			}
			switch (Next++)
			{
			case 0: Steps.Add(MakeShared<FJsonValueObject>(ReadTexts(TEXT("start")))); break;
			case 1: Switch(TEXT("en")); break;
			case 2: Steps.Add(MakeShared<FJsonValueObject>(ReadTexts(TEXT("en")))); break;
			case 3: Switch(TEXT("zh-Hans")); break;
			case 4: Steps.Add(MakeShared<FJsonValueObject>(ReadTexts(TEXT("zh-Hans")))); break;
			case 5: MountMod(); break;
			case 6: Steps.Add(MakeShared<FJsonValueObject>(ReadTexts(TEXT("mod mounted")))); break;
			case 7: Switch(TEXT("pl")); break;
			case 8: Steps.Add(MakeShared<FJsonValueObject>(ReadTexts(TEXT("pl")))); break;
			default:
				Write();
				FPlatformMisc::RequestExit(/*bForce*/ false);
				return false;
			}
			return true;
		}

	private:
		void Switch(const TCHAR* InCulture)
		{
			Notes.Add(FString::Printf(TEXT("SetCurrentCulture(%s): %s"), InCulture, FInternationalization::Get().SetCurrentCulture(InCulture) ? TEXT("true") : TEXT("false")));
		}

		void MountMod()
		{
			if (Mod.IsEmpty())
			{
				Notes.Add(TEXT("no -DreamLocMod"));
				return;
			}
			FText Reason;
			const bool bAdded = IPluginManager::Get().AddToPluginsList(Mod, &Reason);
			Notes.Add(FString::Printf(TEXT("AddToPluginsList(%s): %s %s"), *Mod, bAdded ? TEXT("true") : TEXT("false"), *Reason.ToString()));
			if (bAdded)
			{
				IPluginManager::Get().MountNewlyCreatedPlugin(FPaths::GetBaseFilename(Mod));
				Notes.Add(TEXT("MountNewlyCreatedPlugin"));
			}
		}

		void Write()
		{
			TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
			Root->SetStringField(TEXT("build"), LexToString(FApp::GetBuildConfiguration()));
			Root->SetArrayField(TEXT("steps"), Steps);
			TArray<TSharedPtr<FJsonValue>> NoteValues;
			for (const FString& Note : Notes)
			{
				NoteValues.Add(MakeShared<FJsonValueString>(Note));
			}
			Root->SetArrayField(TEXT("notes"), NoteValues);
			FString Text;
			const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
			FJsonSerializer::Serialize(Root, Writer);
			FFileHelper::SaveStringToFile(Text, *Output, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		}

		FString Output;
		FString Mod;
		int32 Frame = 0;
		int32 Next = 0;
		TArray<TSharedPtr<FJsonValue>> Steps;
		TArray<FString> Notes;
	};
}

class FDreamLocTestHostModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		FString Output;
		if (!FParse::Value(FCommandLine::Get(), TEXT("DreamLocProbe="), Output))
		{
			return;
		}
		FString Mod;
		FParse::Value(FCommandLine::Get(), TEXT("DreamLocMod="), Mod);
		Probe = MakeShared<DreamLocTestHostProbe::FProbe>(Output, Mod);
		Handle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateSP(Probe.ToSharedRef(), &DreamLocTestHostProbe::FProbe::Tick));
	}

	virtual void ShutdownModule() override
	{
		FTSTicker::GetCoreTicker().RemoveTicker(Handle);
		Probe.Reset();
	}

private:
	TSharedPtr<DreamLocTestHostProbe::FProbe> Probe;
	FTSTicker::FDelegateHandle Handle;
};

IMPLEMENT_PRIMARY_GAME_MODULE(FDreamLocTestHostModule, DreamLocTestHost, "DreamLocTestHost");
