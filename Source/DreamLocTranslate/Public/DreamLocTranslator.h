// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamLocTranslateTypes.h"

/**
 * A translation service. Translate blocks until the answer is in: the flow calls it from worker threads, never from
 * the game thread. It sends one request (or, for a service without batches, one per unit) and says what came back;
 * retries, checks and writing back are the flow's.
 */
class DREAMLOCTRANSLATE_API IDreamLocTranslator
{
public:
	virtual ~IDreamLocTranslator() = default;

	virtual EDreamLocTranslatorKind GetKind() const = 0;
	virtual const FDreamLocTranslateProfile& GetProfile() const = 0;

	/** The most units and characters (sources together) one request may carry. */
	virtual int32 GetMaxUnits() const { return 50; }
	virtual int32 GetMaxCharacters() const { return 8000; }

	/** Whether it translates from InSource into InTarget (DreamLoc culture names); OutWhy says why not. */
	virtual bool SupportsCultures(const FString& InSource, const FString& InTarget, FString& OutWhy) const { return true; }

	virtual FDreamLocTranslateReply Translate(const FDreamLocTranslateBatch& InBatch, const TAtomic<bool>& InCancel) = 0;
};

namespace DreamLocTranslators
{
	/** The translator a profile describes; null with OutError when the type is unknown or a setting is missing. */
	DREAMLOCTRANSLATE_API TSharedPtr<IDreamLocTranslator> Make(const FDreamLocTranslateProfile& InProfile, FString& OutError);

	/** The profile types there are: OpenAI, DeepL, Google, Baidu, Tencent, Youdao. */
	DREAMLOCTRANSLATE_API TArray<FString> GetTypes();

	/** The smallest request a profile can answer -- one short text -- for a "test this profile" button. */
	DREAMLOCTRANSLATE_API FDreamLocTranslateReply Ping(const FDreamLocTranslateProfile& InProfile, const FString& InSourceCulture, const FString& InCulture);
}
