// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamLocExtract.h"
#include "DreamLocSettings.h"
#include "DreamLocTranslateTypes.h"

struct FDreamLocTerms;

/** What to translate, and with what. */
struct DREAMLOCTRANSLATE_API FDreamLocTranslateRequest
{
	/** The DLoc folder holding the <culture>/ folders, and the folder the entries' `at` is relative to. */
	FString DLocDirectory;
	FString OwnerDirectory;
	TArray<FString> Cultures;
	/** Only the files whose path (relative to DLoc/<culture>/, without .dloc) starts with one of these; empty: all. */
	TArray<FString> Files;
	/** Which entries: those without a translation, those translated from an older source, machine translations. */
	bool bIncludeUntranslated = true;
	bool bIncludeStale = true;
	bool bIncludeMachine = false;
	/** Only these entries (namespace, key), of those the filters above let through; empty: all of them. */
	TArray<TPair<FString, FString>> Entries;
	/** The profiles to use, by name: the culture's (FDreamLocSettings::GetTranslateProfile) when Profile is empty. */
	FString Profile;
	/** The large model for what a machine translator cannot do; [Translate] Fallback when empty. */
	FString Fallback;
	/** Every profile the user has; DreamLocTranslateConfig::LoadProfiles() when empty. */
	TArray<FDreamLocTranslateProfile> Profiles;
	/** The project's settings; read from DLocDirectory when unset. */
	TOptional<FDreamLocSettings> Settings;
	/** The extraction manifests, for each entry's kind and context; read from OwnerDirectory's when unset. */
	TOptional<TArray<FDreamLocExtractManifest>> Manifests;
	/** Seconds before the first retry of a request that failed on the network, 429 or 5xx; doubled each time. */
	float RetryDelaySeconds = 2.0f;
	int32 NetworkRetries = 3;
	/** Where the report is written; <project>/Saved/DLoc when empty. */
	FString ReportDirectory;
	/** Write the translations into the .dloc files (false: only the result). */
	bool bWrite = true;
};

/**
 * A run of translation, apart from any window (a command line can call it the same way):
 *
 *   1. collect each culture's untranslated and stale entries (and machine ones, when asked) with DreamLocExchange::Collect;
 *   2. attach to each the culture's reviewed translation of the same source, for a large model to stay with;
 *   3. leave out what only a local translator may see when the translator is not local;
 *   4. send in batches of one file at a time, BatchSize at most, Concurrency at once, retrying network errors, 429 and 5xx;
 *   5. check each translation as dloc check does (and as the tools that show the text do, IDreamLocContextProvider);
 *   6. send what the checks turn down to a large model again with what was wrong, Retry times; a machine translator's
 *      go to the Fallback large model, or stay untranslated;
 *   7. write what passes back as `state machine` (DreamLocExchange::Import: a reviewed translation is never touched),
 *      and the report to Saved/DLoc/translate-<culture>-<time>.json.
 *
 * Blocks until done: call it from a worker thread, with the game thread ticking (a tool that lays texts out, for the
 * checks, does it there). InCancel stops it between requests and cuts the requests under way; what was translated by
 * then is still checked and written. InProgress is called from the worker threads, one call at a time.
 */
namespace DreamLocTranslateFlow
{
	using FProgress = TFunction<void(const FDreamLocTranslateProgress&)>;

	DREAMLOCTRANSLATE_API FDreamLocTranslateEstimate Estimate(const FDreamLocTranslateRequest& InRequest);

	DREAMLOCTRANSLATE_API FDreamLocTranslateResult Run(const FDreamLocTranslateRequest& InRequest, const FProgress& InProgress, const TAtomic<bool>& InCancel);

	/**
	 * The machine translations there are, as rows to review (no run): the request's cultures, files and entries, each
	 * row's Problems what the checks say now.
	 */
	DREAMLOCTRANSLATE_API TArray<FDreamLocTranslateRow> LoadForReview(const FDreamLocTranslateRequest& InRequest);

	/** A reviewer's decision about one entry: its translation and state, or (Text unset) no translation at all. */
	struct FReviewEdit
	{
		FString Culture;
		FString File;
		FString Namespace;
		FString Key;
		TOptional<FString> Text;
		/** done, draft, machine; empty for no state line. */
		FString State;
	};

	/**
	 * Writes a person's decisions into the .dloc files under InDLocDirectory: any entry, a reviewed one too (only the
	 * machine's write-back leaves those alone); a stale entry decided on loses its `prev`. False with OutErrors when a file
	 * does not read or write, or an entry is not there.
	 */
	DREAMLOCTRANSLATE_API bool ApplyReview(const FString& InDLocDirectory, TConstArrayView<FReviewEdit> InEdits, TArray<FString>& OutErrors);

	/** The line an entry's key is on in DLoc/<culture>/<file>; 0 when it is not there. */
	DREAMLOCTRANSLATE_API int32 FindEntryLine(const FString& InDLocDirectory, const FString& InCulture, const FString& InFile, const FString& InNamespace,
		const FString& InKey);

	/** Whether a unit only goes to a local translator: its file path or namespace starts with one of the prefixes. */
	DREAMLOCTRANSLATE_API bool IsLocalOnly(TConstArrayView<FString> InPrefixes, const FString& InFile, const FString& InNamespace);

	/** A translation with plural, ordinal or gender forms: a machine translator cannot keep them. */
	DREAMLOCTRANSLATE_API bool HasForms(const FString& InSource);

	/** What the checks say about a translation of InUnit: empty when nothing. On the game thread the tools that lay texts out have their say too. */
	DREAMLOCTRANSLATE_API FString CheckTranslation(const FDreamLocTranslateUnit& InUnit, const FString& InText, const FString& InCulture,
		const FDreamLocTerms& InTerms, const FString& InOwnerDirectory);

	/** The content checks alone (no layout): cheap enough for every entry of a culture. */
	DREAMLOCTRANSLATE_API FString CheckContent(const FDreamLocTranslateUnit& InUnit, const FString& InText, const FString& InCulture, const FDreamLocTerms& InTerms);
}
