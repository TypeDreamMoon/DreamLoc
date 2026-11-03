// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class SDreamLocTranslateWindow;

/** The translation window's tab: Tools > DreamLoc > Translate. */
namespace DreamLocTranslateWindow
{
	DREAMLOCTRANSLATE_API FName GetTabName();

	/** Opens the window (or brings it forward); the window, or null when the tab could not be opened. */
	DREAMLOCTRANSLATE_API TSharedPtr<SDreamLocTranslateWindow> Open();
}
