// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamLocContextProvider.h"
#include "DreamLocDiagnostics.h"
#include "DreamLocFile.h"
#include "DreamLocSync.h"

/** A culture's terms: words with a fixed translation, and words kept as they are. From the culture's terms files. */
struct DREAMLOCEDITOR_API FDreamLocTerms
{
	/** Source word -> its translation. */
	TArray<TPair<FString, FString>> Terms;
	TArray<FString> Keeps;

	void AddFrom(const FDreamLocDocument& InTermsDocument);
};

struct DREAMLOCEDITOR_API FDreamLocCheckRoot
{
	/** A DLoc folder: the one holding the <culture>/ folders. */
	FString Directory;
	/** 0 a plugin's, 1 the project's, 2 and up mounted: which one shows when two translate the same key. */
	int32 Layer = 1;
};

struct DREAMLOCEDITOR_API FDreamLocCheckOptions
{
	TArray<FDreamLocCheckRoot> Roots;
	/** One file (absolute) instead of every file under the roots. Its culture is the folder it is in; that culture's terms still count. */
	FString File;
};

struct DREAMLOCEDITOR_API FDreamLocCheckResult
{
	/** Every finding, file by file in path order, each file's in line order. */
	TArray<FDreamLocReportLine> Lines;
	int32 Files = 0;

	int32 NumErrors() const;
	int32 NumWarnings() const;
};

/**
 * `dloc check`: everything that can be said about the .dloc files without the sources.
 *
 * - 1xxx: what does not read (the parser's own diagnostics), a culture line against its folder, a folder that is no
 *   culture, a translation without a source text, a dloc.ini line that means nothing.
 * - 2xxx, for each translated entry against its `src`: the format arguments; the plural forms the culture needs (the
 *   engine's own pattern check, FTextFormat::ValidatePattern); rich-text tags (`<b>..</b>`, `<color=..>..</>`,
 *   `<img=../>`) in pairs and the same as the source's; blanks at either end; line breaks; `max`; the culture's terms.
 *   Each at the entry's `text` line.
 * - 3xxx: stale translations (at the key), a file's removed block, a key in two files of one culture, a higher layer
 *   translating a key differently from a lower one, and per culture how many texts have no translation.
 */
namespace DreamLocCheck
{
	DREAMLOCEDITOR_API FDreamLocCheckResult Run(const FDreamLocCheckOptions& InOptions);

	/** The 2xxx checks and the stale check of one entry; nothing for an entry without a translation. */
	DREAMLOCEDITOR_API void CheckEntry(const FDreamLocEntry& InEntry, const FString& InCulture, const FDreamLocTerms& InTerms,
		TArray<FDreamLocDiagnostic>& OutDiagnostics);

	/**
	 * The 4xxx checks of one translated entry: the first provider that handles its source lays it out where it is shown
	 * (DLOC4001 when it does not fit, DLOC4002 for characters its font does not have). Nothing when none does, or it
	 * cannot find where the text is.
	 */
	DREAMLOCEDITOR_API void CheckFit(TConstArrayView<IDreamLocContextProvider*> InProviders, const FString& InOwnerDirectory, const FString& InNamespace,
		const FDreamLocEntry& InEntry, const FString& InCulture, TArray<FDreamLocDiagnostic>& OutDiagnostics);

	/** `'Ö' (U+00D6), '𓀀' (U+13000)`. */
	DREAMLOCEDITOR_API FString DescribeGlyphs(TConstArrayView<uint32> InCodepoints);

	/** The width of a text on screen in columns: a full-width character (CJK, kana, hangul, full-width forms) counts 2. */
	DREAMLOCEDITOR_API int32 DisplayWidth(const FString& InText);

	/**
	 * The width a translation shows at, for `max`: format arguments count nothing (their value is not known), a plural,
	 * ordinal or gender argument counts as its widest form, rich-text tags count nothing.
	 */
	DREAMLOCEDITOR_API int32 ShownWidth(const FString& InText);
}
