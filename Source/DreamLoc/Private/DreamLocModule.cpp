// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocModule.h"

#include "DreamLocDiagnostics.h"
#include "DreamLocTextSource.h"
#include "Internationalization/TextLocalizationManager.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FDreamLocModule, DreamLoc);

FDreamLocModule& FDreamLocModule::Get()
{
	return FModuleManager::LoadModuleChecked<FDreamLocModule>(TEXT("DreamLoc"));
}

bool FDreamLocModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded(TEXT("DreamLoc"));
}

void FDreamLocModule::StartupModule()
{
	TextSource = MakeShared<FDreamLocTextSource>();
	// At PostConfigInit the engine has not loaded any text yet, and its first load reads every registered source.
	// A module loaded once the engine runs (a plugin turned on later) has to ask for that load itself.
	FTextLocalizationManager::Get().RegisterTextSource(TextSource.ToSharedRef(), /*InRefreshResources*/ GIsRunning);
}

void FDreamLocModule::ShutdownModule()
{
	// The engine has no way to unregister a source and keeps it until it shuts down itself; it only stops loading.
	if (TextSource.IsValid())
	{
		TextSource->Shutdown();
	}
	TextSource.Reset();
}

void FDreamLocModule::ReloadTranslations()
{
	if (!TextSource.IsValid())
	{
		return;
	}
	FTextLocalizationManager& Manager = FTextLocalizationManager::Get();
#if WITH_EDITOR
	if (GIsEditor)
	{
		if (Manager.IsGameLocalizationPreviewEnabled())
		{
			FString Culture = TextSource->GetLastPreviewCulture();
			if (Culture.IsEmpty())
			{
				Culture = Manager.GetConfiguredGameLocalizationPreviewLanguage();
			}
			UE_LOG(LogDreamLoc, Verbose, TEXT("Reloading translations for the game preview in %s"), *Culture);
			Manager.EnableGameLocalizationPreview(Culture);
		}
		else if (Manager.ShouldForceLoadGameLocalization())
		{
			Manager.RefreshResources();
		}
		return;
	}
#endif
	Manager.RefreshResources();
}
