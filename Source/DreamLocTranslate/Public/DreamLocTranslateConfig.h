// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamLocSettings.h"
#include "DreamLocTranslateTypes.h"

/**
 * Where the translators are set up: the user's %APPDATA%/DreamLoc/translators.ini, shared by every project and never in
 * a repository (keys live there), and the project's DLoc/dloc.ini [Translate], which holds nothing secret.
 */
namespace DreamLocTranslateConfig
{
	/** %APPDATA%/DreamLoc/translators.ini; DREAMLOC_TRANSLATORS_INI, or SetUserConfigPath, puts it elsewhere. */
	DREAMLOCTRANSLATE_API FString GetUserConfigPath();

	/** Reads and writes the user's profiles from InPath instead, until called again with an empty path: for tests. */
	DREAMLOCTRANSLATE_API void SetUserConfigPath(const FString& InPath);

	DREAMLOCTRANSLATE_API TArray<FDreamLocTranslateProfile> ParseProfiles(const FString& InText, TArray<FString>& OutWarnings);
	DREAMLOCTRANSLATE_API FString WriteProfiles(TConstArrayView<FDreamLocTranslateProfile> InProfiles);

	DREAMLOCTRANSLATE_API TArray<FDreamLocTranslateProfile> LoadProfiles(TArray<FString>* OutWarnings = nullptr);
	DREAMLOCTRANSLATE_API bool SaveProfiles(TConstArrayView<FDreamLocTranslateProfile> InProfiles);

	/** The profile of that name; null when there is none. */
	DREAMLOCTRANSLATE_API const FDreamLocTranslateProfile* FindProfile(TConstArrayView<FDreamLocTranslateProfile> InProfiles, const FString& InName);

	/**
	 * InDLocText with its [Translate] section made to say what InSettings says, every other line as it was: what the
	 * window writes back when the project's translation settings change. A file without the section gets it at the end.
	 */
	DREAMLOCTRANSLATE_API FString WriteTranslateSection(const FString& InDLocText, const FDreamLocSettings& InSettings);

	/**
	 * InDLocText with only the [Translate] settings that differ between InBefore and InAfter written: each changed key's
	 * lines where they stood (a new key at the end of the section, one back to its default taken out), every other line --
	 * comments, the order, keys left alone -- as it was. What Project Settings > Plugins > DreamLoc writes back.
	 */
	DREAMLOCTRANSLATE_API FString WriteTranslateChanges(const FString& InDLocText, const FDreamLocSettings& InBefore, const FDreamLocSettings& InAfter);
}
