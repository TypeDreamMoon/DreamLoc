// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocTranslateModule.h"
#include "DreamLocTranslateTestUtil.h"
#include "Framework/Docking/TabManager.h"
#include "SDreamLocTranslateWindow.h"
#include "Widgets/Docking/SDockTab.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamLocTranslateWindowTestsLocal
{
	using namespace DreamLocTranslateTestUtil;

	TSharedPtr<FDreamLocTranslateRowItem> FindItem(const SDreamLocTranslateWindow& InWindow, const FString& InKey)
	{
		const TSharedPtr<FDreamLocTranslateRowItem>* Found = InWindow.GetRowItems().FindByPredicate([&InKey](const TSharedPtr<FDreamLocTranslateRowItem>& InItem)
		{
			return InItem->Row.Key == InKey;
		});
		return Found != nullptr ? *Found : nullptr;
	}

	FString Describe(const FDreamLocEntry& InEntry)
	{
		return FScenario::Field(InEntry, EDreamLocField::Text) + TEXT("|") + FScenario::Field(InEntry, EDreamLocField::State);
	}

	void CloseTab()
	{
		if (const TSharedPtr<SDockTab> Tab = FGlobalTabmanager::Get()->FindExistingLiveTab(DreamLocTranslateWindow::GetTabName()))
		{
			Tab->RequestCloseTab();
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocTranslateWindowTest, "DreamLoc.Translate.Window.OpensTranslatesAgainstAFakeServiceAndWritesTheReviewBack",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FDreamLocTranslateWindowTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocTranslateWindowTestsLocal;
	const FScenarioRef S = MakeShared<FScenario, ESPMode::ThreadSafe>();
	if (!S->Begin(*this, TEXT("TranslateWindow")))
	{
		return false;
	}
	S->Write(TEXT("dloc.ini"), TEXT("[DreamLoc]\nSourceCulture=zh-Hans\nTargetCultures=en\n\n[Translate]\nProfile=model\n"));
	S->Write(TEXT("en/UI/Menu.dloc"), MakeDloc(TEXT("en"), TEXT("Menu"), {
		{ TEXT("Start"), TEXT("开始游戏") }, { TEXT("Quit"), TEXT("退出") }, { TEXT("Back"), TEXT("返回"), TEXT("Back"), TEXT("done") } }));
	S->Write(TEXT("en/UI/Other.dloc"), MakeDloc(TEXT("en"), TEXT("Other"), { { TEXT("Note"), TEXT("备注") } }));
	// What the window reads as the user's profiles: the scenario's file, never the real one.
	TestTrue(TEXT("the profiles saved where the test says"), DreamLocTranslateConfig::SaveProfiles({ S->Fake->Profile(TEXT("OpenAI"), TEXT("model")) }));

	const TSharedPtr<SDreamLocTranslateWindow> Window = DreamLocTranslateWindow::Open();
	if (!Window.IsValid())
	{
		AddError(TEXT("Tools > DreamLoc > Translate did not open its tab"));
		S->End();
		return false;
	}
	Window->SetDirectories(S->DLoc, S->Root);
	Window->SetCultures({ TEXT("en") });
	Window->SetProfile(TEXT("model"));
	const FDreamLocTranslateEstimate Estimate = Window->RunEstimate();
	TestEqual(TEXT("the estimate: the three without a translation"), FString::Printf(TEXT("%d of %d"), Estimate.Distinct, Estimate.Entries), TEXT("3 of 3"));
	TestTrue(TEXT("a run starts"), Window->StartTranslate());
	TestTrue(TEXT("and runs"), Window->IsRunning());
	TestFalse(TEXT("one run at a time"), Window->StartTranslate());

	const double Began = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Window, Began]()
	{
		if (!Window->IsRunning())
		{
			return true;
		}
		if (FPlatformTime::Seconds() - Began > 60.0)
		{
			AddError(TEXT("the window's run did not finish in time"));
			Window->CancelTranslate();
		}
		return false;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, S, Window]()
	{
		TestEqual(TEXT("the results table: a row for each entry sent"), Window->GetRowItems().Num(), 3);
		TestEqual(TEXT("all translated"), Window->GetLastResult().Translated, 3);
		const TSharedPtr<FDreamLocTranslateRowItem> Start = FindItem(*Window, TEXT("Start"));
		const TSharedPtr<FDreamLocTranslateRowItem> Quit = FindItem(*Window, TEXT("Quit"));
		const TSharedPtr<FDreamLocTranslateRowItem> Note = FindItem(*Window, TEXT("Note"));
		TestTrue(TEXT("each row there"), Start.IsValid() && Quit.IsValid() && Note.IsValid());
		if (Start.IsValid() && Quit.IsValid() && Note.IsValid())
		{
			TestEqual(TEXT("a row: its new translation and translator"), Start->Row.NewText + TEXT("|") + Start->Row.Backend, TEXT("en: 开始游戏|model"));
			TestEqual(TEXT("written as machine"), Describe(S->Entry(TEXT("en/UI/Menu.dloc"), TEXT("Start"))), TEXT("en: 开始游戏|machine"));
			TestEqual(TEXT("the reviewed one untouched"), Describe(S->Entry(TEXT("en/UI/Menu.dloc"), TEXT("Back"))), TEXT("Back|done"));

			TestTrue(TEXT("passed"), Window->PassRows({ Start }));
			TestEqual(TEXT("passing marks it done"), Describe(S->Entry(TEXT("en/UI/Menu.dloc"), TEXT("Start"))), TEXT("en: 开始游戏|done"));
			TestTrue(TEXT("rejected"), Window->RejectRows({ Quit }));
			TestFalse(TEXT("rejecting clears it"), S->Entry(TEXT("en/UI/Menu.dloc"), TEXT("Quit")).IsTranslated());
			TestTrue(TEXT("edited"), Window->EditRow(Note, TEXT("A note")));
			TestEqual(TEXT("an edit is a person's draft"), Describe(S->Entry(TEXT("en/UI/Other.dloc"), TEXT("Note"))), TEXT("A note|draft"));
			TestEqual(TEXT("the rows say so"), Start->Review + TEXT(" ") + Quit->Review + TEXT(" ") + Note->Review, TEXT("passed rejected edited"));
			const int32 Line = DreamLocTranslateFlow::FindEntryLine(S->DLoc, TEXT("en"), TEXT("UI/Menu.dloc"), TEXT("Menu"), TEXT("Quit"));
			TArray<FString> Lines;
			S->Read(TEXT("en/UI/Menu.dloc")).ParseIntoArrayLines(Lines, /*InCullEmpty*/ false);
			TestTrue(TEXT("a key's line, to open the file at"), Lines.IsValidIndex(Line - 1) && Lines[Line - 1].StartsWith(TEXT("Quit")));
		}
		Window->SetIncludeMachine(true);
		Window->LoadForReview();
		TestEqual(TEXT("no machine translation left to review"), Window->GetRowItems().Num(), 0);
		CloseTab();
		S->End();
		return true;
	}));
	return true;
}

#endif
