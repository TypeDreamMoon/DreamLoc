// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamLocTranslateTypes.h"

/**
 * What a large model is told: the system message from the plugin's Resources/Prompts/translate.md (a project's
 * DLoc/_prompt.md is added after it, or replaces it when its first line is `<!-- replace -->`), and the batch as JSON.
 */
namespace DreamLocTranslatePrompt
{
	/** The plugin's template; a built-in copy when the file is not there. */
	FString LoadTemplate();

	FString System(const FDreamLocTranslateBatch& InBatch, const FString& InTemplate);
	FString User(const FDreamLocTranslateBatch& InBatch);

	/** The model's answer into InOutReply's texts; false when it is not the JSON asked for. */
	bool ParseAnswer(const FString& InContent, const FDreamLocTranslateBatch& InBatch, FDreamLocTranslateReply& InOutReply);
}
