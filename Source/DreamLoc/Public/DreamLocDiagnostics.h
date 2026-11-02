// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

DREAMLOC_API DECLARE_LOG_CATEGORY_EXTERN(LogDreamLoc, Log, All);

enum class EDreamLocSeverity : uint8
{
	Error,
	Warning,
	Info,
};

/**
 * The DLOCnnnn codes. 1xxx is the text of a file: a .dloc that does not read, or a dloc.ini line that means nothing.
 * 2xxx is what a translation says against its source: arguments, plural forms, rich-text tags, blanks, length, terms.
 * 3xxx is how the files stand against the sources and each other: what sync, migrate and the import of the engine's
 * own localization targets find, and what `dloc check` sees across files. A code, once given, keeps its meaning:
 * editors and scripts match on it.
 */
namespace DreamLocCode
{
	/** A string with no closing quote before the end of its line. */
	inline constexpr int32 UnterminatedString = 1001;
	/** A backslash followed by a character that is not one of `" \ n t r`; both characters are kept. */
	inline constexpr int32 UnknownEscape = 1002;
	/** The file does not start with `dloc <version>`. */
	inline constexpr int32 MissingHeader = 1003;
	/** `dloc 2` and up: written by a newer DreamLoc. */
	inline constexpr int32 UnsupportedVersion = 1004;
	/** A token where the grammar has no place for it. */
	inline constexpr int32 UnexpectedToken = 1005;
	/** A field name inside an entry that is not at/note/max/prev/src/text/state. */
	inline constexpr int32 UnknownField = 1006;
	/** The same field twice in one entry; the first is kept. */
	inline constexpr int32 DuplicateField = 1007;
	/** A field whose value has the wrong shape: an unquoted text, a max that is not a number, an unknown state. */
	inline constexpr int32 InvalidValue = 1008;
	/** The same key twice in one namespace of one file; the first is kept. */
	inline constexpr int32 DuplicateKey = 1009;
	/** An entry before any `namespace` line. */
	inline constexpr int32 EntryOutsideNamespace = 1010;
	/** A `{` with no `}` before the end of the file. */
	inline constexpr int32 UnclosedBlock = 1011;
	/** The `culture` line names another culture than the folder the file is in. The folder wins. */
	inline constexpr int32 CultureMismatch = 1012;
	/** A culture name the engine does not know. */
	inline constexpr int32 UnknownCulture = 1013;
	/** An entry with a translation but no `src`: without the source text there is nothing to check it against. */
	inline constexpr int32 MissingSource = 1014;
	/** A dloc.ini line that is not a section, a `key=value` or a comment. */
	inline constexpr int32 SettingsSyntax = 1015;
	/** A dloc.ini key or section DreamLoc does not read. */
	inline constexpr int32 SettingsUnknownKey = 1016;

	/** The translation's format arguments are not the source's: one is missing, or one the source does not have is used. */
	inline constexpr int32 ArgumentMismatch = 2001;
	/** A plural form the culture needs is missing, or one it does not have is given (the engine's own pattern check). */
	inline constexpr int32 PluralForms = 2002;
	/** The translation's rich-text tags do not open and close in pairs, or are not the source's tags. */
	inline constexpr int32 RichTextTags = 2003;
	/** The source starts or ends with blanks and the translation does not, or the other way round. */
	inline constexpr int32 EdgeBlanks = 2004;
	/** The translation has another number of line breaks than the source. */
	inline constexpr int32 LineBreaks = 2005;
	/** The translation is wider than its `max` (a full-width character counts 2). */
	inline constexpr int32 TooLong = 2006;
	/** The source uses a term and the translation does not use its translation (the culture's terms file). */
	inline constexpr int32 TermNotUsed = 2007;
	/** The source has a word the terms file keeps as it is, and the translation does not have it. */
	inline constexpr int32 KeptWordChanged = 2008;
	/** The translation is not a pattern the engine can format: an argument or a modifier is not closed. */
	inline constexpr int32 InvalidPattern = 2009;

	/** A key that disappeared matched a new key of the same namespace by its source text, one to one: its translations moved. */
	inline constexpr int32 RenameInferred = 3001;
	/** A key that disappeared may have become one of several new keys, or a new key with a similar text: nothing moved. */
	inline constexpr int32 RenameCandidates = 3002;
	/** A namespace that disappeared: most of its texts turned up under a new one, so all its translations moved there. */
	inline constexpr int32 NamespaceMoved = 3003;
	/** Texts the source no longer has, moved into their file's removed block. */
	inline constexpr int32 MovedToRemoved = 3004;
	/** One namespace and key with two different source texts, from two sources. The first is kept. */
	inline constexpr int32 SourceConflict = 3005;
	/** A source changed after its extraction manifest was written: the manifest may not say what the source says now. */
	inline constexpr int32 ManifestOutOfDate = 3006;
	/** An extraction manifest that does not read. Nothing is written: its texts would read as removed. */
	inline constexpr int32 ManifestUnreadable = 3007;
	/** Translations of a source that exists but has no extraction manifest (it was never compiled): left as they are. */
	inline constexpr int32 SourceNotExtracted = 3008;
	/** A .dloc that does not read, or no target culture: nothing is written. */
	inline constexpr int32 SyncBlocked = 3009;
	/** An imported translation was made for another source text than the current one: it is imported as stale. */
	inline constexpr int32 ImportedStale = 3010;
	/** An imported translation that would replace a reviewed one: kept out (-Force lets it in). */
	inline constexpr int32 ImportKept = 3011;
	/** Imported translations of keys no .dloc has: written to Engine/Imported/<target>.dloc until a sync finds their place. */
	inline constexpr int32 ImportUnplaced = 3012;
	/** `dloc migrate` was given a key that is not there, or more than one. */
	inline constexpr int32 MigrateNotFound = 3013;
	/** The engine's gather did not finish. */
	inline constexpr int32 GatherFailed = 3014;
	/** A translation made for a source text that has changed since (it has a `prev`): the source shows until it is reviewed. */
	inline constexpr int32 StaleTranslation = 3015;
	/** How many texts of a culture have no translation yet. */
	inline constexpr int32 Untranslated = 3016;
	/** A file's removed block: entries that are never loaded, kept until someone deletes them. */
	inline constexpr int32 RemovedEntries = 3017;
	/** One namespace and key in two files of one culture and folder: only one of them is used. */
	inline constexpr int32 DuplicateAcrossFiles = 3018;
	/** A higher layer (a mod, the project) translates a key a lower layer translates too, differently: the higher one shows. */
	inline constexpr int32 LayerOverride = 3019;
	/** The .dloc files are not up to date with the extraction manifests: dloc sync would change them. */
	inline constexpr int32 NotSynced = 3020;
}

/** One finding about one file. Line and column are 1-based; a line of 0 is about the whole file. */
struct DREAMLOC_API FDreamLocDiagnostic
{
	EDreamLocSeverity Severity = EDreamLocSeverity::Error;
	int32 Code = 0;
	int32 Line = 0;
	int32 Column = 0;
	FString Message;

	FDreamLocDiagnostic() = default;
	FDreamLocDiagnostic(EDreamLocSeverity InSeverity, int32 InCode, int32 InLine, int32 InColumn, FString InMessage)
		: Severity(InSeverity), Code(InCode), Line(InLine), Column(InColumn), Message(MoveTemp(InMessage))
	{
	}

	bool IsError() const { return Severity == EDreamLocSeverity::Error; }

	/** `DLOC1001` */
	FString GetCodeName() const;

	/** `<file>(<line>,<col>): error DLOC1001: <message>`, the shape compilers print and editors read. */
	FString Format(const FString& InFile) const;

	static int32 CountErrors(TConstArrayView<FDreamLocDiagnostic> InDiagnostics);
	static int32 CountWarnings(TConstArrayView<FDreamLocDiagnostic> InDiagnostics);
};
