// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class SWindow;

/**
 * In-context: hold Alt in the editor (a viewport, the UMG designer, DreamGUI's designer, PIE) and the text under the mouse
 * is outlined; Alt+click it and the translation window opens at its entry. A Slate text widget's namespace and key come
 * from the text itself (FTextInspector); a tool that draws its own texts says through IDreamLocTextLocator. A text without
 * a key, or one the .dloc files do not have, gets a line at the cursor saying why. Off with DreamLoc.InContext 0.
 */
namespace DreamLocInContext
{
	struct FFound
	{
		/** A text is there, with a key or without. */
		bool bText = false;
		/** Its namespace (a package's part stripped) and key; empty when it has none. */
		FString Namespace;
		FString Key;
		/** Why it has none. */
		FString WhyNot;
		/** Where it is, in desktop pixels. */
		FBox2D Rect = FBox2D(ForceInit);
		TWeakPtr<SWindow> Window;

		bool HasKey() const { return !Key.IsEmpty(); }
	};

	/** What a text says about itself: its namespace and key, or why it has none. */
	DREAMLOCTRANSLATE_API FFound Describe(const FText& InText);

	/** The text at a point of the desktop, for that user: a locator's (DreamGUI), else a Slate text widget's. */
	DREAMLOCTRANSLATE_API FFound FindAt(const FVector2D& InScreenPosition, int32 InUserIndex);

	/** What Alt has outlined; unset when nothing is. */
	DREAMLOCTRANSLATE_API TOptional<FFound> GetHighlighted();

	/** The line at the cursor (why a text could not be opened); empty when there is none. */
	DREAMLOCTRANSLATE_API FString GetHint();

	/** What Alt+click does with a text; null for the default, which opens the translation window at it. For a test. */
	DREAMLOCTRANSLATE_API void SetOnPick(TFunction<void(const FFound&)> InOnPick);

	/** The default pick: the translation window opened and the entry chosen, or the hint why not. */
	DREAMLOCTRANSLATE_API void Pick(const FFound& InFound);

	void Register();
	void Unregister();
}
