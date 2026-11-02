// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamLocDiagnostics.h"
#include "DreamLocSettings.h"
#include "Internationalization/ILocalizedTextSource.h"
#include "Internationalization/TextKey.h"
#include "Misc/DateTime.h"

struct FDreamLocDocument;

/** One DLoc/ folder, or one baked folder, and where it ranks. */
struct DREAMLOC_API FDreamLocLayer
{
	/** 0: an enabled plugin's own text. 1: the project. 2 and up: DLC and mods, in the order they were mounted. */
	int32 Index = 1;
	/** "Project", "Plugin.DreamGUI", "Mod.<name>", or what the code that mounted it called it. */
	FString Name;
	/** The absolute path of the DLoc folder: the one holding the <culture>/ folders. */
	FString Directory;
	/**
	 * Not .dloc files but what `dloc bake` made of them: <Directory>/<culture>/DreamLoc.locres per culture, the source
	 * culture's mapping every text to its source, and <Directory>/DreamLoc.locmeta naming it.
	 */
	bool bBaked = false;
};

/**
 * Hands the translations in DLoc/<culture>/**.dloc to the engine's FText translation table.
 *
 * Registered with FTextLocalizationManager at the highest source priority. The engine loads every source into one
 * table and keeps, per text, the entry with the smallest priority number, so each entry's number is
 *
 *     -1000 + culture index * 64 + (63 - layer)
 *
 * The culture index is the position in the engine's prioritized list (0 the most specific), with the dloc.ini
 * fallbacks after it. Every DreamLoc entry ranks above the engine's own locres files (whose numbers start at 0);
 * among DreamLoc's, a more specific culture wins, and within a culture a higher layer: mod over project over plugin.
 *
 * A translation is registered under the hash of its source text -- of `prev` when there is one, so a stale
 * translation fails the engine's source check and the current source shows instead. An untranslated entry
 * registers nothing of its own.
 *
 * Every text DreamLoc has seen also gets an entry mapping its source to itself, at a number below everything else
 * in the table. The engine never takes an entry out of its table; it only overwrites, and it overwrites a
 * translation with the source by loading the native culture's file, which DreamLoc does not have -- the source is in
 * the .dui and the assets. Without these, a translation deleted from a .dloc, or one shown before the preview
 * switched back to the source language, would stay on screen until the editor restarts.
 *
 * Only game text: the Game load flags (or the editor's forced-localized game preview). The editor's own text and the
 * engine's are never touched. In the editor without a game preview, nothing is loaded: the editor shows the source.
 *
 * A packaged game has no DLoc/ folder: the project's translations come from what `dloc bake` wrote into
 * Content/Localization/DreamLoc (the plugins' baked in with them), at the project's layer, with the source culture's
 * baked file as the source-to-source entries. Mods' DLoc/ folders are read as .dloc files over it, packaged or not.
 *
 * LoadLocalizedResources runs on the engine's localization task; everything here is safe to call from any thread.
 */
class DREAMLOC_API FDreamLocTextSource : public ILocalizedTextSource
{
public:
	/** The layers are found again on every load: the project's DLoc/ and every enabled plugin's, plus mounted ones. */
	FDreamLocTextSource();

	/** Exactly these layers and these settings, nothing found on disk. For tests and tools. */
	FDreamLocTextSource(TArray<FDreamLocLayer> InLayers, const FDreamLocSettings& InSettings);

	//~ ILocalizedTextSource
	virtual int32 GetPriority() const override;
	virtual bool GetNativeCultureName(const ELocalizedTextSourceCategory InCategory, FString& OutNativeCultureName) override;
	virtual void GetLocalizedCultureNames(const ELocalizationLoadFlags InLoadFlags, TSet<FString>& OutLocalizedCultureNames) override;
	virtual void LoadLocalizedResources(const ELocalizationLoadFlags InLoadFlags, TArrayView<const FString> InPrioritizedCultures,
		FTextLocalizationResource& InOutNativeResource, FTextLocalizationResource& InOutLocalizedResource) override;

	/** The number of a translation entry. Layer is clamped to 0..63. */
	static int32 MakeEntryPriority(int32 InCultureIndex, int32 InLayer);
	/** The number of the source-to-source entry every seen text gets: below every other entry in the table. */
	static int32 MakeSourceEntryPriority(int32 InLayer);

	/** The project's DLoc/ folder, absolute (it may not exist). */
	static FString GetProjectDirectory();

	/** Where `dloc bake` writes and a packaged game reads: <Project>/Content/Localization/DreamLoc, absolute. */
	static FString GetBakedDirectory();

	/** The base name of the baked files: DreamLoc.locres, DreamLoc.locmeta. */
	static constexpr const TCHAR* BakedName = TEXT("DreamLoc");

	/**
	 * The baked folder is one of the engine's own game localization paths ([Internationalization] LocalizationPaths):
	 * the Native way of shipping, where the engine loads the baked files and DreamLoc leaves them alone.
	 */
	static bool IsBakedDirectoryLoadedByEngine();

	/**
	 * A culture name the engine knows ("en", "zh-Hans", "es-419"), case-insensitively. Not FInternationalization::GetCulture
	 * alone: that makes a culture of any string at all, so a folder named "Drafts" would pass.
	 */
	static bool IsKnownCulture(const FString& InName);
	/**
	 * Layer 0, one per enabled plugin with a DLoc/ folder; layer 1, the project's DLoc/ folder, or, where there is none
	 * (a packaged game), the baked folder unless the engine loads it itself; and from 2, one per enabled mod plugin
	 * (a plugin of the project's Mods/ folder) with a DLoc/ folder, by name.
	 */
	static TArray<FDreamLocLayer> FindLayers();

	/** Every layer the next load reads: found and mounted, in rank order. */
	TArray<FDreamLocLayer> GetLayers() const;

	/**
	 * Adds a layer to every load from now on -- a mod's or DLC's DLoc/ folder, or a test's. Replaces one with the same
	 * directory. Does not reload: call FDreamLocModule::ReloadTranslations when the change should show.
	 */
	void MountLayer(const FDreamLocLayer& InLayer);
	void UnmountLayer(const FString& InDirectory);

	/** The settings the next load uses: the fixed ones, or the project's dloc.ini. */
	FDreamLocSettings GetSettings() const;

	/** Drops every parsed file, so the next load reads them all again. Files are also re-read when their time stamp or size changes. */
	void Invalidate();

	/**
	 * The culture the editor's game localization preview last loaded game text for: what to load again when a .dloc
	 * changes while the preview is on. Empty when no preview has run.
	 */
	FString GetLastPreviewCulture() const;

	/** No more loading: the module is going away, and the engine keeps its registered sources until it shuts down. */
	void Shutdown();

private:
	struct FCachedFile
	{
		FDateTime TimeStamp;
		int64 Size = -1;
		TSharedPtr<FDreamLocDocument> Document;
		bool bUsable = false;
	};

	struct FSeenText
	{
		FString Source;
		int32 Layer = 0;
	};

	FDreamLocSettings ReadSettings_Locked() const;
	TArray<FDreamLocLayer> GetLayers_Locked() const;
	/** The project has a DLoc/ folder, or layers are mounted or fixed: DreamLoc speaks for the game's text. */
	bool IsInUse_Locked() const;
	/** The source culture a baked folder's DreamLoc.locmeta names, and the file mapping each text to its source. */
	static bool ReadBakedNative(const FString& InDirectory, FString& OutCulture, FString& OutNativeLocRes);
	/** The parsed file, or null when it cannot be read or has errors (logged once per version of the file). */
	TSharedPtr<FDreamLocDocument> GetDocument_Locked(const FString& InPath, const FString& InCulture);

	mutable FCriticalSection Lock;
	bool bFixed = false;
	bool bShutdown = false;
	TArray<FDreamLocLayer> FixedLayers;
	FDreamLocSettings FixedSettings;
	TArray<FDreamLocLayer> MountedLayers;
	TMap<FString, FCachedFile> Files;
	TMap<FTextId, FSeenText> SeenTexts;
	FString LastPreviewCulture;

	/** dloc.ini, read again when its time stamp changes. */
	mutable FDreamLocSettings CachedSettings;
	mutable FDateTime CachedSettingsTimeStamp;
	mutable bool bSettingsCached = false;
};
