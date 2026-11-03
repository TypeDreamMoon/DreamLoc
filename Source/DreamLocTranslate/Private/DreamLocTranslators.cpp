// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocTranslatorsPrivate.h"

TSharedPtr<IDreamLocTranslator> DreamLocTranslators::Make(const FDreamLocTranslateProfile& InProfile, FString& OutError)
{
	const FString& Type = InProfile.Type;
	auto Need = [&OutError, &InProfile](bool bInHas, const TCHAR* InWhat)
	{
		if (!bInHas && OutError.IsEmpty())
		{
			OutError = FString::Printf(TEXT("profile %s has no %s"), *InProfile.Name, InWhat);
		}
		return bInHas;
	};
	if (Type.Equals(TEXT("OpenAI"), ESearchCase::IgnoreCase))
	{
		if (!Need(!InProfile.BaseUrl.IsEmpty(), TEXT("BaseUrl")) || !Need(!InProfile.Model.IsEmpty(), TEXT("Model")))
		{
			return nullptr;
		}
		return MakeShared<FDreamLocOpenAITranslator>(InProfile);
	}
	if (Type.Equals(TEXT("DeepL"), ESearchCase::IgnoreCase) || Type.Equals(TEXT("Google"), ESearchCase::IgnoreCase))
	{
		if (!Need(!InProfile.GetApiKey().IsEmpty(), TEXT("ApiKey (or ApiKeyEnv naming a variable that is set)")))
		{
			return nullptr;
		}
		if (Type.Equals(TEXT("DeepL"), ESearchCase::IgnoreCase))
		{
			return MakeShared<FDreamLocDeepLTranslator>(InProfile);
		}
		return MakeShared<FDreamLocGoogleTranslator>(InProfile);
	}
	if (Type.Equals(TEXT("Baidu"), ESearchCase::IgnoreCase) || Type.Equals(TEXT("Tencent"), ESearchCase::IgnoreCase)
		|| Type.Equals(TEXT("Youdao"), ESearchCase::IgnoreCase))
	{
		if (!Need(!InProfile.GetAppId().IsEmpty(), TEXT("AppId (or AppIdEnv naming a variable that is set)"))
			|| !Need(!InProfile.GetSecret().IsEmpty(), TEXT("Secret (or SecretEnv naming a variable that is set)")))
		{
			return nullptr;
		}
		if (Type.Equals(TEXT("Baidu"), ESearchCase::IgnoreCase))
		{
			return MakeShared<FDreamLocBaiduTranslator>(InProfile);
		}
		if (Type.Equals(TEXT("Tencent"), ESearchCase::IgnoreCase))
		{
			return MakeShared<FDreamLocTencentTranslator>(InProfile);
		}
		return MakeShared<FDreamLocYoudaoTranslator>(InProfile);
	}
	OutError = FString::Printf(TEXT("profile %s: no translator of type '%s' (%s)"), *InProfile.Name, *Type, *FString::Join(GetTypes(), TEXT(", ")));
	return nullptr;
}

TArray<FString> DreamLocTranslators::GetTypes()
{
	return { TEXT("OpenAI"), TEXT("DeepL"), TEXT("Google"), TEXT("Baidu"), TEXT("Tencent"), TEXT("Youdao") };
}

FDreamLocTranslateReply DreamLocTranslators::Ping(const FDreamLocTranslateProfile& InProfile, const FString& InSourceCulture, const FString& InCulture)
{
	FDreamLocTranslateReply Reply;
	const TSharedPtr<IDreamLocTranslator> Translator = Make(InProfile, Reply.Error);
	if (!Translator.IsValid())
	{
		return Reply;
	}
	FDreamLocTranslateBatch Batch;
	Batch.SourceCulture = InSourceCulture;
	Batch.Culture = InCulture;
	FDreamLocTranslateUnit& Unit = Batch.Units.AddDefaulted_GetRef();
	Unit.Id = 1;
	Unit.Namespace = TEXT("DreamLoc");
	Unit.Key = TEXT("Ping");
	Unit.Source = InSourceCulture.StartsWith(TEXT("zh")) ? TEXT("你好") : TEXT("Hello");
	FString Why;
	if (!Translator->SupportsCultures(InSourceCulture, InCulture, Why))
	{
		Reply.Error = Why;
		return Reply;
	}
	const TAtomic<bool> Cancel(false);
	Reply = Translator->Translate(Batch, Cancel);
	if (Reply.bSucceeded && !Reply.Texts.Contains(1))
	{
		Reply.bSucceeded = false;
		const FString* UnitError = Reply.UnitErrors.Find(1);
		Reply.Error = UnitError != nullptr ? *UnitError : FString(TEXT("the answer has no translation"));
	}
	return Reply;
}
