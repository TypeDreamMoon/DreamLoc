// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocChips.h"

#include "DreamLocTextDiff.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Text/BaseTextLayoutMarshaller.h"
#include "Framework/Text/SlateTextRun.h"
#include "Framework/Text/TextLayout.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"

namespace DreamLocChipsLocal
{
	using namespace DreamLocChips;

	/** A chip's label: what it stands for, a line break as \n. */
	FString LabelOf(const FString& InText, EKind InKind)
	{
		return InKind == EKind::LineBreak ? FString(TEXT("\\n")) : InText;
	}

	const FSlateBrush* ChipBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::White, 3.0f);
		return &Brush;
	}

	const FSlateBrush* WhiteBrush()
	{
		return FAppStyle::GetBrush(TEXT("WhiteBrush"));
	}

	/** A placeholder in an editable text: its text, on a chip. */
	class FChipRun : public FSlateTextRun
	{
	public:
		static TSharedRef<FChipRun> Create(const TSharedRef<const FString>& InText, const FTextBlockStyle& InStyle, const FTextRange& InRange, EKind InKind)
		{
			return MakeShareable(new FChipRun(InText, InStyle, InRange, InKind));
		}

		virtual int32 OnPaint(const FPaintArgs& PaintArgs, const FTextArgs& TextArgs, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
			FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
		{
			const TSharedRef<ILayoutBlock>& Block = TextArgs.Block;
			const float InverseScale = Inverse(AllottedGeometry.Scale);
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
				AllottedGeometry.ToPaintGeometry(TransformVector(InverseScale, Block->GetSize()), FSlateLayoutTransform(TransformPoint(InverseScale, Block->GetLocationOffset()))),
				ChipBrush(), ESlateDrawEffect::None, ColorOf(Kind) * InWidgetStyle.GetColorAndOpacityTint());
			return FSlateTextRun::OnPaint(PaintArgs, TextArgs, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId + 1, InWidgetStyle, bParentEnabled);
		}

	private:
		FChipRun(const TSharedRef<const FString>& InText, const FTextBlockStyle& InStyle, const FTextRange& InRange, EKind InKind)
			: FSlateTextRun(FRunInfo(), InText, InStyle, InRange)
			, Kind(InKind)
		{
		}

		EKind Kind;
	};

	class FChipMarshaller : public FBaseTextLayoutMarshaller
	{
	public:
		explicit FChipMarshaller(const FTextBlockStyle& InStyle) : Style(InStyle) {}

		virtual void SetText(const FString& InSource, FTextLayout& InOutLayout) override
		{
			TArray<FTextLayout::FNewLineData> NewLines;
			TArray<FString> Lines;
			InSource.Replace(TEXT("\r\n"), TEXT("\n")).ParseIntoArray(Lines, TEXT("\n"), /*InCullEmpty*/ false);
			if (Lines.IsEmpty())
			{
				Lines.Add(FString());
			}
			for (const FString& Line : Lines)
			{
				const TSharedRef<FString> LineText = MakeShared<FString>(Line);
				TArray<TSharedRef<IRun>> Runs;
				for (const FPiece& Piece : Split(Line))
				{
					const FTextRange Range(Piece.Start, Piece.End());
					if (Piece.IsChip())
					{
						Runs.Add(FChipRun::Create(LineText, Style, Range, Piece.Kind));
					}
					else
					{
						Runs.Add(FSlateTextRun::Create(FRunInfo(), LineText, Style, Range));
					}
				}
				if (Runs.IsEmpty())
				{
					Runs.Add(FSlateTextRun::Create(FRunInfo(), LineText, Style, FTextRange(0, 0)));
				}
				NewLines.Emplace(LineText, MoveTemp(Runs));
			}
			InOutLayout.AddLines(NewLines);
		}

		virtual void GetText(FString& OutTarget, const FTextLayout& InLayout) override
		{
			InLayout.GetAsText(OutTarget);
		}

		/** Tokenized again as it is typed, so a placeholder typed in becomes a chip. */
		virtual bool RequiresLiveUpdate() const override { return true; }

	private:
		FTextBlockStyle Style;
	};
}

TArray<DreamLocChips::FPiece> DreamLocChips::Split(const FString& InText)
{
	TArray<FPiece> Out;
	int32 TextStart = 0;
	auto Flush = [&Out, &TextStart](int32 InEnd)
	{
		if (InEnd > TextStart)
		{
			Out.Add({ EKind::Text, TextStart, InEnd - TextStart });
		}
	};
	for (int32 Index = 0; Index < InText.Len(); ++Index)
	{
		const TCHAR Char = InText[Index];
		FPiece Chip;
		if (Char == TEXT('`') && Index + 1 < InText.Len() && (InText[Index + 1] == TEXT('{') || InText[Index + 1] == TEXT('}') || InText[Index + 1] == TEXT('`')))
		{
			Chip = { EKind::Escape, Index, 2 };
		}
		else if (Char == TEXT('{'))
		{
			const int32 Close = InText.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Index);
			if (Close == INDEX_NONE)
			{
				continue;
			}
			Chip = { EKind::Argument, Index, Close - Index + 1 };
		}
		else if (Char == TEXT('<') && Index + 1 < InText.Len() && (FChar::IsAlpha(InText[Index + 1]) || InText[Index + 1] == TEXT('/')))
		{
			const int32 Close = InText.Find(TEXT(">"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Index);
			if (Close == INDEX_NONE)
			{
				continue;
			}
			Chip = { EKind::Tag, Index, Close - Index + 1 };
		}
		else if (Char == TEXT('\r') || Char == TEXT('\n'))
		{
			Chip = { EKind::LineBreak, Index, Char == TEXT('\r') && Index + 1 < InText.Len() && InText[Index + 1] == TEXT('\n') ? 2 : 1 };
		}
		else
		{
			continue;
		}
		Flush(Index);
		Out.Add(Chip);
		Index = Chip.End() - 1;
		TextStart = Chip.End();
	}
	Flush(InText.Len());
	return Out;
}

TArray<FString> DreamLocChips::Placeholders(const FString& InText)
{
	TArray<FString> Out;
	for (const FPiece& Piece : Split(InText))
	{
		if (Piece.IsChip())
		{
			Out.Add(Piece.Kind == EKind::LineBreak ? FString(TEXT("\n")) : InText.Mid(Piece.Start, Piece.Length));
		}
	}
	return Out;
}

TOptional<FString> DreamLocChips::NextMissing(const FString& InSource, const FString& InTranslation)
{
	TMap<FString, int32> Have;
	for (const FString& Placeholder : Placeholders(InTranslation))
	{
		++Have.FindOrAdd(Placeholder);
	}
	for (const FString& Placeholder : Placeholders(InSource))
	{
		int32* Count = Have.Find(Placeholder);
		if (Count == nullptr || *Count == 0)
		{
			return Placeholder;
		}
		--*Count;
	}
	return TOptional<FString>();
}

bool DreamLocChips::FixEdit(const FString& InBefore, const FString& InAfter, FString& OutText, int32& OutCursor)
{
	if (InBefore.Equals(InAfter, ESearchCase::CaseSensitive))
	{
		return false;
	}
	int32 Prefix = 0;
	const int32 Shorter = FMath::Min(InBefore.Len(), InAfter.Len());
	while (Prefix < Shorter && InBefore[Prefix] == InAfter[Prefix])
	{
		++Prefix;
	}
	int32 Suffix = 0;
	while (Suffix < Shorter - Prefix && InBefore[InBefore.Len() - 1 - Suffix] == InAfter[InAfter.Len() - 1 - Suffix])
	{
		++Suffix;
	}
	const int32 RemovedStart = Prefix;
	const int32 RemovedEnd = InBefore.Len() - Suffix;
	const FString Inserted = InAfter.Mid(Prefix, InAfter.Len() - Suffix - Prefix);
	const TArray<FPiece> Pieces = Split(InBefore);
	if (RemovedEnd == RemovedStart)
	{
		// Typed inside a placeholder: it goes after it.
		for (const FPiece& Piece : Pieces)
		{
			if (Piece.IsChip() && Piece.Kind != EKind::LineBreak && Piece.Start < RemovedStart && RemovedStart < Piece.End())
			{
				OutText = InBefore.Left(Piece.End()) + Inserted + InBefore.Mid(Piece.End());
				OutCursor = Piece.End() + Inserted.Len();
				return true;
			}
		}
		return false;
	}
	// Part of a placeholder taken away: all of it goes.
	int32 Start = RemovedStart;
	int32 End = RemovedEnd;
	for (const FPiece& Piece : Pieces)
	{
		if (Piece.IsChip() && Piece.Start < RemovedEnd && RemovedStart < Piece.End())
		{
			Start = FMath::Min(Start, Piece.Start);
			End = FMath::Max(End, Piece.End());
		}
	}
	if (Start == RemovedStart && End == RemovedEnd)
	{
		return false;
	}
	OutText = InBefore.Left(Start) + Inserted + InBefore.Mid(End);
	OutCursor = Start + Inserted.Len();
	return true;
}

void DreamLocChips::ToLineAndColumn(const FString& InText, int32 InOffset, int32& OutLine, int32& OutColumn)
{
	OutLine = 0;
	OutColumn = 0;
	for (int32 Index = 0; Index < FMath::Min(InOffset, InText.Len()); ++Index)
	{
		if (InText[Index] == TEXT('\n'))
		{
			++OutLine;
			OutColumn = 0;
		}
		else if (InText[Index] != TEXT('\r'))
		{
			++OutColumn;
		}
	}
}

TSharedRef<ITextLayoutMarshaller> DreamLocChips::MakeMarshaller(const FTextBlockStyle& InStyle)
{
	return MakeShared<DreamLocChipsLocal::FChipMarshaller>(InStyle);
}

FLinearColor DreamLocChips::ColorOf(EKind InKind)
{
	// Muted: a chip is read past, not looked at.
	switch (InKind)
	{
	case EKind::Argument: return FLinearColor(0.22f, 0.36f, 0.52f, 0.85f);
	case EKind::Tag: return FLinearColor(0.42f, 0.30f, 0.50f, 0.85f);
	case EKind::Escape: return FLinearColor(0.36f, 0.36f, 0.30f, 0.85f);
	case EKind::LineBreak: return FLinearColor(0.30f, 0.30f, 0.30f, 0.85f);
	default: return FLinearColor::Transparent;
	}
}

// ---------------------------------------------------------------------------------------------------------- the read-only text

void SDreamLocChipText::Construct(const FArguments& InArgs)
{
	Text = InArgs._Text;
	OldText = InArgs._OldText;
	ColorAndOpacity = InArgs._ColorAndOpacity;
	bMultiLine = InArgs._MultiLine;
	Font = InArgs._Font.IsSet() ? InArgs._Font.GetValue() : FAppStyle::GetFontStyle(TEXT("NormalFont"));
	ChipFont = Font;
	ChipFont.Size = FMath::Max(7.0f, Font.Size - 1.0f);
	SetClipping(EWidgetClipping::ClipToBounds);
}

const TArray<TArray<SDreamLocChipText::FSegment>>& SDreamLocChipText::GetLines() const
{
	const FString Now = Text.Get();
	const FString Old = OldText.Get();
	if (bCached && Now.Equals(CachedText, ESearchCase::CaseSensitive) && Old.Equals(CachedOldText, ESearchCase::CaseSensitive))
	{
		return Lines;
	}
	bCached = true;
	CachedText = Now;
	CachedOldText = Old;
	Lines.Reset();
	Lines.AddDefaulted();
	TArray<DreamLocTextDiff::FPart> Parts;
	if (Old.IsEmpty())
	{
		Parts.Add({ Now, EDreamLocDiffMark::Same });
	}
	else
	{
		Parts = DreamLocTextDiff::Diff(Old, Now);
	}
	for (const DreamLocTextDiff::FPart& Part : Parts)
	{
		for (const DreamLocChips::FPiece& Piece : DreamLocChips::Split(Part.Text))
		{
			FSegment Segment;
			Segment.Kind = Piece.Kind;
			Segment.Mark = Part.Mark;
			Segment.Text = Part.Text.Mid(Piece.Start, Piece.Length);
			if (Piece.Kind == DreamLocChips::EKind::LineBreak && bMultiLine)
			{
				// A line of its own: the chip ends this one.
				Lines.Last().Add(Segment);
				Lines.AddDefaulted();
				continue;
			}
			Lines.Last().Add(Segment);
		}
	}
	return Lines;
}

float SDreamLocChipText::WidthOf(const FSegment& InSegment) const
{
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	if (InSegment.Kind == DreamLocChips::EKind::Text)
	{
		return Measure->Measure(InSegment.Text, Font).X;
	}
	return Measure->Measure(DreamLocChipsLocal::LabelOf(InSegment.Text, InSegment.Kind), ChipFont).X + 6.0f;
}

FVector2D SDreamLocChipText::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const float LineHeight = Measure->GetMaxCharacterHeight(Font) + 2.0f;
	float Width = 0.0f;
	for (const TArray<FSegment>& Line : GetLines())
	{
		float LineWidth = 0.0f;
		for (const FSegment& Segment : Line)
		{
			LineWidth += WidthOf(Segment) + (Segment.Kind == DreamLocChips::EKind::Text ? 0.0f : 2.0f);
		}
		Width = FMath::Max(Width, LineWidth);
	}
	return FVector2D(Width, LineHeight * (bMultiLine ? GetLines().Num() : 1));
}

int32 SDreamLocChipText::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace DreamLocChipsLocal;
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const float LineHeight = Measure->GetMaxCharacterHeight(Font) + 2.0f;
	const float ChipHeight = Measure->GetMaxCharacterHeight(ChipFont);
	const FLinearColor TextColor = ColorAndOpacity.Get().GetColor(InWidgetStyle) * InWidgetStyle.GetColorAndOpacityTint();
	const ESlateDrawEffect Effect = bParentEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
	float Y = 0.0f;
	for (const TArray<FSegment>& Line : GetLines())
	{
		float X = 0.0f;
		for (const FSegment& Segment : Line)
		{
			if (X > AllottedGeometry.GetLocalSize().X)
			{
				break;
			}
			const float Width = WidthOf(Segment);
			const FVector2f Size(Width, LineHeight - 2.0f);
			if (Segment.Mark == EDreamLocDiffMark::Added)
			{
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(Size, FSlateLayoutTransform(FVector2f(X, Y))), WhiteBrush(), Effect,
					FLinearColor(0.25f, 0.45f, 0.25f, 0.55f));
			}
			if (Segment.Kind == DreamLocChips::EKind::Text)
			{
				FSlateDrawElement::MakeText(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(Size, FSlateLayoutTransform(FVector2f(X, Y))), Segment.Text, Font,
					Effect, Segment.Mark == EDreamLocDiffMark::Removed ? TextColor * FLinearColor(1.0f, 1.0f, 1.0f, 0.55f) : TextColor);
			}
			else
			{
				const FVector2f ChipSize(Width, ChipHeight + 2.0f);
				const FVector2f ChipAt(X, Y + (LineHeight - 2.0f - ChipSize.Y) * 0.5f);
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(ChipSize, FSlateLayoutTransform(ChipAt)), ChipBrush(), Effect,
					DreamLocChips::ColorOf(Segment.Kind) * (Segment.Mark == EDreamLocDiffMark::Removed ? 0.5f : 1.0f));
				FSlateDrawElement::MakeText(OutDrawElements, LayerId + 2, AllottedGeometry.ToPaintGeometry(ChipSize - FVector2f(6.0f, 0.0f), FSlateLayoutTransform(ChipAt + FVector2f(3.0f, 1.0f))),
					LabelOf(Segment.Text, Segment.Kind), ChipFont, Effect, FLinearColor(0.92f, 0.92f, 0.92f, 1.0f));
			}
			if (Segment.Mark == EDreamLocDiffMark::Removed)
			{
				// Struck through.
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 3, AllottedGeometry.ToPaintGeometry(FVector2f(Width, 1.0f), FSlateLayoutTransform(FVector2f(X, Y + (LineHeight - 2.0f) * 0.55f))),
					WhiteBrush(), Effect, FLinearColor(0.75f, 0.35f, 0.35f, 0.9f));
			}
			X += Width + (Segment.Kind == DreamLocChips::EKind::Text ? 0.0f : 2.0f);
		}
		Y += LineHeight;
		if (!bMultiLine)
		{
			break;
		}
	}
	return LayerId + 4;
}

FString SDreamLocChipText::GetShownText() const
{
	TArray<FString> Shown;
	for (const TArray<FSegment>& Line : GetLines())
	{
		FString One;
		for (const FSegment& Segment : Line)
		{
			const FString Piece = Segment.Kind == DreamLocChips::EKind::Text ? Segment.Text : TEXT("[") + DreamLocChipsLocal::LabelOf(Segment.Text, Segment.Kind) + TEXT("]");
			One += Segment.Mark == EDreamLocDiffMark::Removed ? TEXT("-(") + Piece + TEXT(")") : Segment.Mark == EDreamLocDiffMark::Added ? TEXT("+(") + Piece + TEXT(")") : Piece;
		}
		Shown.Add(One);
	}
	return FString::Join(Shown, TEXT("|"));
}
