// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocTranslateHttp.h"
#include "DreamLocTranslateTestUtil.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamLocTranslateServiceTestsLocal
{
	using namespace DreamLocTranslateTestUtil;

	const TCHAR* const Terms = TEXT("dloc 1\nculture en\nterms\n\nterm \"义体\" { text \"Cyberware\" }\nkeep \"DreamMoon\"\n");

	/**
	 * One machine translator against its fake: arguments, tags, a line break, an ampersand, a term and a kept word all come
	 * back where they were, every request signed the way the service checks it.
	 */
	bool RunService(FAutomationTestBase& InTest, const TCHAR* InType, const TCHAR* InTargetCode, TFunction<void(FAutomationTestBase&, FScenario&)> InMore)
	{
		const FScenarioRef S = MakeShared<FScenario, ESPMode::ThreadSafe>();
		if (!S->Begin(InTest, *(FString(TEXT("Translate")) + InType)))
		{
			return false;
		}
		S->Write(TEXT("dloc.ini"), TEXT("[DreamLoc]\nSourceCulture=zh-Hans\nTargetCultures=en\n\n[Translate]\nProfile=mt\n"));
		S->Write(TEXT("en/_terms.dloc"), Terms);
		S->Write(TEXT("en/Hud.dloc"), MakeDloc(TEXT("en"), TEXT("Hud"), {
			{ TEXT("Gold"), TEXT("<b>{Name}</>获得了 {Count} 金币\n下一行") },
			{ TEXT("Amp"), TEXT("A & B <i>斜体</>") },
			{ TEXT("Term"), TEXT("义体已安装") },
			{ TEXT("Keep"), TEXT("DreamMoon 出品") } }));
		FDreamLocTranslateRequest Request = S->MakeRequest();
		Request.Profiles = { S->Fake->Profile(InType, TEXT("mt")) };
		S->Run(Request);
		const FString Type = InType;
		const FString Mark = FString::Printf(TEXT("[%s] "), InTargetCode);
		ADD_LATENT_AUTOMATION_COMMAND(FAfterRun(&InTest, S, [&InTest, Type, Mark, InMore](FScenario& InS)
		{
			const TCHAR* Expected[][2] = {
				{ TEXT("Gold"), TEXT("<b>{Name}</>获得了 {Count} 金币\n下一行") },
				{ TEXT("Amp"), TEXT("A & B <i>斜体</>") },
				{ TEXT("Term"), TEXT("Cyberware已安装") },
				{ TEXT("Keep"), TEXT("DreamMoon 出品") } };
			for (const auto& Pair : Expected)
			{
				const FDreamLocTranslateRow* Row = InS.Row(Pair[0]);
				InTest.TestEqual(*FString::Printf(TEXT("%s: %s translated"), *Type, Pair[0]), FScenario::StatusName(Row), TEXT("translated"));
				InTest.TestEqual(*FString::Printf(TEXT("%s: %s comes back whole"), *Type, Pair[0]),
					FScenario::Field(InS.Entry(TEXT("en/Hud.dloc"), Pair[0]), EDreamLocField::Text), Mark + Pair[1]);
			}
			InTest.TestEqual(*FString::Printf(TEXT("%s: no request turned away"), *Type), InS.Fake->GetSignatureFailures(), 0);
			InTest.TestEqual(*FString::Printf(TEXT("%s: every request checked"), *Type), InS.Fake->GetSignaturesChecked(), InS.Fake->GetCalls().Num());
			InTest.TestFalse(*FString::Printf(TEXT("%s: no argument sent as it is"), *Type), InS.Fake->GetCalls().ContainsByPredicate([](const DreamLocTranslateFake::FCall& InCall)
			{
				return InCall.Body.Contains(TEXT("{Count}")) || InCall.Body.Contains(TEXT("%7BCount%7D"));
			}));
			InMore(InTest, InS);
		}));
		return true;
	}

	bool Has(const FScenario& InScenario, const TCHAR* InService, const FString& InText)
	{
		const TArray<DreamLocTranslateFake::FCall> Calls = InScenario.Fake->GetCalls(InService);
		return Calls.Num() > 0 && Calls[0].Body.Contains(InText);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocTranslateDeepLTest, "DreamLoc.Translate.Service.DeepLKeepsItsXmlTags",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FDreamLocTranslateDeepLTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocTranslateServiceTestsLocal;
	return RunService(*this, TEXT("DeepL"), TEXT("EN-US"), [](FAutomationTestBase& InTest, FScenario& InS)
	{
		InTest.TestTrue(TEXT("tags handled as XML, x left alone"), Has(InS, TEXT("deepl"), TEXT("\"tag_handling\":\"xml\"")) && Has(InS, TEXT("deepl"), TEXT("\"ignore_tags\":[\"x\"]")));
		InTest.TestTrue(TEXT("the languages as DeepL writes them"), Has(InS, TEXT("deepl"), TEXT("\"source_lang\":\"ZH\"")) && Has(InS, TEXT("deepl"), TEXT("\"target_lang\":\"EN-US\"")));
		InTest.TestTrue(TEXT("a placeholder sent"), Has(InS, TEXT("deepl"), TEXT("<x i=\\\"0\\\"/>")));
		InTest.TestTrue(TEXT("the ampersand escaped"), Has(InS, TEXT("deepl"), TEXT("A &amp; B")));
		InTest.TestEqual(TEXT("one request"), InS.Fake->GetCalls(TEXT("deepl")).Num(), 1);
	});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocTranslateGoogleTest, "DreamLoc.Translate.Service.GoogleKeepsItsUntranslatedSpans",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FDreamLocTranslateGoogleTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocTranslateServiceTestsLocal;
	return RunService(*this, TEXT("Google"), TEXT("en"), [](FAutomationTestBase& InTest, FScenario& InS)
	{
		InTest.TestTrue(TEXT("sent as HTML"), Has(InS, TEXT("google"), TEXT("\"format\":\"html\"")));
		InTest.TestTrue(TEXT("the languages as Google writes them"), Has(InS, TEXT("google"), TEXT("\"source\":\"zh-CN\"")) && Has(InS, TEXT("google"), TEXT("\"target\":\"en\"")));
		InTest.TestTrue(TEXT("a placeholder sent"), Has(InS, TEXT("google"), TEXT("<span translate=\\\"no\\\">0</span>")));
		const TArray<DreamLocTranslateFake::FCall> Calls = InS.Fake->GetCalls(TEXT("google"));
		InTest.TestTrue(TEXT("the key in a header"), Calls.Num() == 1 && Calls[0].Headers.Contains(TEXT("x-goog-api-key")));
	});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocTranslateBaiduTest, "DreamLoc.Translate.Service.BaiduSignsOneRequestOfLines",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FDreamLocTranslateBaiduTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocTranslateServiceTestsLocal;
	return RunService(*this, TEXT("Baidu"), TEXT("en"), [](FAutomationTestBase& InTest, FScenario& InS)
	{
		InTest.TestEqual(TEXT("one request for the four texts"), InS.Fake->GetCalls(TEXT("baidu")).Num(), 1);
		InTest.TestTrue(TEXT("the languages as Baidu writes them"), Has(InS, TEXT("baidu"), TEXT("from=zh&to=en")));
		InTest.TestTrue(TEXT("a placeholder sent"), Has(InS, TEXT("baidu"), DreamLocTranslateHttp::UrlEncode(TEXT("⟦0⟧"))));
	});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocTranslateTencentTest, "DreamLoc.Translate.Service.TencentSignsWithTc3",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FDreamLocTranslateTencentTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocTranslateServiceTestsLocal;
	return RunService(*this, TEXT("Tencent"), TEXT("en"), [](FAutomationTestBase& InTest, FScenario& InS)
	{
		const TArray<DreamLocTranslateFake::FCall> Calls = InS.Fake->GetCalls(TEXT("tencent"));
		InTest.TestEqual(TEXT("one request"), Calls.Num(), 1);
		if (Calls.Num() == 1)
		{
			InTest.TestEqual(TEXT("the action"), Calls[0].Headers.FindRef(TEXT("x-tc-action")), TEXT("TextTranslateBatch"));
			InTest.TestEqual(TEXT("the profile's region"), Calls[0].Headers.FindRef(TEXT("x-tc-region")), TEXT("ap-guangzhou"));
			InTest.TestTrue(TEXT("signed with TC3"), Calls[0].Headers.FindRef(TEXT("authorization")).StartsWith(TEXT("TC3-HMAC-SHA256 Credential=AKIDfake0001/")));
		}
		InTest.TestTrue(TEXT("the languages as Tencent writes them"), Has(InS, TEXT("tencent"), TEXT("\"Source\":\"zh\"")) && Has(InS, TEXT("tencent"), TEXT("\"Target\":\"en\"")));
	});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocTranslateYoudaoTest, "DreamLoc.Translate.Service.YoudaoSignsEachText",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FDreamLocTranslateYoudaoTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocTranslateServiceTestsLocal;
	return RunService(*this, TEXT("Youdao"), TEXT("en"), [](FAutomationTestBase& InTest, FScenario& InS)
	{
		InTest.TestEqual(TEXT("a request for each text"), InS.Fake->GetCalls(TEXT("youdao")).Num(), 4);
		InTest.TestTrue(TEXT("the languages as Youdao writes them"), Has(InS, TEXT("youdao"), TEXT("from=zh-CHS&to=en")));
		InTest.TestTrue(TEXT("signed with v3"), Has(InS, TEXT("youdao"), TEXT("signType=v3")));
	});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocTranslateWrongSignatureTest, "DreamLoc.Translate.Service.AWrongSignatureIsTurnedAway",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FDreamLocTranslateWrongSignatureTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocTranslateServiceTestsLocal;
	const FScenarioRef S = MakeShared<FScenario, ESPMode::ThreadSafe>();
	if (!S->Begin(*this, TEXT("TranslateWrongSignature")))
	{
		return false;
	}
	S->Write(TEXT("dloc.ini"), TEXT("[DreamLoc]\nSourceCulture=zh-Hans\nTargetCultures=en, ja\n\n[Translate]\nProfile.en=tencent\nProfile.ja=baidu\n"));
	S->Write(TEXT("en/Hud.dloc"), MakeDloc(TEXT("en"), TEXT("Hud"), { { TEXT("Gold"), TEXT("金币") } }));
	S->Write(TEXT("ja/Hud.dloc"), MakeDloc(TEXT("ja"), TEXT("Hud"), { { TEXT("Gold"), TEXT("金币") } }));
	FDreamLocTranslateProfile Tencent = S->Fake->Profile(TEXT("Tencent"), TEXT("tencent"));
	Tencent.Secret = TEXT("not-the-key");
	FDreamLocTranslateProfile Baidu = S->Fake->Profile(TEXT("Baidu"), TEXT("baidu"));
	Baidu.Secret = TEXT("not-the-key-either");
	FDreamLocTranslateRequest Request = S->MakeRequest();
	Request.Profiles = { Tencent, Baidu };
	S->Run(Request);
	ADD_LATENT_AUTOMATION_COMMAND(FAfterRun(this, S, [this](FScenario& InS)
	{
		const FDreamLocTranslateRow* En = InS.Row(TEXT("Gold"), TEXT("en"));
		const FDreamLocTranslateRow* Ja = InS.Row(TEXT("Gold"), TEXT("ja"));
		TestTrue(TEXT("Tencent turns it away"), En != nullptr && En->Status == EDreamLocTranslateStatus::Failed && En->Reason.Contains(TEXT("AuthFailure.SignatureFailure")));
		TestTrue(TEXT("Baidu turns it away"), Ja != nullptr && Ja->Status == EDreamLocTranslateStatus::Failed && Ja->Reason.Contains(TEXT("54001")));
		TestTrue(TEXT("Baidu is asked in its own code for Japanese"), InS.Fake->GetCalls(TEXT("baidu")).Num() == 1 && InS.Fake->GetCalls(TEXT("baidu"))[0].Body.Contains(TEXT("to=jp")));
		TestEqual(TEXT("both checked and refused"), InS.Fake->GetSignatureFailures(), 2);
		TestFalse(TEXT("no wrong key in a reason"), En != nullptr && En->Reason.Contains(TEXT("not-the-key")));
	}));
	return true;
}

#endif
