// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocSettings.h"
#include "DreamLocTestUtil.h"
#include "DreamLocTranslateConfig.h"
#include "DreamLocTranslateCrypto.h"
#include "DreamLocTranslateProtect.h"
#include "DreamLocTranslateTypes.h"
#include "DreamLocTranslator.h"
#include "HAL/PlatformMisc.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamLocTranslateUnitTestsLocal
{
	FString HexOf(TConstArrayView<uint8> InBytes)
	{
		return DreamLocTranslateCrypto::Hex(InBytes);
	}

	TArray<uint8> Bytes(uint8 InByte, int32 InCount)
	{
		TArray<uint8> Out;
		Out.Init(InByte, InCount);
		return Out;
	}
}

// ---------------------------------------------------------------------------------------------------------- signatures

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocTranslateShaTest, "DreamLoc.Translate.Sign.Sha256AndHmacMatchTheStandardVectors",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FDreamLocTranslateShaTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocTranslateCrypto;
	using namespace DreamLocTranslateUnitTestsLocal;
	// FIPS 180-4 examples: empty, one block, two blocks, a million a's.
	TestEqual(TEXT("sha256('')"), Sha256Hex(FString()), TEXT("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
	TestEqual(TEXT("sha256('abc')"), Sha256Hex(TEXT("abc")), TEXT("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
	TestEqual(TEXT("sha256 of two blocks"), Sha256Hex(TEXT("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")),
		TEXT("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
	TestEqual(TEXT("sha256 of a million a's"), Sha256Hex(FString::ChrN(1000000, TEXT('a'))), TEXT("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
	// RFC 4231: cases 1, 2 and 6 (a key longer than a block).
	TestEqual(TEXT("HMAC case 1"), HexOf(HmacSha256(Bytes(0x0b, 20), Utf8(TEXT("Hi There")))),
		TEXT("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"));
	TestEqual(TEXT("HMAC case 2"), HexOf(HmacSha256(Utf8(TEXT("Jefe")), Utf8(TEXT("what do ya want for nothing?")))),
		TEXT("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"));
	TestEqual(TEXT("HMAC case 6"), HexOf(HmacSha256(Bytes(0xaa, 131), Utf8(TEXT("Test Using Larger Than Block-Size Key - Hash Key First")))),
		TEXT("60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54"));
	TestEqual(TEXT("md5('')"), Md5Hex(FString()), TEXT("d41d8cd98f00b204e9800998ecf8427e"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocTranslateTencentSignTest, "DreamLoc.Translate.Sign.TencentTc3MatchesTheDocumentationExample",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FDreamLocTranslateTencentSignTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocTranslateCrypto;
	using namespace DreamLocTranslateUnitTestsLocal;
	// Tencent Cloud API 3.0, "签名方法 v3" (document 213/30654): DescribeInstances of cvm at 1551113065. The document masks
	// the SecretKey, so the chain is checked from its published SecretDate on; the HMAC itself against RFC 4231 above.
	const FString Payload = TEXT("{\"Limit\": 1, \"Filters\": [{\"Values\": [\"\\u672a\\u547d\\u540d\"], \"Name\": \"instance-name\"}]}");
	TestEqual(TEXT("HashedRequestPayload"), Sha256Hex(Payload), TEXT("35e9c5b0e3ae67532d3c9f17ead6c90222632e5b1ff7f6e89887f1398934f064"));
	TestEqual(TEXT("the date of the timestamp, in UTC"), TencentDate(1551113065), TEXT("2019-02-25"));

	const FTencentSignature Signature = TencentSign(TEXT("AKIDz8krbsJ5yKBZQpn74WFkmLPx3EXAMPLE"), TEXT("masked"), TEXT("cvm"), TEXT("cvm.tencentcloudapi.com"),
		TEXT("DescribeInstances"), Payload, 1551113065);
	const FString Canonical = TEXT("POST\n/\n\ncontent-type:application/json; charset=utf-8\nhost:cvm.tencentcloudapi.com\nx-tc-action:describeinstances\n\n")
		TEXT("content-type;host;x-tc-action\n35e9c5b0e3ae67532d3c9f17ead6c90222632e5b1ff7f6e89887f1398934f064");
	TestEqual(TEXT("CanonicalRequest"), Signature.CanonicalRequest, Canonical);
	TestEqual(TEXT("HashedCanonicalRequest"), Sha256Hex(Signature.CanonicalRequest), TEXT("7019a55be8395899b900fb5564e4200d984910f34794a27cb3fb7d10ff6a1e84"));
	const FString StringToSign = TEXT("TC3-HMAC-SHA256\n1551113065\n2019-02-25/cvm/tc3_request\n7019a55be8395899b900fb5564e4200d984910f34794a27cb3fb7d10ff6a1e84");
	TestEqual(TEXT("StringToSign"), Signature.StringToSign, StringToSign);
	TestEqual(TEXT("CredentialScope"), Signature.CredentialScope, TEXT("2019-02-25/cvm/tc3_request"));

	const TArray<uint8> SecretDate = FromHex(TEXT("da98fb70dcf6b112dc21038d1eeeb3a95c74b4dcb12c1131f864f6066bd02be0"));
	const TArray<uint8> SecretService = HmacSha256(SecretDate, Utf8(TEXT("cvm")));
	TestEqual(TEXT("SecretService"), HexOf(SecretService), TEXT("8d70cbefb03939f929db64d32dc2ba89b1095620119fe3e050e2b18c5bd2752f"));
	const TArray<uint8> SecretSigning = HmacSha256(SecretService, Utf8(TEXT("tc3_request")));
	TestEqual(TEXT("SecretSigning"), HexOf(SecretSigning), TEXT("b596b923aad85185e2d1f6659d2a062e0a86731226e021e61bfe06f7ed05f5af"));
	TestEqual(TEXT("Signature"), TencentSignWithSigningKey(SecretSigning, StringToSign), TEXT("10b1a37a7301a02ca19a647ad722d5e43b4b3cff309d421d85b46093f6ab6c4f"));
	TestTrue(TEXT("the Authorization header names the scope, the signed headers and the signature"),
		Signature.Authorization.StartsWith(TEXT("TC3-HMAC-SHA256 Credential=AKIDz8krbsJ5yKBZQpn74WFkmLPx3EXAMPLE/2019-02-25/cvm/tc3_request, SignedHeaders=content-type;host;x-tc-action, Signature=")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocTranslateBaiduYoudaoSignTest, "DreamLoc.Translate.Sign.BaiduAndYoudaoSignWhatTheirDocumentationSays",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FDreamLocTranslateBaiduYoudaoSignTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocTranslateCrypto;
	// Baidu: MD5(appid + q + salt + key), its documentation's example (appid 2015063000000001, q apple, salt 1435660288,
	// key 12345678); and a Chinese query over two lines, as a batch is sent. Expected values from Python's hashlib.
	TestEqual(TEXT("Baidu, the documentation's example"), BaiduSign(TEXT("2015063000000001"), TEXT("apple"), TEXT("1435660288"), TEXT("12345678")),
		TEXT("f89f9594663708c1605f3d736d01d2d4"));
	TestEqual(TEXT("Baidu, Chinese over two lines"), BaiduSign(TEXT("20240101000000001"), TEXT("你好\n世界"), TEXT("1435660288"), TEXT("abcdef123456")),
		TEXT("52bda9ca4c73ef817806d693b7cc0822"));
	// Youdao v3: SHA256(appKey + input + salt + curtime + appSecret), input the query when 20 characters or fewer, else
	// its first 10, its length and its last 10 (UTF-16 units, as the documentation's JavaScript counts).
	const FString Long = TEXT("欢迎来到新都，旅行者。今天的天气很好，适合出门散步。");
	TestEqual(TEXT("Youdao input of a short query"), YoudaoInput(TEXT("hello")), TEXT("hello"));
	TestEqual(TEXT("Youdao input of a long query"), YoudaoInput(Long), Long.Left(10) + TEXT("26") + Long.Right(10));
	TestEqual(TEXT("Youdao, short"), YoudaoSign(TEXT("appKey123"), TEXT("hello"), TEXT("salt-1"), TEXT("1700000000"), TEXT("secretABC")),
		TEXT("80fac435886ecf91a5adda1ed7807f598296a8ee7f09c8271c9fe81fb0bebb69"));
	TestEqual(TEXT("Youdao, long"), YoudaoSign(TEXT("appKey123"), Long, TEXT("salt-1"), TEXT("1700000000"), TEXT("secretABC")),
		TEXT("0a42fee9c8ce11eeb0ea2da4381bac3466310c8dc8c7323df75714e03abfedd5"));
	return true;
}

// ---------------------------------------------------------------------------------------------------------- placeholders

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocTranslateProtectTest, "DreamLoc.Translate.Protect.WhatAMachineMustNotTouchComesBackAsItWent",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FDreamLocTranslateProtectTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocTranslateProtect;
	const FString Source = TEXT("  <b>{Name}</> 装上了义体 & 走了\n按 `{ 继续 DreamMoon ");
	const TArray<TPair<FString, FString>> Terms = { TPair<FString, FString>(TEXT("义体"), TEXT("Cyberware")) };
	const TArray<FString> Keeps = { TEXT("DreamMoon") };

	const FProtected Brackets = Protect(Source, Terms, Keeps, EStyle::Brackets);
	TestEqual(TEXT("brackets: the text sent"), Brackets.Text, TEXT("⟦0⟧⟦1⟧⟦2⟧ 装上了⟦3⟧ & 走了⟦4⟧按 ⟦5⟧ 继续 ⟦6⟧"));
	TestEqual(TEXT("brackets: the blanks at either end are kept apart"), Brackets.Leading + TEXT("|") + Brackets.Trailing, TEXT("  | "));
	FString Text;
	FString Error;
	TestTrue(TEXT("brackets: a translation with every placeholder comes back"),
		Restore(TEXT("⟦0⟧⟦1⟧⟦2⟧ installed ⟦3⟧ & left⟦4⟧Press ⟦5⟧ to go on ⟦6⟧"), Brackets, EStyle::Brackets, Text, Error));
	TestEqual(TEXT("brackets: the pieces back, the term as its translation"), Text, TEXT("  <b>{Name}</> installed Cyberware & left\nPress `{ to go on DreamMoon "));
	TestTrue(TEXT("brackets: placeholders may move and gain spaces"), Restore(TEXT("⟦ 2 ⟧⟦1⟧⟦0⟧⟦3⟧⟦4⟧⟦5⟧⟦6⟧"), Brackets, EStyle::Brackets, Text, Error));
	TestFalse(TEXT("brackets: a lost placeholder fails"), Restore(TEXT("⟦0⟧⟦1⟧⟦2⟧ installed ⟦3⟧ & left⟦4⟧Press to go on ⟦6⟧"), Brackets, EStyle::Brackets, Text, Error));
	TestTrue(TEXT("brackets: and says which"), Error.Contains(TEXT("lost placeholder 5")));
	TestFalse(TEXT("brackets: a repeated one fails"), Restore(TEXT("⟦0⟧⟦1⟧⟦2⟧⟦3⟧⟦3⟧⟦4⟧⟦5⟧⟦6⟧"), Brackets, EStyle::Brackets, Text, Error));
	TestFalse(TEXT("brackets: a changed one fails"), Restore(TEXT("⟦0⟧⟦1⟧⟦2⟧⟦3⟧⟦4⟧⟦5⟧⟦6⟧⟦9⟧"), Brackets, EStyle::Brackets, Text, Error));

	const FProtected Xml = Protect(Source, Terms, Keeps, EStyle::Xml);
	TestEqual(TEXT("xml: the text sent, escaped"), Xml.Text,
		TEXT("<x i=\"0\"/><x i=\"1\"/><x i=\"2\"/> 装上了<x i=\"3\"/> &amp; 走了<x i=\"4\"/>按 <x i=\"5\"/> 继续 <x i=\"6\"/>"));
	TestTrue(TEXT("xml: DeepL's answer, a tag written as a pair"),
		Restore(TEXT("<x i=\"0\"/><x i=\"1\"></x><x i=\"2\"/> installed <x i=\"3\"/> &amp; left<x i=\"4\"/>Press <x i=\"5\"/> to go on <x i=\"6\"/>"), Xml, EStyle::Xml, Text, Error));
	TestEqual(TEXT("xml: unescaped and put back"), Text, TEXT("  <b>{Name}</> installed Cyberware & left\nPress `{ to go on DreamMoon "));

	const FProtected Html = Protect(TEXT("\"引号\" <i>斜</>"), {}, {}, EStyle::Html);
	TestEqual(TEXT("html: the text sent, escaped"), Html.Text, TEXT("&quot;引号&quot; <span translate=\"no\">0</span>斜<span translate=\"no\">1</span>"));
	TestTrue(TEXT("html: Google's answer, with a numeric entity"),
		Restore(TEXT("&#39;quotes&#39; <span translate=\"no\">0</span>italic<span translate=\"no\">1</span>"), Html, EStyle::Html, Text, Error));
	TestEqual(TEXT("html: put back"), Text, TEXT("'quotes' <i>italic</>"));
	return true;
}

// ---------------------------------------------------------------------------------------------------------- configuration

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocTranslateProfilesTest, "DreamLoc.Translate.Config.ProfilesReadWriteAndStayOutOfTheUsersFolder",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FDreamLocTranslateProfilesTest::RunTest(const FString& Parameters)
{
	const FString Text = TEXT(R"(; mine
[Profile deepseek]
Type=OpenAI
BaseUrl=https://api.deepseek.com/v1
Model=deepseek-chat
ApiKeyEnv=DREAMLOC_TEST_KEY
JsonMode=true
Temperature=0.3
Proxy=http://127.0.0.1:7890

[Profile ollama]
Type=OpenAI
BaseUrl=http://127.0.0.1:11434/v1   ; local by its address
Model=qwen3:14b

[Profile baidu]
Type=Baidu
AppId=2015063000000001
Secret=plain-secret-1234
Colour=blue

[Other]
x=1
)");
	FPlatformMisc::SetEnvironmentVar(TEXT("DREAMLOC_TEST_KEY"), TEXT("sk-from-the-environment"));
	TArray<FString> Warnings;
	const TArray<FDreamLocTranslateProfile> Profiles = DreamLocTranslateConfig::ParseProfiles(Text, Warnings);
	TestEqual(TEXT("three profiles"), Profiles.Num(), 3);
	TestEqual(TEXT("warnings: an unknown key, a section that is no profile, a line in it"), Warnings.Num(), 3);
	if (Profiles.Num() == 3)
	{
		TestEqual(TEXT("the key from its variable"), Profiles[0].GetApiKey(), TEXT("sk-from-the-environment"));
		TestTrue(TEXT("json mode"), Profiles[0].bJsonMode);
		TestEqual(TEXT("temperature"), Profiles[0].Temperature, 0.3f);
		TestEqual(TEXT("proxy"), Profiles[0].Proxy, TEXT("http://127.0.0.1:7890"));
		TestFalse(TEXT("a cloud service is not local"), Profiles[0].IsLocal());
		TestFalse(TEXT("a key from a variable is not in the file"), Profiles[0].HasSecretInFile());
		TestTrue(TEXT("127.0.0.1 is local"), Profiles[1].IsLocal());
		TestEqual(TEXT("a comment after a value is not the value"), Profiles[1].BaseUrl, TEXT("http://127.0.0.1:11434/v1"));
		TestTrue(TEXT("a secret written in the file is"), Profiles[2].HasSecretInFile());
		TestEqual(TEXT("the secrets to keep out of logs"), FString::Join(Profiles[2].GetSecrets(), TEXT(",")), TEXT("plain-secret-1234"));
		TestEqual(TEXT("a machine translator"), int32(Profiles[2].GetKind()), int32(EDreamLocTranslatorKind::Machine));

		FDreamLocTranslateProfile Forced = Profiles[1];
		Forced.Local = false;
		TestFalse(TEXT("Local=false wins over the address"), Forced.IsLocal());
		FDreamLocTranslateProfile Lan;
		Lan.BaseUrl = TEXT("http://gpu-box.local:8000/v1");
		TestTrue(TEXT("a .local host is local"), Lan.IsLocal());

		TArray<FString> Again;
		const TArray<FDreamLocTranslateProfile> Written = DreamLocTranslateConfig::ParseProfiles(DreamLocTranslateConfig::WriteProfiles(Profiles), Again);
		TestEqual(TEXT("written and read again: only the kept unknown key (Colour) warns again"), Again.Num(), 1);
		TestEqual(TEXT("written and read again: the same"), DreamLocTranslateConfig::WriteProfiles(Written), DreamLocTranslateConfig::WriteProfiles(Profiles));
	}
	FPlatformMisc::SetEnvironmentVar(TEXT("DREAMLOC_TEST_KEY"), TEXT(""));

	// The path a test gives is where profiles are read and written; the user's own file is never opened.
	const FString Work = DreamLocTestUtil::MakeTempDirectory(TEXT("TranslateProfiles"));
	const FString Mine = Work / TEXT("translators.ini");
	DreamLocTranslateConfig::SetUserConfigPath(Mine);
	TestEqual(TEXT("the path is the test's"), DreamLocTranslateConfig::GetUserConfigPath(), Mine);
	TestEqual(TEXT("no file yet: no profile"), DreamLocTranslateConfig::LoadProfiles().Num(), 0);
	TestTrue(TEXT("saved"), DreamLocTranslateConfig::SaveProfiles(Profiles));
	TestEqual(TEXT("loaded back"), DreamLocTranslateConfig::LoadProfiles().Num(), 3);
	DreamLocTranslateConfig::SetUserConfigPath(FString());
	TestFalse(TEXT("without the override, the path is not the test's"), DreamLocTranslateConfig::GetUserConfigPath() == Mine);
	DreamLocTestUtil::DeleteDirectory(Work);

	FString MadeError;
	TestFalse(TEXT("an OpenAI profile needs an address"), DreamLocTranslators::Make(FDreamLocTranslateProfile{ TEXT("x"), TEXT("OpenAI") }, MadeError).IsValid());
	TestFalse(TEXT("an unknown type is no translator"), DreamLocTranslators::Make(FDreamLocTranslateProfile{ TEXT("x"), TEXT("Bing") }, MadeError).IsValid());
	TestTrue(TEXT("and says which types there are"), MadeError.Contains(TEXT("Youdao")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocTranslateOtherToolsKeysTest, "DreamLoc.Translate.Config.KeysOfOtherToolsSurviveASave",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FDreamLocTranslateOtherToolsKeysTest::RunTest(const FString& Parameters)
{
	// dreamdocs, the documentation translator, reads its profiles from this file too.
	const FString Text = TEXT(R"([Profile deepseek]
Type=OpenAI
BaseUrl=https://api.deepseek.com/v1
Model=deepseek-chat
ApiKeyEnv=DEEPSEEK_API_KEY
ExtraBody={"thinking": {"type": "disabled"}}
Concurrency=3

[Profile ocgo]
Type=OpenCodeGo
Model=deepseek-v4.1-flash
ApiKeyEnv=OPENCODE_GO_API_KEY
Temperature=0.1
Api=chat
Typo=1
)");
	TArray<FString> Warnings;
	const TArray<FDreamLocTranslateProfile> Profiles = DreamLocTranslateConfig::ParseProfiles(Text, Warnings);
	TestEqual(TEXT("two profiles"), Profiles.Num(), 2);
	TestEqual(TEXT("only the typo warns: dreamdocs' keys are expected"), Warnings.Num(), 1);
	if (Profiles.Num() != 2)
	{
		return false;
	}
	TestEqual(TEXT("its keys are kept in order"), Profiles[0].Others.Num(), 2);
	const FString Written = DreamLocTranslateConfig::WriteProfiles(Profiles);
	for (const TCHAR* Line : { TEXT("ExtraBody={\"thinking\": {\"type\": \"disabled\"}}"), TEXT("Concurrency=3"), TEXT("Type=OpenCodeGo"),
		TEXT("Model=deepseek-v4.1-flash"), TEXT("Temperature=0.1"), TEXT("Api=chat"), TEXT("Typo=1") })
	{
		TestTrue(FString::Printf(TEXT("saving keeps %s"), Line), Written.Contains(FString(Line) + TEXT("\n")));
	}
	TArray<FString> Again;
	TestEqual(TEXT("and reading it back gives the same file"),
		DreamLocTranslateConfig::WriteProfiles(DreamLocTranslateConfig::ParseProfiles(Written, Again)), Written);
	FString MadeError;
	TestFalse(TEXT("an OpenCodeGo profile is listed, not used"), DreamLocTranslators::Make(Profiles[1], MadeError).IsValid());
	TestTrue(TEXT("with a reason"), MadeError.Contains(TEXT("OpenCodeGo")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocTranslateSettingsTest, "DreamLoc.Translate.Config.TheProjectsTranslateSectionReadsAndIsWrittenBack",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FDreamLocTranslateSettingsTest::RunTest(const FString& Parameters)
{
	const FString Text = TEXT(R"([DreamLoc]
SourceCulture=zh-Hans
TargetCultures=en, ja

; how the window translates
[Translate]
Profile=deepseek          ; the default
Profile.ja=deepl
Fallback=deepseek
BatchSize=20
Concurrency=3
Retry=2
Style=DLoc/_house.md
LocalOnly=DNA/
+LocalOnly=Secret.Namespace

[Publish]
Mode=Source
)");
	TArray<FDreamLocDiagnostic> Diagnostics;
	const FDreamLocSettings Settings = FDreamLocSettings::Parse(Text, Diagnostics);
	TestEqual(TEXT("nothing to say"), DreamLocTestUtil::Describe(Diagnostics), FString());
	TestEqual(TEXT("the default profile"), Settings.GetTranslateProfile(TEXT("en")), TEXT("deepseek"));
	TestEqual(TEXT("a culture's own"), Settings.GetTranslateProfile(TEXT("ja")), TEXT("deepl"));
	TestEqual(TEXT("fallback"), Settings.TranslateFallback, TEXT("deepseek"));
	TestEqual(TEXT("numbers"), FString::Printf(TEXT("%d %d %d"), Settings.TranslateBatchSize, Settings.TranslateConcurrency, Settings.TranslateRetry), TEXT("20 3 2"));
	TestEqual(TEXT("style"), Settings.TranslateStyle, TEXT("DLoc/_house.md"));
	TestEqual(TEXT("local only"), FString::Join(Settings.TranslateLocalOnly, TEXT(",")), TEXT("DNA/,Secret.Namespace"));

	TArray<FDreamLocDiagnostic> Bad;
	FDreamLocSettings::Parse(TEXT("[Translate]\nBatchSize=0\nRetry=x\nColour=blue\n"), Bad);
	TestEqual(TEXT("bad numbers and an unknown key"), DreamLocTestUtil::Describe(Bad), TEXT("DLOC1015@2:1 DLOC1015@3:1 DLOC1016@4:1"));

	// Written back: the section says what the settings say, the rest of the file as it was.
	FDreamLocSettings Changed = Settings;
	Changed.TranslateProfile = TEXT("ollama");
	Changed.TranslateProfileByCulture.Reset();
	Changed.TranslateBatchSize = 40;
	Changed.TranslateLocalOnly = { TEXT("DNA/") };
	const FString Written = DreamLocTranslateConfig::WriteTranslateSection(Text, Changed);
	TArray<FDreamLocDiagnostic> Again;
	const FDreamLocSettings Read = FDreamLocSettings::Parse(Written, Again);
	TestEqual(TEXT("written: reads without a word"), DreamLocTestUtil::Describe(Again), FString());
	TestEqual(TEXT("written: the new profile"), Read.GetTranslateProfile(TEXT("ja")), TEXT("ollama"));
	TestEqual(TEXT("written: the default batch size left out"), Written.Contains(TEXT("BatchSize")), false);
	TestEqual(TEXT("written: the rest kept"), Read.TargetCultures.Num() == 2 && Written.Contains(TEXT("[Publish]\nMode=Source")) && Written.Contains(TEXT("; how the window translates")), true);
	const FString Fresh = DreamLocTranslateConfig::WriteTranslateSection(TEXT("[DreamLoc]\nTargetCultures=en\n"), Changed);
	TestTrue(TEXT("a file without the section gets it"), Fresh.StartsWith(TEXT("[DreamLoc]\nTargetCultures=en\n")) && Fresh.Contains(TEXT("[Translate]\nProfile=ollama")));
	return true;
}

#endif
