// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamLocSettings.h"
#include "DreamLocSync.h"
#include "DreamLocTextSource.h"

/**
 * Getting the translations into a packaged game, and keeping the files tidy once they are shipped.
 *
 * `dloc bake` turns the .dloc files of the project and its plugins into what a packaged game reads, in
 * Content/Localization/DreamLoc (which the engine's staging packages like any localization target, for the cultures
 * in CulturesToStage):
 *
 *   DreamLoc.locmeta               the source culture, and which file maps each text to its source
 *   <culture>/DreamLoc.locres      one per target culture: every translation, the project's over the plugins', the
 *                                  dloc.ini fallbacks filled in, each under the hash of the source it was made from
 *                                  (a stale one under its prev, so it still shows the source)
 *   <source culture>/DreamLoc.locres
 *                                  every text mapped to its own source: what switching back to the source language
 *                                  overwrites the translations with
 *
 * With [Publish] Mode=Source DreamLoc reads them in the packaged game and lays mods' .dloc files over them; with
 * Mode=Native the engine reads them itself, through [Internationalization] LocalizationPaths.
 */
namespace DreamLocPublish
{
	struct FBakeOptions
	{
		/** Empty: Content/Localization/DreamLoc of the project. */
		FString OutputDirectory;
		/** Empty: the project's and the enabled plugins' DLoc folders (not mods: they ship on their own). */
		TArray<FDreamLocLayer> Layers;
		/** The cultures and fallbacks to bake; unset: the project's dloc.ini. */
		TOptional<FDreamLocSettings> Settings;
		/** Compare the project's packaging and localization settings with the publish mode. */
		bool bCheckConfig = true;
		bool bDryRun = false;
	};

	struct FBakeResult
	{
		bool bSucceeded = false;
		/** Culture -> translations baked. */
		TMap<FString, int32> Translations;
		/** Texts in the source culture's file. */
		int32 Sources = 0;
		/** Every file written (or that would be), absolute. */
		TArray<FString> Files;
		TArray<FDreamLocReportLine> Messages;
	};

	DREAMLOCEDITOR_API FBakeResult Bake(const FBakeOptions& InOptions);

	/**
	 * `dloc prune`: deletes every removed block, and every file left with nothing in it. The texts in them have been
	 * gone from the sources since some sync; their translations go for good.
	 */
	DREAMLOCEDITOR_API FDreamLocSyncOutput Prune(const TMap<FString, TMap<FString, FString>>& InFiles, const FString& InDLocName = TEXT("DLoc"));

	/** The file `dloc freeze` writes in the DLoc folder. */
	inline constexpr const TCHAR* FrozenFileName = TEXT("dloc.frozen");

	/** `namespace` + U+0001 + `key` of each frozen text. */
	DREAMLOCEDITOR_API TSet<FString> ReadFrozen(const FString& InDLocDirectory);

	/**
	 * `dloc freeze`: records the keys every target culture's files have now as shipped. A later sync that finds one of
	 * them gone from the sources (and not renamed) reports DLOC3022 and writes nothing.
	 */
	DREAMLOCEDITOR_API bool Freeze(const FString& InDLocDirectory, const TMap<FString, TMap<FString, FString>>& InFiles, int32& OutKeys);
}
