// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocChips.h"
#include "DreamLocContextProvider.h"
#include "DreamLocInContext.h"
#include "DreamLocTextSource.h"
#include "DreamLocTranslateModule.h"
#include "DreamLocTranslateTestUtil.h"
#include "Features/IModularFeatures.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/IPluginManager.h"
#include "ISettingsCategory.h"
#include "ISettingsContainer.h"
#include "ISettingsModule.h"
#include "ISettingsSection.h"
#include "Layout/WidgetPath.h"
#include "Misc/ScopeExit.h"
#include "SDreamLocPretranslateDialog.h"
#include "SDreamLocTranslateSettings.h"
#include "SDreamLocTranslateWindow.h"
#include "Types/ISlateMetaData.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamLocTranslateWindowTestsLocal
{
	using namespace DreamLocTranslateTestUtil;

	FString Describe(const FDreamLocEntry& InEntry)
	{
		return FScenario::Field(InEntry, EDreamLocField::Text) + TEXT("|") + FScenario::Field(InEntry, EDreamLocField::State);
	}

	FString Keys(TConstArrayView<TSharedPtr<FDreamLocReviewEntry>> InEntries)
	{
		TArray<FString> Out;
		for (const TSharedPtr<FDreamLocReviewEntry>& Entry : InEntries)
		{
			Out.Add(Entry->Entry.Key);
		}
		return FString::Join(Out, TEXT(" "));
	}

	FString CurrentKey(const SDreamLocTranslateWindow& InWindow)
	{
		return InWindow.GetCurrent().IsValid() ? InWindow.GetCurrent()->Entry.Key : FString(TEXT("(none)"));
	}

	/**
	 * Paints a window and the windows it holds (a menu, a dialog). Under -nullrhi Slate paints nothing (and so ticks no
	 * widget, runs no active timer, builds no hit-test grid): Slate.SkipWidgetDrawingInHeadlessMode is turned off for these
	 * paints alone.
	 */
	void Paint(const TSharedRef<SWindow>& InWindow)
	{
		IConsoleVariable* Skip = IConsoleManager::Get().FindConsoleVariable(TEXT("Slate.SkipWidgetDrawingInHeadlessMode"));
		const bool bSkipping = Skip != nullptr && Skip->GetBool();
		if (bSkipping)
		{
			Skip->Set(false, ECVF_SetByCode);
		}
		TArray<TSharedRef<SWindow>> Windows = { InWindow };
		for (int32 Index = 0; Index < Windows.Num(); ++Index)
		{
			FSlateApplication::Get().ForceRedrawWindow(Windows[Index]);
			Windows.Append(Windows[Index]->GetChildWindows());
		}
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

	/** In a window or any window it holds. */
	TSharedPtr<SWidget> FindInWindows(const TSharedRef<SWindow>& InWindow, FName InTag)
	{
		if (const TSharedPtr<SWidget> Found = FindTagged(InWindow, InTag))
		{
			return Found;
		}
		for (const TSharedRef<SWindow>& Child : InWindow->GetChildWindows())
		{
			if (const TSharedPtr<SWidget> Found = FindInWindows(Child, InTag))
			{
				return Found;
			}
		}
		return nullptr;
	}

	/** The first text block's text under a widget: a button's label, a toggle's. */
	FString TextUnder(const TSharedPtr<SWidget>& InWidget)
	{
		if (!InWidget.IsValid())
		{
			return FString(TEXT("(none)"));
		}
		if (InWidget->GetType() == TEXT("STextBlock"))
		{
			return StaticCastSharedPtr<STextBlock>(InWidget)->GetText().ToString();
		}
		FChildren* Children = InWidget->GetChildren();
		for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
		{
			const FString Found = TextUnder(Children->GetChildAt(Index));
			if (Found != TEXT("(none)"))
			{
				return Found;
			}
		}
		return FString(TEXT("(none)"));
	}

	/** The test's window, the pointer and keyboard it works with (a virtual user's: the real cursor and focus stay the user's). */
	struct FDesk
	{
		FScenarioRef Scenario;
		TSharedPtr<SDreamLocTranslateWindow> Window;
		TSharedPtr<SWindow> Host;
		TSharedPtr<FSlateVirtualUserHandle> User;
		FVector2D Point = FVector2D::ZeroVector;
		/** What is held while the pointer moves and clicks. */
		FModifierKeysState Modifiers;
		int32 Frames = 0;
		/** Where the widget to click was the frame before. */
		TOptional<FVector2D> LastCentre;
		int32 CallsBefore = 0;
		double Began = 0.0;
		bool bFailed = false;

		explicit FDesk(FScenarioRef InScenario) : Scenario(InScenario) {}

		FPointerEvent Event(const TSet<FKey>& InPressed, FKey InButton) const
		{
			return FPointerEvent(User->GetUserIndex(), FSlateApplicationBase::CursorPointerIndex, Point, Point, InPressed, InButton, 0.0f, Modifiers);
		}

		TSharedPtr<SWidget> Tagged(const TCHAR* InTag) const
		{
			return FindInWindows(Host.ToSharedRef(), InTag);
		}

		/** A part of an entry's row in the list. */
		TSharedPtr<SWidget> Row(const TCHAR* InNamespace, const TCHAR* InKey) const
		{
			return Window->GetRowWidget(Window->GetModel().Find(InNamespace, InKey));
		}

		FDreamLocEntry Entry(const TCHAR* InFile, const TCHAR* InKey) const
		{
			return Scenario->Entry(InFile, InKey);
		}

		TSharedPtr<SMultiLineEditableTextBox> Box() const
		{
			return StaticCastSharedPtr<SMultiLineEditableTextBox>(Tagged(TEXT("DreamLocTranslate.Translation")));
		}
	};
	using FDeskRef = TSharedRef<FDesk>;

	/** A widget in a window of the test's own: the review window's size, far off the screens, never activated. */
	FDeskRef MakeDeskWith(const FScenarioRef& InScenario, const TSharedRef<SWidget>& InContent, const FVector2D& InSize = DreamLocTranslateWindow::GetDefaultSize())
	{
		const FDeskRef D = MakeShared<FDesk>(InScenario);
		D->Host = SNew(SWindow)
			.Title(FText::FromString(TEXT("DreamLoc review desk")))
			.ClientSize(InSize)
			.ScreenPosition(FVector2D(-20000.0, -20000.0))
			.AutoCenter(EAutoCenter::None)
			.SaneWindowPlacement(false)
			.ActivationPolicy(EWindowActivationPolicy::Never)
			.FocusWhenFirstShown(false)
			.SupportsMaximize(false)
			.SupportsMinimize(false)
			[
				InContent
			];
		// Made hidden and moved before it is shown: the window is made at 0,0 whatever ScreenPosition says.
		FSlateApplication::Get().AddWindow(D->Host.ToSharedRef(), /*bShowImmediately*/ false);
		D->Host->MoveWindowTo(FVector2D(-20000.0, -20000.0));
		D->Host->ShowWindow();
		D->User = FSlateApplication::Get().FindOrCreateVirtualUser(5);
		return D;
	}

	/** The review window, so. */
	FDeskRef MakeDesk(const FScenarioRef& InScenario, const FVector2D& InSize = DreamLocTranslateWindow::GetDefaultSize())
	{
		const TSharedRef<SDreamLocTranslateWindow> Window = SNew(SDreamLocTranslateWindow).DLocDirectory(InScenario->DLoc).OwnerDirectory(InScenario->Root);
		const FDeskRef D = MakeDeskWith(InScenario, Window, InSize);
		D->Window = Window;
		return D;
	}

	FString TextOf(const TSharedPtr<SWidget>& InBox)
	{
		return InBox.IsValid() ? StaticCastSharedPtr<SEditableTextBox>(InBox)->GetText().ToString() : FString(TEXT("(none)"));
	}

	FString ProfileNames(TConstArrayView<FDreamLocTranslateProfile> InProfiles)
	{
		TArray<FString> Out;
		for (const FDreamLocTranslateProfile& Profile : InProfiles)
		{
			Out.Add(Profile.Name);
		}
		return FString::Join(Out, TEXT(","));
	}

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

	/** A step: InCheck run once (after a paint), the desk's failure aside. */
	void AddCheck(TArray<TFunction<bool()>>& OutSteps, const FDeskRef& InDesk, TFunction<void()> InCheck)
	{
		OutSteps.Add([InDesk, InCheck]()
		{
			if (!InDesk->bFailed)
			{
				Paint(InDesk->Host.ToSharedRef());
				InCheck();
			}
			return true;
		});
	}

	/** A step run again each frame until InDone says so, or a failure after InSeconds. */
	void AddWait(FAutomationTestBase& InTest, TArray<TFunction<bool()>>& OutSteps, const FDeskRef& InDesk, const FString& InWhat, double InSeconds, TFunction<bool()> InDone)
	{
		FAutomationTestBase* Test = &InTest;
		OutSteps.Add([InDesk]() { InDesk->Began = FPlatformTime::Seconds(); return true; });
		OutSteps.Add([Test, InDesk, InWhat, InSeconds, InDone]()
		{
			if (InDesk->bFailed)
			{
				return true;
			}
			Paint(InDesk->Host.ToSharedRef());
			if (InDone())
			{
				return true;
			}
			if (FPlatformTime::Seconds() - InDesk->Began > InSeconds)
			{
				Test->AddError(FString::Printf(TEXT("%s: not after %.0f seconds"), *InWhat, InSeconds));
				InDesk->bFailed = true;
				return true;
			}
			return false;
		});
	}

	/**
	 * A click as the platform delivers one: the pointer onto the widget InFind finds (waiting for it to be laid out), a
	 * frame, the button down, a frame, up.
	 */
	void AddClick(FAutomationTestBase& InTest, TArray<TFunction<bool()>>& OutSteps, const FDeskRef& InDesk, const FString& InWhat, TFunction<TSharedPtr<SWidget>()> InFind,
		FKey InButton = EKeys::LeftMouseButton)
	{
		FAutomationTestBase* Test = &InTest;
		OutSteps.Add([Test, InDesk, InWhat, InFind]()
		{
			FDesk& D = *InDesk;
			if (D.bFailed)
			{
				return true;
			}
			Paint(D.Host.ToSharedRef());
			const TSharedPtr<SWidget> Found = InFind();
			FWidgetPath Path;
			if (!Found.IsValid() || !FSlateApplication::Get().FindPathToWidget(Found.ToSharedRef(), Path) || !Path.IsValid())
			{
				if (++D.Frames > 120)
				{
					Test->AddError(FString::Printf(TEXT("%s is not on screen after 120 frames"), *InWhat));
					D.bFailed = true;
					return true;
				}
				return false;
			}
			const FGeometry& Geometry = Path.Widgets.Last().Geometry;
			const FVector2D Centre = FVector2D(Geometry.LocalToAbsolute(Geometry.GetLocalSize() * 0.5f));
			// Where it is two frames running: a layout that settles a frame late (a wrap box) moves it under the click.
			if (!D.LastCentre.IsSet() || !D.LastCentre->Equals(Centre, 0.5))
			{
				D.LastCentre = Centre;
				if (++D.Frames > 120)
				{
					Test->AddError(FString::Printf(TEXT("%s does not keep still after 120 frames"), *InWhat));
					D.bFailed = true;
					return true;
				}
				return false;
			}
			D.Frames = 0;
			D.LastCentre.Reset();
			D.Point = Centre;
			FSlateApplication::Get().ProcessMouseMoveEvent(D.Event(TSet<FKey>(), EKeys::Invalid));
			return true;
		});
		OutSteps.Add([InDesk, InButton]()
		{
			if (!InDesk->bFailed)
			{
				Paint(InDesk->Host.ToSharedRef());
				FSlateApplication::Get().ProcessMouseButtonDownEvent(nullptr, InDesk->Event(TSet<FKey>({ InButton }), InButton));
			}
			return true;
		});
		OutSteps.Add([InDesk, InButton]()
		{
			if (!InDesk->bFailed)
			{
				Paint(InDesk->Host.ToSharedRef());
				FSlateApplication::Get().ProcessMouseButtonUpEvent(InDesk->Event(TSet<FKey>(), InButton));
				Paint(InDesk->Host.ToSharedRef());
			}
			return true;
		});
	}

	/** A key pressed and let go, to whatever has the user's focus. */
	void AddKey(TArray<TFunction<bool()>>& OutSteps, const FDeskRef& InDesk, FKey InKey, bool bInControl = false, bool bInShift = false, bool bInAlt = false)
	{
		OutSteps.Add([InDesk, InKey, bInControl, bInShift, bInAlt]()
		{
			if (!InDesk->bFailed)
			{
				Paint(InDesk->Host.ToSharedRef());
				const FModifierKeysState Modifiers(bInShift, false, bInControl, false, bInAlt, false, false, false, false);
				const uint32 UserIndex = InDesk->User->GetUserIndex();
				FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(InKey, Modifiers, UserIndex, false, 0, 0));
				FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(InKey, Modifiers, UserIndex, false, 0, 0));
				Paint(InDesk->Host.ToSharedRef());
			}
			return true;
		});
	}

	/** Characters typed, as the platform sends them (a backspace is character 8). */
	void AddType(TArray<TFunction<bool()>>& OutSteps, const FDeskRef& InDesk, const FString& InText)
	{
		OutSteps.Add([InDesk, InText]()
		{
			if (!InDesk->bFailed)
			{
				Paint(InDesk->Host.ToSharedRef());
				for (const TCHAR Char : InText)
				{
					FSlateApplication::Get().ProcessKeyCharEvent(FCharacterEvent(Char, FModifierKeysState(), InDesk->User->GetUserIndex(), false));
				}
				Paint(InDesk->Host.ToSharedRef());
			}
			return true;
		});
	}

	/** The translation box focused for the user, the cursor at an offset into its text. */
	void AddCursor(FAutomationTestBase& InTest, TArray<TFunction<bool()>>& OutSteps, const FDeskRef& InDesk, TFunction<int32()> InOffset)
	{
		FAutomationTestBase* Test = &InTest;
		OutSteps.Add([Test, InDesk, InOffset]()
		{
			FDesk& D = *InDesk;
			if (D.bFailed)
			{
				return true;
			}
			Paint(D.Host.ToSharedRef());
			const TSharedPtr<SMultiLineEditableTextBox> Box = D.Box();
			if (!Box.IsValid())
			{
				Test->AddError(TEXT("no translation box"));
				D.bFailed = true;
				return true;
			}
			FSlateApplication::Get().SetUserFocus(D.User->GetUserIndex(), Box, EFocusCause::Mouse);
			int32 Line = 0;
			int32 Column = 0;
			DreamLocChips::ToLineAndColumn(D.Window->GetEditText(), InOffset(), Line, Column);
			Box->GoTo(FTextLocation(Line, Column));
			return true;
		});
	}

	void AddEnd(TArray<TFunction<bool()>>& OutSteps, const FDeskRef& InDesk)
	{
		OutSteps.Add([InDesk]()
		{
			DreamLocInContext::SetOnPick(nullptr);
			FSlateApplication::Get().DismissAllMenus();
			FSlateApplication::Get().RequestDestroyWindow(InDesk->Host.ToSharedRef());
			InDesk->User.Reset();
			// The review window let go, so it stops watching the folder before the folder goes.
			InDesk->Window.Reset();
			InDesk->Host.Reset();
			return true;
		});
		OutSteps.Add([InDesk]()
		{
			InDesk->Scenario->End();
			return true;
		});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocTranslateDeskTest, "DreamLoc.Translate.Window.AReviewByMouseAndKeyboard",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FDreamLocTranslateDeskTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocTranslateWindowTestsLocal;
	const FScenarioRef S = MakeShared<FScenario, ESPMode::ThreadSafe>();
	if (!S->Begin(*this, TEXT("TranslateDesk")))
	{
		return false;
	}
	S->Write(TEXT("dloc.ini"), TEXT("[DreamLoc]\nSourceCulture=zh-Hans\nTargetCultures=en\n\n[Translate]\nProfile=model\n"));
	S->Write(TEXT("en/UI/Menu.dloc"), MakeDloc(TEXT("en"), TEXT("Menu"), {
		{ TEXT("Start"), TEXT("开始游戏") },
		{ TEXT("Quit"), TEXT("退出"), TEXT("Quit"), TEXT("machine") },
		{ TEXT("Back"), TEXT("返回"), TEXT("Back"), TEXT("done") },
		{ TEXT("Load"), TEXT("读取存档"), TEXT("Load"), TEXT("machine"), TEXT("读取") },
		{ TEXT("Gold"), TEXT("{0}金币"), TEXT("Gold"), TEXT("machine") },
		{ TEXT("Lines"), TEXT("第一行\n第二行"), TEXT("Line one\nLine two"), TEXT("machine") } }));
	S->Write(TEXT("en/UI/Other.dloc"), MakeDloc(TEXT("en"), TEXT("Other"), { { TEXT("Note"), TEXT("备注") } }));
	// What the window reads as the user's profiles: the scenario's file, never the real one.
	TestTrue(TEXT("the profiles saved where the test says"), DreamLocTranslateConfig::SaveProfiles({ S->Fake->Profile(TEXT("OpenAI"), TEXT("model")) }));

	const FDeskRef D = MakeDesk(S);
	TestTrue(TEXT("the test's window off the screens"), D->Host->GetPositionInScreen().X < -10000.0f);
	TestEqual(TEXT("the culture: the first one"), D->Window->GetCulture(), TEXT("en"));
	TArray<TFunction<bool()>> Steps;

	// The filters, with how many each has.
	AddCheck(Steps, D, [this, D]()
	{
		TestEqual(TEXT("All's count"), TextUnder(D->Tagged(TEXT("DreamLocTranslate.Filter.All"))), TEXT("All 7"));
		TestEqual(TEXT("To review's"), TextUnder(D->Tagged(TEXT("DreamLocTranslate.Filter.Review"))), TEXT("To review 3"));
		TestEqual(TEXT("Untranslated's"), TextUnder(D->Tagged(TEXT("DreamLocTranslate.Filter.Untranslated"))), TEXT("Untranslated 2"));
		TestEqual(TEXT("Stale's"), TextUnder(D->Tagged(TEXT("DreamLocTranslate.Filter.Stale"))), TEXT("Stale 1"));
		TestEqual(TEXT("Problems': a placeholder lost"), TextUnder(D->Tagged(TEXT("DreamLocTranslate.Filter.Problems"))), TEXT("Problems 1"));
		TestEqual(TEXT("Reviewed's"), TextUnder(D->Tagged(TEXT("DreamLocTranslate.Filter.Done"))), TEXT("Reviewed 1"));
	});
	AddClick(*this, Steps, D, TEXT("the To review filter"), [D]() { return D->Tagged(TEXT("DreamLocTranslate.Filter.Review")); });
	AddCheck(Steps, D, [this, D]()
	{
		TestEqual(TEXT("a click on a filter"), int32(D->Window->GetFilter()), int32(EDreamLocReviewFilter::Review));
		TestEqual(TEXT("shows what it lets through"), Keys(D->Window->GetShown()), TEXT("Quit Gold Lines"));
	});

	// A row clicked: the entry in the editor.
	AddClick(*this, Steps, D, TEXT("Quit's row"), [D]() { return D->Row(TEXT("Menu"), TEXT("Quit")); });
	AddCheck(Steps, D, [this, D]()
	{
		TestEqual(TEXT("a click on a row chooses it"), CurrentKey(*D->Window), TEXT("Quit"));
		TestEqual(TEXT("its translation in the box"), D->Window->GetEditText(), TEXT("Quit"));
	});
	// Ctrl+Enter: passed, and on to the next one to review.
	AddCursor(*this, Steps, D, []() { return 4; });
	AddKey(Steps, D, EKeys::Enter, /*bInControl*/ true);
	AddCheck(Steps, D, [this, D]()
	{
		TestEqual(TEXT("Ctrl+Enter passes it"), Describe(D->Entry(TEXT("en/UI/Menu.dloc"), TEXT("Quit"))), TEXT("Quit|done"));
		TestEqual(TEXT("and goes to the next to review"), CurrentKey(*D->Window), TEXT("Gold"));
		TestEqual(TEXT("which the filter still shows; the passed one it does not"), Keys(D->Window->GetShown()), TEXT("Gold Lines"));
		TestTrue(TEXT("the checks: the argument lost"), D->Window->GetEditProblems().Contains(TEXT("{0}")));
	});

	// F3: the argument the translation lacks, at the cursor.
	AddCursor(*this, Steps, D, []() { return 0; });
	AddKey(Steps, D, EKeys::F3);
	AddCheck(Steps, D, [this, D]() { TestEqual(TEXT("F3 puts the missing argument in"), D->Window->GetEditText(), TEXT("{0}Gold")); });
	AddWait(*this, Steps, D, TEXT("the checks run again as it is typed"), 10.0, [D]() { return !D->Window->GetEditProblems().Contains(TEXT("{0}")); });
	// Typed inside the argument: it stays whole, the character after it.
	AddCursor(*this, Steps, D, []() { return 1; });
	AddType(Steps, D, TEXT(" "));
	AddCheck(Steps, D, [this, D]() { TestEqual(TEXT("a space typed inside {0} goes after it"), D->Window->GetEditText(), TEXT("{0} Gold")); });
	// A backspace on its brace: it goes whole.
	AddCursor(*this, Steps, D, []() { return 3; });
	AddType(Steps, D, FString(TEXT("\b")));
	AddCheck(Steps, D, [this, D]() { TestEqual(TEXT("a backspace on its brace takes it all"), D->Window->GetEditText(), TEXT(" Gold")); });
	AddCursor(*this, Steps, D, []() { return 0; });
	AddKey(Steps, D, EKeys::F3);
	// Ctrl+Shift+Enter: kept as a draft, and on to the next.
	AddKey(Steps, D, EKeys::Enter, /*bInControl*/ true, /*bInShift*/ true);
	AddCheck(Steps, D, [this, D]()
	{
		TestEqual(TEXT("Ctrl+Shift+Enter keeps it as a draft"), Describe(D->Entry(TEXT("en/UI/Menu.dloc"), TEXT("Gold"))), TEXT("{0} Gold|draft"));
		TestEqual(TEXT("and goes on"), CurrentKey(*D->Window), TEXT("Lines"));
		TestEqual(TEXT("a translation's line break, as the file has it"), D->Window->GetEditText(), TEXT("Line one\nLine two"));
		const TSharedPtr<SWidget> Source = D->Tagged(TEXT("DreamLocTranslate.EditorSource"));
		TestEqual(TEXT("the source, its line break a chip"), Source.IsValid() ? StaticCastSharedPtr<SDreamLocChipText>(Source)->GetShownText() : FString(), TEXT("第一行[\\n]|第二行"));
	});
	// Alt+Up: the one before; the box left without a change writes nothing.
	AddCursor(*this, Steps, D, []() { return 0; });
	AddKey(Steps, D, EKeys::Up, false, false, /*bInAlt*/ true);
	AddCheck(Steps, D, [this, D]()
	{
		TestEqual(TEXT("Alt+Up goes to the one before"), CurrentKey(*D->Window), TEXT("Gold"));
		TestEqual(TEXT("a line break survives the box being left"), Describe(D->Entry(TEXT("en/UI/Menu.dloc"), TEXT("Lines"))), TEXT("Line one\nLine two|machine"));
	});

	// The buttons: Pass and next, clicked.
	AddClick(*this, Steps, D, TEXT("Pass and next"), [D]() { return D->Tagged(TEXT("DreamLocTranslate.PassNext")); });
	AddCheck(Steps, D, [this, D]()
	{
		TestEqual(TEXT("a click on Pass and next passes it"), Describe(D->Entry(TEXT("en/UI/Menu.dloc"), TEXT("Gold"))), TEXT("{0} Gold|done"));
		TestEqual(TEXT("and goes on"), CurrentKey(*D->Window), TEXT("Lines"));
	});

	// The list's menu, for the rows selected: Reject. A right click would push it in a window that takes the focus (the
	// user's, whatever they are doing): it is shown in a quiet window of the test's own, and clicked there.
	AddClick(*this, Steps, D, TEXT("the All filter"), [D]() { return D->Tagged(TEXT("DreamLocTranslate.Filter.All")); });
	AddClick(*this, Steps, D, TEXT("Lines' row"), [D]() { return D->Row(TEXT("Menu"), TEXT("Lines")); });
	AddCheck(Steps, D, [this, D]()
	{
		TestEqual(TEXT("the rows the menu is for"), Keys(D->Window->GetSelectedEntries()), TEXT("Lines"));
		const TSharedPtr<SWidget> Menu = D->Window->MakeContextMenu();
		TestTrue(TEXT("a menu"), Menu.IsValid());
		if (!Menu.IsValid())
		{
			D->bFailed = true;
			return;
		}
		const TSharedRef<SWindow> Holder = SNew(SWindow)
			.ClientSize(FVector2D(320.0, 240.0))
			.AutoCenter(EAutoCenter::None)
			.SaneWindowPlacement(false)
			.ActivationPolicy(EWindowActivationPolicy::Never)
			.FocusWhenFirstShown(false)
			[
				Menu.ToSharedRef()
			];
		FSlateApplication::Get().AddWindowAsNativeChild(Holder, D->Host.ToSharedRef(), /*bShowImmediately*/ false);
		Holder->MoveWindowTo(FVector2D(-19000.0, -20000.0));
		Holder->ShowWindow();
	});
	AddClick(*this, Steps, D, TEXT("the menu's Reject"), [D]() { return D->Tagged(TEXT("DreamLocTranslate.MenuReject")); });
	AddCheck(Steps, D, [this, D]()
	{
		TestFalse(TEXT("Reject from the menu clears it"), D->Entry(TEXT("en/UI/Menu.dloc"), TEXT("Lines")).IsTranslated());
		TestEqual(TEXT("the list follows"), int32(D->Window->GetModel().Find(TEXT("Menu"), TEXT("Lines"))->Status), int32(EDreamLocReviewStatus::Untranslated));
		for (const TSharedRef<SWindow>& Child : TArray<TSharedRef<SWindow>>(D->Host->GetChildWindows()))
		{
			FSlateApplication::Get().RequestDestroyWindow(Child);
		}
	});

	// Translate again, clicked: that entry alone sent.
	AddClick(*this, Steps, D, TEXT("Back's row"), [D]() { return D->Row(TEXT("Menu"), TEXT("Back")); });
	AddCheck(Steps, D, [this, D]()
	{
		TestEqual(TEXT("Back chosen"), CurrentKey(*D->Window), TEXT("Back"));
		D->CallsBefore = D->Scenario->Fake->GetCalls(TEXT("openai")).Num();
	});
	AddClick(*this, Steps, D, TEXT("Translate again"), [D]() { return D->Tagged(TEXT("DreamLocTranslate.Retranslate")); });
	AddWait(*this, Steps, D, TEXT("the entry translated again"), 60.0, [D]() { return !D->Window->IsRunning() && D->Scenario->Fake->GetCalls(TEXT("openai")).Num() > D->CallsBefore; });
	AddCheck(Steps, D, [this, D]()
	{
		const TArray<DreamLocTranslateFake::FCall> Calls = D->Scenario->Fake->GetCalls(TEXT("openai"));
		TestEqual(TEXT("one request"), Calls.Num() - D->CallsBefore, 1);
		TestTrue(TEXT("for that entry alone"), Calls.Num() > 0 && Calls.Last().Body.Contains(TEXT("\\\"key\\\":\\\"Back\\\"")) && !Calls.Last().Body.Contains(TEXT("\\\"key\\\":\\\"Start\\\"")));
		TestEqual(TEXT("a reviewed one translated again: machine, to review"), Describe(D->Entry(TEXT("en/UI/Menu.dloc"), TEXT("Back"))), TEXT("en: 返回|machine"));
		TestEqual(TEXT("still chosen, the new translation in the box"), CurrentKey(*D->Window) + TEXT("|") + D->Window->GetEditText(), TEXT("Back|en: 返回"));
	});

	// A file changed by someone else: read again, the entry chosen kept.
	AddCheck(Steps, D, [D]()
	{
		D->Scenario->Write(TEXT("en/UI/Other.dloc"), MakeDloc(TEXT("en"), TEXT("Other"), { { TEXT("Note"), TEXT("备注"), TEXT("A note"), TEXT("done") } }));
	});
	AddWait(*this, Steps, D, TEXT("the window reading a changed file again"), 20.0, [D]()
	{
		const TSharedPtr<FDreamLocReviewEntry> Note = D->Window->GetModel().Find(TEXT("Other"), TEXT("Note"));
		return Note.IsValid() && Note->Status == EDreamLocReviewStatus::Done;
	});
	AddCheck(Steps, D, [this, D]() { TestEqual(TEXT("the entry chosen kept"), CurrentKey(*D->Window), TEXT("Back")); });
	AddEnd(Steps, D);
	ADD_LATENT_AUTOMATION_COMMAND(FSteps(MoveTemp(Steps)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocTranslatePretranslateTest, "DreamLoc.Translate.Window.APreTranslationFromItsDialogWaitsForReview",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FDreamLocTranslatePretranslateTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocTranslateWindowTestsLocal;
	const FScenarioRef S = MakeShared<FScenario, ESPMode::ThreadSafe>();
	if (!S->Begin(*this, TEXT("TranslatePretranslate")))
	{
		return false;
	}
	S->Write(TEXT("dloc.ini"), TEXT("[DreamLoc]\nSourceCulture=zh-Hans\nTargetCultures=en,ja\n"));
	S->Write(TEXT("en/UI/Menu.dloc"), MakeDloc(TEXT("en"), TEXT("Menu"), {
		{ TEXT("Start"), TEXT("开始游戏") }, { TEXT("Quit"), TEXT("退出") }, { TEXT("Back"), TEXT("返回"), TEXT("Back"), TEXT("done") } }));
	S->Write(TEXT("en/Story/Intro.dloc"), MakeDloc(TEXT("en"), TEXT("Intro"), { { TEXT("Line"), TEXT("很久以前") } }));
	S->Write(TEXT("ja/UI/Menu.dloc"), MakeDloc(TEXT("ja"), TEXT("Menu"), { { TEXT("Start"), TEXT("开始游戏") } }));
	TestTrue(TEXT("the profiles saved where the test says"), DreamLocTranslateConfig::SaveProfiles({ S->Fake->Profile(TEXT("OpenAI"), TEXT("model")) }));

	const FDeskRef D = MakeDesk(S);
	TArray<TFunction<bool()>> Steps;
	TSharedRef<TSharedPtr<SDreamLocPretranslateDialog>> Dialog = MakeShared<TSharedPtr<SDreamLocPretranslateDialog>>();
	AddClick(*this, Steps, D, TEXT("AI pre-translate..."), [D]() { return D->Tagged(TEXT("DreamLocTranslate.Pretranslate")); });
	AddCheck(Steps, D, [this, D, Dialog]()
	{
		*Dialog = D->Window->OpenPretranslate();
		TestTrue(TEXT("a click opens the dialog"), Dialog->IsValid() && D->Host->GetChildWindows().Num() == 1);
		if (!Dialog->IsValid())
		{
			D->bFailed = true;
			return;
		}
		TestEqual(TEXT("for the window's culture"), FString::Join((*Dialog)->GetCultures(), TEXT(",")), TEXT("en"));
		TestFalse(TEXT("no translator for the culture: nothing to start"), (*Dialog)->CanStart());
		TestTrue(TEXT("and it says so, the texts counted all the same"), (*Dialog)->GetEstimateText().ToString().Contains(TEXT("3 entries")));
		(*Dialog)->SetProfile(TEXT("model"));
		TestTrue(TEXT("the translator chosen: the requests (a file each) and tokens too"), (*Dialog)->GetEstimateText().ToString().Contains(TEXT("2 requests")));
		(*Dialog)->SetScope(EDreamLocPretranslateScope::Files);
		(*Dialog)->SetFiles({ TEXT("Story/") });
		TestEqual(TEXT("a folder alone"), (*Dialog)->GetEstimate().Entries, 1);
		(*Dialog)->SetScope(EDreamLocPretranslateScope::All);
		(*Dialog)->SetCultures({ TEXT("en"), TEXT("ja") });
		TestEqual(TEXT("two cultures"), (*Dialog)->GetEstimate().Entries, 4);
		(*Dialog)->SetCultures({ TEXT("ja") });
		TestTrue(TEXT("ready"), (*Dialog)->CanStart());
	});
	// The other culture's, the dialog left open: what came back, said in it.
	AddClick(*this, Steps, D, TEXT("the dialog's Translate"), [D]() { return D->Tagged(TEXT("DreamLocPretranslate.Start")); });
	AddCheck(Steps, D, [this, D]() { TestTrue(TEXT("a click starts the run"), D->Window->IsRunning() || D->Scenario->Fake->GetCalls(TEXT("openai")).Num() > 0); });
	AddWait(*this, Steps, D, TEXT("the pre-translation of ja"), 60.0, [D]() { return !D->Window->IsRunning(); });
	AddCheck(Steps, D, [this, D, Dialog]()
	{
		TestTrue(FString::Printf(TEXT("the dialog says what came back (%s)"), *(*Dialog)->GetResultText().ToString()),
			(*Dialog)->GetResultText().ToString().StartsWith(TEXT("Done: 1 translated, 0 failed, 0 skipped")));
		// The fake marks every text "en: ", whatever the culture asked for.
		TestEqual(TEXT("written as machine"), FScenario::Field(D->Entry(TEXT("ja/UI/Menu.dloc"), TEXT("Start")), EDreamLocField::State), TEXT("machine"));
		TestEqual(TEXT("the window goes to that culture"), D->Window->GetCulture(), TEXT("ja"));
		TestEqual(TEXT("shows them to review"), int32(D->Window->GetFilter()), int32(EDreamLocReviewFilter::Review));
		TestEqual(TEXT("the first chosen"), CurrentKey(*D->Window), TEXT("Start"));
		(*Dialog)->SetCultures({ TEXT("en") });
	});
	// en's, the dialog closed while it runs: the run goes on.
	AddClick(*this, Steps, D, TEXT("the dialog's Translate"), [D]() { return D->Tagged(TEXT("DreamLocPretranslate.Start")); });
	AddCheck(Steps, D, [D]()
	{
		for (const TSharedRef<SWindow>& Child : TArray<TSharedRef<SWindow>>(D->Host->GetChildWindows()))
		{
			FSlateApplication::Get().RequestDestroyWindow(Child);
		}
	});
	AddWait(*this, Steps, D, TEXT("the pre-translation of en"), 60.0, [D]() { return !D->Window->IsRunning(); });
	AddCheck(Steps, D, [this, D]()
	{
		TestEqual(TEXT("all three translated"), D->Window->GetLastResult().Translated, 3);
		TestEqual(TEXT("written as machine"), Describe(D->Entry(TEXT("en/UI/Menu.dloc"), TEXT("Start"))), TEXT("en: 开始游戏|machine"));
		TestEqual(TEXT("the reviewed one untouched"), Describe(D->Entry(TEXT("en/UI/Menu.dloc"), TEXT("Back"))), TEXT("Back|done"));
		TestEqual(TEXT("the window goes to that culture"), D->Window->GetCulture(), TEXT("en"));
		TestEqual(TEXT("the window shows them to review"), int32(D->Window->GetFilter()), int32(EDreamLocReviewFilter::Review));
		TestEqual(TEXT("the first chosen"), CurrentKey(*D->Window), D->Window->GetShown().Num() > 0 ? D->Window->GetShown()[0]->Entry.Key : FString(TEXT("(none)")));
		TestEqual(TEXT("its translation in the box"), D->Window->GetEditText(), D->Window->GetCurrent().IsValid() ? D->Window->GetCurrent()->Translation : FString());
		TestEqual(TEXT("the report beside the sources' project"), IFileManager::Get().DirectoryExists(*(D->Scenario->Root / TEXT("Saved/DLoc"))), true);
	});
	AddEnd(Steps, D);
	ADD_LATENT_AUTOMATION_COMMAND(FSteps(MoveTemp(Steps)));
	return true;
}

namespace DreamLocTranslateWindowTestsLocal
{
	/** A tool that shows the texts of `.fake` sources: a box 100 wide, 12 a character. */
	class FFakeFit : public IDreamLocContextProvider
	{
	public:
		virtual bool HandlesSource(const FString& InSource) const override { return InSource.EndsWith(TEXT(".fake")); }

		virtual bool MeasureTranslation(const FDreamLocFitRequest& InRequest, FDreamLocFitResult& OutResult) override
		{
			OutResult.bMeasured = true;
			OutResult.Box = FVector2f(100.0f, 20.0f);
			OutResult.Needed = FVector2f(12.0f * InRequest.Text.Len(), 20.0f);
			OutResult.bOverflows = OutResult.Needed.X > OutResult.Box.X;
			OutResult.Where = TEXT("Text Gold (FakeFont, 12)");
			return true;
		}
	};

	FString DescribeCandidates(const SDreamLocTranslateWindow& InWindow)
	{
		TArray<FString> Out;
		for (const FDreamLocCandidate& Candidate : InWindow.GetCandidates())
		{
			Out.Add(Candidate.Text + TEXT(" <") + Candidate.From + (Candidate.Problems.IsEmpty() ? TEXT(">") : TEXT(", with problems>")));
		}
		return FString::Join(Out, TEXT(" | "));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocTranslateAssistTest, "DreamLoc.Translate.Window.CandidatesAIFixPreviewDiffAndOverview",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FDreamLocTranslateAssistTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocTranslateWindowTestsLocal;
	const FScenarioRef S = MakeShared<FScenario, ESPMode::ThreadSafe>();
	if (!S->Begin(*this, TEXT("TranslateAssist")))
	{
		return false;
	}
	S->Write(TEXT("dloc.ini"), TEXT("[DreamLoc]\nSourceCulture=zh-Hans\nTargetCultures=en,ja\n\n[Translate]\nProfile=model\nLocalOnly=Secret/\n"));
	FString Menu = MakeDloc(TEXT("en"), TEXT("Menu"), {
		{ TEXT("Back"), TEXT("返回"), TEXT("Back"), TEXT("done") },
		{ TEXT("Return"), TEXT("返回") },
		{ TEXT("Gold"), TEXT("{0}金币"), TEXT("Gold"), TEXT("machine") },
		{ TEXT("Load"), TEXT("读取存档"), TEXT("Load"), TEXT("machine"), TEXT("读取") } });
	Menu.ReplaceInline(TEXT("Gold {\n"), TEXT("Gold {\n    at    Fake/Shop.fake#Gold\n"));
	S->Write(TEXT("en/UI/Menu.dloc"), Menu);
	S->Write(TEXT("en/Secret/Plot.dloc"), MakeDloc(TEXT("en"), TEXT("Plot"), { { TEXT("Twist"), TEXT("他是父亲") } }));
	S->Write(TEXT("ja/UI/Menu.dloc"), MakeDloc(TEXT("ja"), TEXT("Menu"), { { TEXT("Return"), TEXT("返回"), TEXT("戻る"), TEXT("done") } }));
	TestTrue(TEXT("the profiles saved where the test says"), DreamLocTranslateConfig::SaveProfiles({ S->Fake->Profile(TEXT("OpenAI"), TEXT("model")),
		S->Fake->Profile(TEXT("OpenAI"), TEXT("cloudy"), /*bInCloud*/ true) }));
	// The large model: an AI fix of "Gold" comes back with its argument; anything else is marked.
	S->Fake->Chat = [](const DreamLocTranslateFake::FChat& InChat)
	{
		for (const TSharedPtr<FJsonObject>& Entry : InChat.Entries)
		{
			FString Last;
			if (Entry->TryGetStringField(TEXT("lastText"), Last) && Last == TEXT("Gold"))
			{
				DreamLocTranslateFake::FChatAnswer Answer;
				Answer.Content = DreamLocTranslateFake::Entries({ TPair<int32, FString>(int32(Entry->GetNumberField(TEXT("id"))), TEXT("{0} gold")) });
				return Answer;
			}
		}
		return DreamLocTranslateFake::MarkAll(InChat);
	};
	const TSharedRef<FFakeFit> Fit = MakeShared<FFakeFit>();
	IModularFeatures::Get().RegisterModularFeature(IDreamLocContextProvider::GetModularFeatureName(), &Fit.Get());

	const FDeskRef D = MakeDesk(S);
	TArray<TFunction<bool()>> Steps;

	// Candidates already there: the same source reviewed elsewhere, and the reference culture's.
	AddClick(*this, Steps, D, TEXT("Return's row"), [D]() { return D->Row(TEXT("Menu"), TEXT("Return")); });
	AddCheck(Steps, D, [this, D]()
	{
		TestEqual(TEXT("chosen"), CurrentKey(*D->Window), TEXT("Return"));
		TestEqual(TEXT("a reviewed translation of the same source"), DescribeCandidates(*D->Window), TEXT("Back <Reviewed elsewhere>"));
		D->Window->SetReferenceCulture(TEXT("ja"));
		TestEqual(TEXT("and the reference culture's"), DescribeCandidates(*D->Window), TEXT("Back <Reviewed elsewhere> | 戻る <Reference ja>"));
		TestEqual(TEXT("the reference in the list too"), D->Window->GetModel().Find(TEXT("Menu"), TEXT("Return"))->Reference, TEXT("戻る"));
	});
	// Ctrl+2 puts the second in the box; a click on the first puts it there instead; Ctrl+Enter passes it.
	AddCursor(*this, Steps, D, []() { return 0; });
	AddKey(Steps, D, EKeys::Two, /*bInControl*/ true);
	AddCheck(Steps, D, [this, D]()
	{
		TestEqual(TEXT("Ctrl+2: the second candidate in the box"), D->Window->GetEditText(), TEXT("戻る"));
		TestFalse(TEXT("not written yet"), D->Entry(TEXT("en/UI/Menu.dloc"), TEXT("Return")).IsTranslated());
	});
	AddClick(*this, Steps, D, TEXT("the first candidate"), [D]() { return D->Tagged(TEXT("DreamLocTranslate.Candidate.1")); });
	AddCheck(Steps, D, [this, D]() { TestEqual(TEXT("a click on a candidate"), D->Window->GetEditText(), TEXT("Back")); });
	AddCursor(*this, Steps, D, []() { return 4; });
	AddKey(Steps, D, EKeys::Enter, /*bInControl*/ true);
	AddCheck(Steps, D, [this, D]()
	{
		TestEqual(TEXT("passed"), Describe(D->Entry(TEXT("en/UI/Menu.dloc"), TEXT("Return"))), TEXT("Back|done"));
		TestEqual(TEXT("on to the next not reviewed"), CurrentKey(*D->Window), TEXT("Gold"));
	});

	// Gold: where it is shown, and an AI fix of what the checks say.
	AddCheck(Steps, D, [this, D]()
	{
		TestTrue(TEXT("the checks: its argument lost"), D->Window->GetEditProblems().Contains(TEXT("{0}")));
		TestEqual(TEXT("the preview: it fits"), TextUnder(D->Tagged(TEXT("DreamLocTranslate.Preview"))), TEXT("Fits: it needs 48 x 20 of the 100 x 20 box (Text Gold (FakeFont, 12))"));
		TestEqual(TEXT("the fix goes to the culture's large model"), D->Window->GetFixProfile(), TEXT("model"));
		D->CallsBefore = D->Scenario->Fake->GetCalls(TEXT("openai")).Num();
	});
	AddClick(*this, Steps, D, TEXT("the first problem's AI fix"), [D]() { return D->Tagged(TEXT("DreamLocTranslate.Fix.1")); });
	AddWait(*this, Steps, D, TEXT("the AI fix"), 60.0, [D]() { return !D->Window->IsRunning() && D->Window->GetCandidates().Num() > 0; });
	AddCheck(Steps, D, [this, D]()
	{
		const TArray<DreamLocTranslateFake::FCall> Calls = D->Scenario->Fake->GetCalls(TEXT("openai"));
		TestEqual(TEXT("one request"), Calls.Num() - D->CallsBefore, 1);
		TestTrue(TEXT("with the translation and what is wrong with it"), Calls.Num() > D->CallsBefore
			&& Calls[D->CallsBefore].Body.Contains(TEXT("\\\"lastText\\\":\\\"Gold\\\"")) && Calls[D->CallsBefore].Body.Contains(TEXT("\\\"problems\\\":")));
		TestEqual(TEXT("what came back is a candidate"), DescribeCandidates(*D->Window), TEXT("{0} gold <AI fix (model)>"));
		TestEqual(TEXT("not put in the box"), D->Window->GetEditText(), TEXT("Gold"));
		TestEqual(TEXT("nor written"), Describe(D->Entry(TEXT("en/UI/Menu.dloc"), TEXT("Gold"))), TEXT("Gold|machine"));
		TestFalse(TEXT("no report for a suggestion"), IFileManager::Get().DirectoryExists(*(D->Scenario->Root / TEXT("Saved/DLoc"))));
	});
	// Asked of a translator by its button: a candidate that does not fit.
	AddClick(*this, Steps, D, TEXT("Ask cloudy"), [D]() { return D->Tagged(TEXT("DreamLocTranslate.Ask.cloudy")); });
	AddWait(*this, Steps, D, TEXT("the translator's candidate"), 60.0, [D]() { return !D->Window->IsRunning() && D->Window->GetCandidates().Num() > 1; });
	AddCheck(Steps, D, [this, D]()
	{
		TestEqual(TEXT("its candidate after the fix, the checks' say on it kept"), DescribeCandidates(*D->Window),
			TEXT("{0} gold <AI fix (model)> | en: {0}金币 <cloudy, with problems>"));
	});
	AddClick(*this, Steps, D, TEXT("the second candidate"), [D]() { return D->Tagged(TEXT("DreamLocTranslate.Candidate.2")); });
	AddWait(*this, Steps, D, TEXT("the preview laid out again"), 10.0, [D]() { return TextUnder(D->Tagged(TEXT("DreamLocTranslate.Preview"))).StartsWith(TEXT("Does not fit")); });
	AddCheck(Steps, D, [this, D]()
	{
		TestEqual(TEXT("what it needs, and the box"), TextUnder(D->Tagged(TEXT("DreamLocTranslate.Preview"))),
			TEXT("Does not fit: it needs 108 x 20, the box is 100 x 20 (Text Gold (FakeFont, 12))"));
		TestTrue(TEXT("the checks say so too"), D->Window->GetEditProblems().Contains(TEXT("DLOC4001")));
	});

	// A stale entry: what changed in its source.
	AddCheck(Steps, D, [this, D]()
	{
		TestTrue(TEXT("Load chosen"), D->Window->Select(TEXT("Menu"), TEXT("Load")));
		const TSharedPtr<SWidget> Source = D->Tagged(TEXT("DreamLocTranslate.EditorSource"));
		TestEqual(TEXT("the source's change, by character"), Source.IsValid() ? StaticCastSharedPtr<SDreamLocChipText>(Source)->GetShownText() : FString(), TEXT("读取+(存档)"));
		TestEqual(TEXT("its old translation in the box"), D->Window->GetEditText(), TEXT("Load"));
	});
	// A LocalOnly entry: only the local translators to ask.
	AddCheck(Steps, D, [this, D]()
	{
		TestTrue(TEXT("Twist chosen"), D->Window->Select(TEXT("Plot"), TEXT("Twist")));
		TestEqual(TEXT("only local translators"), FString::Join(D->Window->GetCandidateProfiles(), TEXT(",")), TEXT("model"));
	});
	AddCheck(Steps, D, [this, D]()
	{
		TestTrue(TEXT("its ask button"), D->Tagged(TEXT("DreamLocTranslate.Ask.model")).IsValid());
		TestFalse(TEXT("no button for the cloud one"), D->Tagged(TEXT("DreamLocTranslate.Ask.cloudy")).IsValid());
	});

	// The overview: every culture's progress; a click on a part goes there.
	AddClick(*this, Steps, D, TEXT("the overview"), [D]() { return D->Tagged(TEXT("DreamLocTranslate.Overview")); });
	AddCheck(Steps, D, [this, D]()
	{
		TArray<FString> Lines;
		for (const FDreamLocCultureProgress& Each : D->Window->GetOverview())
		{
			Lines.Add(FString::Printf(TEXT("%s %d/%d/%d/%d"), *Each.Culture, Each.Total.Done, Each.Total.Review, Each.Total.Stale, Each.Total.Untranslated));
		}
		TestEqual(TEXT("each culture: reviewed, to review, stale, untranslated"), FString::Join(Lines, TEXT(" ")), TEXT("en 2/1/1/1 ja 1/0/0/0"));
	});
	AddClick(*this, Steps, D, TEXT("ja's reviewed part"), [D]() { return D->Tagged(TEXT("DreamLocTranslate.Overview.ja.Reviewed")); });
	AddCheck(Steps, D, [this, D]()
	{
		TestEqual(TEXT("goes to that culture"), D->Window->GetCulture(), TEXT("ja"));
		TestEqual(TEXT("and that filter"), int32(D->Window->GetFilter()), int32(EDreamLocReviewFilter::Done));
		TestEqual(TEXT("showing it"), Keys(D->Window->GetShown()), TEXT("Return"));
	});
	AddClick(*this, Steps, D, TEXT("en's files"), [D]() { return D->Tagged(TEXT("DreamLocTranslate.OverviewFiles.en")); });
	AddClick(*this, Steps, D, TEXT("Secret/Plot's untranslated part"), [D]() { return D->Tagged(TEXT("DreamLocTranslate.Overview.en.Secret/Plot.Untranslated")); });
	AddCheck(Steps, D, [this, D]()
	{
		TestEqual(TEXT("a file's part: the culture"), D->Window->GetCulture(), TEXT("en"));
		TestEqual(TEXT("the file"), D->Window->GetFile(), TEXT("Secret/Plot"));
		TestEqual(TEXT("showing it"), Keys(D->Window->GetShown()), TEXT("Twist"));
	});
	Steps.Add([Fit]()
	{
		IModularFeatures::Get().UnregisterModularFeature(IDreamLocContextProvider::GetModularFeatureName(), &Fit.Get());
		return true;
	});
	AddEnd(Steps, D);
	ADD_LATENT_AUTOMATION_COMMAND(FSteps(MoveTemp(Steps)));
	return true;
}

namespace DreamLocTranslateWindowTestsLocal
{
	/** The user's keyboard focus on a tagged widget, and everything in it selected (Ctrl+A): what typing then replaces. */
	void AddFocusAll(FAutomationTestBase& InTest, TArray<TFunction<bool()>>& OutSteps, const FDeskRef& InDesk, const TCHAR* InTag)
	{
		FAutomationTestBase* Test = &InTest;
		OutSteps.Add([Test, InDesk, InTag]()
		{
			if (InDesk->bFailed)
			{
				return true;
			}
			Paint(InDesk->Host.ToSharedRef());
			const TSharedPtr<SWidget> Found = InDesk->Tagged(InTag);
			if (!Found.IsValid())
			{
				Test->AddError(FString::Printf(TEXT("no %s"), InTag));
				InDesk->bFailed = true;
				return true;
			}
			FSlateApplication::Get().SetUserFocus(InDesk->User->GetUserIndex(), Found, EFocusCause::Mouse);
			return true;
		});
		AddKey(OutSteps, InDesk, EKeys::A, /*bInControl*/ true);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocTranslatorSettingsTest, "DreamLoc.Translate.Settings.TheTranslatorsPageReadsAndWritesTranslatorsIni",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FDreamLocTranslatorSettingsTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocTranslateWindowTestsLocal;
	const FScenarioRef S = MakeShared<FScenario, ESPMode::ThreadSafe>();
	if (!S->Begin(*this, TEXT("TranslatorSettings")))
	{
		return false;
	}
	TestTrue(TEXT("the profiles saved where the test says"), DreamLocTranslateConfig::SaveProfiles({ S->Fake->Profile(TEXT("OpenAI"), TEXT("model")) }));
	const TSharedRef<SDreamLocTranslatorSettings> Page = SNew(SDreamLocTranslatorSettings);
	const FDeskRef D = MakeDeskWith(S, Page);
	const FString Path = DreamLocTranslateConfig::GetUserConfigPath();
	TestTrue(TEXT("the file is the test's"), Path.StartsWith(S->Root));
	TArray<TFunction<bool()>> Steps;
	AddCheck(Steps, D, [this, Page]()
	{
		TestEqual(TEXT("the translators read"), Page->GetProfiles().Num() == 1 ? Page->GetProfiles()[0]->Name : FString(), TEXT("model"));
		TestFalse(TEXT("nothing to save"), Page->IsDirty());
	});
	AddClick(*this, Steps, D, TEXT("Add"), [D]() { return D->Tagged(TEXT("DreamLocSettings.Add")); });
	AddFocusAll(*this, Steps, D, TEXT("DreamLocSettings.Field.BaseUrl"));
	AddType(Steps, D, TEXT("http://127.0.0.1:9/v1"));
	AddCheck(Steps, D, [this, Page]()
	{
		TestEqual(TEXT("a new one, typed into"), Page->GetProfiles().Num() == 2 ? Page->GetProfiles()[1]->Name + TEXT(" ") + Page->GetProfiles()[1]->BaseUrl : FString(),
			TEXT("profile2 http://127.0.0.1:9/v1"));
		TestTrue(TEXT("to save"), Page->IsDirty());
	});
	AddClick(*this, Steps, D, TEXT("Save"), [D]() { return D->Tagged(TEXT("DreamLocSettings.Save")); });
	AddCheck(Steps, D, [this, Path]()
	{
		FString Text;
		FFileHelper::LoadFileToString(Text, *Path);
		TestTrue(TEXT("written to translators.ini"), Text.Contains(TEXT("[Profile profile2]\nType=OpenAI\nBaseUrl=http://127.0.0.1:9/v1\n")));
		TestEqual(TEXT("both there"), ProfileNames(DreamLocTranslateConfig::LoadProfiles()), TEXT("model,profile2"));
	});
	// Test: the smallest request, to the fake service.
	AddClick(*this, Steps, D, TEXT("model's row"), [D]() { return D->Tagged(TEXT("DreamLocSettings.Profile.model")); });
	AddClick(*this, Steps, D, TEXT("Test"), [D]() { return D->Tagged(TEXT("DreamLocSettings.Test")); });
	AddWait(*this, Steps, D, TEXT("the test's answer"), 30.0, [Page]() { return !Page->GetTestText().ToString().StartsWith(TEXT("Testing")) && !Page->GetTestText().IsEmpty(); });
	AddCheck(Steps, D, [this, Page]() { TestTrue(FString::Printf(TEXT("it answered (%s)"), *Page->GetTestText().ToString()), Page->GetTestText().ToString().StartsWith(TEXT("model answered"))); });
	// Delete, saved.
	AddClick(*this, Steps, D, TEXT("profile2's row"), [D]() { return D->Tagged(TEXT("DreamLocSettings.Profile.profile2")); });
	AddClick(*this, Steps, D, TEXT("Delete"), [D]() { return D->Tagged(TEXT("DreamLocSettings.Delete")); });
	AddClick(*this, Steps, D, TEXT("Save"), [D]() { return D->Tagged(TEXT("DreamLocSettings.Save")); });
	AddCheck(Steps, D, [this, S]()
	{
		TestEqual(TEXT("deleted from the file"), ProfileNames(DreamLocTranslateConfig::LoadProfiles()), TEXT("model"));
		// Changed elsewhere: the page follows.
		DreamLocTranslateConfig::SaveProfiles({ S->Fake->Profile(TEXT("OpenAI"), TEXT("model")), S->Fake->Profile(TEXT("DeepL"), TEXT("deepl")) });
	});
	AddWait(*this, Steps, D, TEXT("the page reading the file changed elsewhere"), 15.0, [Page]() { return Page->GetProfiles().Num() == 2; });
	AddEnd(Steps, D);
	ADD_LATENT_AUTOMATION_COMMAND(FSteps(MoveTemp(Steps)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocSettingsPagesTest, "DreamLoc.Translate.Settings.BothPagesAreThereForTheSettingsEditor",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FDreamLocSettingsPagesTest::RunTest(const FString& Parameters)
{
	// As the settings editor finds a page: its section, and the section's widget -- which it holds weakly.
	ISettingsModule* Settings = FModuleManager::GetModulePtr<ISettingsModule>(TEXT("Settings"));
	TestNotNull(TEXT("the settings module"), Settings);
	struct FPage
	{
		FName Key;
		FName Value;
	};
	const FPage Pages[] = { { TEXT("Editor"), DreamLocTranslateWindow::GetTranslatorsSettingsName() }, { TEXT("Project"), TEXT("DreamLoc") } };
	for (const FPage& Page : Pages)
	{
		const TSharedPtr<ISettingsContainer> Container = Settings != nullptr ? Settings->GetContainer(Page.Key) : nullptr;
		const TSharedPtr<ISettingsCategory> Category = Container.IsValid() ? Container->GetCategory(TEXT("Plugins")) : nullptr;
		const TSharedPtr<ISettingsSection> Section = Category.IsValid() ? Category->GetSection(Page.Value, /*bIgnoreVisibility*/ true) : nullptr;
		TestTrue(FString::Printf(TEXT("%s > Plugins > %s is registered"), *Page.Key.ToString(), *Page.Value.ToString()), Section.IsValid());
		TestTrue(FString::Printf(TEXT("%s > Plugins > %s has its page to show"), *Page.Key.ToString(), *Page.Value.ToString()),
			Section.IsValid() && Section->GetCustomWidget().IsValid());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocProjectSettingsTest, "DreamLoc.Translate.Settings.TheProjectPageWritesOnlyWhatChanged",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FDreamLocProjectSettingsTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocTranslateWindowTestsLocal;
	const FScenarioRef S = MakeShared<FScenario, ESPMode::ThreadSafe>();
	if (!S->Begin(*this, TEXT("ProjectSettings")))
	{
		return false;
	}
	TestTrue(TEXT("the profiles saved where the test says"), DreamLocTranslateConfig::SaveProfiles({ S->Fake->Profile(TEXT("OpenAI"), TEXT("model")) }));
	const FString Before = TEXT("[DreamLoc]\nSourceCulture=zh-Hans\nTargetCultures=en\n\n[Translate]\n; the team's translator\nProfile=model\nBatchSize=20\n; keep the plot local\nLocalOnly=Secret/\n");
	S->Write(TEXT("dloc.ini"), Before);
	const TSharedRef<SDreamLocProjectTranslateSettings> Page = SNew(SDreamLocProjectTranslateSettings).DLocDirectory(S->DLoc);
	const FDeskRef D = MakeDeskWith(S, Page);
	TArray<TFunction<bool()>> Steps;
	AddCheck(Steps, D, [this, D]()
	{
		TestEqual(TEXT("BatchSize read"), TextOf(D->Tagged(TEXT("DreamLocProjectSettings.BatchSize"))), TEXT("20"));
		TestEqual(TEXT("Concurrency: its default"), TextOf(D->Tagged(TEXT("DreamLocProjectSettings.Concurrency"))), TEXT("2"));
	});
	AddFocusAll(*this, Steps, D, TEXT("DreamLocProjectSettings.Concurrency"));
	AddType(Steps, D, TEXT("3"));
	AddClick(*this, Steps, D, TEXT("Save"), [D]() { return D->Tagged(TEXT("DreamLocProjectSettings.Save")); });
	AddCheck(Steps, D, [this, S, Page]()
	{
		TestEqual(TEXT("one key added; the comments, the order, the rest as they were"), S->Read(TEXT("dloc.ini")),
			TEXT("[DreamLoc]\nSourceCulture=zh-Hans\nTargetCultures=en\n\n[Translate]\n; the team's translator\nProfile=model\nBatchSize=20\n; keep the plot local\nLocalOnly=Secret/\nConcurrency=3\n"));
		TestTrue(TEXT("it says so"), Page->GetStatus().ToString().StartsWith(TEXT("Saved to")));
	});
	AddFocusAll(*this, Steps, D, TEXT("DreamLocProjectSettings.BatchSize"));
	AddType(Steps, D, TEXT("40"));
	AddClick(*this, Steps, D, TEXT("Save"), [D]() { return D->Tagged(TEXT("DreamLocProjectSettings.Save")); });
	AddCheck(Steps, D, [this, S]()
	{
		TestEqual(TEXT("a key back to its default taken out, the rest left"), S->Read(TEXT("dloc.ini")),
			TEXT("[DreamLoc]\nSourceCulture=zh-Hans\nTargetCultures=en\n\n[Translate]\n; the team's translator\nProfile=model\n; keep the plot local\nLocalOnly=Secret/\nConcurrency=3\n"));
	});
	AddFocusAll(*this, Steps, D, TEXT("DreamLocProjectSettings.Retry"));
	AddType(Steps, D, TEXT("x"));
	AddClick(*this, Steps, D, TEXT("Save"), [D]() { return D->Tagged(TEXT("DreamLocProjectSettings.Save")); });
	AddCheck(Steps, D, [this, S, Page]()
	{
		TestEqual(TEXT("not a number: not saved"), Page->GetStatus().ToString(), TEXT("Not saved: 'x' is not a whole number of 0 or more."));
		TestFalse(TEXT("the file as it was"), S->Read(TEXT("dloc.ini")).Contains(TEXT("Retry")));
	});
	AddEnd(Steps, D);
	ADD_LATENT_AUTOMATION_COMMAND(FSteps(MoveTemp(Steps)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocInContextTest, "DreamLoc.Translate.InContext.AltClickOnASlateTextFindsItsKey",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FDreamLocInContextTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocTranslateWindowTestsLocal;
	// What a text says about itself.
	auto Says = [](const FText& InText)
	{
		const DreamLocInContext::FFound Found = DreamLocInContext::Describe(InText);
		return Found.HasKey() ? Found.Namespace + TEXT(" / ") + Found.Key : TEXT("none: ") + Found.WhyNot;
	};
	TestEqual(TEXT("a LOCTEXT's namespace and key"), Says(NSLOCTEXT("DreamLocTestMenu", "Start", "开始游戏")), TEXT("DreamLocTestMenu / Start"));
	TestEqual(TEXT("an asset text's: the package's part of the namespace stripped"),
		Says(FText::ChangeKey(TEXT("Menu [0A1B2C3D4E5F]"), TEXT("Quit"), FText::FromString(TEXT("退出")))), TEXT("Menu / Quit"));
	TestTrue(TEXT("FromString: why not"), Says(FText::FromString(TEXT("plain"))).Contains(TEXT("FText::FromString")));
	TestTrue(TEXT("a format: why not"), Says(FText::Format(NSLOCTEXT("DreamLocTestMenu", "Gold", "{0} gold"), 3)).Contains(TEXT("FText::Format")));
	TestTrue(TEXT("culture invariant: why not"), Says(FText::AsCultureInvariant(TEXT("v1.0"))).Contains(TEXT("AsCultureInvariant")));

	const FScenarioRef S = MakeShared<FScenario, ESPMode::ThreadSafe>();
	if (!S->Begin(*this, TEXT("InContext")))
	{
		return false;
	}
	const TSharedRef<bool> bClicked = MakeShared<bool>(false);
	const TSharedRef<SWidget> Content = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(20.0f)
		[
			SNew(SButton)
			.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("Test.StartButton")))
			.OnClicked_Lambda([bClicked]() { *bClicked = true; return FReply::Handled(); })
			[
				// Not hit-testable itself, as UMG's text blocks are: the button is what the mouse is over.
				SNew(STextBlock).AddMetaData<FTagMetaData>(FTagMetaData(TEXT("Test.Start"))).Visibility(EVisibility::SelfHitTestInvisible)
				.Text(NSLOCTEXT("DreamLocTestMenu", "Start", "开始游戏"))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(20.0f)
		[
			SNew(STextBlock).AddMetaData<FTagMetaData>(FTagMetaData(TEXT("Test.Plain"))).Text(FText::FromString(TEXT("plain")))
		];
	const FDeskRef D = MakeDeskWith(S, Content);
	const TSharedRef<TArray<FString>> Picked = MakeShared<TArray<FString>>();
	DreamLocInContext::SetOnPick([Picked](const DreamLocInContext::FFound& InFound)
	{
		Picked->Add(InFound.HasKey() ? InFound.Namespace + TEXT(" / ") + InFound.Key : TEXT("none: ") + InFound.WhyNot);
	});
	FModifierKeysState Alt(false, false, false, false, /*LeftAlt*/ true, false, false, false, false);
	TArray<TFunction<bool()>> Steps;
	// Alt held over a text: outlined.
	AddCheck(Steps, D, [D, Alt]() { D->Modifiers = Alt; });
	AddClick(*this, Steps, D, TEXT("Alt+click on Start"), [D]() { return D->Tagged(TEXT("Test.Start")); });
	AddCheck(Steps, D, [this, D, Picked, bClicked]()
	{
		const TOptional<DreamLocInContext::FFound> Lit = DreamLocInContext::GetHighlighted();
		TestEqual(TEXT("Alt over it outlines it"), Lit.IsSet() ? Lit->Namespace + TEXT(" / ") + Lit->Key : FString(TEXT("(nothing)")), TEXT("DreamLocTestMenu / Start"));
		TestTrue(TEXT("its rectangle on the desktop"), Lit.IsSet() && Lit->Rect.bIsValid && Lit->Rect.Min.X < -10000.0);
		TestEqual(TEXT("Alt+click picks its key"), FString::Join(*Picked, TEXT(" | ")), TEXT("DreamLocTestMenu / Start"));
		TestFalse(TEXT("and the button under it is not clicked"), *bClicked);
	});
	AddClick(*this, Steps, D, TEXT("Alt+click on plain"), [D]() { return D->Tagged(TEXT("Test.Plain")); });
	AddCheck(Steps, D, [this, Picked]()
	{
		TestTrue(TEXT("a text without a key: why not"), Picked->Num() == 2 && (*Picked)[1].Contains(TEXT("FText::FromString")));
	});
	// Without Alt: the outline goes, a click is the button's.
	AddCheck(Steps, D, [D]() { D->Modifiers = FModifierKeysState(); });
	AddClick(*this, Steps, D, TEXT("a click on Start"), [D]() { return D->Tagged(TEXT("Test.Start")); });
	AddCheck(Steps, D, [this, Picked, bClicked]()
	{
		TestFalse(TEXT("no outline without Alt"), DreamLocInContext::GetHighlighted().IsSet());
		TestTrue(TEXT("a click without Alt is the button's"), *bClicked);
		TestEqual(TEXT("and picks nothing"), Picked->Num(), 2);
	});
	// The default pick: a text without a key says why at the cursor.
	AddCheck(Steps, D, [D, Alt]()
	{
		DreamLocInContext::SetOnPick(nullptr);
		D->Modifiers = Alt;
	});
	AddClick(*this, Steps, D, TEXT("Alt+click on plain"), [D]() { return D->Tagged(TEXT("Test.Plain")); });
	AddCheck(Steps, D, [this, D]()
	{
		TestTrue(FString::Printf(TEXT("the hint at the cursor (%s)"), *DreamLocInContext::GetHint()), DreamLocInContext::GetHint().Contains(TEXT("FText::FromString")));
		D->Modifiers = FModifierKeysState();
	});
	AddEnd(Steps, D);
	ADD_LATENT_AUTOMATION_COMMAND(FSteps(MoveTemp(Steps)));
	return true;
}

namespace DreamLocTranslateWindowTestsLocal
{
	/** The first text block under a widget that says InText. */
	TSharedPtr<SWidget> FindText(const TSharedRef<SWidget>& InRoot, const FString& InText)
	{
		if (InRoot->GetType() == TEXT("STextBlock") && StaticCastSharedRef<STextBlock>(InRoot)->GetText().ToString() == InText)
		{
			return InRoot;
		}
		FChildren* Children = InRoot->GetChildren();
		for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
		{
			if (const TSharedPtr<SWidget> Found = FindText(Children->GetChildAt(Index), InText))
			{
				return Found;
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocTranslateCultureListTest, "DreamLoc.Translate.Window.TheCultureListSwitchesTheCulture",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FDreamLocTranslateCultureListTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocTranslateWindowTestsLocal;
	const FScenarioRef S = MakeShared<FScenario, ESPMode::ThreadSafe>();
	if (!S->Begin(*this, TEXT("CultureList")))
	{
		return false;
	}
	S->Write(TEXT("dloc.ini"), TEXT("[DreamLoc]\nSourceCulture=zh-Hans\nTargetCultures=en,ja\n"));
	S->Write(TEXT("en/UI/Menu.dloc"), MakeDloc(TEXT("en"), TEXT("Menu"), { { TEXT("Start"), TEXT("开始游戏") } }));
	S->Write(TEXT("ja/UI/Menu.dloc"), MakeDloc(TEXT("ja"), TEXT("Menu"), { { TEXT("Quit"), TEXT("退出") } }));
	const FDeskRef D = MakeDesk(S);
	TArray<TFunction<bool()>> Steps;
	// The list opened as a click opens it, its menu not focused: a window that takes the focus would take the user's.
	AddCheck(Steps, D, [this, D]()
	{
		TestEqual(TEXT("en to begin with"), D->Window->GetCulture(), TEXT("en"));
		const TSharedPtr<SWidget> Combo = D->Tagged(TEXT("DreamLocTranslate.Culture"));
		TestTrue(TEXT("the culture list"), Combo.IsValid());
		if (Combo.IsValid())
		{
			StaticCastSharedPtr<SComboBox<TSharedPtr<FString>>>(Combo)->SetIsOpen(true, /*bFocusMenu*/ false);
		}
	});
	AddClick(*this, Steps, D, TEXT("ja in the list"), [D]()
	{
		for (const TSharedRef<SWindow>& Child : D->Host->GetChildWindows())
		{
			if (const TSharedPtr<SWidget> Found = FindText(Child, TEXT("ja")))
			{
				return Found;
			}
		}
		return TSharedPtr<SWidget>();
	});
	AddCheck(Steps, D, [this, D]()
	{
		TestEqual(TEXT("a culture chosen from the list"), D->Window->GetCulture(), TEXT("ja"));
		TestEqual(TEXT("its entries shown"), Keys(D->Window->GetShown()), TEXT("Quit"));
		FSlateApplication::Get().DismissAllMenus();
		// What in-context does with an entry the culture shown lacks: the culture that has it.
		TestTrue(TEXT("an entry only en has"), D->Window->SelectInAnyCulture(TEXT("Menu"), TEXT("Start")));
		TestEqual(TEXT("the window goes to en"), D->Window->GetCulture() + TEXT(" ") + CurrentKey(*D->Window), TEXT("en Start"));
		TestFalse(TEXT("one no culture has"), D->Window->SelectInAnyCulture(TEXT("Menu"), TEXT("Nothing")));
		TestEqual(TEXT("and the window stays"), D->Window->GetCulture(), TEXT("en"));
	});
	AddEnd(Steps, D);
	ADD_LATENT_AUTOMATION_COMMAND(FSteps(MoveTemp(Steps)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocTranslateNarrowTest, "DreamLoc.Translate.Window.ANarrowWindowWrapsItsToolbarNothingCutOff",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FDreamLocTranslateNarrowTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocTranslateWindowTestsLocal;
	const FScenarioRef S = MakeShared<FScenario, ESPMode::ThreadSafe>();
	if (!S->Begin(*this, TEXT("Narrow")))
	{
		return false;
	}
	S->Write(TEXT("dloc.ini"), TEXT("[DreamLoc]\nSourceCulture=zh-Hans\nTargetCultures=en\n"));
	S->Write(TEXT("en/UI/Menu.dloc"), MakeDloc(TEXT("en"), TEXT("Menu"), { { TEXT("Start"), TEXT("开始游戏") } }));
	const FDeskRef D = MakeDesk(S, FVector2D(520.0, 760.0));
	TArray<TFunction<bool()>> Steps;
	// A few frames: a wrap box lays out by the width it had the frame before.
	for (int32 Frame = 0; Frame < 3; ++Frame)
	{
		AddCheck(Steps, D, []() {});
	}
	AddCheck(Steps, D, [this, D]()
	{
		const FGeometry Window = D->Window->GetTickSpaceGeometry();
		const double Right = Window.LocalToAbsolute(Window.GetLocalSize()).X;
		for (const TCHAR* Tag : { TEXT("DreamLocTranslate.Filter.All"), TEXT("DreamLocTranslate.Filter.Untranslated"), TEXT("DreamLocTranslate.Filter.Stale"),
			TEXT("DreamLocTranslate.Filter.Review"), TEXT("DreamLocTranslate.Filter.Problems"), TEXT("DreamLocTranslate.Filter.Done"),
			TEXT("DreamLocTranslate.Search"), TEXT("DreamLocTranslate.Profile"), TEXT("DreamLocTranslate.Pretranslate") })
		{
			const TSharedPtr<SWidget> Found = D->Tagged(Tag);
			FWidgetPath Path;
			if (!Found.IsValid() || !FSlateApplication::Get().FindPathToWidget(Found.ToSharedRef(), Path))
			{
				AddError(FString::Printf(TEXT("%s is not laid out"), Tag));
				continue;
			}
			const FGeometry& Geometry = Path.Widgets.Last().Geometry;
			const double Edge = Geometry.LocalToAbsolute(Geometry.GetLocalSize()).X;
			TestTrue(FString::Printf(TEXT("%s inside a 520-wide window (right edge %.0f, the window's %.0f)"), Tag, Edge, Right), Edge <= Right + 0.5);
		}
	});
	AddEnd(Steps, D);
	ADD_LATENT_AUTOMATION_COMMAND(FSteps(MoveTemp(Steps)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocTranslateWindowOpensTest, "DreamLoc.Translate.Window.ToolsDreamLocTranslateOpensTheDesk",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FDreamLocTranslateWindowOpensTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocTranslateWindowTestsLocal;
	const FScenarioRef S = MakeShared<FScenario, ESPMode::ThreadSafe>();
	if (!S->Begin(*this, TEXT("TranslateWindowOpens")))
	{
		return false;
	}
	S->Write(TEXT("dloc.ini"), TEXT("[DreamLoc]\nSourceCulture=zh-Hans\nTargetCultures=en\n"));
	S->Write(TEXT("en/UI/Menu.dloc"), MakeDloc(TEXT("en"), TEXT("Menu"), { { TEXT("Start"), TEXT("开始游戏") } }));
	const TSharedPtr<SDreamLocTranslateWindow> Window = DreamLocTranslateWindow::Open();
	TestTrue(TEXT("Tools > DreamLoc > Translate opens its tab"), Window.IsValid());
	if (Window.IsValid())
	{
		Window->SetDirectories(S->DLoc, S->Root);
		TestEqual(TEXT("pointed at another folder"), Keys(Window->GetShown()), TEXT("Start"));
		TestTrue(TEXT("an entry chosen by key"), Window->Select(TEXT("Menu"), TEXT("Start")));
		TestFalse(TEXT("one that is not there"), Window->Select(TEXT("Menu"), TEXT("Nothing")));
		// Back to the project's folder: the tab outlives the test's, and would watch it as it goes.
		Window->SetDirectories(FDreamLocTextSource::GetProjectDirectory(), FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
		if (const TSharedPtr<SDockTab> Tab = FGlobalTabmanager::Get()->FindExistingLiveTab(DreamLocTranslateWindow::GetTabName()))
		{
			Tab->RequestCloseTab();
		}
	}
	S->End();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocTranslateWindowFitTest, "DreamLoc.Translate.Window.AWindowThatComesBackFillingTheScreenOpensAtTheDefaultSize",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FDreamLocTranslateWindowFitTest::RunTest(const FString& Parameters)
{
	auto Describe = [](const TOptional<FSlateRect>& InRect)
	{
		return InRect.IsSet() ? FString::Printf(TEXT("%.0f,%.0f %.0f x %.0f"), InRect->Left, InRect->Top, InRect->GetSize().X, InRect->GetSize().Y) : FString(TEXT("left as it is"));
	};
	// A 3136-pixel screen at 200%: the size a layout kept, filling it, comes back as the default size in the middle.
	TestEqual(TEXT("filling a high-DPI screen"), Describe(DreamLocTranslateWindow::FitRect(FVector2D(3136.0, 1680.0), false, FSlateRect(0.0f, 0.0f, 3136.0f, 1680.0f), 2.0f)),
		TEXT("288,84 2560 x 1512"));
	TestEqual(TEXT("maximized"), Describe(DreamLocTranslateWindow::FitRect(FVector2D(1280.0, 760.0), true, FSlateRect(0.0f, 0.0f, 1920.0f, 1040.0f), 1.0f)),
		TEXT("320,140 1280 x 760"));
	TestEqual(TEXT("on a second screen"), Describe(DreamLocTranslateWindow::FitRect(FVector2D(1900.0, 1000.0), false, FSlateRect(1920.0f, 0.0f, 3840.0f, 1040.0f), 1.0f)),
		TEXT("2240,140 1280 x 760"));
	TestEqual(TEXT("a size a person chose"), Describe(DreamLocTranslateWindow::FitRect(FVector2D(1500.0, 900.0), false, FSlateRect(0.0f, 0.0f, 1920.0f, 1040.0f), 1.0f)),
		TEXT("left as it is"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocTranslateOwnerTest, "DreamLoc.Translate.Window.APluginsOwnDLocFolderFromTheToolbar",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FDreamLocTranslateOwnerTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocTranslateWindowTestsLocal;
	const FScenarioRef S = MakeShared<FScenario, ESPMode::ThreadSafe>();
	if (!S->Begin(*this, TEXT("TranslateOwner")))
	{
		return false;
	}
	ON_SCOPE_EXIT { S->End(); };
	S->Write(TEXT("dloc.ini"), TEXT("[DreamLoc]\nSourceCulture=zh-Hans\nTargetCultures=en\n"));
	S->Write(TEXT("en/UI/Menu.dloc"), MakeDloc(TEXT("en"), TEXT("Menu"), { { TEXT("Start"), TEXT("开始游戏") } }));
	const TSharedRef<SDreamLocTranslateWindow> Window = SNew(SDreamLocTranslateWindow).DLocDirectory(S->DLoc).OwnerDirectory(S->Root);

	const TArray<FString> Owners = SDreamLocTranslateWindow::FindOwners();
	TestTrue(TEXT("the project's folder first"), Owners.Num() > 0 && Owners[0].IsEmpty());
	TestFalse(TEXT("a plugin without a DLoc/dloc.ini is not offered"), Owners.Contains(TEXT("DreamLoc")));
	const TSharedPtr<SWidget> Combo = FindTagged(Window, TEXT("DreamLocTranslate.Owner"));
	TestTrue(TEXT("the toolbar's list"), Combo.IsValid());
	TestEqual(TEXT("it says whose"), TextUnder(Combo), FString(TEXT("Project")));

	TestFalse(TEXT("no such plugin"), Window->SetOwner(TEXT("NoSuchPlugin")));
	TestEqual(TEXT("the folder as it was"), Window->GetDLocDirectory(), S->DLoc);
	TestEqual(TEXT("its entries"), Window->GetModel().GetEntries().Num(), 1);

	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DreamLoc"));
	if (TestTrue(TEXT("DreamLoc is a plugin here"), Plugin.IsValid()))
	{
		const FString Base = FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir());
		TestTrue(TEXT("a plugin by name"), Window->SetOwner(TEXT("DreamLoc")));
		TestEqual(TEXT("whose it is"), Window->GetOwner(), FString(TEXT("DreamLoc")));
		TestEqual(TEXT("its DLoc folder"), Window->GetDLocDirectory(), Base / TEXT("DLoc"));
		TestEqual(TEXT("the label"), TextUnder(Combo), FString(TEXT("Plugin: DreamLoc")));
		const FDreamLocTranslateRequest Request = Window->MakeBaseRequest();
		TestEqual(TEXT("a pre-translation writes into the plugin's folder"), Request.DLocDirectory, Base / TEXT("DLoc"));
		TestEqual(TEXT("with the plugin's sources"), Request.OwnerDirectory, Base);
	}
	TestTrue(TEXT("back to the project's"), Window->SetOwner(FString()));
	TestEqual(TEXT("nobody's but the project's"), Window->GetOwner(), FString());
	TestEqual(TEXT("the project's DLoc folder"), Window->GetDLocDirectory(), FDreamLocTextSource::GetProjectDirectory());
	return true;
}

namespace DreamLocTranslateWindowTestsLocal
{
	/** A menu the service translates and a story it turns down, as a relay without a session does. */
	bool BeginTurnedDown(FAutomationTestBase& InTest, const FScenarioRef& InScenario, const TCHAR* InName)
	{
		if (!InScenario->Begin(InTest, InName))
		{
			return false;
		}
		InScenario->Write(TEXT("dloc.ini"), TEXT("[DreamLoc]\nSourceCulture=zh-Hans\nTargetCultures=en\n\n[Translate]\nProfile=model\nConcurrency=1\n"));
		InScenario->Write(TEXT("en/UI/Menu.dloc"), MakeDloc(TEXT("en"), TEXT("Menu"), { { TEXT("Start"), TEXT("开始游戏") }, { TEXT("Quit"), TEXT("退出") } }));
		InScenario->Write(TEXT("en/Story/Intro.dloc"), MakeDloc(TEXT("en"), TEXT("Intro"), { { TEXT("Line"), TEXT("很久以前") } }));
		InTest.TestTrue(TEXT("the profiles saved where the test says"), DreamLocTranslateConfig::SaveProfiles({ InScenario->Fake->Profile(TEXT("OpenAI"), TEXT("model")) }));
		InScenario->Fake->Chat = [](const DreamLocTranslateFake::FChat& InChat)
		{
			for (const TSharedPtr<FJsonObject>& Entry : InChat.Entries)
			{
				if (Entry->GetStringField(TEXT("ns")) == TEXT("Intro"))
				{
					DreamLocTranslateFake::FChatAnswer Answer;
					Answer.Status = 400;
					Answer.RawBody = TEXT("{\"error\":{\"message\":\"MissingSessionID\"}}");
					return Answer;
				}
			}
			return DreamLocTranslateFake::MarkAll(InChat);
		};
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocTranslateRunLogTest, "DreamLoc.Translate.Window.TheDialogsLogSaysWhatEachRequestCameToAndWhyItFailed",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FDreamLocTranslateRunLogTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocTranslateWindowTestsLocal;
	const FScenarioRef S = MakeShared<FScenario, ESPMode::ThreadSafe>();
	if (!BeginTurnedDown(*this, S, TEXT("TranslateRunLog")))
	{
		return false;
	}

	const FDeskRef D = MakeDesk(S);
	TArray<TFunction<bool()>> Steps;
	TSharedRef<TSharedPtr<SDreamLocPretranslateDialog>> Dialog = MakeShared<TSharedPtr<SDreamLocPretranslateDialog>>();
	AddClick(*this, Steps, D, TEXT("AI pre-translate..."), [D]() { return D->Tagged(TEXT("DreamLocTranslate.Pretranslate")); });
	AddCheck(Steps, D, [this, D, Dialog]()
	{
		*Dialog = D->Window->OpenPretranslate();
		if (!TestTrue(TEXT("the dialog"), Dialog->IsValid()))
		{
			D->bFailed = true;
			return;
		}
		TestTrue(TEXT("its log open"), (*Dialog)->IsLogOpen());
		TestEqual(TEXT("nothing in it before a run"), (*Dialog)->GetLogLineCount(), 0);
		TestFalse(TEXT("no report to open"), D->Tagged(TEXT("DreamLocPretranslate.OpenReport")).IsValid() && D->Tagged(TEXT("DreamLocPretranslate.OpenReport"))->IsEnabled());
		TestTrue(TEXT("ready"), (*Dialog)->CanStart());
	});
	AddClick(*this, Steps, D, TEXT("the dialog's Translate"), [D]() { return D->Tagged(TEXT("DreamLocPretranslate.Start")); });
	AddWait(*this, Steps, D, TEXT("the pre-translation"), 60.0, [D]() { return !D->Window->IsRunning(); });
	AddCheck(Steps, D, [this, D, Dialog]()
	{
		const FString Log = (*Dialog)->GetLogText();
		const FString Result = (*Dialog)->GetResultText().ToString();
		TestTrue(FString::Printf(TEXT("the dialog says what came back, and where to look (%s)"), *Result),
			Result.StartsWith(TEXT("Done: 2 translated, 1 failed, 0 skipped")) && Result.Contains(TEXT("The log below says why each failed.")));
		TestTrue(FString::Printf(TEXT("the log: the request that came back (%s)"), *Log), Log.Contains(TEXT("en  UI/Menu.dloc  2 texts  model  ok")));
		TestTrue(TEXT("the request turned down, and why"), Log.Contains(TEXT("en  Story/Intro.dloc  1 text  model  failed")) && Log.Contains(TEXT("HTTP 400: MissingSessionID")));
		TestTrue(TEXT("what the culture came to"), Log.Contains(TEXT("DreamLoc translate en: 2 translated, 1 failed, 0 skipped")));
		TestTrue(TEXT("each line with its time"), Log.Len() > 8 && Log[2] == TEXT(':') && Log[5] == TEXT(':'));
		TestEqual(TEXT("every line of the run"), (*Dialog)->GetLogLineCount(), D->Window->GetLastResult().Log.Num());
		TestEqual(TEXT("two say something went wrong: the request, and why the culture's entry failed"), (*Dialog)->GetLogFailureCount(), 2);
		TestEqual(TEXT("its heading counts them"), TextUnder(D->Tagged(TEXT("DreamLocPretranslate.LogCount"))),
			FString::Printf(TEXT("%d lines, 2 failures"), (*Dialog)->GetLogLineCount()));
		const TSharedPtr<SWidget> Report = D->Tagged(TEXT("DreamLocPretranslate.OpenReport"));
		TestTrue(TEXT("the report to open"), (*Dialog)->GetReportPaths().Num() == 1 && IFileManager::Get().FileExists(*(*Dialog)->GetReportPaths()[0]) && Report.IsValid() && Report->IsEnabled());
	});
	AddClick(*this, Steps, D, TEXT("Failures only"), [D]() { return D->Tagged(TEXT("DreamLocPretranslate.LogFailuresOnly")); });
	AddCheck(Steps, D, [this, D, Dialog]()
	{
		TestTrue(TEXT("a click: the failures alone"), (*Dialog)->IsFailuresOnly());
		const FString Log = (*Dialog)->GetLogText();
		TestTrue(TEXT("the failure shown"), Log.Contains(TEXT("HTTP 400: MissingSessionID")));
		TestFalse(TEXT("what went right not"), Log.Contains(TEXT("  ok  ")));
		TArray<FString> Lines;
		Log.ParseIntoArrayLines(Lines);
		TestEqual(TEXT("a line for each failure"), Lines.Num(), (*Dialog)->GetLogFailureCount());
	});
	AddClick(*this, Steps, D, TEXT("the log's heading"), [D]() { return D->Tagged(TEXT("DreamLocPretranslate.LogToggle")); });
	AddCheck(Steps, D, [this, D, Dialog]()
	{
		TestFalse(TEXT("a click folds it away"), (*Dialog)->IsLogOpen());
		const TSharedPtr<SWidget> Lines = D->Tagged(TEXT("DreamLocPretranslate.Log"));
		TestTrue(TEXT("the lines hidden"), Lines.IsValid() && Lines->GetVisibility() == EVisibility::Collapsed);
	});
	// A run started elsewhere (the list's menu): the log is of it, the dialog open meanwhile.
	AddCheck(Steps, D, [this, D, Dialog]()
	{
		(*Dialog)->SetLogOpen(true);
		(*Dialog)->SetFailuresOnly(false);
		TestTrue(TEXT("the story's line translated from the list"), D->Window->TranslateEntries({ D->Window->GetModel().Find(TEXT("Intro"), TEXT("Line")) }, FString()));
	});
	AddWait(*this, Steps, D, TEXT("the run from the list"), 60.0, [D]() { return !D->Window->IsRunning(); });
	AddCheck(Steps, D, [this, D, Dialog]()
	{
		const FString Log = (*Dialog)->GetLogText();
		TestTrue(FString::Printf(TEXT("the new run's lines (%s)"), *Log), Log.Contains(TEXT("DreamLoc translate en: 0 translated, 1 failed, 0 skipped")));
		TestFalse(TEXT("the last run's gone"), Log.Contains(TEXT("UI/Menu.dloc  2 texts")));
	});
	AddEnd(Steps, D);
	ADD_LATENT_AUTOMATION_COMMAND(FSteps(MoveTemp(Steps)));
	return true;
}

#endif
