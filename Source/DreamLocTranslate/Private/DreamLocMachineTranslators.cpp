// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocTranslatorsPrivate.h"

#include "DreamLocTranslateCrypto.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/DateTime.h"
#include "Misc/Guid.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace DreamLocMachineTranslatorsLocal
{
	FString Primary(const FString& InCulture)
	{
		FString Language;
		return InCulture.Split(TEXT("-"), &Language, nullptr) ? Language.ToLower() : InCulture.ToLower();
	}

	/** The service's code: an exact culture first (zh-Hans, pt-BR), then the language alone. */
	FString Lookup(const TMap<FString, FString>& InTable, const FString& InCulture)
	{
		if (const FString* Exact = InTable.Find(InCulture.ToLower()))
		{
			return *Exact;
		}
		const FString* Language = InTable.Find(Primary(InCulture));
		return Language != nullptr ? *Language : FString();
	}

	TSharedPtr<FJsonObject> ParseObject(const FString& InText)
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InText);
		return FJsonSerializer::Deserialize(Reader, Root) ? Root : nullptr;
	}

	FString Serialize(const TSharedRef<FJsonObject>& InObject)
	{
		FString Out;
		FJsonSerializer::Serialize(InObject, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out));
		return Out;
	}

	TArray<TSharedPtr<FJsonValue>> Strings(const TArray<FString>& InValues)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FString& Value : InValues)
		{
			Out.Add(MakeShared<FJsonValueString>(Value));
		}
		return Out;
	}

	FString Salt()
	{
		return FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
	}

	FString HostOf(const FString& InUrl)
	{
		FString Rest = InUrl;
		const int32 Scheme = Rest.Find(TEXT("://"));
		if (Scheme != INDEX_NONE)
		{
			Rest.RightChopInline(Scheme + 3);
		}
		int32 Slash = INDEX_NONE;
		return Rest.FindChar(TEXT('/'), Slash) ? Rest.Left(Slash) : Rest;
	}
}

// ---------------------------------------------------------------------------------------------------------- the base

bool FDreamLocMachineTranslator::SupportsCultures(const FString& InSource, const FString& InTarget, FString& OutWhy) const
{
	for (const TPair<const FString*, bool>& Culture : { TPair<const FString*, bool>(&InSource, false), TPair<const FString*, bool>(&InTarget, true) })
	{
		if (MapCulture(*Culture.Key, Culture.Value).IsEmpty())
		{
			OutWhy = FString::Printf(TEXT("%s does not translate %s %s"), *Profile.Type, Culture.Value ? TEXT("into") : TEXT("from"), **Culture.Key);
			return false;
		}
	}
	return true;
}

FString FDreamLocMachineTranslator::Base(const FString& InDefault) const
{
	FString Url = Profile.BaseUrl.IsEmpty() ? InDefault : Profile.BaseUrl;
	while (Url.EndsWith(TEXT("/")))
	{
		Url.LeftChopInline(1);
	}
	return Url;
}

void FDreamLocMachineTranslator::ReadStatus(const DreamLocTranslateHttp::FResponse& InResponse, FCall& OutCall)
{
	OutCall.HttpStatus = InResponse.Status;
	if (!InResponse.bAnswered)
	{
		OutCall.bRetryable = !InResponse.bCancelled;
		OutCall.Error = InResponse.Error;
	}
	else if (InResponse.Status == 429 || InResponse.Status >= 500)
	{
		OutCall.bRetryable = true;
		OutCall.Error = FString::Printf(TEXT("HTTP %d: %s"), InResponse.Status, *InResponse.Body.Left(200));
	}
	else if (InResponse.Status != 200)
	{
		OutCall.Error = FString::Printf(TEXT("HTTP %d: %s"), InResponse.Status, *InResponse.Body.Left(200));
	}
}

FDreamLocTranslateReply FDreamLocMachineTranslator::Translate(const FDreamLocTranslateBatch& InBatch, const TAtomic<bool>& InCancel)
{
	FDreamLocTranslateReply Reply;
	const FString Source = MapCulture(InBatch.SourceCulture, false);
	const FString Target = MapCulture(InBatch.Culture, true);
	if (Source.IsEmpty() || Target.IsEmpty())
	{
		SupportsCultures(InBatch.SourceCulture, InBatch.Culture, Reply.Error);
		return Reply;
	}
	const DreamLocTranslateProtect::EStyle Style = GetStyle();
	TArray<DreamLocTranslateProtect::FProtected> Protected;
	TArray<FString> Texts;
	for (const FDreamLocTranslateUnit& Unit : InBatch.Units)
	{
		Protected.Add(DreamLocTranslateProtect::Protect(Unit.Source, Unit.Terms, Unit.Keeps, Style));
		Texts.Add(Protected.Last().Text);
		Reply.Characters += Unit.Source.Len();
	}
	const FCall Result = Call(Texts, Source, Target, InCancel);
	Reply.HttpStatus = Result.HttpStatus;
	if (!Result.bSucceeded)
	{
		Reply.bRetryable = Result.bRetryable;
		Reply.Error = Result.Error;
		return Reply;
	}
	if (Result.Texts.Num() != Texts.Num())
	{
		Reply.bMalformed = true;
		Reply.Error = FString::Printf(TEXT("%d translation(s) came back for %d text(s)"), Result.Texts.Num(), Texts.Num());
		return Reply;
	}
	for (int32 Index = 0; Index < InBatch.Units.Num(); ++Index)
	{
		FString Text;
		FString Error;
		if (DreamLocTranslateProtect::Restore(Result.Texts[Index], Protected[Index], Style, Text, Error))
		{
			Reply.Texts.Add(InBatch.Units[Index].Id, Text);
		}
		else
		{
			Reply.UnitErrors.Add(InBatch.Units[Index].Id, Error);
		}
	}
	Reply.bSucceeded = true;
	return Reply;
}

// ---------------------------------------------------------------------------------------------------------- DeepL

FString FDreamLocDeepLTranslator::MapCulture(const FString& InCulture, bool bInTarget) const
{
	static const TSet<FString> Languages = { TEXT("ar"), TEXT("bg"), TEXT("cs"), TEXT("da"), TEXT("de"), TEXT("el"), TEXT("en"), TEXT("es"), TEXT("et"),
		TEXT("fi"), TEXT("fr"), TEXT("hu"), TEXT("id"), TEXT("it"), TEXT("ja"), TEXT("ko"), TEXT("lt"), TEXT("lv"), TEXT("nb"), TEXT("nl"), TEXT("pl"),
		TEXT("pt"), TEXT("ro"), TEXT("ru"), TEXT("sk"), TEXT("sl"), TEXT("sv"), TEXT("tr"), TEXT("uk"), TEXT("zh") };
	const FString Culture = InCulture.ToLower();
	const FString Language = DreamLocMachineTranslatorsLocal::Primary(Culture);
	if (!Languages.Contains(Language))
	{
		return FString();
	}
	if (!bInTarget)
	{
		return Language.ToUpper();
	}
	if (Language == TEXT("en")) { return Culture == TEXT("en-gb") ? TEXT("EN-GB") : TEXT("EN-US"); }
	if (Language == TEXT("pt")) { return Culture == TEXT("pt-br") ? TEXT("PT-BR") : TEXT("PT-PT"); }
	if (Language == TEXT("zh")) { return Culture.Contains(TEXT("hant")) || Culture == TEXT("zh-tw") || Culture == TEXT("zh-hk") ? TEXT("ZH-HANT") : TEXT("ZH-HANS"); }
	return Language.ToUpper();
}

FDreamLocMachineTranslator::FCall FDreamLocDeepLTranslator::Call(const TArray<FString>& InTexts, const FString& InSource, const FString& InTarget, const TAtomic<bool>& InCancel)
{
	using namespace DreamLocMachineTranslatorsLocal;
	FCall Out;
	const FString Key = Profile.GetApiKey();
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetArrayField(TEXT("text"), Strings(InTexts));
	Body->SetStringField(TEXT("source_lang"), InSource);
	Body->SetStringField(TEXT("target_lang"), InTarget);
	Body->SetStringField(TEXT("tag_handling"), TEXT("xml"));
	Body->SetArrayField(TEXT("ignore_tags"), Strings({ TEXT("x") }));
	DreamLocTranslateHttp::FRequest Request;
	// A free key ends in :fx and has a host of its own.
	Request.Url = Base(Key.EndsWith(TEXT(":fx")) ? TEXT("https://api-free.deepl.com") : TEXT("https://api.deepl.com")) + TEXT("/v2/translate");
	Request.Headers.Emplace(TEXT("Content-Type"), TEXT("application/json"));
	Request.Headers.Emplace(TEXT("Authorization"), TEXT("DeepL-Auth-Key ") + Key);
	Request.Body = Serialize(Body);
	Request.Proxy = Profile.Proxy;
	const DreamLocTranslateHttp::FResponse Response = DreamLocTranslateHttp::Send(Request, &InCancel);
	ReadStatus(Response, Out);
	if (!Out.Error.IsEmpty())
	{
		return Out;
	}
	const TSharedPtr<FJsonObject> Root = ParseObject(Response.Body);
	const TArray<TSharedPtr<FJsonValue>>* Translations = nullptr;
	if (!Root.IsValid() || !Root->TryGetArrayField(TEXT("translations"), Translations))
	{
		Out.Error = TEXT("the answer has no translations");
		return Out;
	}
	for (const TSharedPtr<FJsonValue>& Value : *Translations)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		FString Text;
		if (Value->TryGetObject(Object))
		{
			(*Object)->TryGetStringField(TEXT("text"), Text);
		}
		Out.Texts.Add(Text);
	}
	Out.bSucceeded = true;
	return Out;
}

// ---------------------------------------------------------------------------------------------------------- Google

FString FDreamLocGoogleTranslator::MapCulture(const FString& InCulture, bool bInTarget) const
{
	const FString Culture = InCulture.ToLower();
	if (Culture == TEXT("zh-hans") || Culture == TEXT("zh-cn") || Culture == TEXT("zh")) { return TEXT("zh-CN"); }
	if (Culture == TEXT("zh-hant") || Culture == TEXT("zh-tw") || Culture == TEXT("zh-hk")) { return TEXT("zh-TW"); }
	if (Culture == TEXT("pt-br") || Culture == TEXT("pt-pt")) { return Culture.Left(2); }
	const FString Language = DreamLocMachineTranslatorsLocal::Primary(Culture);
	return Language.Len() >= 2 && Language.Len() <= 3 ? Language : FString();
}

FDreamLocMachineTranslator::FCall FDreamLocGoogleTranslator::Call(const TArray<FString>& InTexts, const FString& InSource, const FString& InTarget, const TAtomic<bool>& InCancel)
{
	using namespace DreamLocMachineTranslatorsLocal;
	FCall Out;
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetArrayField(TEXT("q"), Strings(InTexts));
	Body->SetStringField(TEXT("source"), InSource);
	Body->SetStringField(TEXT("target"), InTarget);
	Body->SetStringField(TEXT("format"), TEXT("html"));
	DreamLocTranslateHttp::FRequest Request;
	// The key in a header rather than the query: a URL ends up in logs.
	Request.Url = Base(TEXT("https://translation.googleapis.com")) + TEXT("/language/translate/v2");
	Request.Headers.Emplace(TEXT("Content-Type"), TEXT("application/json"));
	Request.Headers.Emplace(TEXT("X-goog-api-key"), Profile.GetApiKey());
	Request.Body = Serialize(Body);
	Request.Proxy = Profile.Proxy;
	const DreamLocTranslateHttp::FResponse Response = DreamLocTranslateHttp::Send(Request, &InCancel);
	ReadStatus(Response, Out);
	if (!Out.Error.IsEmpty())
	{
		return Out;
	}
	const TSharedPtr<FJsonObject> Root = ParseObject(Response.Body);
	const TSharedPtr<FJsonObject>* Data = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Translations = nullptr;
	if (!Root.IsValid() || !Root->TryGetObjectField(TEXT("data"), Data) || !(*Data)->TryGetArrayField(TEXT("translations"), Translations))
	{
		Out.Error = TEXT("the answer has no data.translations");
		return Out;
	}
	for (const TSharedPtr<FJsonValue>& Value : *Translations)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		FString Text;
		if (Value->TryGetObject(Object))
		{
			(*Object)->TryGetStringField(TEXT("translatedText"), Text);
		}
		Out.Texts.Add(Text);
	}
	Out.bSucceeded = true;
	return Out;
}

// ---------------------------------------------------------------------------------------------------------- Baidu

FString FDreamLocBaiduTranslator::MapCulture(const FString& InCulture, bool bInTarget) const
{
	static const TMap<FString, FString> Table = {
		{ TEXT("zh-hans"), TEXT("zh") }, { TEXT("zh-cn"), TEXT("zh") }, { TEXT("zh"), TEXT("zh") }, { TEXT("zh-hant"), TEXT("cht") }, { TEXT("zh-tw"), TEXT("cht") },
		{ TEXT("en"), TEXT("en") }, { TEXT("ja"), TEXT("jp") }, { TEXT("ko"), TEXT("kor") }, { TEXT("fr"), TEXT("fra") }, { TEXT("de"), TEXT("de") },
		{ TEXT("es"), TEXT("spa") }, { TEXT("ru"), TEXT("ru") }, { TEXT("it"), TEXT("it") }, { TEXT("pt"), TEXT("pt") }, { TEXT("ar"), TEXT("ara") },
		{ TEXT("th"), TEXT("th") }, { TEXT("vi"), TEXT("vie") }, { TEXT("pl"), TEXT("pl") }, { TEXT("nl"), TEXT("nl") }, { TEXT("el"), TEXT("el") },
		{ TEXT("sv"), TEXT("swe") }, { TEXT("fi"), TEXT("fin") }, { TEXT("da"), TEXT("dan") }, { TEXT("cs"), TEXT("cs") }, { TEXT("ro"), TEXT("rom") },
		{ TEXT("bg"), TEXT("bul") }, { TEXT("hu"), TEXT("hu") }, { TEXT("et"), TEXT("est") }, { TEXT("sl"), TEXT("slo") },
	};
	return DreamLocMachineTranslatorsLocal::Lookup(Table, InCulture);
}

FDreamLocMachineTranslator::FCall FDreamLocBaiduTranslator::Call(const TArray<FString>& InTexts, const FString& InSource, const FString& InTarget, const TAtomic<bool>& InCancel)
{
	using namespace DreamLocMachineTranslatorsLocal;
	FCall Out;
	// Several texts in one request, a line each; a text's own line breaks are placeholders by now.
	const FString Query = FString::Join(InTexts, TEXT("\n"));
	const FString AppId = Profile.GetAppId();
	const FString SaltValue = Salt();
	DreamLocTranslateHttp::FRequest Request;
	Request.Url = Base(TEXT("https://fanyi-api.baidu.com")) + TEXT("/api/trans/vip/translate");
	Request.Headers.Emplace(TEXT("Content-Type"), TEXT("application/x-www-form-urlencoded"));
	Request.Body = DreamLocTranslateHttp::FormEncode({
		{ TEXT("q"), Query }, { TEXT("from"), InSource }, { TEXT("to"), InTarget }, { TEXT("appid"), AppId }, { TEXT("salt"), SaltValue },
		{ TEXT("sign"), DreamLocTranslateCrypto::BaiduSign(AppId, Query, SaltValue, Profile.GetSecret()) } });
	Request.Proxy = Profile.Proxy;
	const DreamLocTranslateHttp::FResponse Response = DreamLocTranslateHttp::Send(Request, &InCancel);
	ReadStatus(Response, Out);
	if (!Out.Error.IsEmpty())
	{
		return Out;
	}
	const TSharedPtr<FJsonObject> Root = ParseObject(Response.Body);
	FString Code;
	if (Root.IsValid() && Root->TryGetStringField(TEXT("error_code"), Code) && Code != TEXT("52000"))
	{
		FString Message;
		Root->TryGetStringField(TEXT("error_msg"), Message);
		// 52001 a timeout, 52002 the service's own error, 54003 too often.
		Out.bRetryable = Code == TEXT("52001") || Code == TEXT("52002") || Code == TEXT("54003");
		Out.Error = FString::Printf(TEXT("Baidu error %s: %s"), *Code, *Message);
		return Out;
	}
	const TArray<TSharedPtr<FJsonValue>>* Results = nullptr;
	if (!Root.IsValid() || !Root->TryGetArrayField(TEXT("trans_result"), Results))
	{
		Out.Error = TEXT("the answer has no trans_result");
		return Out;
	}
	for (const TSharedPtr<FJsonValue>& Value : *Results)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		FString Text;
		if (Value->TryGetObject(Object))
		{
			(*Object)->TryGetStringField(TEXT("dst"), Text);
		}
		Out.Texts.Add(Text);
	}
	Out.bSucceeded = true;
	return Out;
}

// ---------------------------------------------------------------------------------------------------------- Tencent

FString FDreamLocTencentTranslator::MapCulture(const FString& InCulture, bool bInTarget) const
{
	static const TMap<FString, FString> Table = {
		{ TEXT("zh-hans"), TEXT("zh") }, { TEXT("zh-cn"), TEXT("zh") }, { TEXT("zh"), TEXT("zh") }, { TEXT("zh-hant"), TEXT("zh-TW") }, { TEXT("zh-tw"), TEXT("zh-TW") },
		{ TEXT("en"), TEXT("en") }, { TEXT("ja"), TEXT("ja") }, { TEXT("ko"), TEXT("ko") }, { TEXT("fr"), TEXT("fr") }, { TEXT("de"), TEXT("de") },
		{ TEXT("es"), TEXT("es") }, { TEXT("ru"), TEXT("ru") }, { TEXT("it"), TEXT("it") }, { TEXT("pt"), TEXT("pt") }, { TEXT("vi"), TEXT("vi") },
		{ TEXT("th"), TEXT("th") }, { TEXT("ar"), TEXT("ar") }, { TEXT("tr"), TEXT("tr") }, { TEXT("id"), TEXT("id") }, { TEXT("ms"), TEXT("ms") },
		{ TEXT("hi"), TEXT("hi") },
	};
	return DreamLocMachineTranslatorsLocal::Lookup(Table, InCulture);
}

FDreamLocMachineTranslator::FCall FDreamLocTencentTranslator::Call(const TArray<FString>& InTexts, const FString& InSource, const FString& InTarget, const TAtomic<bool>& InCancel)
{
	using namespace DreamLocMachineTranslatorsLocal;
	FCall Out;
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetArrayField(TEXT("SourceTextList"), Strings(InTexts));
	Body->SetStringField(TEXT("Source"), InSource);
	Body->SetStringField(TEXT("Target"), InTarget);
	Body->SetNumberField(TEXT("ProjectId"), 0);
	const FString Payload = Serialize(Body);
	const FString Url = Base(TEXT("https://tmt.tencentcloudapi.com"));
	const FString Host = HostOf(Url);
	const int64 Timestamp = FDateTime::UtcNow().ToUnixTimestamp();
	const FString Action = TEXT("TextTranslateBatch");
	const DreamLocTranslateCrypto::FTencentSignature Signature = DreamLocTranslateCrypto::TencentSign(Profile.GetAppId(), Profile.GetSecret(),
		TEXT("tmt"), Host, Action, Payload, Timestamp);
	DreamLocTranslateHttp::FRequest Request;
	Request.Url = Url + TEXT("/");
	Request.Headers.Emplace(TEXT("Content-Type"), TEXT("application/json; charset=utf-8"));
	Request.Headers.Emplace(TEXT("Authorization"), Signature.Authorization);
	Request.Headers.Emplace(TEXT("X-TC-Action"), Action);
	Request.Headers.Emplace(TEXT("X-TC-Version"), TEXT("2018-03-21"));
	Request.Headers.Emplace(TEXT("X-TC-Timestamp"), FString::Printf(TEXT("%lld"), Timestamp));
	Request.Headers.Emplace(TEXT("X-TC-Region"), Profile.Region.IsEmpty() ? TEXT("ap-shanghai") : *Profile.Region);
	Request.Body = Payload;
	Request.Proxy = Profile.Proxy;
	const DreamLocTranslateHttp::FResponse Response = DreamLocTranslateHttp::Send(Request, &InCancel);
	ReadStatus(Response, Out);
	if (!Out.Error.IsEmpty())
	{
		return Out;
	}
	const TSharedPtr<FJsonObject> Root = ParseObject(Response.Body);
	const TSharedPtr<FJsonObject>* Answer = nullptr;
	if (!Root.IsValid() || !Root->TryGetObjectField(TEXT("Response"), Answer))
	{
		Out.Error = TEXT("the answer has no Response");
		return Out;
	}
	const TSharedPtr<FJsonObject>* Error = nullptr;
	if ((*Answer)->TryGetObjectField(TEXT("Error"), Error))
	{
		FString Code;
		FString Message;
		(*Error)->TryGetStringField(TEXT("Code"), Code);
		(*Error)->TryGetStringField(TEXT("Message"), Message);
		Out.bRetryable = Code.StartsWith(TEXT("RequestLimitExceeded")) || Code.StartsWith(TEXT("InternalError"));
		Out.Error = FString::Printf(TEXT("Tencent error %s: %s"), *Code, *Message);
		return Out;
	}
	const TArray<TSharedPtr<FJsonValue>>* Texts = nullptr;
	if (!(*Answer)->TryGetArrayField(TEXT("TargetTextList"), Texts))
	{
		Out.Error = TEXT("the answer has no TargetTextList");
		return Out;
	}
	for (const TSharedPtr<FJsonValue>& Value : *Texts)
	{
		Out.Texts.Add(Value->AsString());
	}
	Out.bSucceeded = true;
	return Out;
}

// ---------------------------------------------------------------------------------------------------------- Youdao

FString FDreamLocYoudaoTranslator::MapCulture(const FString& InCulture, bool bInTarget) const
{
	static const TMap<FString, FString> Table = {
		{ TEXT("zh-hans"), TEXT("zh-CHS") }, { TEXT("zh-cn"), TEXT("zh-CHS") }, { TEXT("zh"), TEXT("zh-CHS") }, { TEXT("zh-hant"), TEXT("zh-CHT") },
		{ TEXT("zh-tw"), TEXT("zh-CHT") }, { TEXT("en"), TEXT("en") }, { TEXT("ja"), TEXT("ja") }, { TEXT("ko"), TEXT("ko") }, { TEXT("fr"), TEXT("fr") },
		{ TEXT("de"), TEXT("de") }, { TEXT("es"), TEXT("es") }, { TEXT("ru"), TEXT("ru") }, { TEXT("pt"), TEXT("pt") }, { TEXT("it"), TEXT("it") },
		{ TEXT("vi"), TEXT("vi") }, { TEXT("ar"), TEXT("ar") }, { TEXT("th"), TEXT("th") }, { TEXT("id"), TEXT("id") },
	};
	return DreamLocMachineTranslatorsLocal::Lookup(Table, InCulture);
}

FDreamLocMachineTranslator::FCall FDreamLocYoudaoTranslator::Call(const TArray<FString>& InTexts, const FString& InSource, const FString& InTarget, const TAtomic<bool>& InCancel)
{
	using namespace DreamLocMachineTranslatorsLocal;
	FCall Out;
	const FString AppKey = Profile.GetAppId();
	const FString Secret = Profile.GetSecret();
	for (const FString& Text : InTexts)
	{
		const FString SaltValue = Salt();
		const FString Curtime = FString::Printf(TEXT("%lld"), FDateTime::UtcNow().ToUnixTimestamp());
		DreamLocTranslateHttp::FRequest Request;
		Request.Url = Base(TEXT("https://openapi.youdao.com")) + TEXT("/api");
		Request.Headers.Emplace(TEXT("Content-Type"), TEXT("application/x-www-form-urlencoded"));
		Request.Body = DreamLocTranslateHttp::FormEncode({
			{ TEXT("q"), Text }, { TEXT("from"), InSource }, { TEXT("to"), InTarget }, { TEXT("appKey"), AppKey }, { TEXT("salt"), SaltValue },
			{ TEXT("sign"), DreamLocTranslateCrypto::YoudaoSign(AppKey, Text, SaltValue, Curtime, Secret) }, { TEXT("signType"), TEXT("v3") },
			{ TEXT("curtime"), Curtime } });
		Request.Proxy = Profile.Proxy;
		const DreamLocTranslateHttp::FResponse Response = DreamLocTranslateHttp::Send(Request, &InCancel);
		FCall One;
		ReadStatus(Response, One);
		if (!One.Error.IsEmpty())
		{
			return One;
		}
		const TSharedPtr<FJsonObject> Root = ParseObject(Response.Body);
		FString Code;
		const TArray<TSharedPtr<FJsonValue>>* Translation = nullptr;
		if (!Root.IsValid() || !Root->TryGetStringField(TEXT("errorCode"), Code))
		{
			Out.Error = TEXT("the answer has no errorCode");
			return Out;
		}
		if (Code != TEXT("0") || !Root->TryGetArrayField(TEXT("translation"), Translation) || Translation->Num() == 0)
		{
			// 411: too often.
			Out.bRetryable = Code == TEXT("411");
			Out.Error = FString::Printf(TEXT("Youdao error %s"), *Code);
			return Out;
		}
		Out.Texts.Add((*Translation)[0]->AsString());
	}
	Out.bSucceeded = true;
	return Out;
}
