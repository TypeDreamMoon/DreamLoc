// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * What the .dloc files say about some texts, for a tool that shows them while they are edited: the DreamGUI designer
 * checking each language's translation against the widget, a hover in an editor. Reads the DLoc folders the game loads
 * (plugins, the project, mods), the highest layer's translation per culture, and keeps each file's reading until the file
 * changes.
 */
namespace DreamLocQuery
{
	struct FTranslation
	{
		FString Culture;
		FString Text;
		/** The .dloc it is in, absolute, and the line of its `text`. */
		FString File;
		int32 Line = 0;
		/** Made from an older source text (it has a `prev`): the game shows the source until it is translated again. */
		bool bStale = false;
	};

	/** Every culture's translation of each key of the namespace: key -> translations, by culture. Keys with none are left out. */
	DREAMLOCEDITOR_API TMap<FString, TArray<FTranslation>> FindTranslations(const FString& InNamespace, TConstArrayView<FString> InKeys);

	/** The same, from these DLoc folders (lowest layer first) instead of the ones the game loads. */
	DREAMLOCEDITOR_API TMap<FString, TArray<FTranslation>> FindTranslationsIn(TConstArrayView<FString> InDLocDirectories, const FString& InNamespace,
		TConstArrayView<FString> InKeys);
}
