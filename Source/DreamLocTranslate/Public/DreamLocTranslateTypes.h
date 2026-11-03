// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamLocExtract.h"

DREAMLOCTRANSLATE_API DECLARE_LOG_CATEGORY_EXTERN(LogDreamLocTranslate, Log, All);

/** What a translator is: a large model, which reads instructions and context, or a machine translation service. */
enum class EDreamLocTranslatorKind : uint8
{
	LargeModel,
	Machine,
};

/**
 * One translator as the user set it up: a `[Profile <name>]` of %APPDATA%/DreamLoc/translators.ini. Keys are written in
 * the file or named by the environment variable that holds them; nothing here goes into a log or a report but the name.
 *
 *   Type      OpenAI (any OpenAI-compatible chat completions service, local or not), DeepL, Google, Baidu, Tencent, Youdao
 *   BaseUrl   the service's address; empty for the service's own (OpenAI needs one)
 *   Model     the model (OpenAI)
 *   ApiKey / ApiKeyEnv        the key (OpenAI, DeepL, Google)
 *   AppId / AppIdEnv          Baidu's appid, Youdao's appKey, Tencent's SecretId
 *   Secret / SecretEnv        Baidu's key, Youdao's app secret, Tencent's SecretKey
 *   Region    Tencent's region (default ap-shanghai)
 *   Proxy     http://host:port the requests go through; empty for none
 *   JsonMode  ask for a JSON object (OpenAI response_format)
 *   Temperature (OpenAI, default 0.2)
 *   Local     true / false; unset: local when the address is localhost, 127.0.0.1, ::1 or a .local host
 */
struct DREAMLOCTRANSLATE_API FDreamLocTranslateProfile
{
	FString Name;
	FString Type;
	FString BaseUrl;
	FString Model;
	FString ApiKey;
	FString ApiKeyEnv;
	FString AppId;
	FString AppIdEnv;
	FString Secret;
	FString SecretEnv;
	FString Region;
	FString Proxy;
	bool bJsonMode = false;
	float Temperature = 0.2f;
	TOptional<bool> Local;

	EDreamLocTranslatorKind GetKind() const;
	/** Whether texts may go to it when they are only for local translators: Local, else its address. */
	bool IsLocal() const;
	/** The key, from the file or its environment variable; empty for none. */
	FString GetApiKey() const;
	FString GetAppId() const;
	FString GetSecret() const;
	/** A key or secret written in the file rather than named by an environment variable. */
	bool HasSecretInFile() const;
	/** Every secret it holds, resolved: what must never show in a log or a report. */
	TArray<FString> GetSecrets() const;
};

/** One text to translate, with what the translator is told about it. */
struct DREAMLOCTRANSLATE_API FDreamLocTranslateUnit
{
	/** Unique in a run: what a reply's texts are matched by. */
	int32 Id = 0;
	/** The .dloc it is in, relative to DLoc/<culture>/. */
	FString File;
	FString Namespace;
	FString Key;
	FString Source;
	FString Note;
	FString Kind;
	FString At;
	int32 Max = -1;
	/** The source texts around it in its file. */
	TArray<FString> Nearby;
	/** What the source tool says about it (speaker, dialogue, what each argument is). */
	TArray<FDreamLocExtractContext> Context;
	/** The culture's terms its source uses, and the words it keeps. */
	TArray<TPair<FString, FString>> Terms;
	TArray<FString> Keeps;
	/** A stale entry: the source it was translated from, and that translation, to be amended rather than redone. */
	FString Prev;
	FString PreviousText;
	/** A reviewed translation of the same source elsewhere in the culture: to stay with when the context allows. */
	FString Reference;
	/** Sent again: what came back before, and what the checks said was wrong with it. */
	FString LastText;
	FString Problems;
};

/** What goes to a translator in one request. */
struct DREAMLOCTRANSLATE_API FDreamLocTranslateBatch
{
	FString SourceCulture;
	FString Culture;
	TArray<FDreamLocTranslateUnit> Units;
	/** The project's style guide for the culture; empty for none. */
	FString Style;
	/** The project's own prompt (DLoc/_prompt.md); see FDreamLocTranslatePrompt. */
	FString ProjectPrompt;
};

/** What a translator sent back. */
struct DREAMLOCTRANSLATE_API FDreamLocTranslateReply
{
	bool bSucceeded = false;
	/** The request may succeed if sent again: no connection, a timeout, 429, 5xx. */
	bool bRetryable = false;
	/** The answer came but could not be read: sent again once. */
	bool bMalformed = false;
	int32 HttpStatus = 0;
	FString Error;
	/** Unit id -> its translation. A unit the reply has no text for failed. */
	TMap<int32, FString> Texts;
	/** Unit id -> why it failed, for a unit that failed on its own (a placeholder lost, a language not served). */
	TMap<int32, FString> UnitErrors;
	TArray<FString> Warnings;
	int64 PromptTokens = 0;
	int64 CompletionTokens = 0;
	int64 Characters = 0;
};

enum class EDreamLocTranslateStatus : uint8
{
	Translated,
	Failed,
	Skipped,
};

/** One entry as a run left it: what the window's results table shows. */
struct DREAMLOCTRANSLATE_API FDreamLocTranslateRow
{
	FString Culture;
	FString File;
	FString Namespace;
	FString Key;
	FString Source;
	/** The translation the entry had before (a stale or machine one). */
	FString OldText;
	FString NewText;
	/** The last translation the checks turned down, for a failed entry. */
	FString RejectedText;
	/** What the checks said: about NewText, or about RejectedText for a failed entry; empty when nothing. */
	FString Problems;
	/** The profile that translated it. */
	FString Backend;
	/** Why it was skipped or failed. */
	FString Reason;
	EDreamLocTranslateStatus Status = EDreamLocTranslateStatus::Skipped;
};

struct DREAMLOCTRANSLATE_API FDreamLocTranslateResult
{
	TArray<FDreamLocTranslateRow> Rows;
	int32 Translated = 0;
	int32 Failed = 0;
	int32 Skipped = 0;
	int32 Requests = 0;
	double Seconds = 0.0;
	int64 PromptTokens = 0;
	int64 CompletionTokens = 0;
	int64 Characters = 0;
	bool bCancelled = false;
	/** Saved/DLoc/translate-<culture>-<time>.json, one for each culture translated. */
	TArray<FString> ReportPaths;
	/** What went wrong before anything was sent (no such profile, a file that does not read...). */
	TArray<FString> Errors;
	TArray<FString> Warnings;
};

/** What a run would send, before it is sent. */
struct DREAMLOCTRANSLATE_API FDreamLocTranslateEstimate
{
	int32 Entries = 0;
	/** Sent once for several entries of the same source and kind. */
	int32 Distinct = 0;
	int32 Characters = 0;
	/** Roughly, for a large model: the prompt and the answer. */
	int32 Tokens = 0;
	int32 Batches = 0;
	/** Left out: only for local translators, a plural form a machine translator cannot keep, a language not served. */
	int32 Skipped = 0;
	/** Every culture has a translator: the requests, tokens and what is left out are counted too. */
	bool bTranslatorKnown = true;
	/** Why it cannot be sent (no translator chosen, a profile that does not work...); the counts above stand regardless. */
	TArray<FString> Errors;
};

struct DREAMLOCTRANSLATE_API FDreamLocTranslateProgress
{
	FString Culture;
	int32 BatchesDone = 0;
	int32 Batches = 0;
	int32 UnitsDone = 0;
	int32 Units = 0;
};
