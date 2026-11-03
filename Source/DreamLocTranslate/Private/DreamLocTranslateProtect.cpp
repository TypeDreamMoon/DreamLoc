// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocTranslateProtect.h"

namespace DreamLocTranslateProtectLocal
{
	using DreamLocTranslateProtect::EStyle;

	struct FSpan
	{
		int32 Start = 0;
		int32 Length = 0;
		FString Back;
	};

	FString Escape(const FString& InText, EStyle InStyle)
	{
		if (InStyle == EStyle::Brackets)
		{
			return InText;
		}
		FString Out = InText.Replace(TEXT("&"), TEXT("&amp;")).Replace(TEXT("<"), TEXT("&lt;")).Replace(TEXT(">"), TEXT("&gt;"));
		return InStyle == EStyle::Html ? Out.Replace(TEXT("\""), TEXT("&quot;")) : Out;
	}

	FString Unescape(const FString& InText, EStyle InStyle)
	{
		if (InStyle == EStyle::Brackets)
		{
			return InText;
		}
		FString Out;
		for (int32 Index = 0; Index < InText.Len(); ++Index)
		{
			int32 End = INDEX_NONE;
			if (InText[Index] != TEXT('&') || !InText.Mid(Index).FindChar(TEXT(';'), End) || End > 10)
			{
				Out.AppendChar(InText[Index]);
				continue;
			}
			const FString Name = InText.Mid(Index + 1, End - 1);
			TCHAR Char = 0;
			if (Name == TEXT("amp")) { Char = TEXT('&'); }
			else if (Name == TEXT("lt")) { Char = TEXT('<'); }
			else if (Name == TEXT("gt")) { Char = TEXT('>'); }
			else if (Name == TEXT("quot")) { Char = TEXT('"'); }
			else if (Name == TEXT("apos")) { Char = TEXT('\''); }
			else if (Name.StartsWith(TEXT("#x")) || Name.StartsWith(TEXT("#X"))) { Char = TCHAR(FCString::Strtoi(*Name.RightChop(2), nullptr, 16)); }
			else if (Name.StartsWith(TEXT("#"))) { Char = TCHAR(FCString::Atoi(*Name.RightChop(1))); }
			if (Char == 0)
			{
				Out.AppendChar(InText[Index]);
				continue;
			}
			Out.AppendChar(Char);
			Index += End;
		}
		return Out;
	}

	FString Placeholder(int32 InIndex, EStyle InStyle)
	{
		switch (InStyle)
		{
		case EStyle::Xml: return FString::Printf(TEXT("<x i=\"%d\"/>"), InIndex);
		case EStyle::Html: return FString::Printf(TEXT("<span translate=\"no\">%d</span>"), InIndex);
		default: return FString::Printf(TEXT("⟦%d⟧"), InIndex);
		}
	}

	bool Overlaps(const TArray<FSpan>& InSpans, int32 InStart, int32 InLength)
	{
		return InSpans.ContainsByPredicate([InStart, InLength](const FSpan& InSpan)
		{
			return InStart < InSpan.Start + InSpan.Length && InSpan.Start < InStart + InLength;
		});
	}

	/** A placeholder in the answer: where it starts and ends, and its number (-1 for one changed past reading). */
	struct FFound
	{
		int32 Start = 0;
		int32 End = 0;
		int32 Index = -1;
	};

	int32 ReadNumber(const FString& InText, int32& InOutAt)
	{
		while (InOutAt < InText.Len() && FChar::IsWhitespace(InText[InOutAt]))
		{
			++InOutAt;
		}
		const int32 Begin = InOutAt;
		while (InOutAt < InText.Len() && FChar::IsDigit(InText[InOutAt]))
		{
			++InOutAt;
		}
		const int32 Number = InOutAt > Begin ? FCString::Atoi(*InText.Mid(Begin, InOutAt - Begin)) : -1;
		while (InOutAt < InText.Len() && FChar::IsWhitespace(InText[InOutAt]))
		{
			++InOutAt;
		}
		return Number;
	}

	TArray<FFound> FindPlaceholders(const FString& InText, EStyle InStyle)
	{
		TArray<FFound> Found;
		for (int32 Index = 0; Index < InText.Len(); ++Index)
		{
			if (InStyle == EStyle::Brackets && InText[Index] == TEXT('⟦'))
			{
				int32 At = Index + 1;
				const int32 Number = ReadNumber(InText, At);
				if (At < InText.Len() && InText[At] == TEXT('⟧'))
				{
					Found.Add({ Index, At + 1, Number });
					Index = At;
				}
			}
			else if (InStyle == EStyle::Xml && InText.Mid(Index, 3) == TEXT("<x "))
			{
				int32 Close = InText.Find(TEXT(">"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Index);
				if (Close == INDEX_NONE)
				{
					continue;
				}
				const FString Tag = InText.Mid(Index, Close - Index + 1);
				int32 Quote = Tag.Find(TEXT("i=\""));
				int32 At = Quote == INDEX_NONE ? 0 : Quote + 3;
				const int32 Number = Quote == INDEX_NONE ? -1 : ReadNumber(Tag, At);
				int32 End = Close + 1;
				if (!Tag.EndsWith(TEXT("/>")) && InText.Mid(End, 4) == TEXT("</x>"))
				{
					End += 4;
				}
				Found.Add({ Index, End, Number });
				Index = End - 1;
			}
			else if (InStyle == EStyle::Html && InText.Mid(Index, 5) == TEXT("<span"))
			{
				const int32 Open = InText.Find(TEXT(">"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Index);
				const int32 Close = Open == INDEX_NONE ? INDEX_NONE : InText.Find(TEXT("</span>"), ESearchCase::IgnoreCase, ESearchDir::FromStart, Open);
				if (Close == INDEX_NONE)
				{
					continue;
				}
				int32 At = Open + 1;
				const int32 Number = ReadNumber(InText, At);
				Found.Add({ Index, Close + 7, At == Close ? Number : -1 });
				Index = Close + 6;
			}
		}
		return Found;
	}
}

DreamLocTranslateProtect::FProtected DreamLocTranslateProtect::Protect(const FString& InSource, TConstArrayView<TPair<FString, FString>> InTerms,
	TConstArrayView<FString> InKeeps, EStyle InStyle)
{
	using namespace DreamLocTranslateProtectLocal;
	FProtected Out;
	int32 Begin = 0;
	int32 End = InSource.Len();
	while (Begin < End && FChar::IsWhitespace(InSource[Begin]))
	{
		++Begin;
	}
	while (End > Begin && FChar::IsWhitespace(InSource[End - 1]))
	{
		--End;
	}
	Out.Leading = InSource.Left(Begin);
	Out.Trailing = InSource.RightChop(End);
	const FString Body = InSource.Mid(Begin, End - Begin);

	// What the engine reads: escapes, arguments, rich-text tags, line breaks.
	TArray<FSpan> Spans;
	for (int32 Index = 0; Index < Body.Len(); ++Index)
	{
		const TCHAR Char = Body[Index];
		if (Char == TEXT('`') && Index + 1 < Body.Len() && (Body[Index + 1] == TEXT('{') || Body[Index + 1] == TEXT('}') || Body[Index + 1] == TEXT('`')))
		{
			Spans.Add({ Index, 2, Body.Mid(Index, 2) });
			++Index;
		}
		else if (Char == TEXT('{'))
		{
			const int32 Close = Body.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Index);
			if (Close != INDEX_NONE)
			{
				Spans.Add({ Index, Close - Index + 1, Body.Mid(Index, Close - Index + 1) });
				Index = Close;
			}
		}
		else if (Char == TEXT('<') && Index + 1 < Body.Len() && (FChar::IsAlpha(Body[Index + 1]) || Body[Index + 1] == TEXT('/')))
		{
			const int32 Close = Body.Find(TEXT(">"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Index);
			if (Close != INDEX_NONE)
			{
				Spans.Add({ Index, Close - Index + 1, Body.Mid(Index, Close - Index + 1) });
				Index = Close;
			}
		}
		else if (Char == TEXT('\r') || Char == TEXT('\n'))
		{
			const int32 Length = Char == TEXT('\r') && Index + 1 < Body.Len() && Body[Index + 1] == TEXT('\n') ? 2 : 1;
			Spans.Add({ Index, Length, Body.Mid(Index, Length) });
			Index += Length - 1;
		}
	}
	// The words kept and the terms, longest first, wherever nothing above already is.
	TArray<TPair<FString, FString>> Words;
	for (const FString& Keep : InKeeps)
	{
		Words.Emplace(Keep, Keep);
	}
	Words.Append(InTerms.GetData(), InTerms.Num());
	Words.Sort([](const TPair<FString, FString>& A, const TPair<FString, FString>& B) { return A.Key.Len() > B.Key.Len(); });
	for (const TPair<FString, FString>& Word : Words)
	{
		if (Word.Key.IsEmpty())
		{
			continue;
		}
		for (int32 At = Body.Find(Word.Key, ESearchCase::CaseSensitive); At != INDEX_NONE;
			At = Body.Find(Word.Key, ESearchCase::CaseSensitive, ESearchDir::FromStart, At + Word.Key.Len()))
		{
			if (!Overlaps(Spans, At, Word.Key.Len()))
			{
				Spans.Add({ At, Word.Key.Len(), Word.Value });
			}
		}
	}
	Spans.Sort([](const FSpan& A, const FSpan& B) { return A.Start < B.Start; });

	int32 Cursor = 0;
	for (const FSpan& Span : Spans)
	{
		Out.Text += Escape(Body.Mid(Cursor, Span.Start - Cursor), InStyle);
		Out.Text += Placeholder(Out.Pieces.Num(), InStyle);
		Out.Pieces.Add(Span.Back);
		Cursor = Span.Start + Span.Length;
	}
	Out.Text += Escape(Body.Mid(Cursor), InStyle);
	return Out;
}

bool DreamLocTranslateProtect::Restore(const FString& InTranslated, const FProtected& InProtected, EStyle InStyle, FString& OutText, FString& OutError)
{
	using namespace DreamLocTranslateProtectLocal;
	const FString Translated = InTranslated.TrimStartAndEnd();
	const TArray<FFound> Found = FindPlaceholders(Translated, InStyle);
	TArray<int32> Seen;
	Seen.SetNumZeroed(InProtected.Pieces.Num());
	for (const FFound& Placeholder : Found)
	{
		if (!Seen.IsValidIndex(Placeholder.Index))
		{
			OutError = TEXT("the service changed a placeholder");
			return false;
		}
		++Seen[Placeholder.Index];
	}
	for (int32 Index = 0; Index < Seen.Num(); ++Index)
	{
		if (Seen[Index] != 1)
		{
			OutError = FString::Printf(TEXT("the service %s placeholder %d (%s)"), Seen[Index] == 0 ? TEXT("lost") : TEXT("repeated"), Index, *InProtected.Pieces[Index]);
			return false;
		}
	}
	FString Body;
	int32 Cursor = 0;
	for (const FFound& Placeholder : Found)
	{
		Body += Unescape(Translated.Mid(Cursor, Placeholder.Start - Cursor), InStyle);
		Body += InProtected.Pieces[Placeholder.Index];
		Cursor = Placeholder.End;
	}
	Body += Unescape(Translated.Mid(Cursor), InStyle);
	OutText = InProtected.Leading + Body + InProtected.Trailing;
	return true;
}
