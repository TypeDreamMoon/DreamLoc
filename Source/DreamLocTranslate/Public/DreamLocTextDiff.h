// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamLocChips.h"

/**
 * What changed from an older text to a newer one, as a stale entry's source shows it: Chinese, Japanese and Korean by
 * character, other languages by word; a placeholder is one piece either way.
 */
namespace DreamLocTextDiff
{
	struct FPart
	{
		FString Text;
		EDreamLocDiffMark Mark = EDreamLocDiffMark::Same;
	};

	/** InOld to InNew in order: what both have, what only InOld had (Removed), what only InNew has (Added). */
	DREAMLOCTRANSLATE_API TArray<FPart> Diff(const FString& InOld, const FString& InNew);

	/** `same -(removed) +(added)`, for a test or a tooltip. */
	DREAMLOCTRANSLATE_API FString Describe(TConstArrayView<FPart> InParts);
}
