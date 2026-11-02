// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamLocDiagnostics.h"

/**
 * One .dloc file, as text and as data.
 *
 *   dloc 1
 *   culture en
 *   namespace /Game/UI/Settings/WBP_SettingsScreen
 *
 *   Title.Text {
 *       at    DUI/UI/Settings.dui#Title
 *       src   "设置"
 *       text  "Settings"
 *       state done
 *   }
 *
 * The model keeps what a person wrote that the data alone would lose -- every comment, where it stood, and the
 * blank lines between the lines of a comment block -- so that a file read and written again is the same file.
 * What it does not keep is layout: field order, spacing and blank lines between entries are the writer's. A file
 * in the writer's layout comes back byte for byte; any other file comes back in that layout, once, and is stable
 * from then on.
 */

/** The fields of an entry, in the order the writer puts them. */
enum class EDreamLocField : uint8
{
	/** Where the text comes from: `<source file>#<node path>`. */
	At,
	/** A note for translators. */
	Note,
	/** The longest the translation may be, in display width (a full-width character counts 2). */
	Max,
	/** The source text the translation was made from, when the source has changed since. */
	Prev,
	/** The current source text. */
	Src,
	/** The translation. */
	Text,
	/** machine / draft / done */
	State,

	Count
};

enum class EDreamLocState : uint8
{
	/** No `state` line. */
	None,
	/** Translated by a machine or an AI; not yet read by a person. */
	Machine,
	/** A person's first pass. */
	Draft,
	/** Reviewed. */
	Done,
};

struct DREAMLOC_API FDreamLocFieldValue
{
	bool bPresent = false;
	/** Strings unescaped; `max` and `state` as written. */
	FString Value;
	/** Comment lines directly above the field, each with its `//`. An empty string is a blank line among them. */
	TArray<FString> Comments;
	/** A comment after the value on the same line. */
	FString TrailingComment;
	int32 Line = 0;
	int32 Column = 0;
};

/** One entry -- `Key { ... }` -- or, in a terms file, one `term "..." { ... }` or `keep "..."`. */
struct DREAMLOC_API FDreamLocEntry
{
	/** The text key; for a term or a keep, the source-language word. */
	FString Key;
	/** Comment lines above the entry. */
	TArray<FString> Comments;
	/** A comment after the `{` on the key's line. */
	FString HeaderComment;
	FDreamLocFieldValue Fields[(int32)EDreamLocField::Count];
	/** Comment lines after the last field, before the `}`. */
	TArray<FString> ClosingComments;
	/** A comment after the `}`. */
	FString TrailingComment;
	int32 Line = 0;
	int32 Column = 0;

	bool Has(EDreamLocField InField) const { return Fields[(int32)InField].bPresent; }
	/** Null when the field is absent. */
	const FString* Find(EDreamLocField InField) const;
	void Set(EDreamLocField InField, const FString& InValue);
	void Remove(EDreamLocField InField);

	/** -1 when there is no `max`. */
	int32 GetMax() const;
	EDreamLocState GetState() const;

	/** Has a translation. */
	bool IsTranslated() const { return Has(EDreamLocField::Text); }
	/** Has a translation made from a source text that has since changed. */
	bool IsStale() const { return Has(EDreamLocField::Text) && Has(EDreamLocField::Prev); }
};

enum class EDreamLocItemKind : uint8
{
	/** `namespace <name>`: the namespace of the entries after it. */
	Namespace,
	/** `<key> { ... }` */
	Entry,
	/** `removed { ... }`: entries whose key the source no longer has, kept until `dloc prune`. Never loaded. */
	Removed,
	/** `term "<word>" { text "..." }` */
	Term,
	/** `keep "<word>"`: a word that stays as it is in every language. */
	Keep,
};

struct DREAMLOC_API FDreamLocItem
{
	EDreamLocItemKind Kind = EDreamLocItemKind::Entry;
	/** Namespace only. */
	FString Namespace;
	/** Comment lines above a namespace or removed line (an entry keeps its own). */
	TArray<FString> Comments;
	/** A comment after a namespace line, or after a removed block's `{`. */
	FString TrailingComment;
	/** Entry, Term, Keep. */
	FDreamLocEntry Entry;
	/** Removed only: its namespace lines and entries. */
	TArray<FDreamLocItem> Children;
	/** Removed only: comment lines before its `}`. */
	TArray<FString> ClosingComments;
	int32 Line = 0;
	int32 Column = 0;
};

struct DREAMLOC_API FDreamLocDocument
{
	/** Comment lines above `dloc 1`. */
	TArray<FString> HeaderComments;
	int32 Version = 1;
	FString VersionComment;
	/** Empty when the file has no `culture` line. */
	FString Culture;
	bool bHasCulture = false;
	FString CultureComment;
	/** A terms file (`terms` after the header): it holds term and keep lines, not translations. */
	bool bTerms = false;
	FString TermsComment;
	TArray<FDreamLocItem> Items;
	/** Comment lines after the last item. */
	TArray<FString> EndComments;

	/**
	 * Every entry in file order, with the namespace in force where it stands. Removed blocks are skipped unless
	 * bInIncludeRemoved; term and keep lines are never visited.
	 */
	void ForEachEntry(TFunctionRef<void(const FString& InNamespace, const FDreamLocEntry& InEntry, bool bInRemoved)> InVisitor,
		bool bInIncludeRemoved = false) const;
};

namespace DreamLocFile
{
	/** The current format version: the number after `dloc`. */
	inline constexpr int32 FormatVersion = 1;

	/** The file extension, dot included. */
	inline constexpr const TCHAR* Extension = TEXT(".dloc");

	/**
	 * Read a .dloc file's text. Returns false when it has errors; OutDocument still holds everything that read, and
	 * OutDiagnostics says what did not. A byte order mark is skipped; CRLF and LF are both line ends.
	 */
	DREAMLOC_API bool Parse(const FString& InText, FDreamLocDocument& OutDocument, TArray<FDreamLocDiagnostic>& OutDiagnostics);

	/** The text of a document, in the writer's layout: LF line ends, a final line end, no byte order mark. */
	DREAMLOC_API FString Write(const FDreamLocDocument& InDocument);

	/** A string as the format quotes it: `"..."` with `\" \\ \n \t \r`. */
	DREAMLOC_API FString Quote(const FString& InText);

	/** A key, namespace, culture or `at` value: bare when it reads back as the same word, else quoted. */
	DREAMLOC_API FString QuoteWordIfNeeded(const FString& InWord, bool bInIsKey);

	/** `at`, `note`, ... */
	DREAMLOC_API const TCHAR* GetFieldName(EDreamLocField InField);

	/** Reads a file from disk (UTF-8) and parses it. False when the file cannot be read or has errors. */
	DREAMLOC_API bool LoadFile(const FString& InPath, FDreamLocDocument& OutDocument, TArray<FDreamLocDiagnostic>& OutDiagnostics);

	/** Writes the document as UTF-8 without a byte order mark. */
	DREAMLOC_API bool SaveFile(const FString& InPath, const FDreamLocDocument& InDocument);
}
