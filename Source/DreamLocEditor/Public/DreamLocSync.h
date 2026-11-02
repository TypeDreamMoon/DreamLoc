// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamLocDiagnostics.h"
#include "DreamLocExtract.h"

/** One finding, and the file it is about (as the report prints it). */
struct DREAMLOCEDITOR_API FDreamLocReportLine
{
	FString File;
	FDreamLocDiagnostic Diagnostic;

	FString Format() const { return Diagnostic.Format(File); }
};

/**
 * What sync works from. Paths of .dloc files are relative to DLoc/<culture>/, with forward slashes.
 */
struct DREAMLOCEDITOR_API FDreamLocSyncInput
{
	/** The manifests of the sources there are. A manifest whose source is gone is not one of them. */
	TArray<FDreamLocExtractManifest> Manifests;
	/** The cultures to bring up to date. */
	TArray<FString> Cultures;
	/** The .dloc files there are now: culture -> path -> text. */
	TMap<FString, TMap<FString, FString>> Files;
	/** Whether a source file (relative to the owner, as an `at` names it) exists. Unset: none does. */
	TFunction<bool(const FString& InSource)> SourceExists;
	/** Providers whose manifests cover every source they read, so a source of theirs without one has no texts left (the engine's gather). */
	TSet<FString> CompleteProviders;
	/** Only texts of sources under these paths are decided about; the others are left as they are. Empty: all. */
	TArray<FString> SourceScope;
	/** How much of a vanished namespace's (key, source text) pairs must turn up in a new namespace for all of it to move. */
	float NamespaceThreshold = 0.8f;
	/** The name report lines give the DLoc folder: `DLoc/en/DUI/UI/Settings.dloc`. */
	FString DLocName = TEXT("DLoc");
	/** What report lines about a source put before its path: the owner folder, or nothing. */
	FString SourceRoot;
};

struct DREAMLOCEDITOR_API FDreamLocSyncOutput
{
	/** culture -> path -> the new text, or unset to delete the file. Only files that change are here. */
	TMap<FString, TMap<FString, TOptional<FString>>> Changes;
	TArray<FDreamLocReportLine> Messages;
	/** Nothing was decided: a file does not read, or there is nothing to sync to. Changes is empty. */
	bool bBlocked = false;

	/** Counted once per culture. */
	int32 Added = 0;
	/** Translated entries whose source text changed: they now have a `prev`. */
	int32 Outdated = 0;
	/** Entries moved into a removed block. */
	int32 Removed = 0;
	/** Entries taken back out of a removed block: their key is back. */
	int32 Revived = 0;
	/** Entries whose translation moved to another key or namespace. */
	int32 Migrated = 0;
	/** Entries that moved from one file to another with their key. */
	int32 Moved = 0;

	int32 NumErrors() const;
	int32 NumWarnings() const;
	int32 NumWrites() const;
	int32 NumDeletes() const;
};

/**
 * Brings the .dloc files of every target culture up to date with the extraction manifests.
 *
 * - A new key gets an entry with its `src` and no `text`, in the file the manifest's source mirrors.
 * - A changed source text: the old `src` becomes `prev` (an earlier `prev` stays), the translation stays. A source
 *   changed back to what `prev` says loses the `prev`.
 * - `at`, `note`, `max`, `src` are the machine's and are rewritten; `text`, `state` and comments are never touched.
 * - A key found in another file -- the source moved, or a translation written by hand -- moves to its file.
 * - A key that disappeared: first the renames. A `renamedFrom` in the manifest moves the translation for certain.
 *   A namespace that disappeared moves whole to a new namespace that has most of its (key, text) pairs (a .dui's class
 *   line changed, or a file without one moved). A vanished key whose source text matches exactly one new key of the
 *   same namespace moves to it. Several candidates, or only a similar text: a warning and nothing moves. What does
 *   not move goes into the file's `removed { }` block, never loaded, kept until `dloc prune`.
 * - A file whose source is gone keeps its translations in its removed block.
 * - A key that comes back is taken out of the removed block with its translation.
 * - Entries with no `at` (written by hand) are left alone unless a manifest claims their key. Translations of a
 *   source that exists but has no manifest -- not compiled since the checkout -- are left alone.
 *
 * Every decision about keys is made once, from all cultures' files together, and applied to each culture alike.
 */
namespace DreamLocSync
{
	DREAMLOCEDITOR_API FDreamLocSyncOutput Run(const FDreamLocSyncInput& InInput);

	struct FDirectoryOptions
	{
		/** The project or plugin folder the sources are relative to. */
		FString OwnerDirectory;
		/** Its DLoc/ folder; empty for <owner>/DLoc. */
		FString DLocDirectory;
		/** Where the manifests are; empty for <owner>/Intermediate/DLoc/extract. */
		FString ExtractDirectory;
		/** The cultures; empty for the dloc.ini TargetCultures. */
		TArray<FString> Cultures;
		/** Only the manifests of sources under these paths (relative to the owner); empty for all. */
		TArray<FString> SourceFilter;
		/** Report what would change without writing it. */
		bool bDryRun = false;
	};

	/** Reads the manifests and the .dloc files from disk, runs, and writes the files that change. */
	DREAMLOCEDITOR_API FDreamLocSyncOutput SyncDirectory(const FDirectoryOptions& InOptions);

	/** Writes (or deletes) the changed files under InDLocDirectory/<culture>/. False when one could not be written. */
	DREAMLOCEDITOR_API bool ApplyChanges(const FString& InDLocDirectory, const FDreamLocSyncOutput& InOutput, TArray<FDreamLocReportLine>& OutMessages);

	/** Every .dloc under InDLocDirectory/<culture>/ for each culture, as Run takes them. */
	DREAMLOCEDITOR_API TMap<FString, TMap<FString, FString>> ReadFiles(const FString& InDLocDirectory, TConstArrayView<FString> InCultures);

	struct FMigrateRequest
	{
		/** The key whose translation moves, and its namespace (empty: wherever the key is, if only one place). */
		FString FromNamespace;
		FString FromKey;
		/** The key it moves to; its namespace empty for the same as the old one. It must have an entry already (sync first). */
		FString ToNamespace;
		FString ToKey;
		/** Replace a translation the new key already has. */
		bool bForce = false;
	};

	/**
	 * `dloc migrate`: moves a translation from one key to another in every culture -- what sync would not decide alone.
	 * The old entry may be in a removed block or still live; it is deleted. The new key's source text is compared with the
	 * old one's: when they differ the translation comes with a `prev`.
	 */
	DREAMLOCEDITOR_API FDreamLocSyncOutput Migrate(const TMap<FString, TMap<FString, FString>>& InFiles, const FMigrateRequest& InRequest,
		const FString& InDLocName = TEXT("DLoc"));
}
