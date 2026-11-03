// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Features/IModularFeature.h"

class SWidget;

/** A text a tool draws itself, found at a point of the screen. */
struct FDreamLocLocatedText
{
	/** The text's namespace and key, as DreamLoc's files have them; empty when it has none. */
	FString Namespace;
	FString Key;
	/** Why it has none: built from other texts, not localizable...; shown at the cursor. */
	FString WhyNot;
	/** Where it is drawn, in desktop pixels. */
	FBox2D Rect = FBox2D(ForceInit);
};

/**
 * What a tool that draws its own texts -- DreamGUI, whose texts are no Slate text widgets -- tells DreamLoc's in-context
 * pick (Alt+click a text in the editor or in PIE, and the translation window opens at it): which of its texts is at a
 * point. Registered as a modular feature under GetModularFeatureName(); asked before the Slate text widgets are looked at.
 */
class IDreamLocTextLocator : public IModularFeature
{
public:
	static FName GetModularFeatureName()
	{
		static const FName Name(TEXT("DreamLocTextLocator"));
		return Name;
	}

	virtual ~IDreamLocTextLocator() = default;

	/**
	 * The text this tool draws at InScreenPosition (desktop pixels). InWidgets are the widgets under the point, the window
	 * first: where to look for the tool's own canvas. False when it draws no text there.
	 */
	virtual bool LocateText(TConstArrayView<TSharedRef<SWidget>> InWidgets, const FVector2D& InScreenPosition, FDreamLocLocatedText& OutText) = 0;
};
