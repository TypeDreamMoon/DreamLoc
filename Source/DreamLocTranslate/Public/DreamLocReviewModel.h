// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamLocCheck.h"
#include "DreamLocContextProvider.h"
#include "DreamLocExchange.h"
#include "DreamLocSettings.h"
#include "DreamLocTranslateFlow.h"

/** Where an entry stands for a reviewer. */
enum class EDreamLocReviewStatus : uint8
{
	/** No translation. */
	Untranslated,
	/** Translated from a source text that has changed since (a `prev`). */
	Stale,
	/** Translated, not reviewed yet: machine, draft, or no state at all. */
	Review,
	/** Reviewed (`state done`). */
	Done,
};

/** What the list shows. Problems: a translation the checks say something about. */
enum class EDreamLocReviewFilter : uint8
{
	All,
	Untranslated,
	Stale,
	Review,
	Problems,
	Done,
	Count
};

/** One entry of a culture, as the review window shows it. */
struct DREAMLOCTRANSLATE_API FDreamLocReviewEntry
{
	/** What the files say about it (File, Namespace, Key, Source, Note, Max, At, Prev, Kind, Context, Terms, Keeps...). */
	FDreamLocExchangeEntry Entry;
	/** The translation the file has: a stale entry's too. */
	FString Translation;
	EDreamLocReviewStatus Status = EDreamLocReviewStatus::Untranslated;
	/** What the content checks say about Translation; empty when nothing (or no translation). */
	FString Problems;
	/** The reference culture's translation of the same key; empty for none. */
	FString Reference;

	bool HasProblems() const { return !Problems.IsEmpty(); }
	/** `namespace` + U+0001 + `key`. */
	FString GetId() const;
	/** A unit as the flow and the checks take it. */
	FDreamLocTranslateUnit MakeUnit() const;
};

/** How many entries of a culture (or of a file) stand where. */
struct DREAMLOCTRANSLATE_API FDreamLocReviewProgress
{
	int32 Done = 0;
	int32 Review = 0;
	int32 Stale = 0;
	int32 Untranslated = 0;

	int32 Total() const { return Done + Review + Stale + Untranslated; }
	void Add(EDreamLocReviewStatus InStatus);
};

/** One culture's progress, and each file's. */
struct DREAMLOCTRANSLATE_API FDreamLocCultureProgress
{
	FString Culture;
	FDreamLocReviewProgress Total;
	/** Path without .dloc -> its progress, sorted by path. */
	TArray<TPair<FString, FDreamLocReviewProgress>> Files;
};

/** A translation the editor offers for an entry: a click puts it in the box. */
struct DREAMLOCTRANSLATE_API FDreamLocCandidate
{
	FString Text;
	/** Where it comes from, as shown: a translator's name, "AI fix", "Reviewed elsewhere", "Reference ja". */
	FString From;
	/** What the checks say about it; empty when nothing. */
	FString Problems;
};

/**
 * One culture's entries, read from the .dloc files, and the reviewer's decisions written back: the review window's data,
 * apart from its widgets. Everything a filter shows, a list counts and a progress bar measures comes from here.
 */
class DREAMLOCTRANSLATE_API FDreamLocReviewModel
{
public:
	/** Reads the culture's files under InDLocDirectory (and the reference culture's, for its translations). */
	void Load(const FString& InDLocDirectory, const FString& InOwnerDirectory, const FString& InCulture, const FString& InReferenceCulture = FString());
	/** Reads the files again, keeping the culture and the reference. */
	void Reload();

	const FString& GetCulture() const { return Culture; }
	const FString& GetReferenceCulture() const { return ReferenceCulture; }
	const FString& GetDLocDirectory() const { return DLocDirectory; }
	const FString& GetOwnerDirectory() const { return OwnerDirectory; }
	const FDreamLocSettings& GetSettings() const { return Settings; }
	const FDreamLocTerms& GetTerms() const { return Terms; }
	const TArray<TSharedPtr<FDreamLocReviewEntry>>& GetEntries() const { return Entries; }
	/** A checksum of everything the entries say: the same after a reload when nothing changed. */
	uint32 GetSignature() const;
	/** What reading the files said (a file that does not read...). */
	const TArray<FString>& GetMessages() const { return Messages; }

	/**
	 * The entries a filter lets through, in file order: those under InFile (a file's path without .dloc, or a folder's with
	 * a trailing slash; empty for every file) whose key, source or translation contains InSearch (any case).
	 */
	TArray<TSharedPtr<FDreamLocReviewEntry>> Filter(EDreamLocReviewFilter InFilter, const FString& InSearch = FString(), const FString& InFile = FString()) const;
	int32 Count(EDreamLocReviewFilter InFilter, const FString& InSearch = FString(), const FString& InFile = FString()) const;
	static bool Passes(const FDreamLocReviewEntry& InEntry, EDreamLocReviewFilter InFilter);

	FDreamLocReviewProgress GetProgress(const FString& InFile = FString()) const;
	/** The files with entries, as paths without .dloc, sorted. */
	TArray<FString> GetFiles() const;

	TSharedPtr<FDreamLocReviewEntry> Find(const FString& InNamespace, const FString& InKey) const;
	/** The reviewed translations of the same source elsewhere in the culture, each once. */
	TArray<FString> FindReviewed(const FString& InSource, const FDreamLocReviewEntry* InExcept = nullptr) const;

	/** What every check says about a translation of InEntry: the content checks, and on the game thread the tools that lay texts out. */
	FString Check(const FDreamLocReviewEntry& InEntry, const FString& InText) const;

	/** Lays a translation of InEntry out where the game shows it, as the tool that shows it does (on the game thread); unset when no tool does. */
	TOptional<FDreamLocFitResult> Measure(const FDreamLocReviewEntry& InEntry, const FString& InText) const;

	/** Where an entry stands, from what the files say. */
	static EDreamLocReviewStatus StatusOf(const FDreamLocExchangeEntry& InEntry);
	/** Every target culture's progress, and each file's: the counts alone, no checks. */
	static TArray<FDreamLocCultureProgress> Overview(const FString& InDLocDirectory);

	/** Writes the decisions into the files and reads them again. False with OutErrors when one could not be written. */
	bool Apply(TConstArrayView<DreamLocTranslateFlow::FReviewEdit> InEdits, TArray<FString>& OutErrors);

	/** An edit of an entry: its file, namespace and key filled in. */
	DreamLocTranslateFlow::FReviewEdit MakeEdit(const FDreamLocReviewEntry& InEntry) const;

private:
	FString DLocDirectory;
	FString OwnerDirectory;
	FString Culture;
	FString ReferenceCulture;
	FDreamLocSettings Settings;
	FDreamLocTerms Terms;
	TArray<TSharedPtr<FDreamLocReviewEntry>> Entries;
	TMap<FString, TSharedPtr<FDreamLocReviewEntry>> ById;
	TArray<FString> Messages;
};
