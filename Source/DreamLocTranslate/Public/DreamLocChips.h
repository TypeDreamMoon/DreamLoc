// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateColor.h"
#include "Styling/SlateTypes.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SLeafWidget.h"

class ITextLayoutMarshaller;

/**
 * The placeholders of a text -- {arguments}, the backtick escapes, rich-text tags, line breaks -- shown as chips: what a
 * translator keeps whole, moves or deletes, but never edits inside. Only the display: the text itself is what it is.
 */
namespace DreamLocChips
{
	enum class EKind : uint8
	{
		Text,
		Argument,
		Escape,
		Tag,
		LineBreak,
	};

	struct FPiece
	{
		EKind Kind = EKind::Text;
		int32 Start = 0;
		int32 Length = 0;

		int32 End() const { return Start + Length; }
		bool IsChip() const { return Kind != EKind::Text; }
	};

	/** The text cut into plain runs and placeholders, in order, covering all of it. */
	DREAMLOCTRANSLATE_API TArray<FPiece> Split(const FString& InText);

	/** The placeholders alone, in order: `{Name}`, `<b>`, `\n`... */
	DREAMLOCTRANSLATE_API TArray<FString> Placeholders(const FString& InText);

	/** The first placeholder of the source that the translation has fewer of: what F3 inserts. Unset when none is missing. */
	DREAMLOCTRANSLATE_API TOptional<FString> NextMissing(const FString& InSource, const FString& InTranslation);

	/**
	 * An edit from InBefore to InAfter that cut into a placeholder, made whole: a deletion that took part of one takes all
	 * of it, a character typed inside one goes after it. False when the edit left every placeholder whole; else the text
	 * as it should be and where the cursor goes (an offset into it).
	 */
	DREAMLOCTRANSLATE_API bool FixEdit(const FString& InBefore, const FString& InAfter, FString& OutText, int32& OutCursor);

	/** A text offset as a line and a column, for a multi-line box. */
	DREAMLOCTRANSLATE_API void ToLineAndColumn(const FString& InText, int32 InOffset, int32& OutLine, int32& OutColumn);

	/** For an SMultiLineEditableText: the placeholders drawn as chips (a line break stays a line). */
	DREAMLOCTRANSLATE_API TSharedRef<ITextLayoutMarshaller> MakeMarshaller(const FTextBlockStyle& InStyle);

	/** What a chip's colour is, by kind. */
	DREAMLOCTRANSLATE_API FLinearColor ColorOf(EKind InKind);
}

/** How a piece of a shown text differs from an older text: the same, taken out, or put in. */
enum class EDreamLocDiffMark : uint8
{
	Same,
	Removed,
	Added,
};

/**
 * A read-only text with its placeholders as chips: on one line (a line break a chip too, the rest cut off at the edge)
 * or on as many lines as it has. Given an older text, it shows what changed: taken out struck through, put in marked.
 */
class DREAMLOCTRANSLATE_API SDreamLocChipText : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SDreamLocChipText)
		: _MultiLine(false)
		, _ColorAndOpacity(FSlateColor::UseForeground())
	{}
		SLATE_ATTRIBUTE(FString, Text)
		/** When set and not empty: the text this one replaced, to show the difference. */
		SLATE_ATTRIBUTE(FString, OldText)
		SLATE_ARGUMENT(bool, MultiLine)
		SLATE_ATTRIBUTE(FSlateColor, ColorAndOpacity)
		/** The font; the editor's normal font when unset. */
		SLATE_ARGUMENT(TOptional<FSlateFontInfo>, Font)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

	/** What is shown, as text: the chips by what they stand for, for a test to read. */
	FString GetShownText() const;

private:
	struct FSegment
	{
		FString Text;
		DreamLocChips::EKind Kind = DreamLocChips::EKind::Text;
		EDreamLocDiffMark Mark = EDreamLocDiffMark::Same;
	};

	/** The segments of each line, rebuilt when the text changes. */
	const TArray<TArray<FSegment>>& GetLines() const;
	float WidthOf(const FSegment& InSegment) const;

	TAttribute<FString> Text;
	TAttribute<FString> OldText;
	TAttribute<FSlateColor> ColorAndOpacity;
	bool bMultiLine = false;
	FSlateFontInfo Font;
	FSlateFontInfo ChipFont;

	mutable FString CachedText;
	mutable FString CachedOldText;
	mutable bool bCached = false;
	mutable TArray<TArray<FSegment>> Lines;
};
