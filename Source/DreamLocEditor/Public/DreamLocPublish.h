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
 *
 * `dloc bake -Plugin=<name>` bakes a plugin's own DLoc/ into the plugin, as localization targets of its .uplugin: for a
 * plugin's own UI, which the engine then shows translated in any project (BakePlugin).
 */
namespace DreamLocPublish
{
	struct FBakeOptions
	{
		/** Empty: Content/Localization/DreamLoc of the project. */
		FString OutputDirectory;
		/** The files' base name, <Name>.locmeta and <culture>/<Name>.locres; empty: DreamLoc. */
		FString Name;
		/** Empty: the project's and the enabled plugins' DLoc folders (not mods: they ship on their own). */
		TArray<FDreamLocLayer> Layers;
		/** The cultures and fallbacks to bake; unset: the project's dloc.ini. */
		TOptional<FDreamLocSettings> Settings;
		/** Only the texts of the namespaces it takes; unset: every text. */
		TFunction<bool(const FString& InNamespace)> NamespaceFilter;
		/** No text to bake: no files, and the ones an earlier bake wrote there go. */
		bool bSkipEmpty = false;
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

	/** One of a plugin's localization targets, as its .uplugin lists it. */
	struct FPluginTarget
	{
		FString Name;
		/** ELocalizationTargetDescriptorLoadingPolicy by its name: Editor, Always, Game, PropertyNames or ToolTips. */
		FString LoadingPolicy;

		bool operator==(const FPluginTarget& InOther) const { return Name == InOther.Name && LoadingPolicy == InOther.LoadingPolicy; }
	};

	/**
	 * The targets `dloc bake -Plugin` writes, split as the engine splits its own editor's, so the editor loads each when
	 * it loads its own:
	 *
	 *   <Plugin>               every text but the two below: LOCTEXT, assets, categories (UObjectCategory); the
	 *                          dloc.ini's [Publish] LoadingPolicy (default Editor)
	 *   <Plugin>PropertyNames  UObjectDisplayNames, properties' and enum values' display names: PropertyNames, so
	 *                          Editor Preferences > Region & Language > Use localized property names turns them off
	 *   <Plugin>ToolTips       UObjectToolTips and UObjectShortTooltips: ToolTips
	 */
	DREAMLOCEDITOR_API TArray<FPluginTarget> GetPluginTargets(const FString& InPluginName, const FDreamLocSettings& InSettings);

	struct FPluginBakeOptions
	{
		/** The plugin's base folder, where its .uplugin is. */
		FString PluginDirectory;
		/** The .uplugin's base name; the targets are named after it. */
		FString PluginName;
		/** Unset: <PluginDirectory>/DLoc/dloc.ini. */
		TOptional<FDreamLocSettings> Settings;
		bool bDryRun = false;
	};

	struct FPluginBakeResult
	{
		bool bSucceeded = false;
		/** The targets that have texts, as the .uplugin now lists them. */
		TArray<FPluginTarget> Targets;
		/** Culture -> translations baked, every target's together. */
		TMap<FString, int32> Translations;
		int32 Sources = 0;
		TArray<FString> Files;
		/** The .uplugin's LocalizationTargets changed (or would have). */
		bool bDescriptorChanged = false;
		TArray<FDreamLocReportLine> Messages;
	};

	/**
	 * `dloc bake -Plugin=<name>`: the plugin's DLoc/ into its own localization targets, which the engine loads with the
	 * plugin -- DreamLoc or not:
	 *
	 *   <Plugin>/Content/Localization/<Target>/<Target>.locmeta
	 *   <Plugin>/Content/Localization/<Target>/<culture>/<Target>.locres     the source culture's too
	 *
	 * and the .uplugin's "LocalizationTargets" set to them (a target with no text is left out, and its files go;
	 * targets of other names stay). Machine translations are baked with the rest, as DreamLoc shows them everywhere.
	 * The engine reads a .uplugin when it starts: the editor shows a new target after a restart.
	 */
	DREAMLOCEDITOR_API FPluginBakeResult BakePlugin(const FPluginBakeOptions& InOptions);

	/**
	 * A .uplugin's text with "LocalizationTargets" set: InTargets in place of every target named in InOwnNames, after
	 * the others, which stay as they were. Only that array's text changes, in the file's indentation and line ends.
	 * OutText is InText when the targets are those already. False, with OutError, when InText is not a JSON object.
	 */
	DREAMLOCEDITOR_API bool SetLocalizationTargets(const FString& InText, const TArray<FString>& InOwnNames, const TArray<FPluginTarget>& InTargets,
		FString& OutText, FString& OutError);

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
