// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocExchange.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "DreamLocFile.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace DreamLocExchangeLocal
{
	void AddMessage(TArray<FDreamLocReportLine>& OutMessages, const FString& InFile, EDreamLocSeverity InSeverity, int32 InCode, const FString& InText)
	{
		OutMessages.Add(FDreamLocReportLine{ InFile, FDreamLocDiagnostic(InSeverity, InCode, 0, 0, InText) });
	}

	FString FieldOr(const FDreamLocEntry& InEntry, EDreamLocField InField)
	{
		const FString* Value = InEntry.Find(InField);
		return Value != nullptr ? *Value : FString();
	}

	/** What an entry is to a translator: untranslated, stale, machine, draft, done, or unreviewed (a text, no state). */
	FString CategoryOf(const FDreamLocEntry& InEntry)
	{
		if (!InEntry.IsTranslated())
		{
			return TEXT("untranslated");
		}
		if (InEntry.IsStale())
		{
			return TEXT("stale");
		}
		const FString* State = InEntry.Find(EDreamLocField::State);
		return State != nullptr ? *State : FString(TEXT("unreviewed"));
	}

	bool IsWanted(const FString& InCategory, const TArray<FString>& InOnly)
	{
		if (InOnly.Num() == 0)
		{
			return InCategory == TEXT("untranslated") || InCategory == TEXT("stale");
		}
		return InOnly.ContainsByPredicate([&InCategory](const FString& InWanted)
		{
			return InWanted.Equals(TEXT("all"), ESearchCase::IgnoreCase) || InWanted.Equals(InCategory, ESearchCase::IgnoreCase);
		});
	}

	// ---- JSON

	FString JsonQuote(const FString& InText)
	{
		FString Out = TEXT("\"");
		for (const TCHAR Char : InText)
		{
			switch (Char)
			{
			case TEXT('"'): Out += TEXT("\\\""); break;
			case TEXT('\\'): Out += TEXT("\\\\"); break;
			case TEXT('\n'): Out += TEXT("\\n"); break;
			case TEXT('\r'): Out += TEXT("\\r"); break;
			case TEXT('\t'): Out += TEXT("\\t"); break;
			default:
				if (Char < 0x20)
				{
					Out += FString::Printf(TEXT("\\u%04x"), (uint32)Char);
				}
				else
				{
					Out.AppendChar(Char);
				}
			}
		}
		return Out + TEXT("\"");
	}

	FString JsonStrings(const TArray<FString>& InValues)
	{
		TArray<FString> Quoted;
		for (const FString& Value : InValues)
		{
			Quoted.Add(JsonQuote(Value));
		}
		return TEXT("[") + FString::Join(Quoted, TEXT(", ")) + TEXT("]");
	}

	FString JsonTerms(const TArray<TPair<FString, FString>>& InTerms)
	{
		TArray<FString> Items;
		for (const TPair<FString, FString>& Term : InTerms)
		{
			Items.Add(FString::Printf(TEXT("{ \"term\": %s, \"text\": %s }"), *JsonQuote(Term.Key), *JsonQuote(Term.Value)));
		}
		return TEXT("[") + FString::Join(Items, TEXT(", ")) + TEXT("]");
	}

	const TCHAR* const AiInstructions =
		TEXT("Translate each entry's src from the source culture into the culture and write it into text; leave everything else as it is. ")
		TEXT("Keep every {argument} exactly as written and add none: the game puts values there. In the target language an argument may take ")
		TEXT("the engine's modifiers, as in {Count}|plural(one=item,other=items), with the plural forms that language needs. Keep rich-text tags ")
		TEXT("(<b>...</b>, <color=...>...</>, <img=.../>) the same and in pairs. Keep blanks at either end and line breaks (\\n) as the source has them. ")
		TEXT("Stay within max, counted in display width (a full-width character counts 2). Use the given translation for each term in terms, ")
		TEXT("and keep each word in keep unchanged. note says what the text is for; kind and context say what it is and what the game knows about it ")
		TEXT("(who speaks a line, the dialogue, the line before it, and in args what each {0}, {1}... stands for); nearby is the texts around it in the ")
		TEXT("same file; a stale entry's prev is the old source and previousText its old translation; reference holds other languages' translations.");

	FString JsonContext(const TArray<FDreamLocExtractContext>& InContext)
	{
		TArray<FString> Items;
		for (const FDreamLocExtractContext& Item : InContext)
		{
			FString Value;
			if (Item.bList)
			{
				TArray<FString> Quoted;
				for (const FString& Element : Item.Values)
				{
					Quoted.Add(JsonQuote(Element));
				}
				Value = TEXT("[") + FString::Join(Quoted, TEXT(", ")) + TEXT("]");
			}
			else
			{
				Value = JsonQuote(Item.Describe());
			}
			Items.Add(FString::Printf(TEXT("%s: %s"), *JsonQuote(Item.Name), *Value));
		}
		return TEXT("{ ") + FString::Join(Items, TEXT(", ")) + TEXT(" }");
	}

	/** The context in one line, for the formats that have no structure for it: `speaker: 老周; args: {0} 找到的零件`. */
	FString ContextLine(const TArray<FDreamLocExtractContext>& InContext)
	{
		TArray<FString> Items;
		for (const FDreamLocExtractContext& Item : InContext)
		{
			Items.Add(Item.Name + TEXT(": ") + Item.Describe());
		}
		return FString::Join(Items, TEXT("; "));
	}

	FString WriteJson(const FDreamLocExchangeBatch& InBatch)
	{
		FString Out = TEXT("{\n");
		Out += TEXT("  \"version\": 1,\n  \"kind\": \"dreamloc-batch\",\n");
		Out += FString::Printf(TEXT("  \"sourceCulture\": %s,\n  \"culture\": %s,\n"), *JsonQuote(InBatch.SourceCulture), *JsonQuote(InBatch.Culture));
		Out += FString::Printf(TEXT("  \"instructions\": %s,\n"), *JsonQuote(AiInstructions));
		Out += FString::Printf(TEXT("  \"terms\": %s,\n  \"keep\": %s,\n"), *JsonTerms(InBatch.Terms.Terms), *JsonStrings(InBatch.Terms.Keeps));
		Out += TEXT("  \"entries\": [");
		for (int32 Index = 0; Index < InBatch.Entries.Num(); ++Index)
		{
			const FDreamLocExchangeEntry& Entry = InBatch.Entries[Index];
			TArray<FString> Members;
			Members.Add(FString::Printf(TEXT("\"file\": %s"), *JsonQuote(Entry.File)));
			Members.Add(FString::Printf(TEXT("\"namespace\": %s"), *JsonQuote(Entry.Namespace)));
			Members.Add(FString::Printf(TEXT("\"key\": %s"), *JsonQuote(Entry.Key)));
			Members.Add(FString::Printf(TEXT("\"src\": %s"), *JsonQuote(Entry.Source)));
			const bool bStale = !Entry.Prev.IsEmpty();
			Members.Add(FString::Printf(TEXT("\"text\": %s"), *JsonQuote(Entry.Text)));
			if (!Entry.State.IsEmpty() && !bStale)
			{
				Members.Add(FString::Printf(TEXT("\"state\": %s"), *JsonQuote(Entry.State)));
			}
			if (!Entry.Note.IsEmpty())
			{
				Members.Add(FString::Printf(TEXT("\"note\": %s"), *JsonQuote(Entry.Note)));
			}
			if (Entry.Max >= 0)
			{
				Members.Add(FString::Printf(TEXT("\"max\": %d"), Entry.Max));
			}
			if (!Entry.At.IsEmpty())
			{
				Members.Add(FString::Printf(TEXT("\"at\": %s"), *JsonQuote(Entry.At)));
			}
			if (bStale)
			{
				Members.Add(FString::Printf(TEXT("\"prev\": %s"), *JsonQuote(Entry.Prev)));
				Members.Add(FString::Printf(TEXT("\"previousText\": %s"), *JsonQuote(Entry.PreviousText)));
			}
			if (!Entry.Kind.IsEmpty())
			{
				Members.Add(FString::Printf(TEXT("\"kind\": %s"), *JsonQuote(Entry.Kind)));
			}
			if (Entry.Context.Num() > 0)
			{
				Members.Add(FString::Printf(TEXT("\"context\": %s"), *JsonContext(Entry.Context)));
			}
			if (Entry.Nearby.Num() > 0)
			{
				Members.Add(FString::Printf(TEXT("\"nearby\": %s"), *JsonStrings(Entry.Nearby)));
			}
			if (Entry.Terms.Num() > 0)
			{
				Members.Add(FString::Printf(TEXT("\"terms\": %s"), *JsonTerms(Entry.Terms)));
			}
			if (Entry.Keeps.Num() > 0)
			{
				Members.Add(FString::Printf(TEXT("\"keep\": %s"), *JsonStrings(Entry.Keeps)));
			}
			if (Entry.References.Num() > 0)
			{
				TArray<FString> Items;
				for (const TPair<FString, FString>& Reference : Entry.References)
				{
					Items.Add(FString::Printf(TEXT("%s: %s"), *JsonQuote(Reference.Key), *JsonQuote(Reference.Value)));
				}
				Members.Add(TEXT("\"reference\": { ") + FString::Join(Items, TEXT(", ")) + TEXT(" }"));
			}
			Out += Index == 0 ? TEXT("\n") : TEXT(",\n");
			Out += TEXT("    { ") + FString::Join(Members, TEXT(", ")) + TEXT(" }");
		}
		Out += InBatch.Entries.Num() > 0 ? TEXT("\n  ]\n}\n") : TEXT("]\n}\n");
		return Out;
	}

	bool ReadJson(const FString& InText, FDreamLocExchangeBatch& OutBatch, FString& OutError)
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InText);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutError = FString::Printf(TEXT("not JSON: %s"), *Reader->GetErrorMessage());
			return false;
		}
		Root->TryGetStringField(TEXT("sourceCulture"), OutBatch.SourceCulture);
		Root->TryGetStringField(TEXT("culture"), OutBatch.Culture);
		const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
		if (!Root->TryGetArrayField(TEXT("entries"), Entries) || Entries == nullptr)
		{
			OutError = TEXT("no \"entries\"");
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Entries)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(Object) || Object == nullptr)
			{
				continue;
			}
			FDreamLocExchangeEntry& Entry = OutBatch.Entries.AddDefaulted_GetRef();
			(*Object)->TryGetStringField(TEXT("file"), Entry.File);
			(*Object)->TryGetStringField(TEXT("namespace"), Entry.Namespace);
			(*Object)->TryGetStringField(TEXT("key"), Entry.Key);
			(*Object)->TryGetStringField(TEXT("src"), Entry.Source);
			(*Object)->TryGetStringField(TEXT("text"), Entry.Text);
			(*Object)->TryGetStringField(TEXT("state"), Entry.State);
			(*Object)->TryGetStringField(TEXT("note"), Entry.Note);
			(*Object)->TryGetStringField(TEXT("at"), Entry.At);
			(*Object)->TryGetNumberField(TEXT("max"), Entry.Max);
		}
		return true;
	}

	// ---- XML, as much as XLIFF needs: elements, attributes, text, the five entities and character references.

	FString XmlEscape(const FString& InText, bool bInAttribute)
	{
		FString Out;
		for (const TCHAR Char : InText)
		{
			switch (Char)
			{
			case TEXT('&'): Out += TEXT("&amp;"); break;
			case TEXT('<'): Out += TEXT("&lt;"); break;
			case TEXT('>'): Out += TEXT("&gt;"); break;
			case TEXT('"'): Out += bInAttribute ? TEXT("&quot;") : TEXT("\""); break;
			case TEXT('\n'): Out += bInAttribute ? TEXT("&#10;") : TEXT("\n"); break;
			case TEXT('\r'): Out += TEXT("&#13;"); break;
			case TEXT('\t'): Out += bInAttribute ? TEXT("&#9;") : TEXT("\t"); break;
			default: Out.AppendChar(Char); break;
			}
		}
		return Out;
	}

	FString XmlUnescape(const FString& InText)
	{
		FString Out;
		for (int32 Index = 0; Index < InText.Len(); ++Index)
		{
			if (InText[Index] != TEXT('&'))
			{
				Out.AppendChar(InText[Index]);
				continue;
			}
			const int32 End = InText.Find(TEXT(";"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Index);
			if (End == INDEX_NONE)
			{
				Out.AppendChar(InText[Index]);
				continue;
			}
			const FString Name = InText.Mid(Index + 1, End - Index - 1);
			if (Name == TEXT("amp")) { Out.AppendChar(TEXT('&')); }
			else if (Name == TEXT("lt")) { Out.AppendChar(TEXT('<')); }
			else if (Name == TEXT("gt")) { Out.AppendChar(TEXT('>')); }
			else if (Name == TEXT("quot")) { Out.AppendChar(TEXT('"')); }
			else if (Name == TEXT("apos")) { Out.AppendChar(TEXT('\'')); }
			else if (Name.StartsWith(TEXT("#x")) || Name.StartsWith(TEXT("#X"))) { Out.AppendChar(static_cast<TCHAR>(FCString::Strtoi(*Name.RightChop(2), nullptr, 16))); }
			else if (Name.StartsWith(TEXT("#"))) { Out.AppendChar(static_cast<TCHAR>(FCString::Atoi(*Name.RightChop(1)))); }
			else { Out += InText.Mid(Index, End - Index + 1); }
			Index = End;
		}
		return Out;
	}

	struct FXmlElement
	{
		FString Name;
		TMap<FString, FString> Attributes;
		/** The text right inside it, entities resolved, blanks kept. */
		FString Text;
		TArray<TSharedRef<FXmlElement>> Children;

		const FXmlElement* Child(const TCHAR* InName) const
		{
			for (const TSharedRef<FXmlElement>& Element : Children)
			{
				if (Element->Name == InName)
				{
					return &Element.Get();
				}
			}
			return nullptr;
		}
	};

	/** The document's root element; false with OutError when a tag is not closed in order. */
	bool ParseXml(const FString& InText, TSharedRef<FXmlElement>& OutRoot, FString& OutError)
	{
		TArray<TSharedRef<FXmlElement>> Stack;
		Stack.Add(MakeShared<FXmlElement>());
		int32 Index = 0;
		const int32 Length = InText.Len();
		while (Index < Length)
		{
			if (InText[Index] != TEXT('<'))
			{
				const int32 Next = InText.Find(TEXT("<"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Index);
				const int32 End = Next == INDEX_NONE ? Length : Next;
				Stack.Last()->Text += XmlUnescape(InText.Mid(Index, End - Index));
				Index = End;
				continue;
			}
			if (InText.Mid(Index, 4) == TEXT("<!--"))
			{
				const int32 End = InText.Find(TEXT("-->"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Index);
				Index = End == INDEX_NONE ? Length : End + 3;
				continue;
			}
			if (InText.Mid(Index, 9) == TEXT("<![CDATA["))
			{
				const int32 End = InText.Find(TEXT("]]>"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Index);
				Stack.Last()->Text += InText.Mid(Index + 9, (End == INDEX_NONE ? Length : End) - Index - 9);
				Index = End == INDEX_NONE ? Length : End + 3;
				continue;
			}
			if (InText.Mid(Index, 2) == TEXT("<?") || InText.Mid(Index, 2) == TEXT("<!"))
			{
				const int32 End = InText.Find(TEXT(">"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Index);
				Index = End == INDEX_NONE ? Length : End + 1;
				continue;
			}
			// A tag: find its end outside quoted attribute values.
			int32 End = Index + 1;
			TCHAR Quote = 0;
			while (End < Length && (Quote != 0 || InText[End] != TEXT('>')))
			{
				if (Quote == 0 && (InText[End] == TEXT('"') || InText[End] == TEXT('\'')))
				{
					Quote = InText[End];
				}
				else if (Quote != 0 && InText[End] == Quote)
				{
					Quote = 0;
				}
				++End;
			}
			if (End >= Length)
			{
				OutError = TEXT("a tag is not closed");
				return false;
			}
			FString Tag = InText.Mid(Index + 1, End - Index - 1);
			Index = End + 1;
			if (Tag.StartsWith(TEXT("/")))
			{
				const FString Name = Tag.RightChop(1).TrimStartAndEnd();
				if (Stack.Num() < 2 || Stack.Last()->Name != Name)
				{
					OutError = FString::Printf(TEXT("</%s> closes nothing open"), *Name);
					return false;
				}
				Stack.Pop();
				continue;
			}
			const bool bSelfClosing = Tag.EndsWith(TEXT("/"));
			if (bSelfClosing)
			{
				Tag.LeftChopInline(1);
			}
			TSharedRef<FXmlElement> Element = MakeShared<FXmlElement>();
			int32 Cursor = 0;
			while (Cursor < Tag.Len() && !FChar::IsWhitespace(Tag[Cursor]))
			{
				++Cursor;
			}
			Element->Name = Tag.Left(Cursor);
			while (Cursor < Tag.Len())
			{
				while (Cursor < Tag.Len() && FChar::IsWhitespace(Tag[Cursor]))
				{
					++Cursor;
				}
				const int32 NameStart = Cursor;
				while (Cursor < Tag.Len() && Tag[Cursor] != TEXT('=') && !FChar::IsWhitespace(Tag[Cursor]))
				{
					++Cursor;
				}
				const FString Name = Tag.Mid(NameStart, Cursor - NameStart);
				while (Cursor < Tag.Len() && Tag[Cursor] != TEXT('"') && Tag[Cursor] != TEXT('\''))
				{
					++Cursor;
				}
				if (Cursor >= Tag.Len())
				{
					break;
				}
				const TCHAR ValueQuote = Tag[Cursor++];
				const int32 ValueStart = Cursor;
				while (Cursor < Tag.Len() && Tag[Cursor] != ValueQuote)
				{
					++Cursor;
				}
				if (!Name.IsEmpty())
				{
					Element->Attributes.Add(Name, XmlUnescape(Tag.Mid(ValueStart, Cursor - ValueStart)));
				}
				++Cursor;
			}
			Stack.Last()->Children.Add(Element);
			if (!bSelfClosing)
			{
				Stack.Add(Element);
			}
		}
		if (Stack.Num() != 1 || Stack[0]->Children.Num() == 0)
		{
			OutError = Stack.Num() != 1 ? FString::Printf(TEXT("<%s> is not closed"), *Stack.Last()->Name) : TEXT("no element");
			return false;
		}
		OutRoot = Stack[0]->Children[0];
		return true;
	}

	const TCHAR* XliffStateOf(const FDreamLocExchangeEntry& InEntry)
	{
		if (InEntry.Text.IsEmpty())
		{
			return TEXT("needs-translation");
		}
		if (!InEntry.Prev.IsEmpty() || InEntry.State == TEXT("machine"))
		{
			return TEXT("needs-review-translation");
		}
		return InEntry.State == TEXT("done") ? TEXT("final") : TEXT("translated");
	}

	FString StateOfXliff(const FString& InState, const FString& InQualifier)
	{
		if (InState == TEXT("final") || InState == TEXT("signed-off"))
		{
			return TEXT("done");
		}
		if (InState == TEXT("translated"))
		{
			return TEXT("draft");
		}
		if (InState == TEXT("needs-review-translation") || InQualifier == TEXT("mt-suggestion"))
		{
			return TEXT("machine");
		}
		return FString();
	}

	FString WriteXliff(const FDreamLocExchangeBatch& InBatch)
	{
		FString Out = TEXT("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<xliff version=\"1.2\" xmlns=\"urn:oasis:names:tc:xliff:document:1.2\">\n");
		TArray<FString> Files;
		for (const FDreamLocExchangeEntry& Entry : InBatch.Entries)
		{
			Files.AddUnique(Entry.File);
		}
		for (const FString& File : Files)
		{
			Out += FString::Printf(TEXT("  <file original=\"%s\" source-language=\"%s\" target-language=\"%s\" datatype=\"plaintext\">\n    <body>\n"),
				*XmlEscape(File, true), *XmlEscape(InBatch.SourceCulture, true), *XmlEscape(InBatch.Culture, true));
			int32 Unit = 0;
			for (const FDreamLocExchangeEntry& Entry : InBatch.Entries)
			{
				if (Entry.File != File)
				{
					continue;
				}
				Out += FString::Printf(TEXT("      <trans-unit id=\"%d\" resname=\"%s\" xml:space=\"preserve\""), ++Unit, *XmlEscape(Entry.Key, true));
				if (Entry.Max >= 0)
				{
					Out += FString::Printf(TEXT(" maxwidth=\"%d\" size-unit=\"char\""), Entry.Max);
				}
				Out += TEXT(">\n");
				Out += FString::Printf(TEXT("        <source>%s</source>\n"), *XmlEscape(Entry.Source, false));
				// A translation with no state of its own has none here either: no `state` attribute.
				const FString State = !Entry.Text.IsEmpty() && Entry.State.IsEmpty() ? FString() : FString::Printf(TEXT(" state=\"%s\""), XliffStateOf(Entry));
				const TCHAR* Qualifier = Entry.State == TEXT("machine") ? TEXT(" state-qualifier=\"mt-suggestion\"") : TEXT("");
				Out += FString::Printf(TEXT("        <target%s%s>%s</target>\n"), *State, Qualifier, *XmlEscape(Entry.Text, false));
				if (!Entry.Note.IsEmpty())
				{
					Out += FString::Printf(TEXT("        <note from=\"developer\">%s</note>\n"), *XmlEscape(Entry.Note, false));
				}
				if (!Entry.Prev.IsEmpty())
				{
					Out += FString::Printf(TEXT("        <note from=\"dreamloc\">The source was \"%s\", translated \"%s\"; it has changed since: translate it again.</note>\n"),
						*XmlEscape(Entry.Prev, false), *XmlEscape(Entry.PreviousText, false));
				}
				Out += TEXT("        <context-group purpose=\"location\">\n");
				Out += FString::Printf(TEXT("          <context context-type=\"x-dreamloc-namespace\">%s</context>\n"), *XmlEscape(Entry.Namespace, false));
				if (!Entry.At.IsEmpty())
				{
					Out += FString::Printf(TEXT("          <context context-type=\"sourcefile\">%s</context>\n"), *XmlEscape(Entry.At, false));
				}
				Out += TEXT("        </context-group>\n");
				if (!Entry.Kind.IsEmpty() || Entry.Context.Num() > 0)
				{
					// The source tool's own words about the text, each as an x- context the translator's tool shows.
					Out += TEXT("        <context-group purpose=\"information\">\n");
					if (!Entry.Kind.IsEmpty())
					{
						Out += FString::Printf(TEXT("          <context context-type=\"x-kind\">%s</context>\n"), *XmlEscape(Entry.Kind, false));
					}
					for (const FDreamLocExtractContext& Item : Entry.Context)
					{
						Out += FString::Printf(TEXT("          <context context-type=\"x-%s\">%s</context>\n"), *XmlEscape(Item.Name, true), *XmlEscape(Item.Describe(), false));
					}
					Out += TEXT("        </context-group>\n");
				}
				Out += TEXT("      </trans-unit>\n");
			}
			Out += TEXT("    </body>\n  </file>\n");
		}
		return Out + TEXT("</xliff>\n");
	}

	bool ReadXliff(const FString& InText, FDreamLocExchangeBatch& OutBatch, FString& OutError)
	{
		TSharedRef<FXmlElement> Root = MakeShared<FXmlElement>();
		if (!ParseXml(InText, Root, OutError))
		{
			return false;
		}
		if (Root->Name != TEXT("xliff"))
		{
			OutError = TEXT("the root element is not <xliff>");
			return false;
		}
		for (const TSharedRef<FXmlElement>& File : Root->Children)
		{
			if (File->Name != TEXT("file"))
			{
				continue;
			}
			OutBatch.SourceCulture = File->Attributes.FindRef(TEXT("source-language"));
			OutBatch.Culture = File->Attributes.FindRef(TEXT("target-language"));
			const FXmlElement* Body = File->Child(TEXT("body"));
			for (const TSharedRef<FXmlElement>& Unit : Body != nullptr ? Body->Children : TArray<TSharedRef<FXmlElement>>())
			{
				if (Unit->Name != TEXT("trans-unit"))
				{
					continue;
				}
				FDreamLocExchangeEntry& Entry = OutBatch.Entries.AddDefaulted_GetRef();
				Entry.File = File->Attributes.FindRef(TEXT("original"));
				Entry.Key = Unit->Attributes.FindRef(TEXT("resname"));
				if (const FXmlElement* Source = Unit->Child(TEXT("source")))
				{
					Entry.Source = Source->Text;
				}
				if (const FXmlElement* Target = Unit->Child(TEXT("target")))
				{
					Entry.Text = Target->Text;
					Entry.State = Entry.Text.IsEmpty() ? FString() : StateOfXliff(Target->Attributes.FindRef(TEXT("state")), Target->Attributes.FindRef(TEXT("state-qualifier")));
				}
				if (const FXmlElement* Group = Unit->Child(TEXT("context-group")))
				{
					for (const TSharedRef<FXmlElement>& Context : Group->Children)
					{
						if (Context->Attributes.FindRef(TEXT("context-type")) == TEXT("x-dreamloc-namespace"))
						{
							Entry.Namespace = Context->Text;
						}
					}
				}
			}
		}
		return true;
	}

	// ---- PO

	FString PoQuote(const FString& InText)
	{
		FString Out = TEXT("\"");
		for (const TCHAR Char : InText)
		{
			switch (Char)
			{
			case TEXT('"'): Out += TEXT("\\\""); break;
			case TEXT('\\'): Out += TEXT("\\\\"); break;
			case TEXT('\n'): Out += TEXT("\\n"); break;
			case TEXT('\r'): Out += TEXT("\\r"); break;
			case TEXT('\t'): Out += TEXT("\\t"); break;
			default: Out.AppendChar(Char); break;
			}
		}
		return Out + TEXT("\"");
	}

	FString PoComment(const FString& InText)
	{
		return InText.Replace(TEXT("\r"), TEXT(" ")).Replace(TEXT("\n"), TEXT(" "));
	}

	FString WritePo(const FDreamLocExchangeBatch& InBatch)
	{
		FString Out = TEXT("msgid \"\"\nmsgstr \"\"\n");
		Out += FString::Printf(TEXT("\"Language: %s\\n\"\n\"Content-Type: text/plain; charset=UTF-8\\n\"\n\"X-DreamLoc-Source-Language: %s\\n\"\n"),
			*InBatch.Culture, *InBatch.SourceCulture);
		for (const FDreamLocExchangeEntry& Entry : InBatch.Entries)
		{
			Out += TEXT("\n");
			if (!Entry.Note.IsEmpty())
			{
				Out += FString::Printf(TEXT("#. %s\n"), *PoComment(Entry.Note));
			}
			if (Entry.Max >= 0)
			{
				Out += FString::Printf(TEXT("#. max %d\n"), Entry.Max);
			}
			Out += FString::Printf(TEXT("#. dloc: %s\n"), *PoComment(Entry.File));
			if (!Entry.Kind.IsEmpty())
			{
				Out += FString::Printf(TEXT("#. dloc-kind: %s\n"), *PoComment(Entry.Kind));
			}
			if (Entry.Context.Num() > 0)
			{
				Out += FString::Printf(TEXT("#. dloc-context: %s\n"), *PoComment(ContextLine(Entry.Context)));
			}
			if (!Entry.At.IsEmpty())
			{
				Out += FString::Printf(TEXT("#: %s\n"), *PoComment(Entry.At));
			}
			const bool bFuzzy = !Entry.Prev.IsEmpty() || Entry.State == TEXT("machine");
			if (bFuzzy)
			{
				Out += TEXT("#, fuzzy\n");
			}
			if (Entry.State == TEXT("done") || Entry.State == TEXT("draft"))
			{
				Out += FString::Printf(TEXT("#. state %s\n"), *Entry.State);
			}
			if (!Entry.Prev.IsEmpty())
			{
				Out += FString::Printf(TEXT("#| msgid %s\n"), *PoQuote(Entry.Prev));
				Out += FString::Printf(TEXT("#. previous translation: %s\n"), *PoComment(Entry.PreviousText));
			}
			Out += FString::Printf(TEXT("msgctxt %s\nmsgid %s\nmsgstr %s\n"), *PoQuote(Entry.Namespace + TEXT("::") + Entry.Key), *PoQuote(Entry.Source), *PoQuote(Entry.Text));
		}
		return Out;
	}

	/** The string of a PO keyword's quoted parts: `"a" "b"` across lines is `ab`. */
	FString PoUnquote(const FString& InQuoted)
	{
		FString Out;
		bool bInside = false;
		for (int32 Index = 0; Index < InQuoted.Len(); ++Index)
		{
			const TCHAR Char = InQuoted[Index];
			if (!bInside)
			{
				bInside = Char == TEXT('"');
				continue;
			}
			if (Char == TEXT('"'))
			{
				bInside = false;
				continue;
			}
			if (Char == TEXT('\\') && Index + 1 < InQuoted.Len())
			{
				const TCHAR Escaped = InQuoted[++Index];
				Out.AppendChar(Escaped == TEXT('n') ? TEXT('\n') : Escaped == TEXT('t') ? TEXT('\t') : Escaped == TEXT('r') ? TEXT('\r') : Escaped);
				continue;
			}
			Out.AppendChar(Char);
		}
		return Out;
	}

	bool ReadPo(const FString& InText, FDreamLocExchangeBatch& OutBatch, FString& OutError)
	{
		TArray<FString> Lines;
		InText.ParseIntoArrayLines(Lines, /*InCullEmpty*/ false);
		struct FUnit
		{
			FString Context, Id, Str, File;
			bool bFuzzy = false;
			FString State;
			int32 Max = -1;
			FString Note;
		};
		FUnit Unit;
		FString* Current = nullptr;
		bool bAny = false;
		auto Flush = [&]()
		{
			if (!Unit.Context.IsEmpty() || !Unit.Id.IsEmpty())
			{
				bAny = true;
				FDreamLocExchangeEntry& Entry = OutBatch.Entries.AddDefaulted_GetRef();
				FString Namespace;
				FString Key;
				if (Unit.Context.Split(TEXT("::"), &Namespace, &Key, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
				{
					Entry.Namespace = Namespace;
					Entry.Key = Key;
				}
				else
				{
					Entry.Key = Unit.Context;
				}
				Entry.Source = Unit.Id;
				Entry.Text = Unit.Str;
				Entry.File = Unit.File;
				Entry.Max = Unit.Max;
				Entry.Note = Unit.Note;
				Entry.State = Entry.Text.IsEmpty() ? FString() : !Unit.State.IsEmpty() ? Unit.State : Unit.bFuzzy ? FString(TEXT("machine")) : FString();
			}
			else if (!Unit.Str.IsEmpty())
			{
				// The header: Language and the source language.
				for (const FString& Field : TArray<FString>{ TEXT("Language: "), TEXT("X-DreamLoc-Source-Language: ") })
				{
					int32 At = Unit.Str.Find(Field);
					if (At != INDEX_NONE)
					{
						FString Value = Unit.Str.Mid(At + Field.Len());
						int32 End = INDEX_NONE;
						Value = Value.FindChar(TEXT('\n'), End) ? Value.Left(End) : Value;
						(Field.StartsWith(TEXT("X-")) ? OutBatch.SourceCulture : OutBatch.Culture) = Value.TrimStartAndEnd();
					}
				}
			}
			Unit = FUnit();
			Current = nullptr;
		};
		for (const FString& Raw : Lines)
		{
			const FString Line = Raw.TrimStartAndEnd();
			if (Line.IsEmpty())
			{
				Flush();
				continue;
			}
			if (Line.StartsWith(TEXT("#")))
			{
				if (Line.StartsWith(TEXT("#,")) && Line.Contains(TEXT("fuzzy")))
				{
					Unit.bFuzzy = true;
				}
				else if (Line.StartsWith(TEXT("#. dloc: ")))
				{
					Unit.File = Line.RightChop(9);
				}
				else if (Line.StartsWith(TEXT("#. max ")))
				{
					Unit.Max = FCString::Atoi(*Line.RightChop(7));
				}
				else if (Line == TEXT("#. state done") || Line == TEXT("#. state draft"))
				{
					Unit.State = Line.RightChop(9);
				}
				else if (Line.StartsWith(TEXT("#. previous translation: ")) || Line.StartsWith(TEXT("#. dloc-")))
				{
					// For the translator only.
				}
				else if (Line.StartsWith(TEXT("#. ")))
				{
					Unit.Note = Unit.Note.IsEmpty() ? Line.RightChop(3) : Unit.Note + TEXT(" ") + Line.RightChop(3);
				}
				continue;
			}
			if (Line.StartsWith(TEXT("msgctxt ")))
			{
				Current = &Unit.Context;
				*Current = PoUnquote(Line.RightChop(8));
			}
			else if (Line.StartsWith(TEXT("msgid ")))
			{
				Current = &Unit.Id;
				*Current = PoUnquote(Line.RightChop(6));
			}
			else if (Line.StartsWith(TEXT("msgstr ")))
			{
				Current = &Unit.Str;
				*Current = PoUnquote(Line.RightChop(7));
			}
			else if (Line.StartsWith(TEXT("\"")) && Current != nullptr)
			{
				*Current += PoUnquote(Line);
			}
			else
			{
				OutError = FString::Printf(TEXT("a line PO does not have: %s"), *Line.Left(60));
				return false;
			}
		}
		Flush();
		if (!bAny && OutBatch.Culture.IsEmpty())
		{
			OutError = TEXT("no entries");
			return false;
		}
		return true;
	}

	// ---- CSV (RFC 4180, UTF-8 with a byte order mark so spreadsheets read it as UTF-8)

	FString CsvField(const FString& InText)
	{
		if (InText.Contains(TEXT(",")) || InText.Contains(TEXT("\"")) || InText.Contains(TEXT("\n")) || InText.Contains(TEXT("\r"))
			|| (InText.Len() > 0 && (FChar::IsWhitespace(InText[0]) || FChar::IsWhitespace(InText[InText.Len() - 1]))))
		{
			return TEXT("\"") + InText.Replace(TEXT("\""), TEXT("\"\"")) + TEXT("\"");
		}
		return InText;
	}

	const TArray<FString> CsvColumns = { TEXT("Namespace"), TEXT("Key"), TEXT("Source"), TEXT("Translation"), TEXT("State"), TEXT("Note"),
		TEXT("Max"), TEXT("File"), TEXT("At"), TEXT("Prev"), TEXT("PreviousTranslation"), TEXT("Kind"), TEXT("Context"), TEXT("SourceCulture"), TEXT("Culture") };

	FString WriteCsv(const FDreamLocExchangeBatch& InBatch)
	{
		FString Out = TEXT("\xFEFF") + FString::Join(CsvColumns, TEXT(",")) + TEXT("\r\n");
		for (const FDreamLocExchangeEntry& Entry : InBatch.Entries)
		{
			TArray<FString> Fields = { Entry.Namespace, Entry.Key, Entry.Source, Entry.Text, Entry.State, Entry.Note,
				Entry.Max >= 0 ? FString::FromInt(Entry.Max) : FString(), Entry.File, Entry.At, Entry.Prev, Entry.PreviousText, Entry.Kind, ContextLine(Entry.Context),
				InBatch.SourceCulture, InBatch.Culture };
			for (FString& Field : Fields)
			{
				Field = CsvField(Field);
			}
			Out += FString::Join(Fields, TEXT(",")) + TEXT("\r\n");
		}
		return Out;
	}

	bool ReadCsvRows(const FString& InText, TArray<TArray<FString>>& OutRows, FString& OutError)
	{
		TArray<FString> Row;
		FString Field;
		bool bQuoted = false;
		bool bAfterQuote = false;
		int32 Index = InText.Len() > 0 && InText[0] == TEXT('\xFEFF') ? 1 : 0;
		for (; Index < InText.Len(); ++Index)
		{
			const TCHAR Char = InText[Index];
			if (bQuoted)
			{
				if (Char == TEXT('"'))
				{
					if (Index + 1 < InText.Len() && InText[Index + 1] == TEXT('"'))
					{
						Field.AppendChar(TEXT('"'));
						++Index;
					}
					else
					{
						bQuoted = false;
						bAfterQuote = true;
					}
				}
				else
				{
					Field.AppendChar(Char);
				}
				continue;
			}
			if (Char == TEXT('"') && Field.IsEmpty() && !bAfterQuote)
			{
				bQuoted = true;
			}
			else if (Char == TEXT(','))
			{
				Row.Add(MoveTemp(Field));
				Field.Reset();
				bAfterQuote = false;
			}
			else if (Char == TEXT('\n') || Char == TEXT('\r'))
			{
				if (Char == TEXT('\r') && Index + 1 < InText.Len() && InText[Index + 1] == TEXT('\n'))
				{
					++Index;
				}
				Row.Add(MoveTemp(Field));
				Field.Reset();
				bAfterQuote = false;
				if (!(Row.Num() == 1 && Row[0].IsEmpty()))
				{
					OutRows.Add(MoveTemp(Row));
				}
				Row.Reset();
			}
			else
			{
				Field.AppendChar(Char);
			}
		}
		if (bQuoted)
		{
			OutError = TEXT("a quoted field is not closed");
			return false;
		}
		if (!Field.IsEmpty() || Row.Num() > 0)
		{
			Row.Add(MoveTemp(Field));
			OutRows.Add(MoveTemp(Row));
		}
		return true;
	}

	bool ReadCsv(const FString& InText, FDreamLocExchangeBatch& OutBatch, FString& OutError)
	{
		TArray<TArray<FString>> Rows;
		if (!ReadCsvRows(InText, Rows, OutError))
		{
			return false;
		}
		if (Rows.Num() == 0)
		{
			OutError = TEXT("no header row");
			return false;
		}
		TMap<FString, int32> Column;
		for (int32 Index = 0; Index < Rows[0].Num(); ++Index)
		{
			Column.Add(Rows[0][Index].TrimStartAndEnd(), Index);
		}
		for (const TCHAR* Required : { TEXT("Namespace"), TEXT("Key"), TEXT("Translation") })
		{
			if (!Column.Contains(Required))
			{
				OutError = FString::Printf(TEXT("no %s column"), Required);
				return false;
			}
		}
		for (int32 RowIndex = 1; RowIndex < Rows.Num(); ++RowIndex)
		{
			const TArray<FString>& Row = Rows[RowIndex];
			auto Get = [&Row, &Column](const TCHAR* InName)
			{
				const int32* Index = Column.Find(InName);
				return Index != nullptr && Row.IsValidIndex(*Index) ? Row[*Index] : FString();
			};
			FDreamLocExchangeEntry& Entry = OutBatch.Entries.AddDefaulted_GetRef();
			Entry.Namespace = Get(TEXT("Namespace"));
			Entry.Key = Get(TEXT("Key"));
			Entry.Source = Get(TEXT("Source"));
			Entry.Text = Get(TEXT("Translation"));
			Entry.State = Get(TEXT("State"));
			Entry.Note = Get(TEXT("Note"));
			Entry.Max = Get(TEXT("Max")).IsEmpty() ? -1 : FCString::Atoi(*Get(TEXT("Max")));
			Entry.File = Get(TEXT("File"));
			Entry.At = Get(TEXT("At"));
			Entry.Prev = Get(TEXT("Prev"));
			OutBatch.SourceCulture = Get(TEXT("SourceCulture")).IsEmpty() ? OutBatch.SourceCulture : Get(TEXT("SourceCulture"));
			OutBatch.Culture = Get(TEXT("Culture")).IsEmpty() ? OutBatch.Culture : Get(TEXT("Culture"));
		}
		return true;
	}

	FString IdOf(const FString& InNamespace, const FString& InKey)
	{
		return InNamespace + TEXT("\x01") + InKey;
	}
}

TOptional<EDreamLocExchangeFormat> DreamLocExchange::FormatOfPath(const FString& InPath)
{
	const FString Extension = FPaths::GetExtension(InPath).ToLower();
	if (Extension == TEXT("json")) { return EDreamLocExchangeFormat::Json; }
	if (Extension == TEXT("xlf") || Extension == TEXT("xliff")) { return EDreamLocExchangeFormat::Xliff; }
	if (Extension == TEXT("po") || Extension == TEXT("pot")) { return EDreamLocExchangeFormat::Po; }
	if (Extension == TEXT("csv")) { return EDreamLocExchangeFormat::Csv; }
	return TOptional<EDreamLocExchangeFormat>();
}

const TCHAR* DreamLocExchange::ExtensionOf(EDreamLocExchangeFormat InFormat)
{
	switch (InFormat)
	{
	case EDreamLocExchangeFormat::Xliff: return TEXT("xlf");
	case EDreamLocExchangeFormat::Po: return TEXT("po");
	case EDreamLocExchangeFormat::Csv: return TEXT("csv");
	default: return TEXT("json");
	}
}

FDreamLocExchangeBatch DreamLocExchange::Collect(const TMap<FString, TMap<FString, FString>>& InFiles, const FExportOptions& InOptions,
	const FString& InSourceCulture, TArray<FDreamLocReportLine>& OutMessages)
{
	using namespace DreamLocExchangeLocal;
	FDreamLocExchangeBatch Batch;
	Batch.SourceCulture = InSourceCulture;
	Batch.Culture = InOptions.Culture;

	auto Parse = [&OutMessages](const FString& InCulture, const FString& InPath, const FString& InText, FDreamLocDocument& OutDocument)
	{
		TArray<FDreamLocDiagnostic> Diagnostics;
		if (!DreamLocFile::Parse(InText, OutDocument, Diagnostics))
		{
			AddMessage(OutMessages, FString::Printf(TEXT("DLoc/%s/%s"), *InCulture, *InPath), EDreamLocSeverity::Warning, DreamLocCode::SyncBlocked,
				TEXT("the file does not read and is left out"));
			return false;
		}
		return true;
	};

	// What the source tools say about each text, by namespace and key.
	TMap<FString, const FDreamLocExtractEntry*> FromManifests;
	for (const FDreamLocExtractManifest& Manifest : InOptions.Manifests)
	{
		for (const FDreamLocExtractNamespace& Namespace : Manifest.Namespaces)
		{
			for (const FDreamLocExtractEntry& Extracted : Namespace.Entries)
			{
				FromManifests.FindOrAdd(IdOf(Namespace.Namespace, Extracted.Key), &Extracted);
			}
		}
	}

	// Other cultures' translations, by namespace and key.
	TMap<FString, TArray<TPair<FString, FString>>> References;
	for (const FString& Culture : InOptions.References)
	{
		const TMap<FString, FString>* Files = InFiles.Find(Culture);
		for (const TPair<FString, FString>& File : Files != nullptr ? *Files : TMap<FString, FString>())
		{
			FDreamLocDocument Document;
			if (FPaths::GetCleanFilename(File.Key).StartsWith(TEXT("_")) || !Parse(Culture, File.Key, File.Value, Document))
			{
				continue;
			}
			Document.ForEachEntry([&References, &Culture](const FString& InNamespace, const FDreamLocEntry& InEntry, bool)
			{
				if (InEntry.IsTranslated() && !InEntry.IsStale())
				{
					References.FindOrAdd(IdOf(InNamespace, InEntry.Key)).Emplace(Culture, *InEntry.Find(EDreamLocField::Text));
				}
			});
		}
	}

	const TMap<FString, FString>* Files = InFiles.Find(InOptions.Culture);
	if (Files == nullptr)
	{
		return Batch;
	}
	TArray<FString> Paths;
	Files->GetKeys(Paths);
	Paths.Sort();

	// The culture's terms first: every entry is matched against them.
	for (const FString& Path : Paths)
	{
		FDreamLocDocument Document;
		if (Parse(InOptions.Culture, Path, Files->FindChecked(Path), Document) && Document.bTerms)
		{
			Batch.Terms.AddFrom(Document);
		}
	}

	for (const FString& Path : Paths)
	{
		if (FPaths::GetCleanFilename(Path).StartsWith(TEXT("_")))
		{
			continue;
		}
		const FString Stem = FPaths::GetBaseFilename(Path, /*bRemovePath*/ false);
		if (InOptions.Files.Num() > 0 && !InOptions.Files.ContainsByPredicate([&Stem](const FString& InPrefix)
		{
			return Stem.StartsWith(FPaths::GetBaseFilename(InPrefix.Replace(TEXT("\\"), TEXT("/")), /*bRemovePath*/ false), ESearchCase::IgnoreCase);
		}))
		{
			continue;
		}
		FDreamLocDocument Document;
		if (!Parse(InOptions.Culture, Path, Files->FindChecked(Path), Document) || Document.bTerms)
		{
			continue;
		}
		TArray<TPair<FString, FDreamLocEntry>> Entries;
		Document.ForEachEntry([&Entries](const FString& InNamespace, const FDreamLocEntry& InEntry, bool)
		{
			if (InEntry.Has(EDreamLocField::Src))
			{
				Entries.Emplace(InNamespace, InEntry);
			}
		});
		for (int32 Index = 0; Index < Entries.Num(); ++Index)
		{
			const FDreamLocEntry& Source = Entries[Index].Value;
			if (!IsWanted(CategoryOf(Source), InOptions.Only))
			{
				continue;
			}
			FDreamLocExchangeEntry& Entry = Batch.Entries.AddDefaulted_GetRef();
			Entry.File = Path;
			Entry.Namespace = Entries[Index].Key;
			Entry.Key = Source.Key;
			Entry.Source = FieldOr(Source, EDreamLocField::Src);
			Entry.Text = FieldOr(Source, EDreamLocField::Text);
			Entry.State = FieldOr(Source, EDreamLocField::State);
			Entry.Note = FieldOr(Source, EDreamLocField::Note);
			Entry.Max = Source.GetMax();
			Entry.At = FieldOr(Source, EDreamLocField::At);
			Entry.Prev = FieldOr(Source, EDreamLocField::Prev);
			if (!Entry.Prev.IsEmpty())
			{
				Entry.PreviousText = MoveTemp(Entry.Text);
				Entry.Text.Reset();
				Entry.State.Reset();
			}
			for (int32 Near = FMath::Max(0, Index - 2); Near <= FMath::Min(Entries.Num() - 1, Index + 2); ++Near)
			{
				if (Near != Index)
				{
					Entry.Nearby.Add(FieldOr(Entries[Near].Value, EDreamLocField::Src));
				}
			}
			if (const FDreamLocExtractEntry* const* Extracted = FromManifests.Find(IdOf(Entry.Namespace, Entry.Key)))
			{
				Entry.Kind = (*Extracted)->Kind;
				Entry.Context = (*Extracted)->Context;
			}
			for (const TPair<FString, FString>& Term : Batch.Terms.Terms)
			{
				if (!Term.Key.IsEmpty() && Entry.Source.Contains(Term.Key, ESearchCase::CaseSensitive))
				{
					Entry.Terms.Add(Term);
				}
			}
			for (const FString& Keep : Batch.Terms.Keeps)
			{
				if (!Keep.IsEmpty() && Entry.Source.Contains(Keep, ESearchCase::CaseSensitive))
				{
					Entry.Keeps.Add(Keep);
				}
			}
			if (const TArray<TPair<FString, FString>>* Found = References.Find(IdOf(Entry.Namespace, Entry.Key)))
			{
				Entry.References = *Found;
			}
		}
	}
	return Batch;
}

FString DreamLocExchange::Write(const FDreamLocExchangeBatch& InBatch, EDreamLocExchangeFormat InFormat)
{
	using namespace DreamLocExchangeLocal;
	switch (InFormat)
	{
	case EDreamLocExchangeFormat::Xliff: return WriteXliff(InBatch);
	case EDreamLocExchangeFormat::Po: return WritePo(InBatch);
	case EDreamLocExchangeFormat::Csv: return WriteCsv(InBatch);
	default: return WriteJson(InBatch);
	}
}

bool DreamLocExchange::Read(const FString& InText, EDreamLocExchangeFormat InFormat, FDreamLocExchangeBatch& OutBatch, FString& OutError)
{
	using namespace DreamLocExchangeLocal;
	OutBatch = FDreamLocExchangeBatch();
	switch (InFormat)
	{
	case EDreamLocExchangeFormat::Xliff: return ReadXliff(InText, OutBatch, OutError);
	case EDreamLocExchangeFormat::Po: return ReadPo(InText, OutBatch, OutError);
	case EDreamLocExchangeFormat::Csv: return ReadCsv(InText, OutBatch, OutError);
	default: return ReadJson(InText, OutBatch, OutError);
	}
}

FDreamLocSyncOutput DreamLocExchange::Import(const FDreamLocExchangeBatch& InBatch, const TMap<FString, TMap<FString, FString>>& InFiles,
	const FImportOptions& InOptions, const FString& InDLocName)
{
	using namespace DreamLocExchangeLocal;
	FDreamLocSyncOutput Output;
	const FString Culture = InOptions.Culture.IsEmpty() ? InBatch.Culture : InOptions.Culture;
	const FString CultureName = InDLocName / Culture;
	if (Culture.IsEmpty())
	{
		Output.bBlocked = true;
		AddMessage(Output.Messages, InDLocName, EDreamLocSeverity::Error, DreamLocCode::ImportUnreadable, TEXT("the file names no culture; give one with -Culture"));
		return Output;
	}

	struct FFile
	{
		FString Path;
		FString Text;
		FDreamLocDocument Document;
		bool bChanged = false;
	};
	TArray<FFile> Files;
	TMap<FString, TPair<int32, int32>> Live;
	if (const TMap<FString, FString>* Existing = InFiles.Find(Culture))
	{
		TArray<FString> Paths;
		Existing->GetKeys(Paths);
		Paths.Sort();
		for (const FString& Path : Paths)
		{
			FFile File;
			File.Path = Path;
			File.Text = Existing->FindChecked(Path);
			TArray<FDreamLocDiagnostic> Diagnostics;
			if (FPaths::GetCleanFilename(Path).StartsWith(TEXT("_")))
			{
				continue;
			}
			if (!DreamLocFile::Parse(File.Text, File.Document, Diagnostics))
			{
				Output.bBlocked = true;
				AddMessage(Output.Messages, CultureName / Path, EDreamLocSeverity::Error, DreamLocCode::SyncBlocked, TEXT("the file does not read; nothing was imported"));
				continue;
			}
			const int32 FileIndex = Files.Add(MoveTemp(File));
			FString Namespace;
			const TArray<FDreamLocItem>& Items = Files[FileIndex].Document.Items;
			for (int32 ItemIndex = 0; ItemIndex < Items.Num(); ++ItemIndex)
			{
				if (Items[ItemIndex].Kind == EDreamLocItemKind::Namespace)
				{
					Namespace = Items[ItemIndex].Namespace;
				}
				else if (Items[ItemIndex].Kind == EDreamLocItemKind::Entry && !Live.Contains(IdOf(Namespace, Items[ItemIndex].Entry.Key)))
				{
					Live.Add(IdOf(Namespace, Items[ItemIndex].Entry.Key), TPair<int32, int32>(FileIndex, ItemIndex));
				}
			}
		}
	}
	if (Output.bBlocked)
	{
		return Output;
	}

	int32 Imported = 0;
	int32 Stale = 0;
	int32 Kept = 0;
	TArray<FString> Unknown;
	for (const FDreamLocExchangeEntry& Entry : InBatch.Entries)
	{
		if (Entry.Text.IsEmpty())
		{
			continue;
		}
		const TPair<int32, int32>* Where = Live.Find(IdOf(Entry.Namespace, Entry.Key));
		if (Where == nullptr)
		{
			Unknown.Add(FString::Printf(TEXT("'%s' (namespace %s)"), *Entry.Key, *Entry.Namespace));
			continue;
		}
		FFile& File = Files[Where->Key];
		FDreamLocEntry& Target = File.Document.Items[Where->Value].Entry;
		const FString State = !InOptions.State.IsEmpty() ? InOptions.State : Entry.State;
		const FString* CurrentSource = Target.Find(EDreamLocField::Src);
		// Translated from the source the export had; when the source has changed since, the translation is stale.
		const bool bMadeFromCurrent = Entry.Source.IsEmpty() || CurrentSource == nullptr || CurrentSource->Equals(Entry.Source, ESearchCase::CaseSensitive);
		if (Target.IsTranslated() && Target.GetState() == EDreamLocState::Done && !InOptions.bForce
			&& !(Target.Find(EDreamLocField::Text)->Equals(Entry.Text, ESearchCase::CaseSensitive) && bMadeFromCurrent))
		{
			++Kept;
			continue;
		}
		const FString Before = DreamLocFile::Write(File.Document);
		Target.Set(EDreamLocField::Text, Entry.Text);
		if (State.IsEmpty())
		{
			Target.Remove(EDreamLocField::State);
		}
		else
		{
			Target.Set(EDreamLocField::State, State);
		}
		if (bMadeFromCurrent)
		{
			Target.Remove(EDreamLocField::Prev);
		}
		else
		{
			Target.Set(EDreamLocField::Prev, Entry.Source);
			++Stale;
		}
		if (DreamLocFile::Write(File.Document) != Before)
		{
			File.bChanged = true;
			++Imported;
		}
	}

	for (const FFile& File : Files)
	{
		if (File.bChanged)
		{
			Output.Changes.FindOrAdd(Culture).Add(File.Path, DreamLocFile::Write(File.Document));
		}
	}
	Output.Added = Imported;
	Output.Outdated = Stale;
	AddMessage(Output.Messages, CultureName, EDreamLocSeverity::Info, DreamLocCode::Imported, FString::Printf(
		TEXT("%d translation(s) imported into %d file(s), %d of them stale (made from an older source text)"), Imported, Output.NumWrites(), Stale));
	if (Kept > 0)
	{
		AddMessage(Output.Messages, CultureName, EDreamLocSeverity::Info, DreamLocCode::ImportKept, FString::Printf(
			TEXT("%d translation(s) left out: the entry has a reviewed one (state done); -Force replaces it"), Kept));
	}
	if (Unknown.Num() > 0)
	{
		TArray<FString> Shown;
		for (int32 Index = 0; Index < Unknown.Num() && Index < 5; ++Index)
		{
			Shown.Add(Unknown[Index]);
		}
		AddMessage(Output.Messages, CultureName, EDreamLocSeverity::Warning, DreamLocCode::ImportUnknownKey, FString::Printf(
			TEXT("%d translation(s) of keys no .dloc of %s has were left out (dloc sync makes the entries): %s%s"), Unknown.Num(), *Culture,
			*FString::Join(Shown, TEXT(", ")), Unknown.Num() > Shown.Num() ? TEXT(", ...") : TEXT("")));
	}
	return Output;
}
