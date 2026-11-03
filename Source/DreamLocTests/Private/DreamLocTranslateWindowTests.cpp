// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocTranslateModule.h"
#include "DreamLocTranslateTestUtil.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/IConsoleManager.h"
#include "Layout/WidgetPath.h"
#include "SDreamLocTranslateWindow.h"
#include "Types/ISlateMetaData.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

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

namespace DreamLocTranslateWindowTestsLocal
{
	/**
	 * Paints one window. Under -nullrhi Slate paints nothing (and so ticks no widget, and builds no hit-test grid):
	 * Slate.SkipWidgetDrawingInHeadlessMode is turned off for this window's paint alone.
	 */
	void Paint(const TSharedRef<SWindow>& InWindow)
	{
		IConsoleVariable* Skip = IConsoleManager::Get().FindConsoleVariable(TEXT("Slate.SkipWidgetDrawingInHeadlessMode"));
		const bool bSkipping = Skip != nullptr && Skip->GetBool();
		if (bSkipping)
		{
			Skip->Set(false, ECVF_SetByCode);
		}
		FSlateApplication::Get().ForceRedrawWindow(InWindow);
		if (bSkipping)
		{
			Skip->Set(true, ECVF_SetByCode);
		}
	}

	TSharedPtr<SWidget> FindTagged(const TSharedRef<SWidget>& InRoot, FName InTag)
	{
		const TSharedPtr<FTagMetaData> Meta = InRoot->GetMetaData<FTagMetaData>();
		if (Meta.IsValid() && Meta->Tag == InTag)
		{
			return InRoot;
		}
		FChildren* Children = InRoot->GetChildren();
		for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
		{
			if (const TSharedPtr<SWidget> Found = FindTagged(Children->GetChildAt(Index), InTag))
			{
				return Found;
			}
		}
		return nullptr;
	}

	/** The test's window, the pointer it clicks with (a virtual user's: the real cursor stays where the user has it), and what each click saw. */
	struct FClicks
	{
		FScenarioRef Scenario;
		TSharedPtr<SDreamLocTranslateWindow> Window;
		TSharedPtr<SWindow> Host;
		TSharedPtr<FSlateVirtualUserHandle> User;
		TSharedPtr<SButton> Button;
		FVector2D Point = FVector2D::ZeroVector;
		int32 Frames = 0;
		int32 CallsBefore = 0;
		bool bFailed = false;

		explicit FClicks(FScenarioRef InScenario) : Scenario(InScenario) {}

		FPointerEvent Event(const TSet<FKey>& InPressed, FKey InButton) const
		{
			return FPointerEvent(User->GetUserIndex(), FSlateApplicationBase::CursorPointerIndex, Point, Point, InPressed, InButton, 0.0f, FModifierKeysState());
		}
	};

	/** Runs one step a frame; a step that returns false runs again the next frame. */
	class FSteps : public IAutomationLatentCommand
	{
	public:
		explicit FSteps(TArray<TFunction<bool()>> InSteps) : Steps(MoveTemp(InSteps)) {}

		virtual bool Update() override
		{
			if (Index < Steps.Num() && Steps[Index]())
			{
				++Index;
			}
			return Index >= Steps.Num();
		}

	private:
		TArray<TFunction<bool()>> Steps;
		int32 Index = 0;
	};

	/**
	 * A click as the platform delivers one: the pointer onto the button, a frame, the button down, a frame, up. With
	 * bInActivatedBetween every pointer capture is released between down and up, as Slate does when a window is
	 * activated (FSlateApplication::ProcessWindowActivatedEvent): what a click into a window that was not active can meet.
	 */
	void AddClick(FAutomationTestBase& InTest, TArray<TFunction<bool()>>& OutSteps, const TSharedRef<FClicks>& InClicks, const FString& InKey, const TCHAR* InTag,
		bool bInActivatedBetween)
	{
		FAutomationTestBase* Test = &InTest;
		OutSteps.Add([Test, InClicks, InKey, InTag]()
		{
			FClicks& C = *InClicks;
			if (C.bFailed)
			{
				return true;
			}
			Paint(C.Host.ToSharedRef());
			const TSharedPtr<FDreamLocTranslateRowItem> Item = FindItem(*C.Window, InKey);
			const TSharedPtr<SWidget> Row = Item.IsValid() ? C.Window->GetRowWidget(Item) : nullptr;
			const TSharedPtr<SWidget> Found = Row.IsValid() ? FindTagged(Row.ToSharedRef(), InTag) : nullptr;
			FWidgetPath Path;
			if (!Found.IsValid() || !FSlateApplication::Get().FindPathToWidget(Found.ToSharedRef(), Path) || !Path.IsValid())
			{
				if (++C.Frames > 120)
				{
					Test->AddError(FString::Printf(TEXT("%s of '%s' is not on screen after 120 frames"), InTag, *InKey));
					C.bFailed = true;
					return true;
				}
				return false;
			}
			C.Frames = 0;
			const FGeometry& Geometry = Path.Widgets.Last().Geometry;
			C.Point = FVector2D(Geometry.LocalToAbsolute(Geometry.GetLocalSize() * 0.5f));
			C.Button = StaticCastSharedPtr<SButton>(Found);
			Test->AddInfo(FString::Printf(TEXT("%s of '%s': %.0f x %.0f laid out, %.0f x %.0f wanted"), InTag, *InKey, Geometry.GetLocalSize().X, Geometry.GetLocalSize().Y,
				Found->GetDesiredSize().X, Found->GetDesiredSize().Y));
			Test->TestTrue(FString::Printf(TEXT("%s of '%s' laid out whole"), InTag, *InKey), Geometry.GetLocalSize().X + 0.5f >= Found->GetDesiredSize().X);
			for (int32 Index = 0; Index < Path.Widgets.Num(); ++Index)
			{
				if (Path.Widgets[Index].Widget == Row)
				{
					const FGeometry& RowGeometry = Path.Widgets[Index].Geometry;
					const double RowRight = RowGeometry.LocalToAbsolute(RowGeometry.GetLocalSize()).X;
					const double ButtonRight = Geometry.LocalToAbsolute(Geometry.GetLocalSize()).X;
					Test->TestTrue(FString::Printf(TEXT("%s of '%s' inside its row (right edge %.0f, the row's %.0f)"), InTag, *InKey, ButtonRight, RowRight),
						ButtonRight <= RowRight + 0.5);
				}
			}
			FSlateApplication::Get().ProcessMouseMoveEvent(C.Event(TSet<FKey>(), EKeys::Invalid));
			return true;
		});
		OutSteps.Add([Test, InClicks, InTag, bInActivatedBetween]()
		{
			FClicks& C = *InClicks;
			if (C.bFailed)
			{
				return true;
			}
			Paint(C.Host.ToSharedRef());
			const bool bHovered = C.Button->IsHovered();
			FSlateApplication::Get().ProcessMouseButtonDownEvent(nullptr, C.Event(TSet<FKey>({ EKeys::LeftMouseButton }), EKeys::LeftMouseButton));
			Test->AddInfo(FString::Printf(TEXT("%s: hovered %d, pressed after the button went down %d"), InTag, bHovered ? 1 : 0, C.Button->IsPressed() ? 1 : 0));
			if (bInActivatedBetween)
			{
				FSlateApplication::Get().ReleaseAllPointerCapture(C.User->GetUserIndex());
				Test->AddInfo(FString::Printf(TEXT("%s: every pointer capture released before the button went up"), InTag));
			}
			return true;
		});
		OutSteps.Add([Test, InClicks, InTag]()
		{
			FClicks& C = *InClicks;
			if (C.bFailed)
			{
				return true;
			}
			Paint(C.Host.ToSharedRef());
			const bool bPressed = C.Button->IsPressed();
			FSlateApplication::Get().ProcessMouseButtonUpEvent(C.Event(TSet<FKey>(), EKeys::LeftMouseButton));
			Test->AddInfo(FString::Printf(TEXT("%s: still pressed a frame later %d"), InTag, bPressed ? 1 : 0));
			Paint(C.Host.ToSharedRef());
			return true;
		});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocTranslateWindowClicksTest, "DreamLoc.Translate.Window.EachRowsButtonsWorkWhenClicked",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FDreamLocTranslateWindowClicksTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocTranslateWindowTestsLocal;
	const FScenarioRef S = MakeShared<FScenario, ESPMode::ThreadSafe>();
	if (!S->Begin(*this, TEXT("TranslateWindowClicks")))
	{
		return false;
	}
	S->Write(TEXT("dloc.ini"), TEXT("[DreamLoc]\nSourceCulture=zh-Hans\nTargetCultures=en\n\n[Translate]\nProfile=profile1\n"));
	S->Write(TEXT("en/UI/Menu.dloc"), MakeDloc(TEXT("en"), TEXT("Menu"), {
		{ TEXT("Start"), TEXT("开始游戏") }, { TEXT("Quit"), TEXT("退出") }, { TEXT("Note"), TEXT("备注") }, { TEXT("Lines"), TEXT("第一行\n第二行") } }));
	TestTrue(TEXT("the profiles saved where the test says"), DreamLocTranslateConfig::SaveProfiles({ S->Fake->Profile(TEXT("OpenAI"), TEXT("profile1")) }));

	// The window in a window of the test's own: the size the window opens at, far off the screen, never activated.
	const TSharedRef<FClicks> C = MakeShared<FClicks>(S);
	C->Window = SNew(SDreamLocTranslateWindow).DLocDirectory(S->DLoc).OwnerDirectory(S->Root);
	C->Host = SNew(SWindow)
		.Title(FText::FromString(TEXT("DreamLoc translate clicks")))
		.ClientSize(DreamLocTranslateWindow::GetDefaultSize())
		.ScreenPosition(FVector2D(-20000.0, -20000.0))
		.AutoCenter(EAutoCenter::None)
		.SaneWindowPlacement(false)
		.ActivationPolicy(EWindowActivationPolicy::Never)
		.FocusWhenFirstShown(false)
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		[
			C->Window.ToSharedRef()
		];
	FSlateApplication::Get().AddWindow(C->Host.ToSharedRef(), /*bShowImmediately*/ true);
	C->User = FSlateApplication::Get().FindOrCreateVirtualUser(5);
	C->Window->SetCultures({ TEXT("en") });
	C->Window->SetProfile(TEXT("profile1"));
	TestTrue(TEXT("a run starts"), C->Window->StartTranslate());

	TArray<TFunction<bool()>> Steps;
	const double Began = FPlatformTime::Seconds();
	Steps.Add([this, C, Began]()
	{
		if (!C->Window->IsRunning())
		{
			TestEqual(TEXT("four rows"), C->Window->GetRowItems().Num(), 4);
			return true;
		}
		if (FPlatformTime::Seconds() - Began > 60.0)
		{
			AddError(TEXT("the run did not finish"));
			C->Window->CancelTranslate();
		}
		return false;
	});
	// The columns as laid out: the translator's name whole.
	Steps.Add([this, C]()
	{
		Paint(C->Host.ToSharedRef());
		const TSharedPtr<FDreamLocTranslateRowItem> Item = FindItem(*C->Window, TEXT("Start"));
		const TSharedPtr<SWidget> Row = Item.IsValid() ? C->Window->GetRowWidget(Item) : nullptr;
		const TSharedPtr<SWidget> Backend = Row.IsValid() ? FindTagged(Row.ToSharedRef(), TEXT("DreamLocTranslate.Backend")) : nullptr;
		FWidgetPath Path;
		if (!Backend.IsValid() || !FSlateApplication::Get().FindPathToWidget(Backend.ToSharedRef(), Path))
		{
			return ++C->Frames > 120;
		}
		C->Frames = 0;
		const FVector2D Laid = FVector2D(Path.Widgets.Last().Geometry.GetLocalSize());
		const FVector2D Wanted = FVector2D(Backend->GetDesiredSize());
		AddInfo(FString::Printf(TEXT("the translator's name: %.0f wide laid out, %.0f wanted"), Laid.X, Wanted.X));
		TestTrue(TEXT("the translator's name fits its column"), Laid.X + 0.5 >= Wanted.X);
		return true;
	});
	AddClick(*this, Steps, C, TEXT("Start"), TEXT("DreamLocTranslate.Pass"), /*bInActivatedBetween*/ false);
	Steps.Add([this, C]()
	{
		TestEqual(TEXT("a click on Pass marks it done"), Describe(C->Scenario->Entry(TEXT("en/UI/Menu.dloc"), TEXT("Start"))), TEXT("en: 开始游戏|done"));
		return true;
	});
	AddClick(*this, Steps, C, TEXT("Quit"), TEXT("DreamLocTranslate.Reject"), /*bInActivatedBetween*/ true);
	Steps.Add([this, C]()
	{
		TestFalse(TEXT("a click on Reject clears it"), C->Scenario->Entry(TEXT("en/UI/Menu.dloc"), TEXT("Quit")).IsTranslated());
		C->CallsBefore = C->Scenario->Fake->GetCalls(TEXT("openai")).Num();
		return true;
	});
	AddClick(*this, Steps, C, TEXT("Note"), TEXT("DreamLocTranslate.Again"), /*bInActivatedBetween*/ true);
	Steps.Add([this, C]()
	{
		TestTrue(TEXT("a click on Again starts a run"), C->Window->IsRunning() || C->Scenario->Fake->GetCalls(TEXT("openai")).Num() > C->CallsBefore);
		return true;
	});
	const double AgainBegan = FPlatformTime::Seconds();
	Steps.Add([this, C, AgainBegan]()
	{
		if (C->Window->IsRunning() && FPlatformTime::Seconds() - AgainBegan < 180.0)
		{
			return false;
		}
		const TArray<DreamLocTranslateFake::FCall> Calls = C->Scenario->Fake->GetCalls(TEXT("openai"));
		TestEqual(TEXT("which sends one request"), Calls.Num() - C->CallsBefore, 1);
		TestTrue(TEXT("for that entry alone"), Calls.Num() > C->CallsBefore && Calls.Last().Body.Contains(TEXT("\\\"key\\\":\\\"Note\\\""))
			&& !Calls.Last().Body.Contains(TEXT("\\\"key\\\":\\\"Start\\\"")));
		const TSharedPtr<FDreamLocTranslateRowItem> Note = FindItem(*C->Window, TEXT("Note"));
		TestTrue(TEXT("and the row shows the new translation"), Note.IsValid() && !Note->bBusy && Note->Row.NewText == TEXT("en: 备注"));
		return true;
	});
	// A translation with a line break: its box focused and left, as clicking into it and away does. Nothing is written.
	Steps.Add([this, C]()
	{
		Paint(C->Host.ToSharedRef());
		const TSharedPtr<FDreamLocTranslateRowItem> Item = FindItem(*C->Window, TEXT("Lines"));
		const TSharedPtr<SWidget> Row = Item.IsValid() ? C->Window->GetRowWidget(Item) : nullptr;
		const TSharedPtr<SWidget> Box = Row.IsValid() ? FindTagged(Row.ToSharedRef(), TEXT("DreamLocTranslate.New")) : nullptr;
		if (!Box.IsValid())
		{
			return ++C->Frames > 120;
		}
		C->Frames = 0;
		const TSharedPtr<SWidget> Source = FindTagged(Row.ToSharedRef(), TEXT("DreamLocTranslate.Source"));
		TestEqual(TEXT("the source's line break shown as \\n on its line"), Source.IsValid() ? StaticCastSharedPtr<STextBlock>(Source)->GetText().ToString() : FString(),
			TEXT("第一行\\n第二行"));
		TestTrue(TEXT("the box takes focus"), FSlateApplication::Get().SetUserFocus(C->User->GetUserIndex(), Box, EFocusCause::Mouse));
		return true;
	});
	Steps.Add([C]()
	{
		Paint(C->Host.ToSharedRef());
		FSlateApplication::Get().ClearUserFocus(C->User->GetUserIndex(), EFocusCause::Mouse);
		Paint(C->Host.ToSharedRef());
		return true;
	});
	Steps.Add([this, C]()
	{
		TestEqual(TEXT("a translation's line break survives its box losing focus"), Describe(C->Scenario->Entry(TEXT("en/UI/Menu.dloc"), TEXT("Lines"))),
			TEXT("en: 第一行\n第二行|machine"));
		return true;
	});
	Steps.Add([C]()
	{
		FSlateApplication::Get().RequestDestroyWindow(C->Host.ToSharedRef());
		C->User.Reset();
		C->Scenario->End();
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FSteps(MoveTemp(Steps)));
	return true;
}

#endif
