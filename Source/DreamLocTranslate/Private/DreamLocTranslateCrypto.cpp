// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocTranslateCrypto.h"

#include "Misc/DateTime.h"
#include "Misc/SecureHash.h"

namespace DreamLocTranslateCryptoLocal
{
	constexpr uint32 K[64] = {
		0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
		0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
		0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
		0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
		0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
		0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
		0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
		0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
	};

	FORCEINLINE uint32 Rotr(uint32 InValue, uint32 InBits)
	{
		return (InValue >> InBits) | (InValue << (32 - InBits));
	}

	void Block(uint32 InOutState[8], const uint8* InBlock)
	{
		uint32 W[64];
		for (int32 Index = 0; Index < 16; ++Index)
		{
			W[Index] = (uint32(InBlock[Index * 4]) << 24) | (uint32(InBlock[Index * 4 + 1]) << 16) | (uint32(InBlock[Index * 4 + 2]) << 8) | uint32(InBlock[Index * 4 + 3]);
		}
		for (int32 Index = 16; Index < 64; ++Index)
		{
			const uint32 S0 = Rotr(W[Index - 15], 7) ^ Rotr(W[Index - 15], 18) ^ (W[Index - 15] >> 3);
			const uint32 S1 = Rotr(W[Index - 2], 17) ^ Rotr(W[Index - 2], 19) ^ (W[Index - 2] >> 10);
			W[Index] = W[Index - 16] + S0 + W[Index - 7] + S1;
		}
		uint32 A = InOutState[0], B = InOutState[1], C = InOutState[2], D = InOutState[3];
		uint32 E = InOutState[4], F = InOutState[5], G = InOutState[6], H = InOutState[7];
		for (int32 Index = 0; Index < 64; ++Index)
		{
			const uint32 S1 = Rotr(E, 6) ^ Rotr(E, 11) ^ Rotr(E, 25);
			const uint32 Choose = (E & F) ^ (~E & G);
			const uint32 Temp1 = H + S1 + Choose + K[Index] + W[Index];
			const uint32 S0 = Rotr(A, 2) ^ Rotr(A, 13) ^ Rotr(A, 22);
			const uint32 Majority = (A & B) ^ (A & C) ^ (B & C);
			const uint32 Temp2 = S0 + Majority;
			H = G; G = F; F = E; E = D + Temp1; D = C; C = B; B = A; A = Temp1 + Temp2;
		}
		InOutState[0] += A; InOutState[1] += B; InOutState[2] += C; InOutState[3] += D;
		InOutState[4] += E; InOutState[5] += F; InOutState[6] += G; InOutState[7] += H;
	}

	TArray<uint8> Bytes(const FString& InText)
	{
		return DreamLocTranslateCrypto::Utf8(InText);
	}

	TArray<uint8> Hmac(const TArray<uint8>& InKey, const FString& InText)
	{
		const TArray<uint8> Data = Bytes(InText);
		return DreamLocTranslateCrypto::HmacSha256(InKey, Data);
	}
}

TArray<uint8> DreamLocTranslateCrypto::Sha256(TConstArrayView<uint8> InData)
{
	using namespace DreamLocTranslateCryptoLocal;
	uint32 State[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
	const uint64 Length = uint64(InData.Num());
	int64 Offset = 0;
	for (; Offset + 64 <= int64(Length); Offset += 64)
	{
		Block(State, InData.GetData() + Offset);
	}
	// The tail, the 0x80 byte, zeros and the length in bits, in one block or two.
	uint8 Tail[128] = {};
	const int32 Rest = int32(Length - Offset);
	if (Rest > 0)
	{
		FMemory::Memcpy(Tail, InData.GetData() + Offset, Rest);
	}
	Tail[Rest] = 0x80;
	const int32 TailLength = Rest + 1 + 8 <= 64 ? 64 : 128;
	const uint64 Bits = Length * 8;
	for (int32 Index = 0; Index < 8; ++Index)
	{
		Tail[TailLength - 1 - Index] = uint8(Bits >> (Index * 8));
	}
	Block(State, Tail);
	if (TailLength == 128)
	{
		Block(State, Tail + 64);
	}
	TArray<uint8> Out;
	Out.SetNumUninitialized(32);
	for (int32 Index = 0; Index < 8; ++Index)
	{
		Out[Index * 4] = uint8(State[Index] >> 24);
		Out[Index * 4 + 1] = uint8(State[Index] >> 16);
		Out[Index * 4 + 2] = uint8(State[Index] >> 8);
		Out[Index * 4 + 3] = uint8(State[Index]);
	}
	return Out;
}

TArray<uint8> DreamLocTranslateCrypto::HmacSha256(TConstArrayView<uint8> InKey, TConstArrayView<uint8> InData)
{
	TArray<uint8> Key(InKey.GetData(), InKey.Num());
	if (Key.Num() > 64)
	{
		Key = Sha256(Key);
	}
	Key.SetNumZeroed(64);
	TArray<uint8> Inner;
	TArray<uint8> Outer;
	Inner.Reserve(64 + InData.Num());
	Outer.Reserve(64 + 32);
	for (int32 Index = 0; Index < 64; ++Index)
	{
		Inner.Add(Key[Index] ^ 0x36);
		Outer.Add(Key[Index] ^ 0x5c);
	}
	Inner.Append(InData.GetData(), InData.Num());
	Outer.Append(Sha256(Inner));
	return Sha256(Outer);
}

TArray<uint8> DreamLocTranslateCrypto::Utf8(const FString& InText)
{
	const FTCHARToUTF8 Converted(*InText, InText.Len());
	return TArray<uint8>(reinterpret_cast<const uint8*>(Converted.Get()), Converted.Length());
}

FString DreamLocTranslateCrypto::Hex(TConstArrayView<uint8> InBytes)
{
	FString Out;
	Out.Reserve(InBytes.Num() * 2);
	for (const uint8 Byte : InBytes)
	{
		Out += FString::Printf(TEXT("%02x"), Byte);
	}
	return Out;
}

TArray<uint8> DreamLocTranslateCrypto::FromHex(const FString& InHex)
{
	TArray<uint8> Out;
	for (int32 Index = 0; Index + 1 < InHex.Len(); Index += 2)
	{
		Out.Add(uint8(FParse::HexDigit(InHex[Index]) * 16 + FParse::HexDigit(InHex[Index + 1])));
	}
	return Out;
}

FString DreamLocTranslateCrypto::Sha256Hex(const FString& InText)
{
	return Hex(Sha256(Utf8(InText)));
}

FString DreamLocTranslateCrypto::Md5Hex(const FString& InText)
{
	const TArray<uint8> Data = Utf8(InText);
	uint8 Digest[16];
	FMD5 Md5;
	Md5.Update(Data.GetData(), Data.Num());
	Md5.Final(Digest);
	return Hex(TConstArrayView<uint8>(Digest, 16));
}

FString DreamLocTranslateCrypto::BaiduSign(const FString& InAppId, const FString& InQuery, const FString& InSalt, const FString& InKey)
{
	return Md5Hex(InAppId + InQuery + InSalt + InKey);
}

FString DreamLocTranslateCrypto::YoudaoInput(const FString& InQuery)
{
	const int32 Length = InQuery.Len();
	return Length <= 20 ? InQuery : InQuery.Left(10) + FString::FromInt(Length) + InQuery.Right(10);
}

FString DreamLocTranslateCrypto::YoudaoSign(const FString& InAppKey, const FString& InQuery, const FString& InSalt, const FString& InCurtime, const FString& InSecret)
{
	return Sha256Hex(InAppKey + YoudaoInput(InQuery) + InSalt + InCurtime + InSecret);
}

FString DreamLocTranslateCrypto::TencentDate(int64 InTimestamp)
{
	const FDateTime Time = FDateTime::FromUnixTimestamp(InTimestamp);
	return FString::Printf(TEXT("%04d-%02d-%02d"), Time.GetYear(), Time.GetMonth(), Time.GetDay());
}

TArray<uint8> DreamLocTranslateCrypto::TencentSigningKey(const FString& InSecretKey, const FString& InDate, const FString& InService)
{
	using namespace DreamLocTranslateCryptoLocal;
	const TArray<uint8> SecretDate = Hmac(Bytes(TEXT("TC3") + InSecretKey), InDate);
	const TArray<uint8> SecretService = Hmac(SecretDate, InService);
	return Hmac(SecretService, TEXT("tc3_request"));
}

FString DreamLocTranslateCrypto::TencentSignWithSigningKey(TConstArrayView<uint8> InSecretSigning, const FString& InStringToSign)
{
	const TArray<uint8> Data = Utf8(InStringToSign);
	return Hex(HmacSha256(InSecretSigning, Data));
}

DreamLocTranslateCrypto::FTencentSignature DreamLocTranslateCrypto::TencentSign(const FString& InSecretId, const FString& InSecretKey, const FString& InService,
	const FString& InHost, const FString& InAction, const FString& InPayload, int64 InTimestamp, const FString& InContentType)
{
	FTencentSignature Out;
	const FString SignedHeaders = TEXT("content-type;host;x-tc-action");
	Out.CanonicalRequest = FString::Printf(TEXT("POST\n/\n\ncontent-type:%s\nhost:%s\nx-tc-action:%s\n\n%s\n%s"),
		*InContentType, *InHost, *InAction.ToLower(), *SignedHeaders, *Sha256Hex(InPayload));
	const FString Date = TencentDate(InTimestamp);
	Out.CredentialScope = FString::Printf(TEXT("%s/%s/tc3_request"), *Date, *InService);
	Out.StringToSign = FString::Printf(TEXT("TC3-HMAC-SHA256\n%lld\n%s\n%s"), InTimestamp, *Out.CredentialScope, *Sha256Hex(Out.CanonicalRequest));
	Out.Signature = TencentSignWithSigningKey(TencentSigningKey(InSecretKey, Date, InService), Out.StringToSign);
	Out.Authorization = FString::Printf(TEXT("TC3-HMAC-SHA256 Credential=%s/%s, SignedHeaders=%s, Signature=%s"),
		*InSecretId, *Out.CredentialScope, *SignedHeaders, *Out.Signature);
	return Out;
}
