// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * What a machine translator must not touch, taken out before a text is sent and put back after: {arguments}, the
 * backtick escapes, rich-text tags, line breaks, the words kept as they are and the terms (which come back as the term
 * list's translation, since the service does not know it). Blanks at either end are stripped and put back.
 *
 * Each piece becomes a placeholder the service leaves alone: DeepL an XML tag it ignores, Google a span marked
 * translate="no", the others ⟦n⟧. A placeholder that comes back missing, twice or changed fails the text.
 */
namespace DreamLocTranslateProtect
{
	enum class EStyle : uint8
	{
		/** `<x i="0"/>`, the text XML-escaped: DeepL with tag_handling=xml and ignore_tags=x. */
		Xml,
		/** `<span translate="no">0</span>`, the text HTML-escaped: Google with format=html. */
		Html,
		/** ⟦0⟧: Baidu, Tencent, Youdao. */
		Brackets,
	};

	struct FProtected
	{
		/** What is sent. */
		FString Text;
		FString Leading;
		FString Trailing;
		/** Placeholder n -> what it puts back. */
		TArray<FString> Pieces;
	};

	DREAMLOCTRANSLATE_API FProtected Protect(const FString& InSource, TConstArrayView<TPair<FString, FString>> InTerms, TConstArrayView<FString> InKeeps,
		EStyle InStyle);

	/** The translation with every piece back; false with OutError when a placeholder is missing, repeated or changed. */
	DREAMLOCTRANSLATE_API bool Restore(const FString& InTranslated, const FProtected& InProtected, EStyle InStyle, FString& OutText, FString& OutError);
}
