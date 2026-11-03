// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * The hashes the services sign requests with, over UTF-8 bytes, as lowercase hex. The engine has MD5 and SHA-1 but no
 * SHA-256, so SHA-256 and its HMAC are here (FIPS 180-4, RFC 2104), tested against the published vectors.
 */
namespace DreamLocTranslateCrypto
{
	DREAMLOCTRANSLATE_API TArray<uint8> Sha256(TConstArrayView<uint8> InData);
	DREAMLOCTRANSLATE_API TArray<uint8> HmacSha256(TConstArrayView<uint8> InKey, TConstArrayView<uint8> InData);

	DREAMLOCTRANSLATE_API TArray<uint8> Utf8(const FString& InText);
	DREAMLOCTRANSLATE_API FString Hex(TConstArrayView<uint8> InBytes);
	DREAMLOCTRANSLATE_API TArray<uint8> FromHex(const FString& InHex);

	DREAMLOCTRANSLATE_API FString Sha256Hex(const FString& InText);
	DREAMLOCTRANSLATE_API FString Md5Hex(const FString& InText);

	/** Baidu: MD5(appid + q + salt + key). */
	DREAMLOCTRANSLATE_API FString BaiduSign(const FString& InAppId, const FString& InQuery, const FString& InSalt, const FString& InKey);

	/** Youdao v3's `input`: q, or its first 10 characters, its length and its last 10 when it is longer than 20. */
	DREAMLOCTRANSLATE_API FString YoudaoInput(const FString& InQuery);
	/** Youdao v3: SHA256(appKey + input + salt + curtime + appSecret). */
	DREAMLOCTRANSLATE_API FString YoudaoSign(const FString& InAppKey, const FString& InQuery, const FString& InSalt, const FString& InCurtime, const FString& InSecret);

	/** Tencent Cloud API 3.0 (TC3-HMAC-SHA256), for a POST of a JSON body with no query string. */
	struct FTencentSignature
	{
		FString CanonicalRequest;
		FString StringToSign;
		FString CredentialScope;
		FString Signature;
		FString Authorization;
	};
	DREAMLOCTRANSLATE_API FString TencentDate(int64 InTimestamp);
	DREAMLOCTRANSLATE_API FTencentSignature TencentSign(const FString& InSecretId, const FString& InSecretKey, const FString& InService, const FString& InHost,
		const FString& InAction, const FString& InPayload, int64 InTimestamp, const FString& InContentType = TEXT("application/json; charset=utf-8"));
	/** The last step alone: HMAC(SecretSigning, StringToSign), for the documentation's published SecretSigning. */
	DREAMLOCTRANSLATE_API FString TencentSignWithSigningKey(TConstArrayView<uint8> InSecretSigning, const FString& InStringToSign);
	/** SecretSigning from the SecretKey: HMAC chain over "TC3" + key, the date, the service and "tc3_request". */
	DREAMLOCTRANSLATE_API TArray<uint8> TencentSigningKey(const FString& InSecretKey, const FString& InDate, const FString& InService);
}
