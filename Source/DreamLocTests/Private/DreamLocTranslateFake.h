// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "DreamLocTranslateCrypto.h"
#include "DreamLocTranslateTypes.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "HttpPath.h"
#include "HttpResultCallback.h"
#include "HttpRouteHandle.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "IPAddress.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

/**
 * Translation services on 127.0.0.1, for tests that never leave the machine: an OpenAI-compatible chat completions
 * service and the five machine translators, each checking its key or signature the way the real one does. They run on
 * the game thread (the HTTP server ticks there), so the code under test runs on another.
 *
 * A machine "translation" is the text with `[<target code>] ` in front: what the placeholders protect comes back as it
 * went. A text with LOSE in it comes back without its placeholders.
 */
namespace DreamLocTranslateFake
{
	struct FCall
	{
		FString Service;
		FString Body;
		/** Lower-case name -> the values, joined again with ", " (the server splits them at commas). */
		TMap<FString, FString> Headers;
	};

	/** One request to the large model, as the fake read it. */
	struct FChat
	{
		FString System;
		TArray<TSharedPtr<FJsonObject>> Entries;
		/** Which request this is, from 0. */
		int32 Call = 0;
	};

	struct FChatAnswer
	{
		int32 Status = 200;
		/** The assistant's message. */
		FString Content;
		/** The whole body as it is, instead of a chat completion around Content. */
		FString RawBody;
	};

	inline FString Serialize(const TSharedRef<FJsonObject>& InObject)
	{
		FString Out;
		FJsonSerializer::Serialize(InObject, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out));
		return Out;
	}

	inline TSharedPtr<FJsonObject> Parse(const FString& InText)
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InText);
		return FJsonSerializer::Deserialize(Reader, Root) ? Root : nullptr;
	}

	/** The default translation of a large model: the culture and the source, so every argument, tag and break stays. */
	inline FString Mark(const FString& InCulture, const FString& InSource)
	{
		return InCulture + TEXT(": ") + InSource;
	}

	/** An answer: these texts by id. */
	inline FString Entries(TConstArrayView<TPair<int32, FString>> InTexts)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const TPair<int32, FString>& Text : InTexts)
		{
			const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetNumberField(TEXT("id"), Text.Key);
			Entry->SetStringField(TEXT("text"), Text.Value);
			Values.Add(MakeShared<FJsonValueObject>(Entry));
		}
		const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetArrayField(TEXT("entries"), Values);
		return Serialize(Root);
	}

	/** Every entry of a request marked. */
	inline FChatAnswer MarkAll(const FChat& InChat, const FString& InCulture = TEXT("en"))
	{
		TArray<TPair<int32, FString>> Texts;
		for (const TSharedPtr<FJsonObject>& Entry : InChat.Entries)
		{
			Texts.Emplace(int32(Entry->GetNumberField(TEXT("id"))), Mark(InCulture, Entry->GetStringField(TEXT("src"))));
		}
		FChatAnswer Answer;
		Answer.Content = Entries(Texts);
		return Answer;
	}

	class FServices : public TSharedFromThis<FServices, ESPMode::ThreadSafe>
	{
	public:
		FString OpenAIKey = TEXT("sk-fake-openai-0001");
		FString DeepLKey = TEXT("fake-deepl-0001:fx");
		FString GoogleKey = TEXT("fake-google-0001");
		FString BaiduAppId = TEXT("2024000000000001");
		FString BaiduKey = TEXT("fake-baidu-key-0001");
		FString TencentId = TEXT("AKIDfake0001");
		FString TencentKey = TEXT("fake-tencent-key-0001");
		FString YoudaoKey = TEXT("fake-youdao-app-0001");
		FString YoudaoSecret = TEXT("fake-youdao-secret-0001");

		/** The large model; unset: every entry marked. */
		TFunction<FChatAnswer(const FChat&)> Chat;
		/** Seconds each answer waits before it is sent. */
		float Delay = 0.0f;

		~FServices() { Stop(); }

		bool Start(FString& OutError)
		{
			// A free port: bound, read, let go, then listened on.
			ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
			FSocket* Probe = Sockets != nullptr ? Sockets->CreateSocket(NAME_Stream, TEXT("DreamLocFakePort"), false) : nullptr;
			if (Probe == nullptr)
			{
				OutError = TEXT("no socket to find a free port with");
				return false;
			}
			const TSharedRef<FInternetAddr> Address = Sockets->CreateInternetAddr();
			Address->SetLoopbackAddress();
			Address->SetPort(0);
			if (Probe->Bind(*Address))
			{
				Probe->GetAddress(*Address);
				Port = uint32(Address->GetPort());
			}
			Probe->Close();
			Sockets->DestroySocket(Probe);
			if (Port == 0)
			{
				OutError = TEXT("no free port on 127.0.0.1");
				return false;
			}
			FHttpServerModule& Server = FHttpServerModule::Get();
			Router = Server.GetHttpRouter(Port, /*bFailOnBindFailure*/ true);
			if (!Router.IsValid())
			{
				OutError = FString::Printf(TEXT("the HTTP server does not listen on port %u"), Port);
				return false;
			}
			auto Bind = [this](const TCHAR* InPath, bool (FServices::*InHandler)(const FHttpServerRequest&, const FHttpResultCallback&))
			{
				Routes.Add(Router->BindRoute(FHttpPath(InPath), EHttpServerRequestVerbs::VERB_POST, FHttpRequestHandler::CreateSP(this, InHandler)));
			};
			Bind(TEXT("/openai"), &FServices::OnOpenAI);
			Bind(TEXT("/deepl"), &FServices::OnDeepL);
			Bind(TEXT("/google"), &FServices::OnGoogle);
			Bind(TEXT("/baidu"), &FServices::OnBaidu);
			Bind(TEXT("/tencent"), &FServices::OnTencent);
			Bind(TEXT("/youdao"), &FServices::OnYoudao);
			Server.StartAllListeners();
			const TWeakPtr<FServices, ESPMode::ThreadSafe> Weak = AsShared();
			Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak](float)
			{
				if (const TSharedPtr<FServices, ESPMode::ThreadSafe> Services = Weak.Pin())
				{
					Services->Flush();
				}
				return true;
			}));
			return true;
		}

		void Stop()
		{
			if (Ticker.IsValid())
			{
				FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
				Ticker.Reset();
			}
			for (FDeferred& Waiting : Deferred)
			{
				Waiting.Done(FHttpServerResponse::Error(EHttpServerResponseCodes::ServerError));
			}
			Deferred.Reset();
			if (Router.IsValid())
			{
				for (const FHttpRouteHandle& Route : Routes)
				{
					if (Route.IsValid())
					{
						Router->UnbindRoute(Route);
					}
				}
			}
			Routes.Reset();
			Router.Reset();
		}

		FString Url(const TCHAR* InService) const
		{
			return FString::Printf(TEXT("http://127.0.0.1:%u/%s"), Port, InService);
		}

		/** A profile of that type at this fake, with the keys it checks; Local unless bInCloud. */
		FDreamLocTranslateProfile Profile(const TCHAR* InType, const TCHAR* InName, bool bInCloud = false) const
		{
			FDreamLocTranslateProfile Out;
			Out.Name = InName;
			Out.Type = InType;
			Out.Local = !bInCloud;
			const FString Type = FString(InType).ToLower();
			if (Type == TEXT("openai"))
			{
				Out.BaseUrl = Url(TEXT("openai/v1"));
				Out.Model = TEXT("fake-model");
				Out.ApiKey = OpenAIKey;
				Out.bJsonMode = true;
			}
			else if (Type == TEXT("deepl")) { Out.BaseUrl = Url(TEXT("deepl")); Out.ApiKey = DeepLKey; }
			else if (Type == TEXT("google")) { Out.BaseUrl = Url(TEXT("google")); Out.ApiKey = GoogleKey; }
			else if (Type == TEXT("baidu")) { Out.BaseUrl = Url(TEXT("baidu")); Out.AppId = BaiduAppId; Out.Secret = BaiduKey; }
			else if (Type == TEXT("tencent")) { Out.BaseUrl = Url(TEXT("tencent")); Out.AppId = TencentId; Out.Secret = TencentKey; Out.Region = TEXT("ap-guangzhou"); }
			else if (Type == TEXT("youdao")) { Out.BaseUrl = Url(TEXT("youdao")); Out.AppId = YoudaoKey; Out.Secret = YoudaoSecret; }
			return Out;
		}

		TArray<FCall> GetCalls(const TCHAR* InService = nullptr) const
		{
			if (InService == nullptr)
			{
				return Calls;
			}
			return Calls.FilterByPredicate([InService](const FCall& InCall) { return InCall.Service == InService; });
		}

		int32 GetMaxPending() const { return MaxPending; }
		int32 GetSignatureFailures() const { return SignatureFailures; }
		/** What each signed request was checked against, by service: how many passed. */
		int32 GetSignaturesChecked() const { return SignaturesChecked; }

	private:
		struct FDeferred
		{
			double At = 0.0;
			FHttpResultCallback Done;
			int32 Status = 200;
			FString Body;
		};

		uint32 Port = 0;
		TSharedPtr<IHttpRouter> Router;
		TArray<FHttpRouteHandle> Routes;
		FTSTicker::FDelegateHandle Ticker;
		TArray<FDeferred> Deferred;
		TArray<FCall> Calls;
		int32 ChatCalls = 0;
		int32 MaxPending = 0;
		int32 SignatureFailures = 0;
		int32 SignaturesChecked = 0;

		static TUniquePtr<FHttpServerResponse> Make(int32 InStatus, const FString& InBody)
		{
			TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(InBody, TEXT("application/json"));
			Response->Code = EHttpServerResponseCodes(InStatus);
			return Response;
		}

		void Answer(const FHttpResultCallback& InDone, int32 InStatus, const FString& InBody)
		{
			if (Delay <= 0.0f)
			{
				InDone(Make(InStatus, InBody));
				return;
			}
			Deferred.Add({ FPlatformTime::Seconds() + Delay, InDone, InStatus, InBody });
			MaxPending = FMath::Max(MaxPending, Deferred.Num());
		}

		void Flush()
		{
			const double Now = FPlatformTime::Seconds();
			for (int32 Index = 0; Index < Deferred.Num();)
			{
				if (Deferred[Index].At <= Now)
				{
					const FDeferred Due = Deferred[Index];
					Deferred.RemoveAt(Index);
					Due.Done(Make(Due.Status, Due.Body));
				}
				else
				{
					++Index;
				}
			}
		}

		FCall& Record(const TCHAR* InService, const FHttpServerRequest& InRequest)
		{
			FCall& Call = Calls.AddDefaulted_GetRef();
			Call.Service = InService;
			const FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(InRequest.Body.GetData()), InRequest.Body.Num());
			Call.Body = FString::ConstructFromPtrSize(Text.Get(), Text.Length());
			for (const TPair<FString, TArray<FString>>& Header : InRequest.Headers)
			{
				TArray<FString> Values;
				for (const FString& Value : Header.Value)
				{
					Values.Add(Value.TrimStartAndEnd());
				}
				Call.Headers.Add(Header.Key.ToLower(), FString::Join(Values, TEXT(", ")));
			}
			return Call;
		}

		static TMap<FString, FString> ReadForm(const FString& InBody)
		{
			TMap<FString, FString> Fields;
			TArray<FString> Pairs;
			InBody.ParseIntoArray(Pairs, TEXT("&"));
			for (const FString& Pair : Pairs)
			{
				FString Name;
				FString Value;
				if (!Pair.Split(TEXT("="), &Name, &Value))
				{
					Name = Pair;
				}
				Fields.Add(FGenericPlatformHttp::UrlDecode(Name), FGenericPlatformHttp::UrlDecode(Value.Replace(TEXT("+"), TEXT(" "))));
			}
			return Fields;
		}

		/** A machine translation: the target code in front; a text with LOSE in it loses its placeholders. */
		static FString Machine(const FString& InText, const FString& InTarget)
		{
			FString Text = InText;
			if (Text.Contains(TEXT("LOSE")))
			{
				for (const TCHAR* Open : { TEXT("⟦"), TEXT("<x "), TEXT("<span") })
				{
					for (int32 At = Text.Find(Open); At != INDEX_NONE; At = Text.Find(Open))
					{
						const TCHAR* Close = FCString::Strcmp(Open, TEXT("⟦")) == 0 ? TEXT("⟧") : FCString::Strcmp(Open, TEXT("<x ")) == 0 ? TEXT("/>") : TEXT("</span>");
						const int32 End = Text.Find(Close, ESearchCase::CaseSensitive, ESearchDir::FromStart, At);
						Text.RemoveAt(At, End == INDEX_NONE ? 1 : End - At + FCString::Strlen(Close));
					}
				}
			}
			return FString::Printf(TEXT("[%s] %s"), *InTarget, *Text);
		}

		bool OnOpenAI(const FHttpServerRequest& InRequest, const FHttpResultCallback& InDone)
		{
			const FCall& Call = Record(TEXT("openai"), InRequest);
			if (!OpenAIKey.IsEmpty() && Call.Headers.FindRef(TEXT("authorization")) != TEXT("Bearer ") + OpenAIKey)
			{
				++SignatureFailures;
				Answer(InDone, 401, TEXT("{\"error\":{\"message\":\"Incorrect API key provided\"}}"));
				return true;
			}
			++SignaturesChecked;
			FChat Asked;
			Asked.Call = ChatCalls++;
			const TSharedPtr<FJsonObject> Root = Parse(Call.Body);
			const TArray<TSharedPtr<FJsonValue>>* Messages = nullptr;
			if (Root.IsValid() && Root->TryGetArrayField(TEXT("messages"), Messages) && Messages->Num() == 2)
			{
				Asked.System = (*Messages)[0]->AsObject()->GetStringField(TEXT("content"));
				const TSharedPtr<FJsonObject> User = Parse((*Messages)[1]->AsObject()->GetStringField(TEXT("content")));
				const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
				if (User.IsValid() && User->TryGetArrayField(TEXT("entries"), Entries))
				{
					for (const TSharedPtr<FJsonValue>& Entry : *Entries)
					{
						Asked.Entries.Add(Entry->AsObject());
					}
				}
			}
			const FChatAnswer Reply = this->Chat ? this->Chat(Asked) : MarkAll(Asked);
			if (!Reply.RawBody.IsEmpty() || Reply.Status != 200)
			{
				Answer(InDone, Reply.Status, Reply.RawBody);
				return true;
			}
			const TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
			Message->SetStringField(TEXT("role"), TEXT("assistant"));
			Message->SetStringField(TEXT("content"), Reply.Content);
			const TSharedRef<FJsonObject> Choice = MakeShared<FJsonObject>();
			Choice->SetObjectField(TEXT("message"), Message);
			const TSharedRef<FJsonObject> Usage = MakeShared<FJsonObject>();
			Usage->SetNumberField(TEXT("prompt_tokens"), 100);
			Usage->SetNumberField(TEXT("completion_tokens"), 20);
			const TSharedRef<FJsonObject> Completion = MakeShared<FJsonObject>();
			TArray<TSharedPtr<FJsonValue>> Choices;
			Choices.Add(MakeShared<FJsonValueObject>(Choice));
			Completion->SetArrayField(TEXT("choices"), Choices);
			Completion->SetObjectField(TEXT("usage"), Usage);
			Answer(InDone, 200, Serialize(Completion));
			return true;
		}

		bool OnDeepL(const FHttpServerRequest& InRequest, const FHttpResultCallback& InDone)
		{
			const FCall& Call = Record(TEXT("deepl"), InRequest);
			if (Call.Headers.FindRef(TEXT("authorization")) != TEXT("DeepL-Auth-Key ") + DeepLKey)
			{
				++SignatureFailures;
				Answer(InDone, 403, TEXT("{\"message\":\"Authorization failed\"}"));
				return true;
			}
			++SignaturesChecked;
			const TSharedPtr<FJsonObject> Root = Parse(Call.Body);
			TArray<TSharedPtr<FJsonValue>> Out;
			const FString Target = Root.IsValid() ? Root->GetStringField(TEXT("target_lang")) : FString();
			for (const TSharedPtr<FJsonValue>& Text : Root.IsValid() ? Root->GetArrayField(TEXT("text")) : TArray<TSharedPtr<FJsonValue>>())
			{
				const TSharedRef<FJsonObject> Translation = MakeShared<FJsonObject>();
				Translation->SetStringField(TEXT("detected_source_language"), TEXT("ZH"));
				Translation->SetStringField(TEXT("text"), Machine(Text->AsString(), Target));
				Out.Add(MakeShared<FJsonValueObject>(Translation));
			}
			const TSharedRef<FJsonObject> Answered = MakeShared<FJsonObject>();
			Answered->SetArrayField(TEXT("translations"), Out);
			Answer(InDone, 200, Serialize(Answered));
			return true;
		}

		bool OnGoogle(const FHttpServerRequest& InRequest, const FHttpResultCallback& InDone)
		{
			const FCall& Call = Record(TEXT("google"), InRequest);
			if (Call.Headers.FindRef(TEXT("x-goog-api-key")) != GoogleKey)
			{
				++SignatureFailures;
				Answer(InDone, 400, TEXT("{\"error\":{\"code\":400,\"message\":\"API key not valid. Please pass a valid API key.\"}}"));
				return true;
			}
			++SignaturesChecked;
			const TSharedPtr<FJsonObject> Root = Parse(Call.Body);
			TArray<TSharedPtr<FJsonValue>> Out;
			const FString Target = Root.IsValid() ? Root->GetStringField(TEXT("target")) : FString();
			for (const TSharedPtr<FJsonValue>& Text : Root.IsValid() ? Root->GetArrayField(TEXT("q")) : TArray<TSharedPtr<FJsonValue>>())
			{
				const TSharedRef<FJsonObject> Translation = MakeShared<FJsonObject>();
				Translation->SetStringField(TEXT("translatedText"), Machine(Text->AsString(), Target));
				Out.Add(MakeShared<FJsonValueObject>(Translation));
			}
			const TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
			Data->SetArrayField(TEXT("translations"), Out);
			const TSharedRef<FJsonObject> Answered = MakeShared<FJsonObject>();
			Answered->SetObjectField(TEXT("data"), Data);
			Answer(InDone, 200, Serialize(Answered));
			return true;
		}

		bool OnBaidu(const FHttpServerRequest& InRequest, const FHttpResultCallback& InDone)
		{
			const FCall& Call = Record(TEXT("baidu"), InRequest);
			const TMap<FString, FString> Form = ReadForm(Call.Body);
			const FString Query = Form.FindRef(TEXT("q"));
			if (Form.FindRef(TEXT("appid")) != BaiduAppId
				|| Form.FindRef(TEXT("sign")) != DreamLocTranslateCrypto::BaiduSign(BaiduAppId, Query, Form.FindRef(TEXT("salt")), BaiduKey))
			{
				++SignatureFailures;
				Answer(InDone, 200, TEXT("{\"error_code\":\"54001\",\"error_msg\":\"Invalid Sign\"}"));
				return true;
			}
			++SignaturesChecked;
			TArray<FString> Lines;
			Query.ParseIntoArray(Lines, TEXT("\n"), /*CullEmpty*/ false);
			TArray<TSharedPtr<FJsonValue>> Out;
			for (const FString& Line : Lines)
			{
				const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
				Result->SetStringField(TEXT("src"), Line);
				Result->SetStringField(TEXT("dst"), Machine(Line, Form.FindRef(TEXT("to"))));
				Out.Add(MakeShared<FJsonValueObject>(Result));
			}
			const TSharedRef<FJsonObject> Answered = MakeShared<FJsonObject>();
			Answered->SetStringField(TEXT("from"), Form.FindRef(TEXT("from")));
			Answered->SetStringField(TEXT("to"), Form.FindRef(TEXT("to")));
			Answered->SetArrayField(TEXT("trans_result"), Out);
			Answer(InDone, 200, Serialize(Answered));
			return true;
		}

		bool OnTencent(const FHttpServerRequest& InRequest, const FHttpResultCallback& InDone)
		{
			const FCall& Call = Record(TEXT("tencent"), InRequest);
			const int64 Timestamp = FCString::Atoi64(*Call.Headers.FindRef(TEXT("x-tc-timestamp")));
			const DreamLocTranslateCrypto::FTencentSignature Expected = DreamLocTranslateCrypto::TencentSign(TencentId, TencentKey, TEXT("tmt"),
				Call.Headers.FindRef(TEXT("host")), Call.Headers.FindRef(TEXT("x-tc-action")), Call.Body, Timestamp, Call.Headers.FindRef(TEXT("content-type")));
			TArray<FString> Parts;
			Expected.Authorization.ParseIntoArray(Parts, TEXT(","));
			for (FString& Part : Parts)
			{
				Part.TrimStartAndEndInline();
			}
			const TSharedRef<FJsonObject> Response = MakeShared<FJsonObject>();
			Response->SetStringField(TEXT("RequestId"), TEXT("fake"));
			if (Call.Headers.FindRef(TEXT("authorization")) != FString::Join(Parts, TEXT(", ")) || Call.Headers.FindRef(TEXT("x-tc-version")) != TEXT("2018-03-21")
				|| FMath::Abs(FDateTime::UtcNow().ToUnixTimestamp() - Timestamp) > 300)
			{
				++SignatureFailures;
				const TSharedRef<FJsonObject> Error = MakeShared<FJsonObject>();
				Error->SetStringField(TEXT("Code"), TEXT("AuthFailure.SignatureFailure"));
				Error->SetStringField(TEXT("Message"), TEXT("The provided credentials could not be validated."));
				Response->SetObjectField(TEXT("Error"), Error);
			}
			else
			{
				++SignaturesChecked;
				const TSharedPtr<FJsonObject> Root = Parse(Call.Body);
				TArray<TSharedPtr<FJsonValue>> Out;
				for (const TSharedPtr<FJsonValue>& Text : Root.IsValid() ? Root->GetArrayField(TEXT("SourceTextList")) : TArray<TSharedPtr<FJsonValue>>())
				{
					Out.Add(MakeShared<FJsonValueString>(Machine(Text->AsString(), Root->GetStringField(TEXT("Target")))));
				}
				Response->SetArrayField(TEXT("TargetTextList"), Out);
			}
			const TSharedRef<FJsonObject> Answered = MakeShared<FJsonObject>();
			Answered->SetObjectField(TEXT("Response"), Response);
			Answer(InDone, 200, Serialize(Answered));
			return true;
		}

		bool OnYoudao(const FHttpServerRequest& InRequest, const FHttpResultCallback& InDone)
		{
			const FCall& Call = Record(TEXT("youdao"), InRequest);
			const TMap<FString, FString> Form = ReadForm(Call.Body);
			const FString Query = Form.FindRef(TEXT("q"));
			if (Form.FindRef(TEXT("appKey")) != YoudaoKey || Form.FindRef(TEXT("signType")) != TEXT("v3")
				|| Form.FindRef(TEXT("sign")) != DreamLocTranslateCrypto::YoudaoSign(YoudaoKey, Query, Form.FindRef(TEXT("salt")), Form.FindRef(TEXT("curtime")), YoudaoSecret))
			{
				++SignatureFailures;
				Answer(InDone, 200, TEXT("{\"errorCode\":\"202\"}"));
				return true;
			}
			++SignaturesChecked;
			const TSharedRef<FJsonObject> Answered = MakeShared<FJsonObject>();
			Answered->SetStringField(TEXT("errorCode"), TEXT("0"));
			TArray<TSharedPtr<FJsonValue>> Translation;
			Translation.Add(MakeShared<FJsonValueString>(Machine(Query, Form.FindRef(TEXT("to")))));
			Answered->SetArrayField(TEXT("translation"), Translation);
			Answer(InDone, 200, Serialize(Answered));
			return true;
		}
	};
}
