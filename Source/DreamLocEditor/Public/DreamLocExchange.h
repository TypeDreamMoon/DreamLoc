// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamLocCheck.h"
#include "DreamLocSync.h"

/**
 * Translations out of the .dloc files and back in: for an AI, a translator's tool, or a spreadsheet.
 *
 *   dloc export -Culture en [-Only untranslated,stale] [-Format json|xliff|po|csv] [-File DUI/UI/Settings] [-Reference ja]
 *   (translate)
 *   dloc import <file> [-State machine] [-Force]
 *   dloc check
 *
 * Each exported entry carries what a translator needs: the source text, the note and length limit, where it comes
 * from, the source texts around it in its file, what the source tool says about it (kind and context: the speaker of
 * a line, what each argument is), the culture's terms the source uses, for a stale entry the source it was translated
 * from and that translation, and (with -Reference) other cultures' translations. JSON is the format
 * for an AI: one object, its own instructions in it, each entry's `text` to fill in. Import matches entries by
 * namespace and key, never overwrites a reviewed translation (state done) without -Force, and imports a translation
 * made for a source text that has changed since as stale, with a `prev`.
 */
enum class EDreamLocExchangeFormat : uint8
{
	Json,
	Xliff,
	Po,
	Csv,
};

struct DREAMLOCEDITOR_API FDreamLocExchangeEntry
{
	/** The .dloc it is in, relative to DLoc/<culture>/. */
	FString File;
	FString Namespace;
	FString Key;
	FString Source;
	/** The translation; empty for none. */
	FString Text;
	/** machine, draft, done, or empty. */
	FString State;
	FString Note;
	int32 Max = -1;
	FString At;
	/** A stale entry's old source, and the translation made from it. A stale entry is exported with no `Text`: to translate again. */
	FString Prev;
	FString PreviousText;
	/** The source texts before and after it in its file. */
	TArray<FString> Nearby;
	/** What the source tool says the text is, and knows about it (the speaker, what each argument is): from its manifest. */
	FString Kind;
	TArray<FDreamLocExtractContext> Context;
	/** The culture's terms the source uses: word -> translation. */
	TArray<TPair<FString, FString>> Terms;
	/** Words the source has that stay as they are. */
	TArray<FString> Keeps;
	/** Other cultures' translations of the same key: culture -> text. */
	TArray<TPair<FString, FString>> References;
};

struct DREAMLOCEDITOR_API FDreamLocExchangeBatch
{
	FString SourceCulture = TEXT("zh-Hans");
	FString Culture;
	TArray<FDreamLocExchangeEntry> Entries;
	/** Every term and kept word of the culture. */
	FDreamLocTerms Terms;
};

namespace DreamLocExchange
{
	struct FExportOptions
	{
		FString Culture;
		/** untranslated, stale, machine, draft, done, all. Empty: untranslated and stale. */
		TArray<FString> Only;
		/** Only the files whose path (relative to DLoc/<culture>/, without .dloc) starts with one of these. */
		TArray<FString> Files;
		/** Cultures whose translations come along for reference. */
		TArray<FString> References;
		/** The extraction manifests, for each entry's kind and context. */
		TArray<FDreamLocExtractManifest> Manifests;
	};

	/** The entries to export, from the .dloc files as sync reads them (culture -> path -> text). */
	DREAMLOCEDITOR_API FDreamLocExchangeBatch Collect(const TMap<FString, TMap<FString, FString>>& InFiles, const FExportOptions& InOptions,
		const FString& InSourceCulture, TArray<FDreamLocReportLine>& OutMessages);

	DREAMLOCEDITOR_API FString Write(const FDreamLocExchangeBatch& InBatch, EDreamLocExchangeFormat InFormat);

	/** False when the text is not that format; OutError says why. */
	DREAMLOCEDITOR_API bool Read(const FString& InText, EDreamLocExchangeFormat InFormat, FDreamLocExchangeBatch& OutBatch, FString& OutError);

	/** By the file's extension: .json, .xlf / .xliff, .po / .pot, .csv. */
	DREAMLOCEDITOR_API TOptional<EDreamLocExchangeFormat> FormatOfPath(const FString& InPath);

	DREAMLOCEDITOR_API const TCHAR* ExtensionOf(EDreamLocExchangeFormat InFormat);

	struct FImportOptions
	{
		/** The culture to import into; empty: the batch's. */
		FString Culture;
		/** The state every imported translation gets; empty: what the file says. */
		FString State;
		/** Replace reviewed translations (state done) too. */
		bool bForce = false;
	};

	/** Puts a batch's translations into the files (culture -> path -> text); only files that change are in the output. */
	DREAMLOCEDITOR_API FDreamLocSyncOutput Import(const FDreamLocExchangeBatch& InBatch, const TMap<FString, TMap<FString, FString>>& InFiles,
		const FImportOptions& InOptions, const FString& InDLocName = TEXT("DLoc"));
}
