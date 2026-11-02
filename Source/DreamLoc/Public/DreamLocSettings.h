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
