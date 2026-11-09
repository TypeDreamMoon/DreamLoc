// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamLocExtract.h"
#include "DreamLocSync.h"

struct FDreamLocSettings;

/**
 * The engine's own texts -- LOCTEXT and NSLOCTEXT in C++, FText properties of assets (UMG, DataTables, Blueprints) --
 * and the engine's own localization targets.
 *
 * `dloc gather` runs the engine's GatherText with a configuration of DreamLoc's making, as far as the manifest and no
 * further: no archive, no PO, no locres. The manifest becomes one Engine extraction manifest per source file, at
 * Intermediate/DLoc/extract/Engine/<file>.json, and `dloc sync` takes them like any other.
 *
 * `dloc import-engine` takes the translations a project already has in a localization target's archives
 * (Content/Localization/<target>/<culture>/<target>.archive) into the .dloc files, once, so the localization dashboard
 * is not needed any more.
 */
namespace DreamLocEngine
{
	struct FGatherOptions
	{
		/** Empty: the project. */
		FString ProjectDirectory;
		/** Folders searched for LOCTEXT / NSLOCTEXT, relative to the project. */
		TArray<FString> SourcePaths;
		/** Folders of assets, relative to the project. */
		TArray<FString> ContentPaths;
		/** Path patterns left out, relative to the project. */
		TArray<FString> ExcludePaths;
		/** The texts only the editor has too: C++ inside #if WITH_EDITOR, editor-only asset properties. */
		bool bEditorOnlyData = false;
		/**
		 * The meta-data of the reflected types whose headers are under SourcePaths too, as the engine gathers its own
		 * editor's: DisplayName in UObjectDisplayNames, ToolTip and ShortTooltip in UObjectToolTips and
		 * UObjectShortTooltips (keyed by the field's path), Category in UObjectCategory (keyed by itself). Enum values'
		 * display names are in UObjectDisplayNames, which the engine leaves out of its own.
		 */
		bool bMetaData = false;
		/** The culture the source texts are in. */
		FString NativeCulture = TEXT("zh-Hans");
		/** Where the Engine manifests go; empty: the project's extract folder. */
		FString ExtractDirectory;
		/** Where the engine's configuration and manifest are written; empty: <project>/Intermediate/DLoc/gather. */
		FString WorkDirectory;
	};

	struct FGatherResult
	{
		bool bSucceeded = false;
		/** Texts in the Engine manifests. */
		int32 Texts = 0;
		/** Source files with texts: one manifest each. */
		int32 Sources = 0;
		int32 ManifestsWritten = 0;
		int32 ManifestsDeleted = 0;
		TArray<FDreamLocReportLine> Messages;
	};

	/** The options dloc.ini's [Gather] section and SourceCulture give. */
	DREAMLOCEDITOR_API FGatherOptions MakeGatherOptions(const FDreamLocSettings& InSettings);

	/** Runs the engine's gather and writes the Engine manifests (and the stamp that says they cover every source searched). */
	DREAMLOCEDITOR_API FGatherResult Gather(const FGatherOptions& InOptions);

	/**
	 * The engine's gather manifest as extraction manifests, one per file, each text once per file it was found in.
	 * C++ texts are in line order and carry no line in `at` (a line number would change with every edit above it); an
	 * asset's texts carry the object path inside the package; a meta-data text is in the header its type is declared
	 * in, with the field in `at` (`DreamCanvas:SortOrder`) and what it is in `kind` (`tooltip`, `display name`, `category`).
	 */
	DREAMLOCEDITOR_API bool ReadGatherManifest(const FString& InManifestPath, const FString& InProjectDirectory, const FString& InNativeCulture,
		TArray<FDreamLocExtractManifest>& OutManifests, FString& OutError);

	/** One translation of a localization target's archive. */
	struct FImportedText
	{
		FString Namespace;
		FString Key;
		/** The source text the translation was made from. */
		FString Source;
		FString Translation;
	};

	/**
	 * The translations of a localization target: culture -> texts. InNativeCulture empty: the target's .locmeta says, or
	 * its Config/Localization/<target>_Gather.ini. The native culture's archive is not read: it holds no translations.
	 */
	DREAMLOCEDITOR_API bool ReadTarget(const FString& InProjectDirectory, const FString& InTarget, const FString& InNativeCulture,
		TMap<FString, TArray<FImportedText>>& OutTexts, TArray<FDreamLocReportLine>& OutMessages);

	/**
	 * Puts translations into the .dloc files (culture -> path -> text, as sync reads them). A key with an entry takes the
	 * translation there -- with a `prev` when it was made from another source text -- unless it has a reviewed one
	 * (`state done`) and bInForce is false. A key no file has goes into Engine/Imported/<target>.dloc, written like a
	 * hand-made file; the next sync that finds the key moves it to its place.
	 */
	DREAMLOCEDITOR_API FDreamLocSyncOutput ApplyImport(const TMap<FString, TArray<FImportedText>>& InTexts, const TMap<FString, TMap<FString, FString>>& InFiles,
		const FString& InTarget, bool bInForce, const FString& InDLocName = TEXT("DLoc"));

	struct FImportOptions
	{
		/** Empty: the project. */
		FString ProjectDirectory;
		/** Empty: <project>/DLoc. */
		FString DLocDirectory;
		FString Target = TEXT("Game");
		/** Empty: what the target says. */
		FString NativeCulture;
		/** Only these cultures; empty: the dloc.ini TargetCultures. */
		TArray<FString> Cultures;
		bool bForce = false;
		bool bDryRun = false;
	};

	/** ReadTarget, then ApplyImport on the .dloc files on disk, then writes them. */
	DREAMLOCEDITOR_API FDreamLocSyncOutput ImportTarget(const FImportOptions& InOptions);

	/** The engine's own localization targets `dloc prefill` reads by default: the editor's UI, its property names, tooltips, categories, and the engine's. */
	DREAMLOCEDITOR_API TArray<FString> GetDefaultPrefillTargets();

	/** What the engine's translations of one culture say a source text is, from its own targets. */
	struct FEngineTranslations
	{
		/** Source -> the translation the most entries with it have (on a tie, the first target's, then the first read). */
		TMap<FString, FString> BySource;
		/** Namespace + U+001F + source -> the same, among the entries of that namespace: the meaning of the namespace first. */
		TMap<FString, FString> ByNamespaceAndSource;
		/** Translations read (an entry whose translation was made from another source is not one). */
		int32 Pairs = 0;
	};

	/**
	 * Joins each target's <native culture>/<target>.locres and <culture>/<target>.locres by namespace and key, under
	 * InLocalizationDirectory/<target>/ (empty: the engine's Content/Localization): what the engine ships, the source text
	 * to its translation. A target with no file for the culture is reported and skipped.
	 */
	DREAMLOCEDITOR_API FEngineTranslations ReadEngineTranslations(const FString& InLocalizationDirectory, const TArray<FString>& InTargets,
		const FString& InNativeCulture, const FString& InCulture, TArray<FDreamLocReportLine>& OutMessages);

	/** The comment a prefilled translation gets above its `text`, so a reviewer can tell it from the others (sync never touches comments). */
	DREAMLOCEDITOR_API FString GetPrefillMarker(const FString& InCulture);

	/**
	 * Gives every entry of a culture's .dloc files (path -> text, as sync reads them) that has no translation the engine's
	 * translation of its source text: the one of its namespace when there is one, else any. The translation is written
	 * with InState (draft, machine, done; empty for none) and the marker comment. Translated, stale and removed entries,
	 * and the `_` files (terms), are left alone. Output.Added counts the entries filled; OutUntranslated, those without a
	 * translation it looked at.
	 */
	DREAMLOCEDITOR_API FDreamLocSyncOutput ApplyPrefill(const FEngineTranslations& InTranslations, const TMap<FString, FString>& InFiles, const FString& InCulture,
		const FString& InState, const FString& InDLocName = TEXT("DLoc"), int32* OutUntranslated = nullptr);

	struct FPrefillOptions
	{
		/** The DLoc folder filled: the project's or a plugin's. */
		FString DLocDirectory;
		/** Empty: GetDefaultPrefillTargets. */
		TArray<FString> Targets;
		/** Where the targets are; empty: the engine's Content/Localization. */
		FString LocalizationDirectory;
		/** The targets' source culture. */
		FString NativeCulture = TEXT("en");
		/** Empty: the dloc.ini TargetCultures. */
		TArray<FString> Cultures;
		FString State = TEXT("draft");
		bool bDryRun = false;
	};

	/**
	 * `dloc prefill`: the engine's own translations into the untranslated entries whose source text the engine has, for a
	 * plugin whose UI says what the editor says ("Visibility", "Render Transform"). Writes nothing with bDryRun; the
	 * output says how many entries it fills either way. Only for a DLoc folder whose source culture is the targets'.
	 */
	DREAMLOCEDITOR_API FDreamLocSyncOutput Prefill(const FPrefillOptions& InOptions);
}
