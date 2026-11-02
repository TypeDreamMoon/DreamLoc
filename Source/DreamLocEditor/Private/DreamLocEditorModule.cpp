// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocEditorModule.h"

#include "DirectoryWatcherModule.h"
#include "DreamLocDiagnostics.h"
#include "DreamLocFile.h"
#include "DreamLocModule.h"
#include "DreamLocSettings.h"
#include "DreamLocTextSource.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "IDirectoryWatcher.h"
#include "Internationalization/TextLocalizationManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FDreamLocEditorModule, DreamLocEditor);

namespace DreamLocEditorLocal
{
	/** Quiet time after the last change before reloading: an editor saving a file often writes it more than once. */
	constexpr double DebounceSeconds = 0.25;
	constexpr double WatchSyncSeconds = 1.0;

	IDirectoryWatcher* GetDirectoryWatcher()
	{
		FDirectoryWatcherModule& Module = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));
		return Module.Get();
	}

	/** A change worth a reload: a .dloc, dloc.ini, or a folder (a culture folder made, renamed or deleted). */
	bool IsRelevant(const FString& InFilename)
	{
		const FString Extension = FPaths::GetExtension(InFilename, /*bIncludeDot*/ true);
		return Extension.Equals(DreamLocFile::Extension, ESearchCase::IgnoreCase)
			|| FPaths::GetCleanFilename(InFilename).Equals(FDreamLocSettings::FileName, ESearchCase::IgnoreCase)
			|| Extension.IsEmpty();
	}
}

FDreamLocEditorModule& FDreamLocEditorModule::Get()
{
	return FModuleManager::LoadModuleChecked<FDreamLocEditorModule>(TEXT("DreamLocEditor"));
}

void FDreamLocEditorModule::StartupModule()
{
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FDreamLocEditorModule::Tick), 0.0f);

	ConsoleObjects.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("DreamLoc.Preview"),
		TEXT("DreamLoc.Preview <culture>: show the game text in that culture (the engine's game localization preview). No culture: back to the source."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& InArgs)
		{
			FTextLocalizationManager& Manager = FTextLocalizationManager::Get();
			if (InArgs.Num() > 0 && !InArgs[0].IsEmpty())
			{
				Manager.EnableGameLocalizationPreview(InArgs[0]);
			}
			else
			{
				Manager.DisableGameLocalizationPreview();
			}
		}),
		ECVF_Default));

	ConsoleObjects.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("DreamLoc.Reload"),
		TEXT("DreamLoc.Reload: read the .dloc files again and hand the translations to the engine."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			if (FDreamLocModule::IsAvailable())
			{
				FDreamLocModule::Get().ReloadTranslations();
			}
		}),
		ECVF_Default));
}

void FDreamLocEditorModule::ShutdownModule()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	for (IConsoleObject* Object : ConsoleObjects)
	{
		IConsoleManager::Get().UnregisterConsoleObject(Object);
	}
	ConsoleObjects.Reset();

	if (FModuleManager::Get().IsModuleLoaded(TEXT("DirectoryWatcher")))
	{
		if (IDirectoryWatcher* Watcher = DreamLocEditorLocal::GetDirectoryWatcher())
		{
			for (const TPair<FString, FDelegateHandle>& Watch : Watches)
			{
				Watcher->UnregisterDirectoryChangedCallback_Handle(Watch.Key, Watch.Value);
			}
		}
	}
	Watches.Reset();
}

TArray<FString> FDreamLocEditorModule::GetWatchedDirectories() const
{
	TArray<FString> Directories;
	Watches.GetKeys(Directories);
	return Directories;
}

void FDreamLocEditorModule::SyncWatches()
{
	NextWatchSync = FPlatformTime::Seconds() + DreamLocEditorLocal::WatchSyncSeconds;
	if (!FDreamLocModule::IsAvailable() || !FDreamLocModule::Get().GetTextSource().IsValid())
	{
		return;
	}
	IDirectoryWatcher* Watcher = DreamLocEditorLocal::GetDirectoryWatcher();
	if (Watcher == nullptr)
	{
		return;
	}

	TSet<FString> Wanted;
	for (const FDreamLocLayer& Layer : FDreamLocModule::Get().GetTextSource()->GetLayers())
	{
		if (IFileManager::Get().DirectoryExists(*Layer.Directory))
		{
			Wanted.Add(Layer.Directory);
		}
	}

	for (auto It = Watches.CreateIterator(); It; ++It)
	{
		if (!Wanted.Contains(It.Key()))
		{
			Watcher->UnregisterDirectoryChangedCallback_Handle(It.Key(), It.Value());
			It.RemoveCurrent();
		}
	}
	for (const FString& Directory : Wanted)
	{
		if (!Watches.Contains(Directory))
		{
			FDelegateHandle Handle;
			if (Watcher->RegisterDirectoryChangedCallback_Handle(Directory,
				IDirectoryWatcher::FDirectoryChanged::CreateRaw(this, &FDreamLocEditorModule::HandleDirectoryChanged),
				Handle, IDirectoryWatcher::WatchOptions::IncludeDirectoryChanges))
			{
				Watches.Add(Directory, Handle);
				UE_LOG(LogDreamLoc, Verbose, TEXT("Watching %s"), *Directory);
			}
		}
	}
}

bool FDreamLocEditorModule::Tick(float InDeltaTime)
{
	const double Now = FPlatformTime::Seconds();
	if (Now >= NextWatchSync)
	{
		SyncWatches();
	}
	if (PendingChangeTime > 0.0 && Now - PendingChangeTime >= DreamLocEditorLocal::DebounceSeconds)
	{
		PendingChangeTime = 0.0;
		if (FDreamLocModule::IsAvailable())
		{
			++ReloadCount;
			UE_LOG(LogDreamLoc, Log, TEXT("Translations changed on disk; reloading."));
			FDreamLocModule::Get().ReloadTranslations();
		}
	}
	return true;
}

void FDreamLocEditorModule::HandleDirectoryChanged(const TArray<FFileChangeData>& InChanges)
{
	for (const FFileChangeData& Change : InChanges)
	{
		if (DreamLocEditorLocal::IsRelevant(Change.Filename))
		{
			PendingChangeTime = FPlatformTime::Seconds();
			return;
		}
	}
}
