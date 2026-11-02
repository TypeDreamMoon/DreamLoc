// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Containers/Ticker.h"

struct FFileChangeData;

/**
 * Translations that follow the files: every DLoc/ folder DreamLoc loads from is watched, and a change to a .dloc or
 * to dloc.ini reloads the translations once the changes stop for a moment. The folders are checked once a second,
 * so a DLoc/ folder made while the editor runs, or a layer mounted later, is watched from then on.
 *
 * Console commands:
 *   DreamLoc.Preview <culture>   the game localization preview in that culture; no culture turns it off
 *   DreamLoc.Reload              reload the translations now
 */
class DREAMLOCEDITOR_API FDreamLocEditorModule : public IModuleInterface
{
public:
	static FDreamLocEditorModule& Get();

	/** How many reloads a file change has caused. */
	int32 GetReloadCount() const { return ReloadCount; }

	/** The folders being watched, absolute. */
	TArray<FString> GetWatchedDirectories() const;

	/** Bring the watched folders in line with the layers now, rather than within the second. */
	void SyncWatches();

	//~ IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	bool Tick(float InDeltaTime);
	void HandleDirectoryChanged(const TArray<FFileChangeData>& InChanges);

	FTSTicker::FDelegateHandle TickHandle;
	TMap<FString, FDelegateHandle> Watches;
	double NextWatchSync = 0.0;
	/** The time of the last change not yet reloaded, or 0. */
	double PendingChangeTime = 0.0;
	int32 ReloadCount = 0;
	TArray<class IConsoleObject*> ConsoleObjects;
};
