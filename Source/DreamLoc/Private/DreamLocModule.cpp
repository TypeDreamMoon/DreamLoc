// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocModule.h"

#include "DreamLocDiagnostics.h"
#include "DreamLocTextSource.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Internationalization/TextLocalizationManager.h"
#include "Misc/Paths.h"
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

	for (const FDreamLocLayer& Layer : FDreamLocTextSource::FindLayers())
	{
		NextMountedLayer = FMath::Max(NextMountedLayer, Layer.Index + 1);
	}
	// The engine says "mounted" only for a plugin with content; a mod of nothing but translations, mounted with
	// MountNewlyCreatedPlugin, only gets "created". Both, then: the same folder is one layer however often it is heard of.
	MountedHandle = IPluginManager::Get().OnNewPluginMounted().AddRaw(this, &FDreamLocModule::HandlePluginMounted);
	CreatedHandle = IPluginManager::Get().OnNewPluginCreated().AddRaw(this, &FDreamLocModule::HandlePluginMounted);
	UnmountedHandle = IPluginManager::Get().OnPluginUnmounted().AddRaw(this, &FDreamLocModule::HandlePluginUnmounted);
}

void FDreamLocModule::ShutdownModule()
{
	if (IPluginManager* Plugins = FModuleManager::Get().IsModuleLoaded(TEXT("Projects")) ? &IPluginManager::Get() : nullptr)
	{
		Plugins->OnNewPluginMounted().Remove(MountedHandle);
		Plugins->OnNewPluginCreated().Remove(CreatedHandle);
		Plugins->OnPluginUnmounted().Remove(UnmountedHandle);
	}
	// The engine has no way to unregister a source and keeps it until it shuts down itself; it only stops loading.
	if (TextSource.IsValid())
	{
		TextSource->Shutdown();
	}
	TextSource.Reset();
}

void FDreamLocModule::HandlePluginMounted(IPlugin& InPlugin)
{
	FString Directory = FPaths::ConvertRelativePathToFull(InPlugin.GetBaseDir() / TEXT("DLoc"));
	FPaths::NormalizeDirectoryName(Directory);
	if (!TextSource.IsValid() || !IFileManager::Get().DirectoryExists(*Directory))
	{
		return;
	}
	for (const FDreamLocLayer& Existing : TextSource->GetLayers())
	{
		if (Existing.Index >= 2 && Existing.Directory.Equals(Directory, ESearchCase::IgnoreCase))
		{
			return;
		}
	}
	FDreamLocLayer Layer;
	Layer.Index = NextMountedLayer++;
	Layer.Name = TEXT("Mod.") + InPlugin.GetName();
	Layer.Directory = Directory;
	UE_LOG(LogDreamLoc, Log, TEXT("%s mounted with translations: layer %d (%s)"), *InPlugin.GetName(), Layer.Index, *Directory);
	TextSource->MountLayer(Layer);
	ReloadTranslations();
}

void FDreamLocModule::HandlePluginUnmounted(IPlugin& InPlugin)
{
	if (TextSource.IsValid())
	{
		TextSource->UnmountLayer(InPlugin.GetBaseDir() / TEXT("DLoc"));
		ReloadTranslations();
	}
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
