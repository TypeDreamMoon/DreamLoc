// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocTranslateTestUtil.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamLocTranslateFlowTestsLocal
{
	using namespace DreamLocTranslateTestUtil;
	using DreamLocTranslateFake::FChat;
	using DreamLocTranslateFake::FChatAnswer;

	FString Ini(const FString& InTranslate)
	{
		return TEXT("[DreamLoc]\nSourceCulture=zh-Hans\nTargetCultures=en\n\n[Translate]\n") + InTranslate;
	}

	FString Field(const FDreamLocEntry& InEntry, EDreamLocField InField)
	{
		return FScenario::Field(InEntry, InField);
	}

	bool AnyCallHas(const FScenario& InScenario, const TCHAR* InService, const FString& InText)
	{
		return InScenario.Fake->GetCalls(InService).ContainsByPredicate([&InText](const DreamLocTranslateFake::FCall& InCall) { return InCall.Body.Contains(InText); });
	}

	const TSharedPtr<FJsonObject>* FindSent(const TArray<TSharedPtr<FJsonObject>>& InSent, const FString& InKey)
	{
		return InSent.FindByPredicate([&InKey](const TSharedPtr<FJsonObject>& InEntry) { return InEntry->GetStringField(TEXT("key")) == InKey; });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocTranslateBatchesTest, "DreamLoc.Translate.Flow.BatchesAFileAtATimeAndSendsTwoAtOnce",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FDreamLocTranslateBatchesTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocTranslateFlowTestsLocal;
	const FScenarioRef S = MakeShared<FScenario, ESPMode::ThreadSafe>();
	if (!S->Begin(*this, TEXT("TranslateBatches")))
	{
		return false;
	}
	S->Write(TEXT("dloc.ini"), Ini(TEXT("Profile=model\nBatchSize=2\nConcurrency=2\n")));
	S->Write(TEXT("en/UI/A.dloc"), MakeDloc(TEXT("en"), TEXT("UI.A"), { { TEXT("A1"), TEXT("开始游戏") }, { TEXT("A2"), TEXT("继续") }, { TEXT("A3"), TEXT("设置") } }));
	S->Write(TEXT("en/UI/B.dloc"), MakeDloc(TEXT("en"), TEXT("UI.B"), { { TEXT("B1"), TEXT("音量") }, { TEXT("B2"), TEXT("画质") }, { TEXT("B3"), TEXT("开始游戏") } }));
	FDreamLocTranslateRequest Request = S->MakeRequest();
	Request.Profiles = { S->Fake->Profile(TEXT("OpenAI"), TEXT("model")) };
	const FDreamLocTranslateEstimate Estimate = DreamLocTranslateFlow::Estimate(Request);
	TestEqual(TEXT("the estimate"), FString::Printf(TEXT("%d entries, %d to send, %d requests, %d characters"), Estimate.Entries, Estimate.Distinct, Estimate.Batches,
		Estimate.Characters), TEXT("6 entries, 5 to send, 3 requests, 12 characters"));
	// No translator chosen: what there is to translate is still counted; only the requests are not.
	FDreamLocTranslateRequest Unchosen = Request;
	FDreamLocSettings NoProfile = FDreamLocSettings::LoadFromDirectory(S->DLoc);
	NoProfile.TranslateProfile.Reset();
	Unchosen.Settings = NoProfile;
	const FDreamLocTranslateEstimate Counted = DreamLocTranslateFlow::Estimate(Unchosen);
	TestEqual(TEXT("the estimate without a translator"), FString::Printf(TEXT("%d entries, %d to send, %d requests, %d characters, known %d"), Counted.Entries,
		Counted.Distinct, Counted.Batches, Counted.Characters, Counted.bTranslatorKnown ? 1 : 0), TEXT("6 entries, 5 to send, 0 requests, 12 characters, known 0"));
	TestTrue(TEXT("and says why it cannot send"), FString::Join(Counted.Errors, TEXT("\n")).Contains(TEXT("no translator chosen")));
	S->Fake->Delay = 0.25f;
	S->Run(Request);
	ADD_LATENT_AUTOMATION_COMMAND(FAfterRun(this, S, [this](FScenario& InS)
	{
		TestEqual(TEXT("every entry translated"), InS.Result.Translated, 6);
		const TArray<DreamLocTranslateFake::FCall> Calls = InS.Fake->GetCalls(TEXT("openai"));
		TestEqual(TEXT("three requests: A1 A2, A3, B1 B2"), Calls.Num(), 3);
		for (const DreamLocTranslateFake::FCall& Call : Calls)
		{
			const TSharedPtr<FJsonObject> Root = DreamLocTranslateFake::Parse(Call.Body);
			const TSharedPtr<FJsonObject> User = Root.IsValid() ? DreamLocTranslateFake::Parse(Root->GetArrayField(TEXT("messages"))[1]->AsObject()->GetStringField(TEXT("content"))) : nullptr;
			TSet<FString> Namespaces;
			int32 Count = 0;
			for (const TSharedPtr<FJsonValue>& Entry : User.IsValid() ? User->GetArrayField(TEXT("entries")) : TArray<TSharedPtr<FJsonValue>>())
			{
				Namespaces.Add(Entry->AsObject()->GetStringField(TEXT("ns")));
				++Count;
			}
			TestEqual(TEXT("a request holds one file's entries"), Namespaces.Num(), 1);
			TestTrue(TEXT("and no more than BatchSize"), Count >= 1 && Count <= 2);
			TestTrue(TEXT("in JSON mode"), Call.Body.Contains(TEXT("\"response_format\":{\"type\":\"json_object\"}")));
		}
		TestEqual(TEXT("two requests at once"), InS.Fake->GetMaxPending(), 2);
		TestEqual(TEXT("the same source sent once"), InS.SentToModel().Num(), 5);
		const FDreamLocEntry B3 = InS.Entry(TEXT("en/UI/B.dloc"), TEXT("B3"));
		TestEqual(TEXT("the same source gets the same translation"), Field(B3, EDreamLocField::Text), TEXT("en: 开始游戏"));
		TestEqual(TEXT("written as a machine translation"), Field(B3, EDreamLocField::State), TEXT("machine"));
		TestEqual(TEXT("one report"), InS.Result.ReportPaths.Num(), 1);
		FString Report;
		if (InS.Result.ReportPaths.Num() == 1 && FFileHelper::LoadFileToString(Report, *InS.Result.ReportPaths[0]))
		{
			TestTrue(TEXT("the report counts them"), Report.Contains(TEXT("\"translated\": 6")));
			TestTrue(TEXT("and names the translator"), Report.Contains(TEXT("\"backend\": \"model\"")));
			TestTrue(TEXT("and the usage"), Report.Contains(TEXT("\"promptTokens\": 300")));
		}
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocTranslateSameNameTest, "DreamLoc.Translate.Flow.AFieldNameManyClassesShareIsSentOncePerKind",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FDreamLocTranslateSameNameTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocTranslateFlowTestsLocal;
	const FScenarioRef S = MakeShared<FScenario, ESPMode::ThreadSafe>();
	if (!S->Begin(*this, TEXT("TranslateSameName")))
	{
		return false;
	}
	// A plugin's meta-data as `dloc gather` makes it: one display name in three classes' headers, and a tooltip of the same words.
	S->Write(TEXT("dloc.ini"), Ini(TEXT("Profile=model\n")));
	S->Write(TEXT("en/Source/A.h.dloc"), MakeDloc(TEXT("en"), TEXT("UObjectDisplayNames"), { { TEXT("Button:Visibility"), TEXT("Visibility") } }));
	S->Write(TEXT("en/Source/B.h.dloc"), MakeDloc(TEXT("en"), TEXT("UObjectDisplayNames"), { { TEXT("Image:Visibility"), TEXT("Visibility") }, { TEXT("EFit.Visibility"), TEXT("Visibility") } }));
	S->Write(TEXT("en/Source/C.h.dloc"), MakeDloc(TEXT("en"), TEXT("UObjectToolTips"), { { TEXT("Button:Visibility"), TEXT("Visibility") } }));
	auto Manifest = [](const TCHAR* InSource, const TCHAR* InNamespace, TArray<TPair<FString, FString>> InKeysAndKinds)
	{
		FDreamLocExtractManifest Out;
		Out.Provider = TEXT("Engine");
		Out.Source = InSource;
		for (const TPair<FString, FString>& Each : InKeysAndKinds)
		{
			FDreamLocExtractEntry Entry;
			Entry.Key = Each.Key;
			Entry.Source = TEXT("Visibility");
			Entry.Kind = Each.Value;
			Out.FindOrAddNamespace(InNamespace).Entries.Add(MoveTemp(Entry));
		}
		return Out;
	};
	FDreamLocTranslateRequest Request = S->MakeRequest();
	Request.Profiles = { S->Fake->Profile(TEXT("OpenAI"), TEXT("model")) };
	Request.Manifests = TArray<FDreamLocExtractManifest>{
		Manifest(TEXT("Source/A.h"), TEXT("UObjectDisplayNames"), { { TEXT("Button:Visibility"), TEXT("display name") } }),
		Manifest(TEXT("Source/B.h"), TEXT("UObjectDisplayNames"), { { TEXT("Image:Visibility"), TEXT("display name") }, { TEXT("EFit.Visibility"), TEXT("display name") } }),
		Manifest(TEXT("Source/C.h"), TEXT("UObjectToolTips"), { { TEXT("Button:Visibility"), TEXT("tooltip") } }) };
	const FDreamLocTranslateEstimate Estimate = DreamLocTranslateFlow::Estimate(Request);
	TestEqual(TEXT("four entries, two to send: the display name once, the tooltip once"), FString::Printf(TEXT("%d entries, %d to send"), Estimate.Entries, Estimate.Distinct),
		TEXT("4 entries, 2 to send"));
	S->Run(Request);
	ADD_LATENT_AUTOMATION_COMMAND(FAfterRun(this, S, [this](FScenario& InS)
	{
		TestEqual(TEXT("every entry translated"), InS.Result.Translated, 4);
		TestEqual(TEXT("two texts sent"), InS.SentToModel().Num(), 2);
		TestEqual(TEXT("the answer in every class's entry"), Field(InS.Entry(TEXT("en/Source/B.h.dloc"), TEXT("EFit.Visibility")), EDreamLocField::Text), TEXT("en: Visibility"));
		TestEqual(TEXT("and the first's"), Field(InS.Entry(TEXT("en/Source/A.h.dloc"), TEXT("Button:Visibility")), EDreamLocField::Text), TEXT("en: Visibility"));
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocTranslateRetriesTest, "DreamLoc.Translate.Flow.WaitsAfter429AndSendsAgainAnAnswerThatDoesNotRead",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FDreamLocTranslateRetriesTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocTranslateFlowTestsLocal;
	const FScenarioRef S = MakeShared<FScenario, ESPMode::ThreadSafe>();
	if (!S->Begin(*this, TEXT("TranslateRetries")))
	{
		return false;
	}
	S->Write(TEXT("dloc.ini"), Ini(TEXT("Profile=model\n")));
	S->Write(TEXT("en/Menu.dloc"), MakeDloc(TEXT("en"), TEXT("Menu"), { { TEXT("Quit"), TEXT("退出游戏") } }));
	S->Fake->Chat = [](const FChat& InChat)
	{
		FChatAnswer Answer;
		if (InChat.Call == 0)
		{
			Answer.Status = 429;
			Answer.RawBody = TEXT("{\"error\":{\"message\":\"Rate limit reached\"}}");
		}
		else if (InChat.Call == 1)
		{
			Answer.Content = TEXT("Sorry, I can only answer in prose.");
		}
		else
		{
			Answer = DreamLocTranslateFake::MarkAll(InChat);
		}
		return Answer;
	};
	FDreamLocTranslateRequest Request = S->MakeRequest();
	Request.Profiles = { S->Fake->Profile(TEXT("OpenAI"), TEXT("model")) };
	S->Run(Request);
	ADD_LATENT_AUTOMATION_COMMAND(FAfterRun(this, S, [this](FScenario& InS)
	{
		TestEqual(TEXT("translated in the end"), FScenario::StatusName(InS.Row(TEXT("Quit"))), TEXT("translated"));
		TestEqual(TEXT("three requests: 429, prose, the answer"), InS.Result.Requests, 3);
		TestEqual(TEXT("written"), Field(InS.Entry(TEXT("en/Menu.dloc"), TEXT("Quit")), EDreamLocField::Text), TEXT("en: 退出游戏"));
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocTranslateChecksTest, "DreamLoc.Translate.Flow.WhatTheChecksTurnDownGoesBackWithWhatWasWrong",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FDreamLocTranslateChecksTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocTranslateFlowTestsLocal;
	const FScenarioRef S = MakeShared<FScenario, ESPMode::ThreadSafe>();
	if (!S->Begin(*this, TEXT("TranslateChecks")))
	{
		return false;
	}
	S->Write(TEXT("dloc.ini"), Ini(TEXT("Profile=model\nRetry=1\n")));
	S->Write(TEXT("en/Bag.dloc"), MakeDloc(TEXT("en"), TEXT("Bag"), { { TEXT("Count"), TEXT("共{Count}件") }, { TEXT("Broken"), TEXT("<b>警告</>：{Name}已离线") } }));
	S->Fake->Chat = [](const FChat& InChat)
	{
		TArray<TPair<int32, FString>> Texts;
		for (const TSharedPtr<FJsonObject>& Entry : InChat.Entries)
		{
			const int32 Id = int32(Entry->GetNumberField(TEXT("id")));
			if (Entry->GetStringField(TEXT("key")) == TEXT("Broken"))
			{
				Texts.Emplace(Id, TEXT("en: Warning: offline"));
			}
			else
			{
				// Right only once told what was wrong.
				Texts.Emplace(Id, Entry->HasField(TEXT("problems")) ? DreamLocTranslateFake::Mark(TEXT("en"), Entry->GetStringField(TEXT("src"))) : FString(TEXT("en: total")));
			}
		}
		FChatAnswer Answer;
		Answer.Content = DreamLocTranslateFake::Entries(Texts);
		return Answer;
	};
	FDreamLocTranslateRequest Request = S->MakeRequest();
	Request.Profiles = { S->Fake->Profile(TEXT("OpenAI"), TEXT("model")) };
	S->Run(Request);
	ADD_LATENT_AUTOMATION_COMMAND(FAfterRun(this, S, [this](FScenario& InS)
	{
		TestEqual(TEXT("two requests: the batch, and the two the checks turned down"), InS.Fake->GetCalls(TEXT("openai")).Num(), 2);
		const TArray<TSharedPtr<FJsonObject>> Sent = InS.SentToModel();
		const TSharedPtr<FJsonObject>* Again = Sent.Num() == 4 ? &Sent[2] : nullptr;
		TestTrue(TEXT("sent again with the last translation"), Again != nullptr && (*Again)->GetStringField(TEXT("lastText")) == TEXT("en: total"));
		TestTrue(TEXT("and what the checks said"), Again != nullptr && (*Again)->GetStringField(TEXT("problems")).Contains(TEXT("DLOC2001")));
		TestEqual(TEXT("right the second time"), FScenario::StatusName(InS.Row(TEXT("Count"))), TEXT("translated"));
		TestEqual(TEXT("and written"), Field(InS.Entry(TEXT("en/Bag.dloc"), TEXT("Count")), EDreamLocField::Text), TEXT("en: 共{Count}件"));
		const FDreamLocTranslateRow* Broken = InS.Row(TEXT("Broken"));
		TestTrue(TEXT("wrong both times: failed"), Broken != nullptr && Broken->Status == EDreamLocTranslateStatus::Failed);
		TestTrue(TEXT("the rejected translation kept for the report"), Broken != nullptr && Broken->RejectedText == TEXT("en: Warning: offline"));
		TestTrue(TEXT("and why"), Broken != nullptr && Broken->Problems.Contains(TEXT("DLOC2001")) && Broken->Problems.Contains(TEXT("DLOC2003")));
		TestFalse(TEXT("left untranslated"), InS.Entry(TEXT("en/Bag.dloc"), TEXT("Broken")).IsTranslated());
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocTranslateMachineAloneTest, "DreamLoc.Translate.Flow.AMachineTranslatorAloneFailsALostPlaceholderAndNeverSeesPluralForms",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FDreamLocTranslateMachineAloneTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocTranslateFlowTestsLocal;
	const FScenarioRef S = MakeShared<FScenario, ESPMode::ThreadSafe>();
	if (!S->Begin(*this, TEXT("TranslateMachineAlone")))
	{
		return false;
	}
	S->Write(TEXT("dloc.ini"), Ini(TEXT("Profile=baidu\n")));
	S->Write(TEXT("en/Shop.dloc"), MakeDloc(TEXT("en"), TEXT("Shop"), { { TEXT("Plain"), TEXT("获得{Count}金币") }, { TEXT("Lost"), TEXT("LOSE{Count}金币") },
		{ TEXT("Forms"), TEXT("{Count}|plural(other=个)物品") } }));
	FDreamLocTranslateRequest Request = S->MakeRequest();
	Request.Profiles = { S->Fake->Profile(TEXT("Baidu"), TEXT("baidu")) };
	S->Run(Request);
	ADD_LATENT_AUTOMATION_COMMAND(FAfterRun(this, S, [this](FScenario& InS)
	{
		TestEqual(TEXT("a plain text: translated, its argument back"), Field(InS.Entry(TEXT("en/Shop.dloc"), TEXT("Plain")), EDreamLocField::Text), TEXT("[en] 获得{Count}金币"));
		const FDreamLocTranslateRow* Lost = InS.Row(TEXT("Lost"));
		TestTrue(TEXT("a lost placeholder: failed"), Lost != nullptr && Lost->Status == EDreamLocTranslateStatus::Failed && Lost->Reason.Contains(TEXT("lost placeholder 0")));
		const FDreamLocTranslateRow* Forms = InS.Row(TEXT("Forms"));
		TestTrue(TEXT("plural forms: skipped, with no Fallback"), Forms != nullptr && Forms->Status == EDreamLocTranslateStatus::Skipped
			&& Forms->Reason.Contains(TEXT("plural")) && Forms->Reason.Contains(TEXT("Fallback")));
		TestFalse(TEXT("and never sent"), AnyCallHas(InS, TEXT("baidu"), TEXT("plural")));
		TestFalse(TEXT("neither written"), InS.Entry(TEXT("en/Shop.dloc"), TEXT("Lost")).IsTranslated() || InS.Entry(TEXT("en/Shop.dloc"), TEXT("Forms")).IsTranslated());
		TestEqual(TEXT("the signature checked"), InS.Fake->GetSignatureFailures(), 0);
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocTranslateMachineFallbackTest, "DreamLoc.Translate.Flow.WhatAMachineTranslatorCannotDoGoesToTheFallback",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FDreamLocTranslateMachineFallbackTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocTranslateFlowTestsLocal;
	const FScenarioRef S = MakeShared<FScenario, ESPMode::ThreadSafe>();
	if (!S->Begin(*this, TEXT("TranslateMachineFallback")))
	{
		return false;
	}
	S->Write(TEXT("dloc.ini"), Ini(TEXT("Profile=baidu\nFallback=model\n")));
	S->Write(TEXT("en/Shop.dloc"), MakeDloc(TEXT("en"), TEXT("Shop"), { { TEXT("Plain"), TEXT("获得{Count}金币") }, { TEXT("Lost"), TEXT("LOSE{Count}金币") },
		{ TEXT("Forms"), TEXT("{Count}|plural(other=个)物品") } }));
	S->Fake->Chat = [](const FChat& InChat)
	{
		TArray<TPair<int32, FString>> Texts;
		for (const TPair<int32, FString>& Source : Sources(InChat))
		{
			Texts.Emplace(Source.Key, Source.Value.Contains(TEXT("|plural")) ? FString(TEXT("en: {Count}|plural(one=item,other=items)")) : DreamLocTranslateFake::Mark(TEXT("en"), Source.Value));
		}
		FChatAnswer Answer;
		Answer.Content = DreamLocTranslateFake::Entries(Texts);
		return Answer;
	};
	FDreamLocTranslateRequest Request = S->MakeRequest();
	Request.Profiles = { S->Fake->Profile(TEXT("Baidu"), TEXT("baidu")), S->Fake->Profile(TEXT("OpenAI"), TEXT("model")) };
	S->Run(Request);
	ADD_LATENT_AUTOMATION_COMMAND(FAfterRun(this, S, [this](FScenario& InS)
	{
		const FDreamLocTranslateRow* Plain = InS.Row(TEXT("Plain"));
		const FDreamLocTranslateRow* Lost = InS.Row(TEXT("Lost"));
		const FDreamLocTranslateRow* Forms = InS.Row(TEXT("Forms"));
		TestTrue(TEXT("the plain text by the machine translator"), Plain != nullptr && Plain->Backend == TEXT("baidu") && Plain->Status == EDreamLocTranslateStatus::Translated);
		TestTrue(TEXT("the lost placeholder by the large model"), Lost != nullptr && Lost->Backend == TEXT("model") && Lost->Status == EDreamLocTranslateStatus::Translated);
		TestTrue(TEXT("saying what the machine translator could not do"), Lost != nullptr && Lost->Reason.Contains(TEXT("from baidu")));
		TestTrue(TEXT("the plural forms by the large model"), Forms != nullptr && Forms->Backend == TEXT("model") && Forms->Status == EDreamLocTranslateStatus::Translated);
		TestEqual(TEXT("written"), Field(InS.Entry(TEXT("en/Shop.dloc"), TEXT("Forms")), EDreamLocField::Text), TEXT("en: {Count}|plural(one=item,other=items)"));
		TestEqual(TEXT("the large model was sent just those two"), InS.SentToModel().Num(), 2);
		TestFalse(TEXT("the machine translator never saw the plural forms"), AnyCallHas(InS, TEXT("baidu"), TEXT("plural")));
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocTranslateLocalOnlyTest, "DreamLoc.Translate.Flow.LocalOnlyTextsNeverGoToACloudTranslator",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FDreamLocTranslateLocalOnlyTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocTranslateFlowTestsLocal;
	const FScenarioRef S = MakeShared<FScenario, ESPMode::ThreadSafe>();
	if (!S->Begin(*this, TEXT("TranslateLocalOnly")))
	{
		return false;
	}
	S->Write(TEXT("dloc.ini"), Ini(TEXT("Profile=cloud\nFallback=local\nLocalOnly=Secret/\n")));
	S->Write(TEXT("en/Secret/Plot.dloc"), MakeDloc(TEXT("en"), TEXT("Plot"), { { TEXT("Twist"), TEXT("她其实是间谍") } }));
	S->Write(TEXT("en/Public/Menu.dloc"), MakeDloc(TEXT("en"), TEXT("Menu"), { { TEXT("Title"), TEXT("主菜单") } }));
	FDreamLocTranslateProfile Cloud = S->Fake->Profile(TEXT("OpenAI"), TEXT("cloud"), /*bInCloud*/ true);
	Cloud.Model = TEXT("cloud-model");
	FDreamLocTranslateProfile Local = S->Fake->Profile(TEXT("OpenAI"), TEXT("local"));
	Local.Model = TEXT("local-model");
	FDreamLocTranslateRequest Request = S->MakeRequest();
	Request.Profiles = { Cloud };
	const FDreamLocTranslateEstimate Alone = DreamLocTranslateFlow::Estimate(Request);
	TestEqual(TEXT("with the cloud translator alone, the secret text is left out"), FString::Printf(TEXT("%d sent, %d left out"), Alone.Distinct, Alone.Skipped), TEXT("1 sent, 1 left out"));
	Request.Profiles = { Cloud, Local };
	S->Run(Request);
	ADD_LATENT_AUTOMATION_COMMAND(FAfterRun(this, S, [this](FScenario& InS)
	{
		const FDreamLocTranslateRow* Twist = InS.Row(TEXT("Twist"));
		const FDreamLocTranslateRow* Title = InS.Row(TEXT("Title"));
		TestTrue(TEXT("the secret text by the local translator"), Twist != nullptr && Twist->Backend == TEXT("local") && Twist->Status == EDreamLocTranslateStatus::Translated);
		TestTrue(TEXT("the other by the cloud one"), Title != nullptr && Title->Backend == TEXT("cloud") && Title->Status == EDreamLocTranslateStatus::Translated);
		TestFalse(TEXT("the cloud one never saw it"), InS.SentToModel(TEXT("cloud-model")).ContainsByPredicate([](const TSharedPtr<FJsonObject>& InEntry)
		{
			return InEntry->GetStringField(TEXT("src")).Contains(TEXT("间谍"));
		}));
		TestEqual(TEXT("the local one saw only it"), InS.SentToModel(TEXT("local-model")).Num(), 1);
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocTranslateReviewedTest, "DreamLoc.Translate.Flow.AReviewedTranslationIsNeverTouchedAndIsTheReferenceForTheSameSource",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FDreamLocTranslateReviewedTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocTranslateFlowTestsLocal;
	const FScenarioRef S = MakeShared<FScenario, ESPMode::ThreadSafe>();
	if (!S->Begin(*this, TEXT("TranslateReviewed")))
	{
		return false;
	}
	S->Write(TEXT("dloc.ini"), Ini(TEXT("Profile=model\n")));
	S->Write(TEXT("_style.md"), TEXT("STYLE FOR EVERY CULTURE"));
	S->Write(TEXT("_style.en.md"), TEXT("STYLE FOR ENGLISH"));
	S->Write(TEXT("_prompt.md"), TEXT("THE PROJECT'S OWN WORDS"));
	const FString Menu = MakeDloc(TEXT("en"), TEXT("Menu"), {
		{ TEXT("Back"), TEXT("返回"), TEXT("Back"), TEXT("done") },
		{ TEXT("Return"), TEXT("返回") },
		{ TEXT("Hint"), TEXT("按{Key}返回上一页"), TEXT("Press {Key} to go back"), TEXT("machine"), TEXT("按{Key}返回") },
		{ TEXT("Reviewed"), TEXT("确认退出吗？"), TEXT("Quit?"), TEXT("done"), TEXT("退出吗？") } });
	S->Write(TEXT("en/Menu.dloc"), Menu);
	FDreamLocTranslateRequest Request = S->MakeRequest();
	Request.Profiles = { S->Fake->Profile(TEXT("OpenAI"), TEXT("model")) };
	S->Run(Request);
	ADD_LATENT_AUTOMATION_COMMAND(FAfterRun(this, S, [this](FScenario& InS)
	{
		const TArray<TSharedPtr<FJsonObject>> Sent = InS.SentToModel();
		TestEqual(TEXT("sent: the untranslated one and the stale one"), Sent.Num(), 2);
		const TSharedPtr<FJsonObject>* Return = FindSent(Sent, TEXT("Return"));
		TestTrue(TEXT("the reviewed translation of the same source as a reference"), Return != nullptr && (*Return)->GetStringField(TEXT("reference")) == TEXT("Back"));
		const TSharedPtr<FJsonObject>* Hint = FindSent(Sent, TEXT("Hint"));
		TestTrue(TEXT("the stale one with its old source and translation"), Hint != nullptr && (*Hint)->GetStringField(TEXT("prev")) == TEXT("按{Key}返回")
			&& (*Hint)->GetStringField(TEXT("previousText")) == TEXT("Press {Key} to go back"));
		const TArray<DreamLocTranslateFake::FCall> Calls = InS.Fake->GetCalls(TEXT("openai"));
		const FString System = Calls.Num() > 0 ? DreamLocTranslateFake::Parse(Calls[0].Body)->GetArrayField(TEXT("messages"))[0]->AsObject()->GetStringField(TEXT("content")) : FString();
		TestTrue(TEXT("the culture's style guide in the instructions"), System.Contains(TEXT("STYLE FOR ENGLISH")) && !System.Contains(TEXT("STYLE FOR EVERY CULTURE")));
		TestTrue(TEXT("and the project's own prompt after them"), System.Contains(TEXT("THE PROJECT'S OWN WORDS")));
		TestTrue(TEXT("and the languages"), System.Contains(TEXT("English")));

		const FDreamLocEntry Back = InS.Entry(TEXT("en/Menu.dloc"), TEXT("Back"));
		TestEqual(TEXT("the reviewed one as it was"), Field(Back, EDreamLocField::Text) + TEXT("|") + Field(Back, EDreamLocField::State), TEXT("Back|done"));
		const FDreamLocEntry Reviewed = InS.Entry(TEXT("en/Menu.dloc"), TEXT("Reviewed"));
		TestEqual(TEXT("a reviewed one gone stale: as it was"), Field(Reviewed, EDreamLocField::Text) + TEXT("|") + Field(Reviewed, EDreamLocField::Prev) + TEXT("|")
			+ Field(Reviewed, EDreamLocField::State), TEXT("Quit?|退出吗？|done"));
		const FDreamLocTranslateRow* ReviewedRow = InS.Row(TEXT("Reviewed"));
		TestTrue(TEXT("and listed as left for a person"), ReviewedRow != nullptr && ReviewedRow->Status == EDreamLocTranslateStatus::Skipped && ReviewedRow->Reason.Contains(TEXT("done")));
		const FDreamLocEntry Hinted = InS.Entry(TEXT("en/Menu.dloc"), TEXT("Hint"));
		TestEqual(TEXT("the stale one translated again, its prev gone"), Field(Hinted, EDreamLocField::Text) + TEXT("|") + Field(Hinted, EDreamLocField::Prev) + TEXT("|")
			+ Field(Hinted, EDreamLocField::State), TEXT("en: 按{Key}返回上一页||machine"));
		const FDreamLocTranslateRow* HintRow = InS.Row(TEXT("Hint"));
		TestTrue(TEXT("its old translation in the table"), HintRow != nullptr && HintRow->OldText == TEXT("Press {Key} to go back"));
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocTranslateCancelTest, "DreamLoc.Translate.Flow.CancellingStillWritesWhatWasTranslated",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FDreamLocTranslateCancelTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocTranslateFlowTestsLocal;
	const FScenarioRef S = MakeShared<FScenario, ESPMode::ThreadSafe>();
	if (!S->Begin(*this, TEXT("TranslateCancel")))
	{
		return false;
	}
	S->Write(TEXT("dloc.ini"), Ini(TEXT("Profile=model\nConcurrency=1\n")));
	for (const TCHAR* Name : { TEXT("One"), TEXT("Two"), TEXT("Three") })
	{
		S->Write(FString::Printf(TEXT("en/C/%s.dloc"), Name), MakeDloc(TEXT("en"), Name, { { TEXT("Line"), FString::Printf(TEXT("第%s行"), Name) } }));
	}
	S->Fake->Delay = 0.3f;
	S->OnProgress = [](FScenario& InS, const FDreamLocTranslateProgress& InProgress)
	{
		if (InProgress.BatchesDone >= 1)
		{
			InS.Cancel = true;
		}
	};
	FDreamLocTranslateRequest Request = S->MakeRequest();
	Request.Profiles = { S->Fake->Profile(TEXT("OpenAI"), TEXT("model")) };
	S->Run(Request);
	ADD_LATENT_AUTOMATION_COMMAND(FAfterRun(this, S, [this](FScenario& InS)
	{
		TestTrue(TEXT("cancelled"), InS.Result.bCancelled);
		TestEqual(TEXT("one request answered before"), InS.Result.Requests, 1);
		TestEqual(TEXT("its entry translated, the others skipped"), FString::Printf(TEXT("%d %d"), InS.Result.Translated, InS.Result.Skipped), TEXT("1 2"));
		int32 Written = 0;
		for (const TCHAR* Name : { TEXT("One"), TEXT("Two"), TEXT("Three") })
		{
			Written += InS.Entry(FString::Printf(TEXT("en/C/%s.dloc"), Name), TEXT("Line")).IsTranslated() ? 1 : 0;
		}
		TestEqual(TEXT("and it is written"), Written, 1);
		TestTrue(TEXT("the others say why"), InS.Result.Rows.ContainsByPredicate([](const FDreamLocTranslateRow& InRow) { return InRow.Reason.Contains(TEXT("cancelled")); }));
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocTranslateSecretsTest, "DreamLoc.Translate.Flow.NoKeyInTheLogTheResultOrTheReport",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FDreamLocTranslateSecretsTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocTranslateFlowTestsLocal;
	const FScenarioRef S = MakeShared<FScenario, ESPMode::ThreadSafe>();
	if (!S->Begin(*this, TEXT("TranslateSecrets")))
	{
		return false;
	}
	S->Write(TEXT("dloc.ini"), Ini(TEXT("Profile=model\n")));
	S->Write(TEXT("en/Menu.dloc"), MakeDloc(TEXT("en"), TEXT("Menu"), { { TEXT("Hello"), TEXT("你好") } }));
	const FString Key = S->Fake->OpenAIKey;
	// A service that says the key back in its error, as some do.
	S->Fake->Chat = [Key](const FChat&)
	{
		FChatAnswer Answer;
		Answer.Status = 401;
		Answer.RawBody = FString::Printf(TEXT("{\"error\":{\"message\":\"Incorrect API key provided: %s. Check it.\"}}"), *Key);
		return Answer;
	};
	FDreamLocTranslateRequest Request = S->MakeRequest();
	Request.Profiles = { S->Fake->Profile(TEXT("OpenAI"), TEXT("model")) };
	S->Run(Request, /*bInCaptureLog*/ true);
	ADD_LATENT_AUTOMATION_COMMAND(FAfterRun(this, S, [this, Key](FScenario& InS)
	{
		const FDreamLocTranslateRow* Hello = InS.Row(TEXT("Hello"));
		TestTrue(TEXT("failed, with the service's words"), Hello != nullptr && Hello->Status == EDreamLocTranslateStatus::Failed && Hello->Reason.Contains(TEXT("Incorrect API key provided: ***")));
		TestFalse(TEXT("no key in the row"), Hello != nullptr && Hello->Reason.Contains(Key));
		FString Report;
		TestTrue(TEXT("a report"), InS.Result.ReportPaths.Num() == 1 && FFileHelper::LoadFileToString(Report, *InS.Result.ReportPaths[0]));
		TestTrue(TEXT("saying what failed"), Report.Contains(TEXT("Incorrect API key")));
		TestFalse(TEXT("no key in the report"), Report.Contains(Key));
		TestTrue(TEXT("the log has the run"), InS.LogText.Contains(TEXT("DreamLoc translate en")));
		TestFalse(TEXT("no key in the log"), InS.LogText.Contains(Key));
		TestFalse(TEXT("none in the result's messages"), FString::Join(InS.Result.Errors, TEXT("\n")).Contains(Key) || FString::Join(InS.Result.Warnings, TEXT("\n")).Contains(Key));
		TestTrue(TEXT("the run's log says what the service said"), InS.LogLinesText().Contains(TEXT("Incorrect API key provided: ***")));
		TestFalse(TEXT("but not the key"), InS.LogLinesText().Contains(Key));
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocTranslateLogTest, "DreamLoc.Translate.Flow.TheLogSaysWhatEachRequestCameToAndWhyATextFailed",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FDreamLocTranslateLogTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocTranslateFlowTestsLocal;
	const FScenarioRef S = MakeShared<FScenario, ESPMode::ThreadSafe>();
	if (!S->Begin(*this, TEXT("TranslateLog")))
	{
		return false;
	}
	S->Write(TEXT("dloc.ini"), Ini(TEXT("Profile=model\nRetry=0\nConcurrency=1\n")));
	S->Write(TEXT("en/A.dloc"), MakeDloc(TEXT("en"), TEXT("Gone"), { { TEXT("Hello"), TEXT("你好") } }));
	S->Write(TEXT("en/B.dloc"), MakeDloc(TEXT("en"), TEXT("Bag"), { { TEXT("Count"), TEXT("共{Count}件") }, { TEXT("Lost"), TEXT("丢失") }, { TEXT("Fine"), TEXT("很好") } }));
	S->Write(TEXT("en/C.dloc"), MakeDloc(TEXT("en"), TEXT("Prose"), { { TEXT("Line"), TEXT("很久以前") } }));
	// A: the service turns the request down. B: one argument lost, one entry without a text, one right. C: prose, twice.
	S->Fake->Chat = [](const FChat& InChat)
	{
		FChatAnswer Answer;
		const FString Namespace = InChat.Entries.Num() > 0 ? InChat.Entries[0]->GetStringField(TEXT("ns")) : FString();
		if (Namespace == TEXT("Gone"))
		{
			Answer.Status = 400;
			Answer.RawBody = TEXT("{\"error\":{\"message\":\"MissingSessionID\"}}");
			return Answer;
		}
		if (Namespace == TEXT("Prose"))
		{
			Answer.Content = TEXT("Sure! Here is the translation:\nOnce upon a time");
			return Answer;
		}
		TArray<FString> Parts;
		for (const TSharedPtr<FJsonObject>& Entry : InChat.Entries)
		{
			const int32 Id = int32(Entry->GetNumberField(TEXT("id")));
			const FString Key = Entry->GetStringField(TEXT("key"));
			Parts.Add(Key == TEXT("Lost") ? FString::Printf(TEXT("{\"id\":%d}"), Id)
				: FString::Printf(TEXT("{\"id\":%d,\"text\":\"%s\"}"), Id, Key == TEXT("Count") ? TEXT("en: total") : *DreamLocTranslateFake::Mark(TEXT("en"), Entry->GetStringField(TEXT("src")))));
		}
		Answer.Content = TEXT("{\"entries\":[") + FString::Join(Parts, TEXT(",")) + TEXT("]}");
		return Answer;
	};
	FDreamLocTranslateRequest Request = S->MakeRequest();
	Request.Profiles = { S->Fake->Profile(TEXT("OpenAI"), TEXT("model")) };
	S->Run(Request, /*bInCaptureLog*/ true);
	ADD_LATENT_AUTOMATION_COMMAND(FAfterRun(this, S, [this](FScenario& InS)
	{
		using ELevel = EDreamLocTranslateLogLevel;
		const FString All = InS.LogLinesText();
		auto Has = [this, &InS, &All](const TCHAR* InWhat, ELevel InLevel, TArray<FString> InParts)
		{
			if (InS.FindLogLine(InLevel, InParts) == nullptr)
			{
				AddError(FString::Printf(TEXT("the log: %s -- no line (level %d) with: %s\nThe log:\n%s"), InWhat, int32(InLevel), *FString::Join(InParts, TEXT(" | ")), *All));
			}
		};
		TestEqual(TEXT("one translated, four failed"), FString::Printf(TEXT("%d %d"), InS.Result.Translated, InS.Result.Failed), TEXT("1 4"));
		TestEqual(TEXT("every line given as it was written, and kept in the result"), InS.LogLines.Num(), InS.Result.Log.Num());
		Has(TEXT("what the culture sends"), ELevel::Info, { TEXT("DreamLoc translate en: 5 entries, 5 texts to send to model, 0 left out") });
		Has(TEXT("a request that came back: its file, texts, translator, time, tokens"), ELevel::Info, { TEXT("en  B.dloc  3 texts  model  ok  "), TEXT(" s  100 + 20 tokens") });
		Has(TEXT("what the answer left out"), ELevel::Warning, { TEXT("en  B.dloc  3 texts  model: an answer entry without a text was left out") });
		Has(TEXT("a request the service turned down, in its words"), ELevel::Error, { TEXT("en  A.dloc  1 text  model  failed  "), TEXT(" s  HTTP 400: MissingSessionID") });
		const FString Prose = TEXT("100 + 20 tokens  the answer is not the JSON asked for (it began: \"Sure! Here is the translation:\\nOnce upon a time\")");
		Has(TEXT("an answer that does not read, how it began, sent again"), ELevel::Warning, { TEXT("en  C.dloc  1 text  model  failed  "), Prose + TEXT("; sent again") });
		Has(TEXT("and failed again"), ELevel::Error, { TEXT("en  C.dloc  1 text  model  failed  "), Prose });
		Has(TEXT("a text the checks turned down: what they said, its source, what came"), ELevel::Error,
			{ TEXT("en  B.dloc  Bag / Count  failed: the checks turned it down: DLOC2001"), TEXT("; src \"共{Count}件\"; turned down \"en: total\"") });
		Has(TEXT("a text the answer has none for"), ELevel::Error, { TEXT("en  B.dloc  Bag / Lost  failed: the answer has no translation for it; src \"丢失\"") });
		Has(TEXT("what the culture came to"), ELevel::Info, { TEXT("DreamLoc translate en: 1 translated, 4 failed, 0 skipped; 4 request(s) to model in "), TEXT(" s, 300 + 60 tokens") });
		Has(TEXT("why its entries failed"), ELevel::Error, { TEXT("DreamLoc translate en: why they failed: "), TEXT("1 HTTP 400: MissingSessionID"), TEXT("1 the checks turned it down"),
			TEXT("1 the answer has no translation for it"), TEXT("1 the answer is not the JSON asked for"), TEXT("; the checks said "), TEXT("DLOC2001 1") });
		TestEqual(TEXT("a report"), InS.Result.ReportPaths.Num(), 1);
		if (InS.Result.ReportPaths.Num() == 1)
		{
			Has(TEXT("where the report is"), ELevel::Info, { TEXT("DreamLoc translate en: the report: ") + InS.Result.ReportPaths[0] });
			FString Report;
			FFileHelper::LoadFileToString(Report, *InS.Result.ReportPaths[0]);
			TestTrue(TEXT("the report has the log"), Report.Contains(TEXT("\"log\"")) && Report.Contains(TEXT("en  A.dloc  1 text  model  failed")));
		}
		TestTrue(TEXT("the lines in the Output Log too"), InS.LogText.Contains(TEXT("LogDreamLocTranslate: en  A.dloc  1 text  model  failed")));
		TestTrue(TEXT("a line on one line: a break in a text written as \\n"), !All.Contains(TEXT("translation:\nOnce")));
	}));
	return true;
}

#endif
