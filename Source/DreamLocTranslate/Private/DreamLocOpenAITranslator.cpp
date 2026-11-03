// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocTranslatorsPrivate.h"

#include "DreamLocTranslateHttp.h"
#include "DreamLocTranslatePrompt.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace DreamLocOpenAITranslatorLocal
{
	/** An error body's message (`{"error": {"message": ...}}`), or the body's start. */
	FString ErrorOf(const FString& InBody)
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InBody);
		const TSharedPtr<FJsonObject>* Error = nullptr;
		FString Message;
		if (FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid() && Root->TryGetObjectField(TEXT("error"), Error) && Error != nullptr
			&& (*Error)->TryGetStringField(TEXT("message"), Message))
		{
			return Message;
		}
		return InBody.Left(200);
	}
}

FDreamLocOpenAITranslator::FDreamLocOpenAITranslator(const FDreamLocTranslateProfile& InProfile)
	: Profile(InProfile)
	, Template(DreamLocTranslatePrompt::LoadTemplate())
{
}

FDreamLocTranslateReply FDreamLocOpenAITranslator::Translate(const FDreamLocTranslateBatch& InBatch, const TAtomic<bool>& InCancel)
{
	using namespace DreamLocOpenAITranslatorLocal;
	FDreamLocTranslateReply Reply;

	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("model"), Profile.Model);
	Body->SetNumberField(TEXT("temperature"), Profile.Temperature);
	TArray<TSharedPtr<FJsonValue>> Messages;
	for (const TPair<const TCHAR*, FString>& Message : {
		TPair<const TCHAR*, FString>(TEXT("system"), DreamLocTranslatePrompt::System(InBatch, Template)),
		TPair<const TCHAR*, FString>(TEXT("user"), DreamLocTranslatePrompt::User(InBatch)) })
	{
		const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("role"), Message.Key);
		Object->SetStringField(TEXT("content"), Message.Value);
		Messages.Add(MakeShared<FJsonValueObject>(Object));
	}
	Body->SetArrayField(TEXT("messages"), Messages);
	if (Profile.bJsonMode)
	{
		const TSharedRef<FJsonObject> Format = MakeShared<FJsonObject>();
		Format->SetStringField(TEXT("type"), TEXT("json_object"));
		Body->SetObjectField(TEXT("response_format"), Format);
	}
	FString Json;
	FJsonSerializer::Serialize(Body, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json));

	DreamLocTranslateHttp::FRequest Request;
	FString Base = Profile.BaseUrl;
	while (Base.EndsWith(TEXT("/")))
	{
		Base.LeftChopInline(1);
	}
	Request.Url = Base + TEXT("/chat/completions");
	Request.Headers.Emplace(TEXT("Content-Type"), TEXT("application/json"));
	const FString Key = Profile.GetApiKey();
	if (!Key.IsEmpty())
	{
		Request.Headers.Emplace(TEXT("Authorization"), TEXT("Bearer ") + Key);
	}
	Request.Body = Json;
	Request.Proxy = Profile.Proxy;
	Request.TimeoutSeconds = 180.0f;

	const DreamLocTranslateHttp::FResponse Response = DreamLocTranslateHttp::Send(Request, &InCancel);
	Reply.HttpStatus = Response.Status;
	if (!Response.bAnswered)
	{
		Reply.bRetryable = !Response.bCancelled;
		Reply.Error = Response.Error;
		return Reply;
	}
	if (Response.Status == 429 || Response.Status >= 500)
	{
		Reply.bRetryable = true;
		Reply.Error = FString::Printf(TEXT("HTTP %d: %s"), Response.Status, *ErrorOf(Response.Body));
		return Reply;
	}
	if (Response.Status != 200)
	{
		Reply.Error = FString::Printf(TEXT("HTTP %d: %s"), Response.Status, *ErrorOf(Response.Body));
		return Reply;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response.Body);
	const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
	FString Content;
	if (FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid() && Root->TryGetArrayField(TEXT("choices"), Choices) && Choices != nullptr && Choices->Num() > 0)
	{
		const TSharedPtr<FJsonObject>* Choice = nullptr;
		const TSharedPtr<FJsonObject>* Message = nullptr;
		if ((*Choices)[0]->TryGetObject(Choice) && (*Choice)->TryGetObjectField(TEXT("message"), Message))
		{
			(*Message)->TryGetStringField(TEXT("content"), Content);
		}
		const TSharedPtr<FJsonObject>* Usage = nullptr;
		if (Root->TryGetObjectField(TEXT("usage"), Usage))
		{
			double Number = 0.0;
			if ((*Usage)->TryGetNumberField(TEXT("prompt_tokens"), Number)) { Reply.PromptTokens = int64(Number); }
			if ((*Usage)->TryGetNumberField(TEXT("completion_tokens"), Number)) { Reply.CompletionTokens = int64(Number); }
		}
	}
	if (Content.IsEmpty() || !DreamLocTranslatePrompt::ParseAnswer(Content, InBatch, Reply))
	{
		Reply.bMalformed = true;
		Reply.Error = TEXT("the answer is not the JSON asked for");
		return Reply;
	}
	Reply.bSucceeded = true;
	return Reply;
}
