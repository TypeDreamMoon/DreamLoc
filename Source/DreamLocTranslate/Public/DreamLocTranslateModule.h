// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Layout/SlateRect.h"

class SDreamLocTranslateWindow;
class SWindow;

/** The translation window's tab: Tools > DreamLoc > Translate. */
namespace DreamLocTranslateWindow
{
	DREAMLOCTRANSLATE_API FName GetTabName();

	/** The size its window opens at, in Slate units. */
	DREAMLOCTRANSLATE_API FVector2D GetDefaultSize();

	/**
	 * A window maximized, or bigger than 90% of its screen's work area either way, back to the default size (no more than
	 * 90% of the work area) in the middle of its screen. False when it was not too big.
	 */
	DREAMLOCTRANSLATE_API bool FitWindow(SWindow& InWindow);

	/** What FitWindow makes of a window of that size (in pixels) on that work area; unset when it is left as it is. */
	DREAMLOCTRANSLATE_API TOptional<FSlateRect> FitRect(const FVector2D& InSize, bool bInMaximized, const FSlateRect& InWorkArea, float InDPIScale);

	/** Opens the window (or brings it forward); the window, or null when the tab could not be opened. */
	DREAMLOCTRANSLATE_API TSharedPtr<SDreamLocTranslateWindow> Open();

	/** The translators' page: Editor Preferences > Plugins > DreamLoc translators. */
	DREAMLOCTRANSLATE_API FName GetTranslatorsSettingsName();
	DREAMLOCTRANSLATE_API void OpenTranslatorsSettings();
}
