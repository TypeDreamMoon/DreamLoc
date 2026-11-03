// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamLocTranslateHttp.h"
#include "DreamLocTranslateProtect.h"
#include "DreamLocTranslator.h"

/** An OpenAI-compatible chat completions service: OpenAI, DeepSeek, Qwen... and Ollama, LM Studio, vLLM on this machine. */
class FDreamLocOpenAITranslator final : public IDreamLocTranslator
{
public:
	explicit FDreamLocOpenAITranslator(const FDreamLocTranslateProfile& InProfile);

	virtual EDreamLocTranslatorKind GetKind() const override { return EDreamLocTranslatorKind::LargeModel; }
	virtual const FDreamLocTranslateProfile& GetProfile() const override { return Profile; }
	virtual int32 GetMaxUnits() const override { return 50; }
	virtual int32 GetMaxCharacters() const override { return 6000; }
	virtual FDreamLocTranslateReply Translate(const FDreamLocTranslateBatch& InBatch, const TAtomic<bool>& InCancel) override;

private:
	FDreamLocTranslateProfile Profile;
	FString Template;
};

/**
 * A machine translation service: each unit's text protected (DreamLocTranslateProtect), sent, put back together. A text
 * whose placeholders do not come back intact fails on its own.
 */
class FDreamLocMachineTranslator : public IDreamLocTranslator
{
public:
	explicit FDreamLocMachineTranslator(const FDreamLocTranslateProfile& InProfile) : Profile(InProfile) {}

	virtual EDreamLocTranslatorKind GetKind() const override { return EDreamLocTranslatorKind::Machine; }
	virtual const FDreamLocTranslateProfile& GetProfile() const override { return Profile; }
	virtual bool SupportsCultures(const FString& InSource, const FString& InTarget, FString& OutWhy) const override;
	virtual FDreamLocTranslateReply Translate(const FDreamLocTranslateBatch& InBatch, const TAtomic<bool>& InCancel) override;

	/** The service's code for a DreamLoc culture; empty when it has none. */
	virtual FString MapCulture(const FString& InCulture, bool bInTarget) const = 0;

protected:
	struct FCall
	{
		bool bSucceeded = false;
		bool bRetryable = false;
		int32 HttpStatus = 0;
		FString Error;
		TArray<FString> Texts;
	};

	virtual DreamLocTranslateProtect::EStyle GetStyle() const { return DreamLocTranslateProtect::EStyle::Brackets; }
	/** Translates the texts, in order: one answer for each. */
	virtual FCall Call(const TArray<FString>& InTexts, const FString& InSource, const FString& InTarget, const TAtomic<bool>& InCancel) = 0;

	/** The service's address: the profile's BaseUrl, else InDefault; without a trailing slash. */
	FString Base(const FString& InDefault) const;
	/** What an HTTP answer says about retrying, for the common cases. */
	static void ReadStatus(const DreamLocTranslateHttp::FResponse& InResponse, FCall& OutCall);

	FDreamLocTranslateProfile Profile;
};

class FDreamLocDeepLTranslator final : public FDreamLocMachineTranslator
{
public:
	using FDreamLocMachineTranslator::FDreamLocMachineTranslator;
	virtual int32 GetMaxUnits() const override { return 50; }
	virtual int32 GetMaxCharacters() const override { return 30000; }
	virtual FString MapCulture(const FString& InCulture, bool bInTarget) const override;

protected:
	virtual DreamLocTranslateProtect::EStyle GetStyle() const override { return DreamLocTranslateProtect::EStyle::Xml; }
	virtual FCall Call(const TArray<FString>& InTexts, const FString& InSource, const FString& InTarget, const TAtomic<bool>& InCancel) override;
};

class FDreamLocGoogleTranslator final : public FDreamLocMachineTranslator
{
public:
	using FDreamLocMachineTranslator::FDreamLocMachineTranslator;
	virtual int32 GetMaxUnits() const override { return 100; }
	virtual int32 GetMaxCharacters() const override { return 25000; }
	virtual FString MapCulture(const FString& InCulture, bool bInTarget) const override;

protected:
	virtual DreamLocTranslateProtect::EStyle GetStyle() const override { return DreamLocTranslateProtect::EStyle::Html; }
	virtual FCall Call(const TArray<FString>& InTexts, const FString& InSource, const FString& InTarget, const TAtomic<bool>& InCancel) override;
};

class FDreamLocBaiduTranslator final : public FDreamLocMachineTranslator
{
public:
	using FDreamLocMachineTranslator::FDreamLocMachineTranslator;
	virtual int32 GetMaxUnits() const override { return 30; }
	virtual int32 GetMaxCharacters() const override { return 2000; }
	virtual FString MapCulture(const FString& InCulture, bool bInTarget) const override;

protected:
	virtual FCall Call(const TArray<FString>& InTexts, const FString& InSource, const FString& InTarget, const TAtomic<bool>& InCancel) override;
};

class FDreamLocTencentTranslator final : public FDreamLocMachineTranslator
{
public:
	using FDreamLocMachineTranslator::FDreamLocMachineTranslator;
	virtual int32 GetMaxUnits() const override { return 50; }
	virtual int32 GetMaxCharacters() const override { return 2000; }
	virtual FString MapCulture(const FString& InCulture, bool bInTarget) const override;

protected:
	virtual FCall Call(const TArray<FString>& InTexts, const FString& InSource, const FString& InTarget, const TAtomic<bool>& InCancel) override;
};

class FDreamLocYoudaoTranslator final : public FDreamLocMachineTranslator
{
public:
	using FDreamLocMachineTranslator::FDreamLocMachineTranslator;
	virtual int32 GetMaxUnits() const override { return 20; }
	virtual int32 GetMaxCharacters() const override { return 5000; }
	virtual FString MapCulture(const FString& InCulture, bool bInTarget) const override;

protected:
	/** One text per request: the service has no batch of its own. */
	virtual FCall Call(const TArray<FString>& InTexts, const FString& InSource, const FString& InTarget, const TAtomic<bool>& InCancel) override;
};
