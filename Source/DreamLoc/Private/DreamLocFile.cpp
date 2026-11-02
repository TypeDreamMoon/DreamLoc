// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocFile.h"

#include "Misc/FileHelper.h"

namespace DreamLocFileLocal
{
	enum class ETokenKind : uint8
	{
		Word,
		String,
		LBrace,
		RBrace,
		Comment,
		End,
	};

	struct FToken
	{
		ETokenKind Kind = ETokenKind::End;
		/** A word as written; a string unescaped; a comment from its `//` on, trailing blanks trimmed. */
		FString Text;
		int32 Line = 0;
		int32 Column = 0;
		/** Nothing but blanks before it on its line: what tells a comment of its own from one after a value. */
		bool bFirstOnLine = false;
	};

	const TCHAR* const FieldNames[(int32)EDreamLocField::Count] = {
		TEXT("at"), TEXT("note"), TEXT("max"), TEXT("prev"), TEXT("src"), TEXT("text"), TEXT("state")
	};

	/** The words with a meaning of their own outside an entry. A key spelt like one of them is written quoted. */
	const TCHAR* const Keywords[] = {
		TEXT("dloc"), TEXT("culture"), TEXT("terms"), TEXT("namespace"), TEXT("removed"), TEXT("term"), TEXT("keep")
	};

	bool IsKeyword(const FString& InWord)
	{
		for (const TCHAR* Keyword : Keywords)
		{
			if (InWord.Equals(Keyword, ESearchCase::CaseSensitive))
			{
				return true;
			}
		}
		return false;
	}

	bool IsBlank(TCHAR InChar)
	{
		return InChar == TEXT(' ') || InChar == TEXT('\t') || InChar == TEXT('\f') || InChar == TEXT('\v');
	}

	bool EndsWord(TCHAR InChar)
	{
		return IsBlank(InChar) || InChar == TEXT('\n') || InChar == TEXT('\r') || InChar == TEXT('"')
			|| InChar == TEXT('{') || InChar == TEXT('}');
	}

	bool IsAllDigits(const FString& InText)
	{
		if (InText.IsEmpty())
		{
			return false;
		}
		for (const TCHAR Char : InText)
		{
			if (Char < TEXT('0') || Char > TEXT('9'))
			{
				return false;
			}
		}
		return true;
	}

	int32 FindField(const FString& InName)
	{
		for (int32 Index = 0; Index < (int32)EDreamLocField::Count; ++Index)
		{
			if (InName.Equals(FieldNames[Index], ESearchCase::CaseSensitive))
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	void Tokenize(const FString& InText, TArray<FToken>& OutTokens, TArray<FDreamLocDiagnostic>& OutDiagnostics)
	{
		const TCHAR* Chars = *InText;
		const int32 Length = InText.Len();
		int32 Offset = 0;
		int32 Line = 1;
		int32 LineStart = 0;
		bool bLineHasToken = false;
		if (Length > 0 && Chars[0] == TEXT('\xFEFF'))
		{
			Offset = 1;
			LineStart = 1;
		}

		while (Offset < Length)
		{
			const TCHAR Char = Chars[Offset];
			if (Char == TEXT('\r') || Char == TEXT('\n'))
			{
				Offset += (Char == TEXT('\r') && Offset + 1 < Length && Chars[Offset + 1] == TEXT('\n')) ? 2 : 1;
				++Line;
				LineStart = Offset;
				bLineHasToken = false;
				continue;
			}
			if (IsBlank(Char))
			{
				++Offset;
				continue;
			}

			FToken Token;
			Token.Line = Line;
			Token.Column = Offset - LineStart + 1;
			Token.bFirstOnLine = !bLineHasToken;
			bLineHasToken = true;

			if (Char == TEXT('/') && Offset + 1 < Length && Chars[Offset + 1] == TEXT('/'))
			{
				const int32 Start = Offset;
				while (Offset < Length && Chars[Offset] != TEXT('\r') && Chars[Offset] != TEXT('\n'))
				{
					++Offset;
				}
				Token.Kind = ETokenKind::Comment;
				Token.Text = FString::ConstructFromPtrSize(Chars + Start, Offset - Start).TrimEnd();
			}
			else if (Char == TEXT('{') || Char == TEXT('}'))
			{
				Token.Kind = Char == TEXT('{') ? ETokenKind::LBrace : ETokenKind::RBrace;
				Token.Text = FString::ConstructFromPtrSize(&Char, 1);
				++Offset;
			}
			else if (Char == TEXT('"'))
			{
				Token.Kind = ETokenKind::String;
				++Offset;
				bool bClosed = false;
				while (Offset < Length)
				{
					const TCHAR Inner = Chars[Offset];
					if (Inner == TEXT('"'))
					{
						++Offset;
						bClosed = true;
						break;
					}
					if (Inner == TEXT('\r') || Inner == TEXT('\n'))
					{
						break;
					}
					if (Inner == TEXT('\\'))
					{
						const TCHAR Escaped = Offset + 1 < Length ? Chars[Offset + 1] : TEXT('\0');
						if (Escaped == TEXT('\0') || Escaped == TEXT('\r') || Escaped == TEXT('\n'))
						{
							++Offset;
							break;
						}
						switch (Escaped)
						{
						case TEXT('"'):  Token.Text.AppendChar(TEXT('"'));  break;
						case TEXT('\\'): Token.Text.AppendChar(TEXT('\\')); break;
						case TEXT('n'):  Token.Text.AppendChar(TEXT('\n')); break;
						case TEXT('t'):  Token.Text.AppendChar(TEXT('\t')); break;
						case TEXT('r'):  Token.Text.AppendChar(TEXT('\r')); break;
						default:
							// Both characters are kept, as .dui keeps them: a translator's `é` should survive a
							// rewrite and be pointed at, not silently lose its backslash.
							OutDiagnostics.Emplace(EDreamLocSeverity::Warning, DreamLocCode::UnknownEscape, Line, Offset - LineStart + 1,
								FString::Printf(TEXT("'\\%c' is not an escape; the escapes are \\\" \\\\ \\n \\t \\r, and both characters are kept"), Escaped));
							Token.Text.AppendChar(TEXT('\\'));
							Token.Text.AppendChar(Escaped);
							break;
						}
						Offset += 2;
						continue;
					}
					Token.Text.AppendChar(Inner);
					++Offset;
				}
				if (!bClosed)
				{
					OutDiagnostics.Emplace(EDreamLocSeverity::Error, DreamLocCode::UnterminatedString, Token.Line, Token.Column,
						TEXT("this string has no closing quote on its line; a line break inside a string is written \\n"));
				}
			}
			else
			{
				const int32 Start = Offset;
				while (Offset < Length && !EndsWord(Chars[Offset]))
				{
					++Offset;
				}
				Token.Kind = ETokenKind::Word;
				Token.Text = FString::ConstructFromPtrSize(Chars + Start, Offset - Start);
			}
			OutTokens.Add(MoveTemp(Token));
		}

		FToken End;
		End.Kind = ETokenKind::End;
		End.Line = Line;
		End.Column = Offset - LineStart + 1;
		End.bFirstOnLine = true;
		OutTokens.Add(MoveTemp(End));
	}

	class FParser
	{
	public:
		FParser(TArray<FToken>&& InTokens, FDreamLocDocument& InDocument, TArray<FDreamLocDiagnostic>& InDiagnostics)
			: Tokens(MoveTemp(InTokens)), Document(InDocument), Diagnostics(InDiagnostics)
		{
		}

		void Run()
		{
			ParseHeader();
			while (Peek().Kind != ETokenKind::End)
			{
				ParseItem(Document.Items, /*bInRemoved*/ false);
			}
			Document.EndComments = TakeComments();
		}

	private:
		TArray<FToken> Tokens;
		int32 Index = 0;
		FDreamLocDocument& Document;
		TArray<FDreamLocDiagnostic>& Diagnostics;

		/** Comments of their own lines met since the last thing that took them. */
		TArray<FString> PendingComments;
		int32 LastCommentLine = 0;
		/** Where a comment after the last token on its line goes, while that line is still the current one. */
		FString* TrailingTarget = nullptr;
		/** The line of the last token taken. */
		int32 LastLine = 0;

		FString Namespace;
		bool bHasNamespace = false;
		bool bReportedOutsideNamespace = false;
		/** Namespace + key of every active entry so far, to the line it is on. */
		TMap<FString, int32> SeenKeys;

		void Error(int32 InCode, const FToken& InAt, FString InMessage)
		{
			Diagnostics.Emplace(EDreamLocSeverity::Error, InCode, InAt.Line, InAt.Column, MoveTemp(InMessage));
		}

		/** The next token that is not a comment, collecting the comments on the way. */
		const FToken& Peek()
		{
			while (Tokens[Index].Kind == ETokenKind::Comment)
			{
				const FToken& Comment = Tokens[Index++];
				if (!Comment.bFirstOnLine && TrailingTarget != nullptr && Comment.Line == LastLine)
				{
					*TrailingTarget = Comment.Text;
					TrailingTarget = nullptr;
					continue;
				}
				if (PendingComments.Num() > 0 && Comment.Line > LastCommentLine + 1)
				{
					PendingComments.Add(FString());
				}
				PendingComments.Add(Comment.Text);
				LastCommentLine = Comment.Line;
			}
			// Whatever comes next on this line belongs to the token about to be read, not to the last one.
			TrailingTarget = nullptr;
			return Tokens[Index];
		}

		const FToken& Next()
		{
			const FToken& Token = Peek();
			LastLine = Token.Line;
			if (Token.Kind != ETokenKind::End)
			{
				++Index;
			}
			return Token;
		}

		TArray<FString> TakeComments()
		{
			TArray<FString> Taken = MoveTemp(PendingComments);
			PendingComments.Reset();
			return Taken;
		}

		/** A word or a string on the same line as InOwner, the keyword it is the value of. */
		bool ReadValue(const FToken& InOwner, FString& OutValue)
		{
			const FToken& Value = Peek();
			if ((Value.Kind == ETokenKind::Word || Value.Kind == ETokenKind::String) && Value.Line == InOwner.Line)
			{
				OutValue = Next().Text;
				return true;
			}
			Error(DreamLocCode::InvalidValue, InOwner, FString::Printf(TEXT("'%s' needs a value on the same line"), *InOwner.Text));
			return false;
		}

		void SkipRestOfLine(int32 InLine)
		{
			while (Peek().Kind != ETokenKind::End && Peek().Line == InLine)
			{
				Next();
			}
		}

		/** Past the `}` that closes the `{` just taken, however deep. */
		void SkipBlock()
		{
			int32 Depth = 1;
			while (Depth > 0 && Peek().Kind != ETokenKind::End)
			{
				const ETokenKind Kind = Next().Kind;
				Depth += Kind == ETokenKind::LBrace ? 1 : Kind == ETokenKind::RBrace ? -1 : 0;
			}
		}

		void ParseHeader()
		{
			const FToken& First = Peek();
			Document.HeaderComments = TakeComments();
			if (First.Kind == ETokenKind::Word && First.Text.Equals(TEXT("dloc"), ESearchCase::CaseSensitive))
			{
				Next();
				const FToken& Version = Peek();
				if (Version.Kind == ETokenKind::Word && Version.Line == First.Line && IsAllDigits(Version.Text))
				{
					Next();
					Document.Version = FCString::Atoi(*Version.Text);
					if (Document.Version != DreamLocFile::FormatVersion)
					{
						Error(DreamLocCode::UnsupportedVersion, Version, FString::Printf(
							TEXT("this file is format version %d, and this DreamLoc reads version %d"), Document.Version, DreamLocFile::FormatVersion));
					}
					TrailingTarget = &Document.VersionComment;
				}
				else
				{
					Error(DreamLocCode::MissingHeader, First, TEXT("'dloc' is followed by the format version, as in 'dloc 1'"));
				}
			}
			else
			{
				Error(DreamLocCode::MissingHeader, First, TEXT("a .dloc file starts with 'dloc 1'"));
			}

			for (;;)
			{
				const FToken& Token = Peek();
				if (Token.Kind != ETokenKind::Word)
				{
					break;
				}
				if (!Document.bHasCulture && Token.Text.Equals(TEXT("culture"), ESearchCase::CaseSensitive))
				{
					Next();
					if (ReadValue(Token, Document.Culture))
					{
						Document.bHasCulture = true;
						TrailingTarget = &Document.CultureComment;
					}
				}
				else if (!Document.bTerms && Token.Text.Equals(TEXT("terms"), ESearchCase::CaseSensitive))
				{
					Next();
					Document.bTerms = true;
					TrailingTarget = &Document.TermsComment;
				}
				else
				{
					break;
				}
			}
		}

		void ParseItem(TArray<FDreamLocItem>& OutItems, bool bInRemoved)
		{
			const FToken& Token = Peek();
			TArray<FString> Comments = TakeComments();

			switch (Token.Kind)
			{
			case ETokenKind::Word:
				if (Token.Text.Equals(TEXT("namespace"), ESearchCase::CaseSensitive))
				{
					Next();
					FDreamLocItem Item;
					Item.Kind = EDreamLocItemKind::Namespace;
					Item.Comments = MoveTemp(Comments);
					Item.Line = Token.Line;
					Item.Column = Token.Column;
					if (ReadValue(Token, Item.Namespace))
					{
						Namespace = Item.Namespace;
						bHasNamespace = true;
					}
					OutItems.Add(MoveTemp(Item));
					TrailingTarget = &OutItems.Last().TrailingComment;
					return;
				}
				if (!bInRemoved && Token.Text.Equals(TEXT("removed"), ESearchCase::CaseSensitive))
				{
					ParseRemoved(Token, MoveTemp(Comments), OutItems);
					return;
				}
				if (Token.Text.Equals(TEXT("term"), ESearchCase::CaseSensitive) || Token.Text.Equals(TEXT("keep"), ESearchCase::CaseSensitive))
				{
					ParseTerm(Token, MoveTemp(Comments), OutItems);
					return;
				}
				if (IsKeyword(Token.Text))
				{
					Next();
					Error(DreamLocCode::UnexpectedToken, Token, FString::Printf(
						TEXT("'%s' belongs to the header at the top of the file, or (for a key spelt that way) is written quoted"), *Token.Text));
					SkipRestOfLine(Token.Line);
					return;
				}
				ParseEntry(Token, MoveTemp(Comments), OutItems, bInRemoved);
				return;

			case ETokenKind::String:
				ParseEntry(Token, MoveTemp(Comments), OutItems, bInRemoved);
				return;

			case ETokenKind::LBrace:
				Next();
				Error(DreamLocCode::UnexpectedToken, Token, TEXT("a '{' with no key before it"));
				SkipBlock();
				return;

			case ETokenKind::RBrace:
				Next();
				Error(DreamLocCode::UnexpectedToken, Token, TEXT("a '}' with no '{' to close"));
				return;

			default:
				return;
			}
		}

		void ParseRemoved(const FToken& InKeyword, TArray<FString>&& InComments, TArray<FDreamLocItem>& OutItems)
		{
			Next();
			FDreamLocItem Item;
			Item.Kind = EDreamLocItemKind::Removed;
			Item.Comments = MoveTemp(InComments);
			Item.Line = InKeyword.Line;
			Item.Column = InKeyword.Column;
			const FToken& Open = Peek();
			if (Open.Kind != ETokenKind::LBrace)
			{
				Error(DreamLocCode::UnexpectedToken, Open, TEXT("expected '{' after 'removed'"));
				return;
			}
			Next();
			TrailingTarget = &Item.TrailingComment;

			// The block's entries take the namespace in force where it starts; its own namespace lines end with it.
			const FString OuterNamespace = Namespace;
			const bool bOuterHasNamespace = bHasNamespace;
			for (;;)
			{
				const FToken& Token = Peek();
				if (Token.Kind == ETokenKind::RBrace)
				{
					Item.ClosingComments = TakeComments();
					Next();
					break;
				}
				if (Token.Kind == ETokenKind::End)
				{
					Item.ClosingComments = TakeComments();
					Error(DreamLocCode::UnclosedBlock, Open, TEXT("this 'removed {' has no '}'"));
					break;
				}
				ParseItem(Item.Children, /*bInRemoved*/ true);
			}
			Namespace = OuterNamespace;
			bHasNamespace = bOuterHasNamespace;
			OutItems.Add(MoveTemp(Item));
		}

		void ParseTerm(const FToken& InKeyword, TArray<FString>&& InComments, TArray<FDreamLocItem>& OutItems)
		{
			Next();
			FDreamLocItem Item;
			Item.Kind = InKeyword.Text.Equals(TEXT("term"), ESearchCase::CaseSensitive) ? EDreamLocItemKind::Term : EDreamLocItemKind::Keep;
			Item.Line = InKeyword.Line;
			Item.Column = InKeyword.Column;
			Item.Entry.Comments = MoveTemp(InComments);
			Item.Entry.Line = InKeyword.Line;
			Item.Entry.Column = InKeyword.Column;
			const FToken& Word = Peek();
			if (Word.Kind == ETokenKind::String && Word.Line == InKeyword.Line)
			{
				Item.Entry.Key = Next().Text;
			}
			else
			{
				Error(DreamLocCode::InvalidValue, InKeyword, FString::Printf(TEXT("'%s' is followed by the quoted word, as in %s \"DreamMoon\""),
					*InKeyword.Text, *InKeyword.Text));
				SkipRestOfLine(InKeyword.Line);
				return;
			}
			if (Item.Kind == EDreamLocItemKind::Term)
			{
				if (Peek().Kind != ETokenKind::LBrace)
				{
					Error(DreamLocCode::UnexpectedToken, Peek(), TEXT("expected '{' after the term, as in term \"义体\" { text \"Cyberware\" }"));
					return;
				}
				ParseBlock(Item.Entry);
			}
			OutItems.Add(MoveTemp(Item));
			TrailingTarget = &OutItems.Last().Entry.TrailingComment;
		}

		void ParseEntry(const FToken& InKey, TArray<FString>&& InComments, TArray<FDreamLocItem>& OutItems, bool bInRemoved)
		{
			Next();
			FDreamLocItem Item;
			Item.Kind = EDreamLocItemKind::Entry;
			Item.Line = InKey.Line;
			Item.Column = InKey.Column;
			Item.Entry.Key = InKey.Text;
			Item.Entry.Comments = MoveTemp(InComments);
			Item.Entry.Line = InKey.Line;
			Item.Entry.Column = InKey.Column;

			if (!bHasNamespace && !bReportedOutsideNamespace)
			{
				bReportedOutsideNamespace = true;
				Error(DreamLocCode::EntryOutsideNamespace, InKey, FString::Printf(
					TEXT("'%s' comes before any 'namespace' line; for the empty namespace write namespace \"\""), *InKey.Text));
			}

			if (Peek().Kind != ETokenKind::LBrace)
			{
				Error(DreamLocCode::UnexpectedToken, Peek(), FString::Printf(TEXT("expected '{' after the key '%s'"), *InKey.Text));
				return;
			}
			ParseBlock(Item.Entry);

			if (!bInRemoved)
			{
				const FString Id = Namespace + TEXT("\x01") + Item.Entry.Key;
				if (const int32* FirstLine = SeenKeys.Find(Id))
				{
					Error(DreamLocCode::DuplicateKey, InKey, FString::Printf(
						TEXT("'%s' is already in namespace '%s' at line %d; only the first is used"), *Item.Entry.Key, *Namespace, *FirstLine));
				}
				else
				{
					SeenKeys.Add(Id, InKey.Line);
				}
			}
			OutItems.Add(MoveTemp(Item));
			TrailingTarget = &OutItems.Last().Entry.TrailingComment;
		}

		/** `{ field value ... }`, the `{` next. */
		void ParseBlock(FDreamLocEntry& OutEntry)
		{
			const FToken& Open = Next();
			TrailingTarget = &OutEntry.HeaderComment;
			for (;;)
			{
				const FToken& Token = Peek();
				if (Token.Kind == ETokenKind::RBrace)
				{
					OutEntry.ClosingComments = TakeComments();
					Next();
					return;
				}
				if (Token.Kind == ETokenKind::End)
				{
					OutEntry.ClosingComments = TakeComments();
					Error(DreamLocCode::UnclosedBlock, Open, FString::Printf(TEXT("the '{' of '%s' has no '}'"), *OutEntry.Key));
					return;
				}

				TArray<FString> FieldComments = TakeComments();
				if (Token.Kind != ETokenKind::Word)
				{
					Next();
					Error(DreamLocCode::UnexpectedToken, Token, TEXT("expected a field name: at, note, max, prev, src, text or state"));
					if (Token.Kind == ETokenKind::LBrace)
					{
						SkipBlock();
					}
					continue;
				}
				Next();

				const int32 FieldIndex = FindField(Token.Text);
				if (FieldIndex == INDEX_NONE)
				{
					Error(DreamLocCode::UnknownField, Token, FString::Printf(
						TEXT("'%s' is not a field; an entry has at, note, max, prev, src, text and state"), *Token.Text));
					const FToken& Value = Peek();
					if ((Value.Kind == ETokenKind::Word || Value.Kind == ETokenKind::String) && Value.Line == Token.Line)
					{
						Next();
					}
					continue;
				}

				const FToken& Value = Peek();
				if (!(Value.Kind == ETokenKind::Word || Value.Kind == ETokenKind::String) || Value.Line != Token.Line)
				{
					Error(DreamLocCode::InvalidValue, Token, FString::Printf(TEXT("'%s' has no value on its line"), *Token.Text));
					continue;
				}
				Next();

				const EDreamLocField Field = (EDreamLocField)FieldIndex;
				switch (Field)
				{
				case EDreamLocField::Note:
				case EDreamLocField::Prev:
				case EDreamLocField::Src:
				case EDreamLocField::Text:
					if (Value.Kind != ETokenKind::String)
					{
						Error(DreamLocCode::InvalidValue, Value, FString::Printf(TEXT("'%s' takes a quoted string"), *Token.Text));
					}
					break;
				case EDreamLocField::Max:
					if (Value.Kind != ETokenKind::Word || !IsAllDigits(Value.Text))
					{
						Error(DreamLocCode::InvalidValue, Value, FString::Printf(TEXT("'max' takes a whole number, not '%s'"), *Value.Text));
					}
					break;
				case EDreamLocField::State:
					if (Value.Kind != ETokenKind::Word || !(Value.Text == TEXT("machine") || Value.Text == TEXT("draft") || Value.Text == TEXT("done")))
					{
						Error(DreamLocCode::InvalidValue, Value, FString::Printf(TEXT("'state' is machine, draft or done, not '%s'"), *Value.Text));
					}
					break;
				default:
					break;
				}

				FDreamLocFieldValue& Slot = OutEntry.Fields[FieldIndex];
				if (Slot.bPresent)
				{
					Error(DreamLocCode::DuplicateField, Token, FString::Printf(
						TEXT("'%s' is already given at line %d; only the first is used"), *Token.Text, Slot.Line));
					continue;
				}
				Slot.bPresent = true;
				Slot.Value = Value.Text;
				Slot.Comments = MoveTemp(FieldComments);
				Slot.Line = Token.Line;
				Slot.Column = Token.Column;
				TrailingTarget = &Slot.TrailingComment;
			}
		}
	};

	// ---------------------------------------------------------------- writing

	const TCHAR* const Indent = TEXT("    ");

	void WriteComments(FString& Out, const TArray<FString>& InComments, const FString& InIndent)
	{
		for (const FString& Comment : InComments)
		{
			if (!Comment.IsEmpty())
			{
				Out += InIndent;
				Out += Comment;
			}
			Out += TEXT("\n");
		}
	}

	void WriteLine(FString& Out, const FString& InIndent, const FString& InText, const FString& InTrailingComment)
	{
		Out += InIndent;
		Out += InText;
		if (!InTrailingComment.IsEmpty())
		{
			Out += TEXT(" ");
			Out += InTrailingComment;
		}
		Out += TEXT("\n");
	}

	FString FormatFieldValue(EDreamLocField InField, const FString& InValue)
	{
		switch (InField)
		{
		case EDreamLocField::At:
			return DreamLocFile::QuoteWordIfNeeded(InValue, /*bInIsKey*/ false);
		case EDreamLocField::Max:
		case EDreamLocField::State:
			return DreamLocFile::QuoteWordIfNeeded(InValue, /*bInIsKey*/ false);
		default:
			return DreamLocFile::Quote(InValue);
		}
	}

	FString PadFieldName(EDreamLocField InField)
	{
		FString Name = DreamLocFile::GetFieldName(InField);
		const int32 Width = 6;
		if (Name.Len() < Width)
		{
			Name += FString::ChrN(Width - Name.Len(), TEXT(' '));
		}
		else
		{
			Name += TEXT(" ");
		}
		return Name;
	}

	bool HasInnerComments(const FDreamLocEntry& InEntry)
	{
		if (!InEntry.HeaderComment.IsEmpty() || InEntry.ClosingComments.Num() > 0)
		{
			return true;
		}
		for (const FDreamLocFieldValue& Field : InEntry.Fields)
		{
			if (Field.bPresent && (Field.Comments.Num() > 0 || !Field.TrailingComment.IsEmpty()))
			{
				return true;
			}
		}
		return false;
	}

	/** `<head> { ... }` over several lines: the shape of every translation entry. */
	void WriteBlock(FString& Out, const FString& InHead, const FDreamLocEntry& InEntry, const FString& InIndent)
	{
		const FString FieldIndent = InIndent + Indent;
		WriteComments(Out, InEntry.Comments, InIndent);
		WriteLine(Out, InIndent, InHead + TEXT(" {"), InEntry.HeaderComment);
		for (int32 Index = 0; Index < (int32)EDreamLocField::Count; ++Index)
		{
			const FDreamLocFieldValue& Field = InEntry.Fields[Index];
			if (!Field.bPresent)
			{
				continue;
			}
			WriteComments(Out, Field.Comments, FieldIndent);
			WriteLine(Out, FieldIndent, PadFieldName((EDreamLocField)Index) + FormatFieldValue((EDreamLocField)Index, Field.Value), Field.TrailingComment);
		}
		WriteComments(Out, InEntry.ClosingComments, FieldIndent);
		WriteLine(Out, InIndent, TEXT("}"), InEntry.TrailingComment);
	}

	void WriteTerm(FString& Out, const FDreamLocItem& InItem, const FString& InIndent)
	{
		const FDreamLocEntry& Entry = InItem.Entry;
		if (InItem.Kind == EDreamLocItemKind::Keep)
		{
			WriteComments(Out, Entry.Comments, InIndent);
			WriteLine(Out, InIndent, TEXT("keep ") + DreamLocFile::Quote(Entry.Key), Entry.TrailingComment);
			return;
		}
		const FString Head = TEXT("term ") + DreamLocFile::Quote(Entry.Key);
		if (HasInnerComments(Entry))
		{
			WriteBlock(Out, Head, Entry, InIndent);
			return;
		}
		// One line, as a glossary reads best: the translation first, then what explains it.
		static const EDreamLocField TermOrder[] = {
			EDreamLocField::Text, EDreamLocField::Note, EDreamLocField::At, EDreamLocField::Max,
			EDreamLocField::Prev, EDreamLocField::Src, EDreamLocField::State
		};
		FString Line = Head + TEXT(" {");
		for (const EDreamLocField Field : TermOrder)
		{
			const FDreamLocFieldValue& Value = Entry.Fields[(int32)Field];
			if (Value.bPresent)
			{
				Line += FString::Printf(TEXT(" %s %s"), DreamLocFile::GetFieldName(Field), *FormatFieldValue(Field, Value.Value));
			}
		}
		Line += TEXT(" }");
		WriteComments(Out, Entry.Comments, InIndent);
		WriteLine(Out, InIndent, Line, Entry.TrailingComment);
	}

	void WriteItems(FString& Out, const TArray<FDreamLocItem>& InItems, const FString& InIndent, bool bInBlock)
	{
		bool bFirst = true;
		EDreamLocItemKind Previous = EDreamLocItemKind::Entry;
		for (const FDreamLocItem& Item : InItems)
		{
			const bool bIsTermLine = Item.Kind == EDreamLocItemKind::Term || Item.Kind == EDreamLocItemKind::Keep;
			bool bBlankBefore = true;
			if (bFirst)
			{
				// The first namespace sits right under the header, as in every file sync writes; anything else is
				// set off by a blank line. The first line inside a block sits right under its `{`.
				bBlankBefore = !bInBlock && !(Item.Kind == EDreamLocItemKind::Namespace && Item.Comments.Num() == 0);
			}
			else if (bIsTermLine && (Previous == EDreamLocItemKind::Term || Previous == EDreamLocItemKind::Keep))
			{
				bBlankBefore = false;
			}
			if (bBlankBefore)
			{
				Out += TEXT("\n");
			}

			switch (Item.Kind)
			{
			case EDreamLocItemKind::Namespace:
				WriteComments(Out, Item.Comments, InIndent);
				WriteLine(Out, InIndent, TEXT("namespace ") + DreamLocFile::QuoteWordIfNeeded(Item.Namespace, false), Item.TrailingComment);
				break;
			case EDreamLocItemKind::Entry:
				WriteBlock(Out, DreamLocFile::QuoteWordIfNeeded(Item.Entry.Key, /*bInIsKey*/ true), Item.Entry, InIndent);
				break;
			case EDreamLocItemKind::Term:
			case EDreamLocItemKind::Keep:
				WriteTerm(Out, Item, InIndent);
				break;
			case EDreamLocItemKind::Removed:
				WriteComments(Out, Item.Comments, InIndent);
				WriteLine(Out, InIndent, TEXT("removed {"), Item.TrailingComment);
				WriteItems(Out, Item.Children, InIndent + Indent, /*bInBlock*/ true);
				WriteComments(Out, Item.ClosingComments, InIndent + Indent);
				WriteLine(Out, InIndent, TEXT("}"), FString());
				break;
			}
			bFirst = false;
			Previous = Item.Kind;
		}
	}

	void VisitEntries(const TArray<FDreamLocItem>& InItems, FString& InOutNamespace, bool bInRemoved, bool bInIncludeRemoved,
		TFunctionRef<void(const FString&, const FDreamLocEntry&, bool)> InVisitor)
	{
		for (const FDreamLocItem& Item : InItems)
		{
			switch (Item.Kind)
			{
			case EDreamLocItemKind::Namespace:
				InOutNamespace = Item.Namespace;
				break;
			case EDreamLocItemKind::Entry:
				InVisitor(InOutNamespace, Item.Entry, bInRemoved);
				break;
			case EDreamLocItemKind::Removed:
				if (bInIncludeRemoved)
				{
					FString BlockNamespace = InOutNamespace;
					VisitEntries(Item.Children, BlockNamespace, /*bInRemoved*/ true, bInIncludeRemoved, InVisitor);
				}
				break;
			default:
				break;
			}
		}
	}
}

// ---------------------------------------------------------------- FDreamLocEntry

const FString* FDreamLocEntry::Find(EDreamLocField InField) const
{
	const FDreamLocFieldValue& Field = Fields[(int32)InField];
	return Field.bPresent ? &Field.Value : nullptr;
}

void FDreamLocEntry::Set(EDreamLocField InField, const FString& InValue)
{
	FDreamLocFieldValue& Field = Fields[(int32)InField];
	Field.bPresent = true;
	Field.Value = InValue;
}

void FDreamLocEntry::Remove(EDreamLocField InField)
{
	Fields[(int32)InField] = FDreamLocFieldValue();
}

int32 FDreamLocEntry::GetMax() const
{
	const FString* Value = Find(EDreamLocField::Max);
	return Value != nullptr && DreamLocFileLocal::IsAllDigits(*Value) ? FCString::Atoi(**Value) : -1;
}

EDreamLocState FDreamLocEntry::GetState() const
{
	const FString* Value = Find(EDreamLocField::State);
	if (Value == nullptr)
	{
		return EDreamLocState::None;
	}
	if (*Value == TEXT("machine"))
	{
		return EDreamLocState::Machine;
	}
	if (*Value == TEXT("draft"))
	{
		return EDreamLocState::Draft;
	}
	if (*Value == TEXT("done"))
	{
		return EDreamLocState::Done;
	}
	return EDreamLocState::None;
}

void FDreamLocDocument::ForEachEntry(TFunctionRef<void(const FString&, const FDreamLocEntry&, bool)> InVisitor, bool bInIncludeRemoved) const
{
	FString Namespace;
	DreamLocFileLocal::VisitEntries(Items, Namespace, /*bInRemoved*/ false, bInIncludeRemoved, InVisitor);
}

// ---------------------------------------------------------------- DreamLocFile

bool DreamLocFile::Parse(const FString& InText, FDreamLocDocument& OutDocument, TArray<FDreamLocDiagnostic>& OutDiagnostics)
{
	using namespace DreamLocFileLocal;
	OutDocument = FDreamLocDocument();
	const int32 ErrorsBefore = FDreamLocDiagnostic::CountErrors(OutDiagnostics);
	TArray<FToken> Tokens;
	Tokenize(InText, Tokens, OutDiagnostics);
	FParser Parser(MoveTemp(Tokens), OutDocument, OutDiagnostics);
	Parser.Run();
	return FDreamLocDiagnostic::CountErrors(OutDiagnostics) == ErrorsBefore;
}

FString DreamLocFile::Write(const FDreamLocDocument& InDocument)
{
	using namespace DreamLocFileLocal;
	FString Out;
	WriteComments(Out, InDocument.HeaderComments, FString());
	WriteLine(Out, FString(), FString::Printf(TEXT("dloc %d"), InDocument.Version), InDocument.VersionComment);
	if (InDocument.bHasCulture)
	{
		WriteLine(Out, FString(), TEXT("culture ") + QuoteWordIfNeeded(InDocument.Culture, false), InDocument.CultureComment);
	}
	if (InDocument.bTerms)
	{
		WriteLine(Out, FString(), TEXT("terms"), InDocument.TermsComment);
	}
	WriteItems(Out, InDocument.Items, FString(), /*bInBlock*/ false);
	if (InDocument.EndComments.Num() > 0)
	{
		Out += TEXT("\n");
		WriteComments(Out, InDocument.EndComments, FString());
	}
	return Out;
}

FString DreamLocFile::Quote(const FString& InText)
{
	FString Out;
	Out.Reserve(InText.Len() + 2);
	Out.AppendChar(TEXT('"'));
	for (const TCHAR Char : InText)
	{
		switch (Char)
		{
		case TEXT('"'):  Out += TEXT("\\\""); break;
		case TEXT('\\'): Out += TEXT("\\\\"); break;
		case TEXT('\n'): Out += TEXT("\\n"); break;
		case TEXT('\t'): Out += TEXT("\\t"); break;
		case TEXT('\r'): Out += TEXT("\\r"); break;
		default:         Out.AppendChar(Char); break;
		}
	}
	Out.AppendChar(TEXT('"'));
	return Out;
}

FString DreamLocFile::QuoteWordIfNeeded(const FString& InWord, bool bInIsKey)
{
	using namespace DreamLocFileLocal;
	if (InWord.IsEmpty() || InWord.StartsWith(TEXT("//")) || (bInIsKey && IsKeyword(InWord)))
	{
		return Quote(InWord);
	}
	for (const TCHAR Char : InWord)
	{
		if (EndsWord(Char) || Char < TEXT(' ') || FChar::IsWhitespace(Char))
		{
			return Quote(InWord);
		}
	}
	return InWord;
}

const TCHAR* DreamLocFile::GetFieldName(EDreamLocField InField)
{
	return (int32)InField < (int32)EDreamLocField::Count ? DreamLocFileLocal::FieldNames[(int32)InField] : TEXT("");
}

bool DreamLocFile::LoadFile(const FString& InPath, FDreamLocDocument& OutDocument, TArray<FDreamLocDiagnostic>& OutDiagnostics)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *InPath))
	{
		OutDocument = FDreamLocDocument();
		OutDiagnostics.Emplace(EDreamLocSeverity::Error, DreamLocCode::MissingHeader, 0, 0, TEXT("the file cannot be read"));
		return false;
	}
	return Parse(Text, OutDocument, OutDiagnostics);
}

bool DreamLocFile::SaveFile(const FString& InPath, const FDreamLocDocument& InDocument)
{
	return FFileHelper::SaveStringToFile(Write(InDocument), *InPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
