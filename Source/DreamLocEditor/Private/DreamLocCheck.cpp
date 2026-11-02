// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocCheck.h"

#include "DreamLocSettings.h"
#include "DreamLocTextSource.h"
#include "HAL/FileManager.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Text.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace DreamLocCheckLocal
{
	enum class ETagKind : uint8
	{
		Open,
		Close,
		Single,
	};

	struct FTag
	{
		FString Name;
		ETagKind Kind = ETagKind::Open;
	};

	bool IsNameChar(TCHAR InChar)
	{
		return FChar::IsAlnum(InChar) || InChar == TEXT('_') || InChar == TEXT('-') || InChar == TEXT('.');
	}

	/** Where the `>` of a tag that starts at InStart is, quoted values skipped; INDEX_NONE when there is none. */
	int32 FindTagEnd(const FString& InText, int32 InStart, int32 InEnd)
	{
		bool bQuoted = false;
		for (int32 Index = InStart + 1; Index < InEnd; ++Index)
		{
			const TCHAR Char = InText[Index];
			if (Char == TEXT('"'))
			{
				bQuoted = !bQuoted;
			}
			else if (!bQuoted && Char == TEXT('>'))
			{
				return Index;
			}
			else if (!bQuoted && Char == TEXT('<'))
			{
				return INDEX_NONE;
			}
		}
		return INDEX_NONE;
	}

	/** A `<` that starts a tag: one followed by a letter or a `/`. */
	bool StartsTag(const FString& InText, int32 InIndex, int32 InEnd)
	{
		return InText[InIndex] == TEXT('<') && InIndex + 1 < InEnd && (InText[InIndex + 1] == TEXT('/') || FChar::IsAlpha(InText[InIndex + 1]));
	}

	/** The rich-text tags of a text, in order: `<b>`, `<color=#fff>`, `</b>`, `</>`, `<img=Icon/>`. */
	TArray<FTag> ParseTags(const FString& InText)
	{
		TArray<FTag> Tags;
		const int32 Length = InText.Len();
		for (int32 Index = 0; Index < Length; ++Index)
		{
			if (!StartsTag(InText, Index, Length))
			{
				continue;
			}
			const int32 End = FindTagEnd(InText, Index, Length);
			if (End == INDEX_NONE)
			{
				continue;
			}
			const FString Inner = InText.Mid(Index + 1, End - Index - 1);
			FTag Tag;
			if (Inner.StartsWith(TEXT("/")))
			{
				Tag.Kind = ETagKind::Close;
				Tag.Name = Inner.RightChop(1).TrimStartAndEnd();
			}
			else
			{
				Tag.Kind = Inner.EndsWith(TEXT("/")) ? ETagKind::Single : ETagKind::Open;
				int32 NameEnd = 0;
				while (NameEnd < Inner.Len() && IsNameChar(Inner[NameEnd]))
				{
					++NameEnd;
				}
				Tag.Name = Inner.Left(NameEnd);
			}
			Tags.Add(MoveTemp(Tag));
			Index = End;
		}
		return Tags;
	}

	/** Empty when every tag opened is closed in order; else what is wrong. */
	FString CheckTagPairs(const TArray<FTag>& InTags)
	{
		TArray<FString> Open;
		for (const FTag& Tag : InTags)
		{
			if (Tag.Kind == ETagKind::Open)
			{
				Open.Add(Tag.Name);
			}
			else if (Tag.Kind == ETagKind::Close)
			{
				if (Open.Num() == 0)
				{
					return FString::Printf(TEXT("'</%s>' closes a tag that is not open"), *Tag.Name);
				}
				if (!Tag.Name.IsEmpty() && Tag.Name != Open.Last())
				{
					return FString::Printf(TEXT("'</%s>' closes '<%s>'"), *Tag.Name, *Open.Last());
				}
				Open.Pop();
			}
		}
		return Open.Num() > 0 ? FString::Printf(TEXT("'<%s>' is not closed"), *Open.Last()) : FString();
	}

	/** The names of the tags a text opens (and the single ones), sorted: what the source and the translation must share. */
	TArray<FString> TagNames(const TArray<FTag>& InTags)
	{
		TArray<FString> Names;
		for (const FTag& Tag : InTags)
		{
			if (Tag.Kind != ETagKind::Close)
			{
				Names.Add(Tag.Name);
			}
		}
		Names.Sort();
		return Names;
	}

	FString DescribeTags(const TArray<FString>& InNames)
	{
		if (InNames.Num() == 0)
		{
			return TEXT("none");
		}
		TArray<FString> Shown;
		for (const FString& Name : InNames)
		{
			Shown.Add(FString::Printf(TEXT("<%s>"), *Name));
		}
		return FString::Join(Shown, TEXT(" "));
	}

	bool IsWide(uint32 InCodePoint)
	{
		return (InCodePoint >= 0x1100 && InCodePoint <= 0x115F)
			|| (InCodePoint >= 0x2E80 && InCodePoint <= 0x303E)
			|| (InCodePoint >= 0x3041 && InCodePoint <= 0x33FF)
			|| (InCodePoint >= 0x3400 && InCodePoint <= 0x4DBF)
			|| (InCodePoint >= 0x4E00 && InCodePoint <= 0x9FFF)
			|| (InCodePoint >= 0xA000 && InCodePoint <= 0xA4CF)
			|| (InCodePoint >= 0xAC00 && InCodePoint <= 0xD7A3)
			|| (InCodePoint >= 0xF900 && InCodePoint <= 0xFAFF)
			|| (InCodePoint >= 0xFE30 && InCodePoint <= 0xFE4F)
			|| (InCodePoint >= 0xFF00 && InCodePoint <= 0xFF60)
			|| (InCodePoint >= 0xFFE0 && InCodePoint <= 0xFFE6)
			|| (InCodePoint >= 0x1F300 && InCodePoint <= 0x1F64F)
			|| (InCodePoint >= 0x20000 && InCodePoint <= 0x3FFFD);
	}

	int32 CodePointWidth(uint32 InCodePoint)
	{
		if (InCodePoint < 0x20 || (InCodePoint >= 0x7F && InCodePoint < 0xA0) || (InCodePoint >= 0x0300 && InCodePoint <= 0x036F)
			|| InCodePoint == 0x200B || InCodePoint == 0xFEFF)
		{
			return 0;
		}
		return IsWide(InCodePoint) ? 2 : 1;
	}

	/** The code point at InIndex, and how many characters it takes. */
	uint32 CodePointAt(const FString& InText, int32 InIndex, int32 InEnd, int32& OutLength)
	{
		const uint32 First = static_cast<uint32>(InText[InIndex]);
		if (First >= 0xD800 && First <= 0xDBFF && InIndex + 1 < InEnd)
		{
			const uint32 Second = static_cast<uint32>(InText[InIndex + 1]);
			if (Second >= 0xDC00 && Second <= 0xDFFF)
			{
				OutLength = 2;
				return 0x10000 + ((First - 0xD800) << 10) + (Second - 0xDC00);
			}
		}
		OutLength = 1;
		return First;
	}

	/** The `)` that closes the `(` at InOpen, quotes and nested brackets skipped; INDEX_NONE when there is none. */
	int32 FindClosingParen(const FString& InText, int32 InOpen, int32 InEnd)
	{
		int32 Depth = 0;
		bool bQuoted = false;
		for (int32 Index = InOpen; Index < InEnd; ++Index)
		{
			const TCHAR Char = InText[Index];
			if (Char == TEXT('`') && Index + 1 < InEnd)
			{
				++Index;
			}
			else if (Char == TEXT('"'))
			{
				bQuoted = !bQuoted;
			}
			else if (!bQuoted && Char == TEXT('('))
			{
				++Depth;
			}
			else if (!bQuoted && Char == TEXT(')') && --Depth == 0)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	int32 ShownWidthOf(const FString& InText, int32 InBegin, int32 InEnd);

	/** The widest of a modifier's forms: `one=change,other=changes`, `"he","she"`. */
	int32 WidestForm(const FString& InText, int32 InBegin, int32 InEnd)
	{
		int32 Widest = 0;
		int32 Start = InBegin;
		int32 Depth = 0;
		bool bQuoted = false;
		auto TakeForm = [&](int32 InFormEnd)
		{
			int32 Begin = Start;
			// Past `name=` when the form has one.
			for (int32 Index = Start; Index < InFormEnd; ++Index)
			{
				if (InText[Index] == TEXT('"') || InText[Index] == TEXT('{'))
				{
					break;
				}
				if (InText[Index] == TEXT('='))
				{
					Begin = Index + 1;
					break;
				}
			}
			int32 End = InFormEnd;
			while (Begin < End && FChar::IsWhitespace(InText[Begin]))
			{
				++Begin;
			}
			while (End > Begin && FChar::IsWhitespace(InText[End - 1]))
			{
				--End;
			}
			if (End - Begin >= 2 && InText[Begin] == TEXT('"') && InText[End - 1] == TEXT('"'))
			{
				++Begin;
				--End;
			}
			Widest = FMath::Max(Widest, ShownWidthOf(InText, Begin, End));
		};
		for (int32 Index = InBegin; Index < InEnd; ++Index)
		{
			const TCHAR Char = InText[Index];
			if (Char == TEXT('`') && Index + 1 < InEnd)
			{
				++Index;
			}
			else if (Char == TEXT('"'))
			{
				bQuoted = !bQuoted;
			}
			else if (!bQuoted && (Char == TEXT('(') || Char == TEXT('{')))
			{
				++Depth;
			}
			else if (!bQuoted && (Char == TEXT(')') || Char == TEXT('}')))
			{
				--Depth;
			}
			else if (!bQuoted && Depth == 0 && Char == TEXT(','))
			{
				TakeForm(Index);
				Start = Index + 1;
			}
		}
		TakeForm(InEnd);
		return Widest;
	}

	int32 ShownWidthOf(const FString& InText, int32 InBegin, int32 InEnd)
	{
		int32 Width = 0;
		int32 Index = InBegin;
		while (Index < InEnd)
		{
			const TCHAR Char = InText[Index];
			if (Char == TEXT('`') && Index + 1 < InEnd)
			{
				int32 Length = 1;
				Width += CodePointWidth(CodePointAt(InText, Index + 1, InEnd, Length));
				Index += 1 + Length;
				continue;
			}
			if (Char == TEXT('{'))
			{
				int32 Close = INDEX_NONE;
				for (int32 Search = Index + 1; Search < InEnd; ++Search)
				{
					if (InText[Search] == TEXT('}'))
					{
						Close = Search;
						break;
					}
				}
				if (Close != INDEX_NONE)
				{
					Index = Close + 1;
					if (Index < InEnd && InText[Index] == TEXT('|'))
					{
						int32 Open = Index + 1;
						while (Open < InEnd && FChar::IsAlnum(InText[Open]))
						{
							++Open;
						}
						if (Open < InEnd && InText[Open] == TEXT('('))
						{
							const int32 Paren = FindClosingParen(InText, Open, InEnd);
							if (Paren != INDEX_NONE)
							{
								Width += WidestForm(InText, Open + 1, Paren);
								Index = Paren + 1;
							}
						}
					}
					continue;
				}
			}
			if (StartsTag(InText, Index, InEnd))
			{
				const int32 End = FindTagEnd(InText, Index, InEnd);
				if (End != INDEX_NONE)
				{
					Index = End + 1;
					continue;
				}
			}
			int32 Length = 1;
			Width += CodePointWidth(CodePointAt(InText, Index, InEnd, Length));
			Index += Length;
		}
		return Width;
	}

	int32 CountLineBreaks(const FString& InText)
	{
		int32 Count = 0;
		for (const TCHAR Char : InText)
		{
			Count += Char == TEXT('\n') ? 1 : 0;
		}
		return Count;
	}

	bool StartsBlank(const FString& InText)
	{
		return InText.Len() > 0 && FChar::IsWhitespace(InText[0]);
	}

	bool EndsBlank(const FString& InText)
	{
		return InText.Len() > 0 && FChar::IsWhitespace(InText[InText.Len() - 1]);
	}

	TSet<FString> ArgumentNames(const FTextFormat& InFormat)
	{
		TArray<FString> Names;
		InFormat.GetFormatArgumentNames(Names);
		return TSet<FString>(Names);
	}

	FString JoinSorted(const TSet<FString>& InNames)
	{
		TArray<FString> Names = InNames.Array();
		Names.Sort();
		TArray<FString> Shown;
		for (const FString& Name : Names)
		{
			Shown.Add(FString::Printf(TEXT("{%s}"), *Name));
		}
		return FString::Join(Shown, TEXT(", "));
	}

	struct FFile
	{
		FString Path;
		FString Culture;
		int32 Root = INDEX_NONE;
		FDreamLocDocument Document;
		TArray<FDreamLocDiagnostic> Diagnostics;
		bool bRead = false;
		/** Read for its terms only: what is wrong with it is not reported. */
		bool bReport = true;
	};

	/** The culture folder InPath is in under InRoot, or empty. */
	FString FindCultureOf(const FString& InPath, const FString& InRoot)
	{
		FString Relative = InPath;
		const FString RootDirectory = InRoot.EndsWith(TEXT("/")) ? InRoot : InRoot + TEXT("/");
		if (FPaths::MakePathRelativeTo(Relative, *RootDirectory) && !Relative.StartsWith(TEXT("..")))
		{
			FString Culture;
			if (Relative.Split(TEXT("/"), &Culture, nullptr))
			{
				return Culture;
			}
		}
		return FString();
	}

	TArray<FString> FindDlocFiles(const FString& InDirectory)
	{
		TArray<FString> Paths;
		if (IFileManager::Get().DirectoryExists(*InDirectory))
		{
			IFileManager::Get().FindFilesRecursive(Paths, *InDirectory, TEXT("*.dloc"), /*Files*/ true, /*Directories*/ false);
		}
		for (FString& Path : Paths)
		{
			FPaths::NormalizeFilename(Path);
		}
		Paths.Sort();
		return Paths;
	}

	/** What only the file's place can tell: its culture against its folder, and entries that will not load. */
	void CheckPlacement(const FFile& InFile, TArray<FDreamLocDiagnostic>& OutDiagnostics)
	{
		if (!InFile.Culture.IsEmpty())
		{
			if (!FDreamLocTextSource::IsKnownCulture(InFile.Culture))
			{
				OutDiagnostics.Emplace(EDreamLocSeverity::Warning, DreamLocCode::UnknownCulture, 0, 0, FString::Printf(
					TEXT("the folder '%s' is not a culture the engine knows; the file is never loaded"), *InFile.Culture));
			}
			if (InFile.Document.bHasCulture && !InFile.Document.Culture.Equals(InFile.Culture, ESearchCase::IgnoreCase))
			{
				OutDiagnostics.Emplace(EDreamLocSeverity::Warning, DreamLocCode::CultureMismatch, 0, 0, FString::Printf(
					TEXT("the file says 'culture %s' but is in the %s folder; it is loaded as %s"), *InFile.Document.Culture, *InFile.Culture, *InFile.Culture));
			}
		}
		InFile.Document.ForEachEntry([&OutDiagnostics](const FString&, const FDreamLocEntry& InEntry, bool)
		{
			if (InEntry.IsTranslated() && !InEntry.Has(EDreamLocField::Src))
			{
				OutDiagnostics.Emplace(EDreamLocSeverity::Warning, DreamLocCode::MissingSource, InEntry.Line, InEntry.Column, FString::Printf(
					TEXT("'%s' has a text but no src; it is not loaded until it has one"), *InEntry.Key));
			}
		});
	}

	struct FPlace
	{
		int32 File = INDEX_NONE;
		int32 Line = 0;
		int32 Column = 0;
		FString Text;
	};

	FString Where(const FFile& InFile, const FPlace& InPlace)
	{
		return FString::Printf(TEXT("%s(%d,%d)"), *InFile.Path, InPlace.Line, FMath::Max(InPlace.Column, 1));
	}
}

void FDreamLocTerms::AddFrom(const FDreamLocDocument& InTermsDocument)
{
	for (const FDreamLocItem& Item : InTermsDocument.Items)
	{
		if (Item.Kind == EDreamLocItemKind::Term)
		{
			if (const FString* Text = Item.Entry.Find(EDreamLocField::Text))
			{
				Terms.Emplace(Item.Entry.Key, *Text);
			}
		}
		else if (Item.Kind == EDreamLocItemKind::Keep)
		{
			Keeps.AddUnique(Item.Entry.Key);
		}
	}
}

int32 FDreamLocCheckResult::NumErrors() const
{
	int32 Count = 0;
	for (const FDreamLocReportLine& Line : Lines)
	{
		Count += Line.Diagnostic.IsError() ? 1 : 0;
	}
	return Count;
}

int32 FDreamLocCheckResult::NumWarnings() const
{
	int32 Count = 0;
	for (const FDreamLocReportLine& Line : Lines)
	{
		Count += Line.Diagnostic.Severity == EDreamLocSeverity::Warning ? 1 : 0;
	}
	return Count;
}

int32 DreamLocCheck::DisplayWidth(const FString& InText)
{
	using namespace DreamLocCheckLocal;
	int32 Width = 0;
	for (int32 Index = 0; Index < InText.Len();)
	{
		int32 Length = 1;
		Width += CodePointWidth(CodePointAt(InText, Index, InText.Len(), Length));
		Index += Length;
	}
	return Width;
}

int32 DreamLocCheck::ShownWidth(const FString& InText)
{
	return DreamLocCheckLocal::ShownWidthOf(InText, 0, InText.Len());
}

void DreamLocCheck::CheckEntry(const FDreamLocEntry& InEntry, const FString& InCulture, const FDreamLocTerms& InTerms, TArray<FDreamLocDiagnostic>& OutDiagnostics)
{
	using namespace DreamLocCheckLocal;
	const FString* Text = InEntry.Find(EDreamLocField::Text);
	if (Text == nullptr)
	{
		return;
	}
	const FString* Source = InEntry.Find(EDreamLocField::Src);
	const FDreamLocFieldValue& Field = InEntry.Fields[(int32)EDreamLocField::Text];
	auto Add = [&OutDiagnostics, &Field](EDreamLocSeverity InSeverity, int32 InCode, const FString& InMessage)
	{
		OutDiagnostics.Emplace(InSeverity, InCode, Field.Line, Field.Column, InMessage);
	};

	// The format pattern: valid, the source's arguments, the culture's plural forms.
	const FTextFormat Format = FTextFormat::FromString(*Text);
	if (!Format.IsValid())
	{
		TArray<FString> Errors;
		Format.ValidatePattern(nullptr, Errors);
		Add(EDreamLocSeverity::Error, DreamLocCode::InvalidPattern, FString::Printf(TEXT("the translation is not a pattern the engine can format: %s"),
			Errors.Num() > 0 ? *Errors[0] : TEXT("it does not compile")));
	}
	else
	{
		if (Source != nullptr)
		{
			const FTextFormat SourceFormat = FTextFormat::FromString(*Source);
			if (SourceFormat.IsValid())
			{
				const TSet<FString> SourceNames = ArgumentNames(SourceFormat);
				const TSet<FString> TextNames = ArgumentNames(Format);
				const TSet<FString> Missing = SourceNames.Difference(TextNames);
				const TSet<FString> Extra = TextNames.Difference(SourceNames);
				if (Missing.Num() > 0 || Extra.Num() > 0)
				{
					TArray<FString> Parts;
					if (Missing.Num() > 0)
					{
						Parts.Add(FString::Printf(TEXT("the source's %s %s missing"), *JoinSorted(Missing), Missing.Num() == 1 ? TEXT("is") : TEXT("are")));
					}
					if (Extra.Num() > 0)
					{
						Parts.Add(FString::Printf(TEXT("%s %s not in the source"), *JoinSorted(Extra), Extra.Num() == 1 ? TEXT("is") : TEXT("are")));
					}
					Add(EDreamLocSeverity::Error, DreamLocCode::ArgumentMismatch, FString::Join(Parts, TEXT("; ")));
				}
			}
		}
		const FCulturePtr Culture = FInternationalization::Get().GetCulture(InCulture);
		if (Culture.IsValid() && FDreamLocTextSource::IsKnownCulture(InCulture))
		{
			TArray<FString> Errors;
			Format.ValidatePattern(Culture, Errors);
			for (const FString& Error : Errors)
			{
				// A plural form in a culture that has only one is not wrong, just of no use.
				const bool bRedundant = Error.Contains(TEXT("redundant"));
				Add(bRedundant ? EDreamLocSeverity::Warning : EDreamLocSeverity::Error, DreamLocCode::PluralForms,
					FString::Printf(TEXT("%s (%s)"), *Error, *InCulture));
			}
		}
	}

	if (Source != nullptr)
	{
		// Rich-text tags.
		const TArray<FTag> TextTags = ParseTags(*Text);
		const TArray<FTag> SourceTags = ParseTags(*Source);
		if (TextTags.Num() > 0 || SourceTags.Num() > 0)
		{
			const FString Pairs = CheckTagPairs(TextTags);
			const TArray<FString> TextNames = TagNames(TextTags);
			const TArray<FString> SourceNames = TagNames(SourceTags);
			if (!Pairs.IsEmpty())
			{
				Add(EDreamLocSeverity::Error, DreamLocCode::RichTextTags, FString::Printf(TEXT("the translation's tags do not pair up: %s"), *Pairs));
			}
			else if (TextNames != SourceNames)
			{
				Add(EDreamLocSeverity::Error, DreamLocCode::RichTextTags, FString::Printf(TEXT("the source's tags are %s, the translation's %s"),
					*DescribeTags(SourceNames), *DescribeTags(TextNames)));
			}
		}

		// Blanks at the ends, line breaks.
		if (StartsBlank(*Source) != StartsBlank(*Text) || EndsBlank(*Source) != EndsBlank(*Text))
		{
			const bool bStart = StartsBlank(*Source) != StartsBlank(*Text);
			const bool bSourceHas = bStart ? StartsBlank(*Source) : EndsBlank(*Source);
			Add(EDreamLocSeverity::Warning, DreamLocCode::EdgeBlanks, FString::Printf(TEXT("the source %s with a blank and the translation %s"),
				bStart ? TEXT("starts") : TEXT("ends"), bSourceHas ? TEXT("does not") : TEXT("does")));
		}
		const int32 SourceBreaks = CountLineBreaks(*Source);
		const int32 TextBreaks = CountLineBreaks(*Text);
		if (SourceBreaks != TextBreaks)
		{
			Add(EDreamLocSeverity::Warning, DreamLocCode::LineBreaks, FString::Printf(TEXT("the source has %d line break(s) and the translation %d"),
				SourceBreaks, TextBreaks));
		}

		// Terms.
		for (const TPair<FString, FString>& Term : InTerms.Terms)
		{
			if (!Term.Key.IsEmpty() && Source->Contains(Term.Key, ESearchCase::CaseSensitive) && !Text->Contains(Term.Value, ESearchCase::IgnoreCase))
			{
				Add(EDreamLocSeverity::Warning, DreamLocCode::TermNotUsed, FString::Printf(TEXT("the source has the term \"%s\", translated \"%s\", and the translation does not"),
					*Term.Key, *Term.Value));
			}
		}
		for (const FString& Keep : InTerms.Keeps)
		{
			if (!Keep.IsEmpty() && Source->Contains(Keep, ESearchCase::CaseSensitive) && !Text->Contains(Keep, ESearchCase::CaseSensitive))
			{
				Add(EDreamLocSeverity::Warning, DreamLocCode::KeptWordChanged, FString::Printf(TEXT("\"%s\" stays as it is in every language, and the translation does not have it"),
					*Keep));
			}
		}
	}

	// Length.
	const int32 Max = InEntry.GetMax();
	if (Max >= 0)
	{
		const int32 Width = ShownWidth(*Text);
		if (Width > Max)
		{
			Add(EDreamLocSeverity::Warning, DreamLocCode::TooLong, FString::Printf(TEXT("the translation is %d wide and max is %d (a full-width character counts 2)"),
				Width, Max));
		}
	}

	if (InEntry.Has(EDreamLocField::Prev))
	{
		OutDiagnostics.Emplace(EDreamLocSeverity::Warning, DreamLocCode::StaleTranslation, InEntry.Line, InEntry.Column, FString::Printf(
			TEXT("'%s' was translated from \"%s\"; the source is now \"%s\", and shows until someone reviews the translation and deletes prev"),
			*InEntry.Key, **InEntry.Find(EDreamLocField::Prev), Source != nullptr ? **Source : TEXT("")));
	}
}

FDreamLocCheckResult DreamLocCheck::Run(const FDreamLocCheckOptions& InOptions)
{
	using namespace DreamLocCheckLocal;
	FDreamLocCheckResult Result;
	TArray<FDreamLocReportLine> FolderLines;

	TArray<FDreamLocCheckRoot> Roots = InOptions.Roots;
	for (FDreamLocCheckRoot& Root : Roots)
	{
		Root.Directory = FPaths::ConvertRelativePathToFull(Root.Directory);
		FPaths::NormalizeDirectoryName(Root.Directory);
	}

	// dloc.ini of each root.
	for (const FDreamLocCheckRoot& Root : Roots)
	{
		const FString SettingsPath = Root.Directory / FDreamLocSettings::FileName;
		FString Text;
		if (InOptions.File.IsEmpty() && IFileManager::Get().FileExists(*SettingsPath) && FFileHelper::LoadFileToString(Text, *SettingsPath))
		{
			TArray<FDreamLocDiagnostic> Diagnostics;
			FDreamLocSettings::Parse(Text, Diagnostics);
			for (const FDreamLocDiagnostic& Diagnostic : Diagnostics)
			{
				Result.Lines.Add(FDreamLocReportLine{ SettingsPath, Diagnostic });
			}
		}
	}

	// The files: every one under the roots, or one and its culture's terms files.
	TArray<FFile> Files;
	if (!InOptions.File.IsEmpty())
	{
		FString Path = FPaths::ConvertRelativePathToFull(InOptions.File);
		FPaths::NormalizeFilename(Path);
		FFile& File = Files.AddDefaulted_GetRef();
		File.Path = Path;
		for (int32 RootIndex = 0; RootIndex < Roots.Num() && File.Root == INDEX_NONE; ++RootIndex)
		{
			File.Culture = FindCultureOf(Path, Roots[RootIndex].Directory);
			File.Root = File.Culture.IsEmpty() ? INDEX_NONE : RootIndex;
		}
		if (File.Root != INDEX_NONE)
		{
			for (const FString& Other : FindDlocFiles(Roots[File.Root].Directory / File.Culture))
			{
				if (!Other.Equals(Path, ESearchCase::IgnoreCase))
				{
					FFile& Terms = Files.AddDefaulted_GetRef();
					Terms.Path = Other;
					Terms.Culture = Files[0].Culture;
					Terms.Root = Files[0].Root;
					Terms.bReport = false;
				}
			}
		}
	}
	else
	{
		for (int32 RootIndex = 0; RootIndex < Roots.Num(); ++RootIndex)
		{
			TArray<FString> Cultures;
			IFileManager::Get().FindFiles(Cultures, *(Roots[RootIndex].Directory / TEXT("*")), /*Files*/ false, /*Directories*/ true);
			Cultures.Sort();
			for (const FString& Culture : Cultures)
			{
				for (const FString& Path : FindDlocFiles(Roots[RootIndex].Directory / Culture))
				{
					FFile& File = Files.AddDefaulted_GetRef();
					File.Path = Path;
					File.Culture = Culture;
					File.Root = RootIndex;
				}
			}
		}
	}

	for (FFile& File : Files)
	{
		File.bRead = DreamLocFile::LoadFile(File.Path, File.Document, File.Diagnostics);
		if (!File.bReport && !File.Document.bTerms)
		{
			continue;
		}
		CheckPlacement(File, File.Diagnostics);
	}

	// Each root and culture's terms.
	TMap<FString, FDreamLocTerms> Terms;
	auto TermsKey = [](const FFile& InFile) { return FString::Printf(TEXT("%d/%s"), InFile.Root, *InFile.Culture.ToLower()); };
	for (const FFile& File : Files)
	{
		if (File.bRead && File.Document.bTerms)
		{
			Terms.FindOrAdd(TermsKey(File)).AddFrom(File.Document);
		}
	}

	// Each translated entry against its source; removed blocks.
	const FDreamLocTerms NoTerms;
	for (FFile& File : Files)
	{
		if (!File.bReport || !File.bRead || File.Document.bTerms)
		{
			continue;
		}
		const FDreamLocTerms* CultureTerms = Terms.Find(TermsKey(File));
		File.Document.ForEachEntry([&](const FString&, const FDreamLocEntry& InEntry, bool)
		{
			CheckEntry(InEntry, File.Culture, CultureTerms != nullptr ? *CultureTerms : NoTerms, File.Diagnostics);
		});
		for (const FDreamLocItem& Item : File.Document.Items)
		{
			if (Item.Kind == EDreamLocItemKind::Removed)
			{
				int32 Count = 0;
				for (const FDreamLocItem& Child : Item.Children)
				{
					Count += Child.Kind == EDreamLocItemKind::Entry ? 1 : 0;
				}
				File.Diagnostics.Emplace(EDreamLocSeverity::Info, DreamLocCode::RemovedEntries, Item.Line, Item.Column, FString::Printf(
					TEXT("%d entr%s the source no longer has; never loaded, kept until someone deletes them"), Count, Count == 1 ? TEXT("y") : TEXT("ies")));
			}
		}
	}

	// Across files: a key twice in a culture of one root, a layer over another, what has no translation yet.
	if (InOptions.File.IsEmpty())
	{
		TMap<FString, FPlace> FirstInRoot;
		TMap<FString, TArray<FPlace>> ByCulture;
		TMap<FString, TPair<int32, int32>> Counts;
		for (int32 FileIndex = 0; FileIndex < Files.Num(); ++FileIndex)
		{
			FFile& File = Files[FileIndex];
			if (!File.bRead || File.Document.bTerms || !FDreamLocTextSource::IsKnownCulture(File.Culture)
				|| FPaths::GetCleanFilename(File.Path).StartsWith(TEXT("_")))
			{
				continue;
			}
			const FString CultureKey = File.Culture.ToLower();
			TPair<int32, int32>& Count = Counts.FindOrAdd(FString::Printf(TEXT("%d/%s"), File.Root, *File.Culture));
			File.Document.ForEachEntry([&](const FString& InNamespace, const FDreamLocEntry& InEntry, bool)
			{
				if (InEntry.Has(EDreamLocField::Src))
				{
					++Count.Value;
					Count.Key += InEntry.IsTranslated() ? 0 : 1;
				}
				const FString Id = InNamespace + TEXT("\x01") + InEntry.Key;
				const FPlace Place{ FileIndex, InEntry.Line, InEntry.Column, InEntry.Find(EDreamLocField::Text) != nullptr ? *InEntry.Find(EDreamLocField::Text) : FString() };
				const FString RootId = FString::Printf(TEXT("%d/%s/"), File.Root, *CultureKey) + Id;
				if (const FPlace* First = FirstInRoot.Find(RootId))
				{
					if (First->File != FileIndex)
					{
						File.Diagnostics.Emplace(EDreamLocSeverity::Warning, DreamLocCode::DuplicateAcrossFiles, InEntry.Line, InEntry.Column, FString::Printf(
							TEXT("'%s' (namespace %s) is also in %s; only one of them is used"), *InEntry.Key, *InNamespace, *Where(Files[First->File], *First)));
					}
					return;
				}
				FirstInRoot.Add(RootId, Place);
				if (InEntry.IsTranslated())
				{
					ByCulture.FindOrAdd(CultureKey + TEXT("/") + Id).Add(Place);
				}
			});
		}
		for (const TPair<FString, TArray<FPlace>>& Pair : ByCulture)
		{
			const TArray<FPlace>& Places = Pair.Value;
			for (int32 Upper = 0; Upper < Places.Num(); ++Upper)
			{
				for (int32 Lower = 0; Lower < Places.Num(); ++Lower)
				{
					const FFile& UpperFile = Files[Places[Upper].File];
					const FFile& LowerFile = Files[Places[Lower].File];
					if (Roots[UpperFile.Root].Layer > Roots[LowerFile.Root].Layer && !Places[Upper].Text.Equals(Places[Lower].Text, ESearchCase::CaseSensitive))
					{
						Files[Places[Upper].File].Diagnostics.Emplace(EDreamLocSeverity::Info, DreamLocCode::LayerOverride, Places[Upper].Line, Places[Upper].Column,
							FString::Printf(TEXT("this translation shows instead of \"%s\" in %s, a lower layer"), *Places[Lower].Text, *Where(LowerFile, Places[Lower])));
					}
				}
			}
		}
		TArray<FString> CountKeys;
		Counts.GetKeys(CountKeys);
		CountKeys.Sort();
		for (const FString& Key : CountKeys)
		{
			const TPair<int32, int32>& Count = Counts.FindChecked(Key);
			FString RootIndex;
			FString Culture;
			Key.Split(TEXT("/"), &RootIndex, &Culture);
			if (Count.Key > 0)
			{
				FolderLines.Add(FDreamLocReportLine{ Roots[FCString::Atoi(*RootIndex)].Directory / Culture, FDreamLocDiagnostic(EDreamLocSeverity::Info,
					DreamLocCode::Untranslated, 0, 0, FString::Printf(TEXT("%d of %d text(s) have no translation"), Count.Key, Count.Value)) });
			}
		}
	}

	for (FFile& File : Files)
	{
		if (!File.bReport)
		{
			continue;
		}
		++Result.Files;
		File.Diagnostics.StableSort([](const FDreamLocDiagnostic& InA, const FDreamLocDiagnostic& InB)
		{
			return InA.Line != InB.Line ? InA.Line < InB.Line : InA.Column < InB.Column;
		});
		for (const FDreamLocDiagnostic& Diagnostic : File.Diagnostics)
		{
			Result.Lines.Add(FDreamLocReportLine{ File.Path, Diagnostic });
		}
	}
	Result.Lines.Append(MoveTemp(FolderLines));
	return Result;
}
