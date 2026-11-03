// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * One HTTP request, waited for: what a translator sends, from a worker thread. The engine's HTTP module completes it on
 * its own thread, so the game thread need not tick for the answer to arrive.
 *
 * Proxy: the engine has one proxy for every request (FHttpModule::SetProxyAddress), which libcurl reads as a request is
 * set up. A request with a proxy of its own sets that one, starts, and puts the previous one back, under a lock, so
 * the requests of different profiles each go their own way.
 */
namespace DreamLocTranslateHttp
{
	struct FRequest
	{
		FString Verb = TEXT("POST");
		FString Url;
		TArray<TPair<FString, FString>> Headers;
		/** Sent as UTF-8. */
		FString Body;
		/** http://host:port; empty for a direct connection. */
		FString Proxy;
		float TimeoutSeconds = 60.0f;
	};

	struct FResponse
	{
		/** False when no answer came at all: no connection, a timeout, cancelled. */
		bool bAnswered = false;
		bool bCancelled = false;
		int32 Status = 0;
		FString Body;
		FString Error;
	};

	DREAMLOCTRANSLATE_API FResponse Send(const FRequest& InRequest, const TAtomic<bool>* InCancel = nullptr);

	/** application/x-www-form-urlencoded, each value percent-encoded as UTF-8. */
	DREAMLOCTRANSLATE_API FString FormEncode(TConstArrayView<TPair<FString, FString>> InFields);
	DREAMLOCTRANSLATE_API FString UrlEncode(const FString& InText);
}
