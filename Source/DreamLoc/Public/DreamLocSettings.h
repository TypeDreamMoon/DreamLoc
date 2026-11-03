// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamLocDiagnostics.h"

/**
 * DLoc/dloc.ini, the project's localization settings.
 *
 *   [DreamLoc]
 *   SourceCulture=zh-Hans          ; the language the source text is written in (default zh-Hans)
 *   TargetCultures=en, ja          ; the languages translated into (or one +TargetCultures=xx line each)
 *
 *   [Fallback]
 *   pt-PT=pt-BR                    ; when pt-PT has no translation, try pt-BR before showing the source
 *
 *   [Gather]                       ; what `dloc gather` hands the engine's text gatherer, relative to the project
 *   SourcePaths=Source             ; folders of C++ searched for LOCTEXT / NSLOCTEXT (default Source)
 *   ContentPaths=Content           ; folders of assets (default Content)
 *   ExcludePaths=...               ; path patterns left out, `*` the wildcard (default: the folders of editor modules
 *                                  ; under Source, Content/Developers and Content/Localization)
 *
 *   [Publish]
 *   Mode=Source                    ; how a packaged game gets the translations `dloc bake` writes (default Source):
 *                                  ; Source, DreamLoc reads them and mods' .dloc files go over them; Native, the
 *                                  ; engine reads them itself (Content/Localization/DreamLoc in LocalizationPaths)
 *
 *   [Translate]                    ; the editor's translation window (DreamLocTranslate); no keys here, they are the user's
 *   Profile=deepseek               ; the translator used by default: a [Profile <name>] of the user's translators.ini
 *   Profile.ja=deepl               ; one for a culture
 *   Fallback=deepseek              ; the large model a machine translator hands what it cannot do (plural forms, failures)
 *   BatchSize=40                   ; entries per request at most (default 40)
 *   Concurrency=2                  ; requests at once (default 2)
 *   Retry=1                        ; times a translation the checks turn down is sent again with what was wrong (default 1)
 *   Style=DLoc/_style.md           ; a style guide, relative to the project; DLoc/_style.<culture>.md wins when there is one
 *   +LocalOnly=DNA/                ; file paths or namespaces starting so only go to a local translator (default none)
 *
 * A comment is a line starting with `;` or `#`, or the rest of a line after ` ;`. Missing file, missing keys: the
 * defaults. A list key replaces the default; `+Key=` adds to it. The file is read by DreamLoc alone, never by the
 * engine's config system: it sits with the translations, not in Config/, so a translator's checkout carries it.
 */
struct DREAMLOC_API FDreamLocSettings
{
	/** The culture the source text is written in. Its text is in the sources; it has no folder under DLoc/. */
	FString SourceCulture = TEXT("zh-Hans");
	/** The cultures the project translates into, in the order written. */
	TArray<FString> TargetCultures;
	/** Extra fallbacks, in the order written: when the key culture has no translation, the value culture is tried. */
	TArray<TPair<FString, FString>> Fallbacks;
	/** Folders of C++ the engine gather searches, relative to the project. */
	TArray<FString> GatherSourcePaths = { TEXT("Source") };
	/** Folders of assets the engine gather reads, relative to the project. */
	TArray<FString> GatherContentPaths = { TEXT("Content") };
	/** Path patterns (`*` matches anything) the engine gather leaves out, relative to the project. */
	TArray<FString> GatherExcludePaths = { TEXT("Source/*Editor/*"), TEXT("Content/Developers/*"), TEXT("Content/Localization/*") };
	/** A packaged game's translations are read by the engine itself rather than by DreamLoc ([Publish] Mode=Native). */
	bool bPublishNative = false;

	/** [Translate] Profile: the translator profile used by default; empty for none. */
	FString TranslateProfile;
	/** [Translate] Profile.<culture>: a culture's own, in the order written. */
	TArray<TPair<FString, FString>> TranslateProfileByCulture;
	/** [Translate] Fallback: the large-model profile a machine translator hands what it cannot translate. */
	FString TranslateFallback;
	int32 TranslateBatchSize = 40;
	int32 TranslateConcurrency = 2;
	int32 TranslateRetry = 1;
	/** [Translate] Style: a style guide for the translator, relative to the project. */
	FString TranslateStyle = TEXT("DLoc/_style.md");
	/** [Translate] LocalOnly: file paths (relative to DLoc/<culture>/) or namespaces whose texts only go to a local translator. */
	TArray<FString> TranslateLocalOnly;

	/** The profile for a culture: its own, else the default. */
	FString GetTranslateProfile(const FString& InCulture) const;

	/** The file name, in the DLoc/ folder. */
	static constexpr const TCHAR* FileName = TEXT("dloc.ini");

	static FDreamLocSettings Parse(const FString& InText, TArray<FDreamLocDiagnostic>& OutDiagnostics);

	/** <InDLocDirectory>/dloc.ini, or the defaults when there is none. */
	static FDreamLocSettings LoadFromDirectory(const FString& InDLocDirectory, TArray<FDreamLocDiagnostic>* OutDiagnostics = nullptr);

	/**
	 * The engine's prioritized cultures with the extra fallbacks after them: for each culture in the list, in order,
	 * the cultures it falls back to that are not in the list yet. A fallback's own fallbacks follow it. So every
	 * extra culture ranks below every culture the engine asked for.
	 */
	TArray<FString> ExpandFallbacks(TConstArrayView<FString> InPrioritizedCultures) const;
};
