// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocTranslateHttp.h"

#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/ScopeLock.h"

namespace DreamLocTranslateHttpLocal
{
	/** Held while the engine's one proxy is set for a request of ours and the request is set up. */
	FCriticalSection ProxyLock;

	struct FState
	{
		FEvent* Done = nullptr;
		FCriticalSection Lock;
		DreamLocTranslateHttp::FResponse Response;

		FState() : Done(FPlatformProcess::GetSynchEventFromPool(/*bIsManualReset*/ true)) {}
		~FState() { FPlatformProcess::ReturnSynchEventToPool(Done); }
	};
}

DreamLocTranslateHttp::FResponse DreamLocTranslateHttp::Send(const FRequest& InRequest, const TAtomic<bool>* InCancel)
{
	using namespace DreamLocTranslateHttpLocal;
	TSharedRef<FState, ESPMode::ThreadSafe> State = MakeShared<FState, ESPMode::ThreadSafe>();
	if (InCancel != nullptr && InCancel->Load())
	{
		State->Response.bCancelled = true;
		State->Response.Error = TEXT("cancelled");
		return State->Response;
	}

	FHttpModule& Http = FHttpModule::Get();
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = Http.CreateRequest();
	Request->SetVerb(InRequest.Verb);
	Request->SetURL(InRequest.Url);
	for (const TPair<FString, FString>& Header : InRequest.Headers)
	{
		Request->SetHeader(Header.Key, Header.Value);
	}
	if (!InRequest.Body.IsEmpty() || InRequest.Verb != TEXT("GET"))
	{
		Request->SetContentAsString(InRequest.Body);
	}
	Request->SetTimeout(InRequest.TimeoutSeconds);
	Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);
	Request->OnProcessRequestComplete().BindLambda([State](FHttpRequestPtr, FHttpResponsePtr InResponse, bool bInConnected)
	{
		{
			FScopeLock ScopeLock(&State->Lock);
			State->Response.bAnswered = bInConnected && InResponse.IsValid();
			if (InResponse.IsValid())
			{
				State->Response.Status = InResponse->GetResponseCode();
				State->Response.Body = InResponse->GetContentAsString();
			}
			if (!State->Response.bAnswered && State->Response.Error.IsEmpty())
			{
				State->Response.Error = TEXT("no answer (connection failed or timed out)");
			}
		}
		State->Done->Trigger();
	});

	bool bStarted = false;
	{
		// The proxy is read while the request is set up, which ProcessRequest does before it returns.
		FScopeLock ScopeLock(&ProxyLock);
		const FString Previous = Http.GetProxyAddress();
		Http.SetProxyAddress(InRequest.Proxy);
		bStarted = Request->ProcessRequest();
		Http.SetProxyAddress(Previous);
	}
	if (!bStarted)
	{
		FScopeLock ScopeLock(&State->Lock);
		State->Response.Error = TEXT("the request could not be started");
		return State->Response;
	}

	bool bCancelSent = false;
	while (!State->Done->Wait(FTimespan::FromMilliseconds(50)))
	{
		if (!bCancelSent && InCancel != nullptr && InCancel->Load())
		{
			bCancelSent = true;
			Request->CancelRequest();
		}
	}
	FScopeLock ScopeLock(&State->Lock);
	FResponse Response = State->Response;
	if (bCancelSent)
	{
		Response.bCancelled = true;
		Response.Error = TEXT("cancelled");
	}
	return Response;
}

FString DreamLocTranslateHttp::UrlEncode(const FString& InText)
{
	const FTCHARToUTF8 Utf8(*InText, InText.Len());
	FString Out;
	for (int32 Index = 0; Index < Utf8.Length(); ++Index)
	{
		const uint8 Byte = uint8(Utf8.Get()[Index]);
		const bool bPlain = (Byte >= 'A' && Byte <= 'Z') || (Byte >= 'a' && Byte <= 'z') || (Byte >= '0' && Byte <= '9') || Byte == '-' || Byte == '_' || Byte == '.' || Byte == '~';
		if (bPlain)
		{
			Out.AppendChar(TCHAR(Byte));
		}
		else
		{
			Out += FString::Printf(TEXT("%%%02X"), Byte);
		}
	}
	return Out;
}

FString DreamLocTranslateHttp::FormEncode(TConstArrayView<TPair<FString, FString>> InFields)
{
	TArray<FString> Parts;
	for (const TPair<FString, FString>& Field : InFields)
	{
		Parts.Add(UrlEncode(Field.Key) + TEXT("=") + UrlEncode(Field.Value));
	}
	return FString::Join(Parts, TEXT("&"));
}
