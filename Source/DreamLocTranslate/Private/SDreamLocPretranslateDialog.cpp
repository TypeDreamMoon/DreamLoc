// Copyright DreamMoon. All Rights Reserved.

#include "SDreamLocPretranslateDialog.h"

#include "DreamLocTranslateConfig.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/PlatformProcess.h"
#include "SDreamLocTranslateWindow.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/StyleColors.h"
#include "Types/ISlateMetaData.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "DreamLocPretranslateDialog"

namespace DreamLocPretranslateDialogLocal
{
	const TCHAR* ScopeTag(EDreamLocPretranslateScope InScope)
	{
		switch (InScope)
		{
		case EDreamLocPretranslateScope::Shown: return TEXT("DreamLocPretranslate.Scope.Shown");
		case EDreamLocPretranslateScope::Selected: return TEXT("DreamLocPretranslate.Scope.Selected");
		case EDreamLocPretranslateScope::Files: return TEXT("DreamLocPretranslate.Scope.Files");
		default: return TEXT("DreamLocPretranslate.Scope.All");
		}
	}

	TSharedRef<SWidget> Heading(const FText& InText)
	{
		return SNew(STextBlock).Text(InText).Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")));
	}
}

void SDreamLocPretranslateDialog::Construct(const FArguments& InArgs, const TSharedRef<SDreamLocTranslateWindow>& InWindow)
{
	using namespace DreamLocPretranslateDialogLocal;
	Window = InWindow;
	if (!InWindow->GetCulture().IsEmpty())
	{
		Cultures.Add(InWindow->GetCulture());
	}
	Profile = InWindow->GetProfile();
	if (!InWindow->GetFile().IsEmpty())
	{
		Files.Add(InWindow->GetFile());
	}
	ProfileChoices.Add(MakeShared<FString>());
	for (const FDreamLocTranslateProfile& Each : InWindow->GetProfiles())
	{
		ProfileChoices.Add(MakeShared<FString>(Each.Name));
	}
	auto ProfileLabel = [WeakWindow = Window](const FString& InName) -> FText
	{
		const TSharedPtr<SDreamLocTranslateWindow> Owner = WeakWindow.Pin();
		const FDreamLocTranslateProfile* Found = Owner.IsValid() ? DreamLocTranslateConfig::FindProfile(Owner->GetProfiles(), InName) : nullptr;
		if (InName.IsEmpty())
		{
			return LOCTEXT("EachCulturesOwn", "Each culture's own translator");
		}
		return Found != nullptr ? FText::Format(LOCTEXT("ProfileLabel", "{0} ({1}, {2})"), FText::FromString(Found->Name), FText::FromString(Found->Type),
			Found->IsLocal() ? LOCTEXT("Local", "local") : LOCTEXT("Cloud", "cloud")) : FText::FromString(InName);
	};

	TSharedRef<SVerticalBox> Scopes = SNew(SVerticalBox);
	for (const EDreamLocPretranslateScope Each : { EDreamLocPretranslateScope::All, EDreamLocPretranslateScope::Shown, EDreamLocPretranslateScope::Selected, EDreamLocPretranslateScope::Files })
	{
		Scopes->AddSlot().AutoHeight().Padding(0.0f, 1.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.AddMetaData<FTagMetaData>(FTagMetaData(ScopeTag(Each)))
				.Style(FAppStyle::Get(), TEXT("RadioButton"))
				.IsChecked_Lambda([this, Each]() { return Scope == Each ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this, Each](ECheckBoxState) { SetScope(Each); })
				[
					SNew(STextBlock).Text_Lambda([this, Each]() { return ScopeLabel(Each); })
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				Each != EDreamLocPretranslateScope::Files ? SNullWidget::NullWidget :
				SAssignNew(FilesBox, SEditableTextBox)
				.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("DreamLocPretranslate.FilesText")))
				.HintText(LOCTEXT("FilesHint", "ui/menu, story/ (paths without .dloc, folders with a slash; commas between)"))
				.Text(FText::FromString(FString::Join(Files, TEXT(", "))))
				.IsEnabled_Lambda([this]() { return Scope == EDreamLocPretranslateScope::Files; })
				.OnTextChanged_Lambda([this](const FText& InText)
				{
					TArray<FString> Parts;
					InText.ToString().ParseIntoArray(Parts, TEXT(","));
					for (FString& Part : Parts)
					{
						Part.TrimStartAndEndInline();
					}
					Parts.RemoveAll([](const FString& InPart) { return InPart.IsEmpty(); });
					Files = Parts;
					Recount();
				})
			]
		];
	}

	auto State = [this](const TCHAR* InTag, const FText& InLabel, const FText& InTip, bool* InValue) -> TSharedRef<SWidget>
	{
		return SNew(SCheckBox)
			.AddMetaData<FTagMetaData>(FTagMetaData(InTag))
			.ToolTipText(InTip)
			.IsChecked_Lambda([InValue]() { return *InValue ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this, InValue](ECheckBoxState InState) { *InValue = InState == ECheckBoxState::Checked; Recount(); })
			[
				SNew(STextBlock).Text(InLabel)
			];
	};

	ChildSlot
	[
		SNew(SBox).Padding(12.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[Heading(LOCTEXT("CulturesHeading", "Cultures"))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 10.0f)[MakeCultures()]
			+ SVerticalBox::Slot().AutoHeight()[Heading(LOCTEXT("ScopeHeading", "Entries"))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 10.0f)[Scopes]
			+ SVerticalBox::Slot().AutoHeight()[Heading(LOCTEXT("StatesHeading", "Of those, the ones"))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 10.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 16.0f, 0.0f)
				[
					State(TEXT("DreamLocPretranslate.Untranslated"), LOCTEXT("Untranslated", "Untranslated"), LOCTEXT("UntranslatedTip", "Entries with no translation"), &bUntranslated)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 16.0f, 0.0f)
				[
					State(TEXT("DreamLocPretranslate.Stale"), LOCTEXT("Stale", "Stale"), LOCTEXT("StaleTip", "Entries whose source changed since they were translated"), &bStale)
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					State(TEXT("DreamLocPretranslate.Machine"), LOCTEXT("Machine", "Machine, again"), LOCTEXT("MachineTip", "Machine translations not reviewed yet, translated again"), &bMachine)
				]
			]
			+ SVerticalBox::Slot().AutoHeight()[Heading(LOCTEXT("ProfileHeading", "Translator"))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 10.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SAssignNew(ProfileCombo, SComboBox<TSharedPtr<FString>>)
					.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("DreamLocPretranslate.Profile")))
					.OptionsSource(&ProfileChoices)
					.OnGenerateWidget_Lambda([ProfileLabel](TSharedPtr<FString> InItem) -> TSharedRef<SWidget> { return SNew(STextBlock).Text(ProfileLabel(InItem.IsValid() ? *InItem : FString())); })
					.OnSelectionChanged_Lambda([this](TSharedPtr<FString> InItem, ESelectInfo::Type InHow)
					{
						if (InItem.IsValid() && InHow != ESelectInfo::Direct)
						{
							SetProfile(*InItem);
						}
					})
					[
						SNew(STextBlock).Text_Lambda([this, ProfileLabel]() { return ProfileLabel(Profile); })
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight()[Heading(LOCTEXT("EstimateHeading", "What it sends"))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 10.0f)
			[
				SNew(STextBlock)
				.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("DreamLocPretranslate.Estimate")))
				.AutoWrapText(true)
				.Text_Lambda([this]() { return ResultText.IsEmpty() ? EstimateText : FText::Format(LOCTEXT("ResultAndEstimate", "{0}\n\n{1}"), ResultText, EstimateText); })
			]
			// The buttons at the bottom while the log is folded away; under the estimate while the log takes the room.
			+ SVerticalBox::Slot().FillHeight(1.0f)
			[
				SNew(SSpacer).Visibility_Lambda([this]() { return bLogOpen ? EVisibility::Collapsed : EVisibility::Visible; })
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(SProgressBar)
					.Visibility_Lambda([this]() { const TSharedPtr<SDreamLocTranslateWindow> Owner = Window.Pin(); return Owner.IsValid() && Owner->IsRunning() ? EVisibility::Visible : EVisibility::Hidden; })
					.Percent_Lambda([this]() -> TOptional<float> { const TSharedPtr<SDreamLocTranslateWindow> Owner = Window.Pin(); return Owner.IsValid() ? Owner->GetRunFraction() : 0.0f; })
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(SButton)
					.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("DreamLocPretranslate.Cancel")))
					.Text(LOCTEXT("Cancel", "Cancel"))
					.Visibility_Lambda([this]() { const TSharedPtr<SDreamLocTranslateWindow> Owner = Window.Pin(); return Owner.IsValid() && Owner->IsRunning() ? EVisibility::Visible : EVisibility::Collapsed; })
					.OnClicked_Lambda([this]() { if (const TSharedPtr<SDreamLocTranslateWindow> Owner = Window.Pin()) { Owner->CancelRun(); } return FReply::Handled(); })
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
					.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("DreamLocPretranslate.Start")))
					.ButtonStyle(FAppStyle::Get(), TEXT("PrimaryButton"))
					.IsEnabled_Lambda([this]() { return CanStart(); })
					.Text_Lambda([this]()
					{
						return Estimate.Distinct > 0
							? FText::Format(LOCTEXT("StartCount", "Translate {0} {0}|plural(one=text,other=texts)"), Estimate.Distinct)
							: LOCTEXT("Start", "Translate");
					})
					.OnClicked_Lambda([this]() { Start(); return FReply::Handled(); })
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 10.0f, 0.0f, 0.0f)[MakeLogHeader()]
			+ SVerticalBox::Slot().FillHeight(1.0f).Padding(0.0f, 4.0f, 0.0f, 0.0f)
			[
				SNew(SBorder)
				.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("DreamLocPretranslate.Log")))
				.Visibility_Lambda([this]() { return bLogOpen ? EVisibility::Visible : EVisibility::Collapsed; })
				.BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
				.Padding(2.0f)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					[
						SAssignNew(LogList, SListView<TSharedPtr<FDreamLocTranslateLogLine>>)
						.ListItemsSource(&ShownLog)
						.SelectionMode(ESelectionMode::Multi)
						.OnGenerateRow(this, &SDreamLocPretranslateDialog::MakeLogRow)
					]
					+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Visibility_Lambda([this]() { return ShownLog.IsEmpty() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						.Text_Lambda([this]()
						{
							return LogLines.IsEmpty() ? LOCTEXT("LogEmpty", "Nothing yet: a line for each request as it comes back, and for each text that fails.")
								: LOCTEXT("LogNoFailures", "Nothing went wrong.");
						})
					]
				]
			]
		]
	];
	if (ProfileCombo.IsValid())
	{
		for (const TSharedPtr<FString>& Choice : ProfileChoices)
		{
			if (*Choice == Profile)
			{
				ProfileCombo->SetSelectedItem(Choice);
			}
		}
	}
	Recount();
}

TSharedRef<SWidget> SDreamLocPretranslateDialog::MakeCultures()
{
	TSharedRef<SWrapBox> Box = SNew(SWrapBox).UseAllottedSize(true);
	const TSharedPtr<SDreamLocTranslateWindow> Owner = Window.Pin();
	if (!Owner.IsValid())
	{
		return Box;
	}
	for (const FString& Each : Owner->GetSettings().TargetCultures)
	{
		Box->AddSlot().Padding(0.0f, 0.0f, 16.0f, 2.0f)
		[
			SNew(SCheckBox)
			.AddMetaData<FTagMetaData>(FTagMetaData(*(TEXT("DreamLocPretranslate.Culture.") + Each)))
			.IsChecked_Lambda([this, Each]() { return Cultures.Contains(Each) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this, Each](ECheckBoxState InState)
			{
				TArray<FString> Chosen = Cultures;
				if (InState == ECheckBoxState::Checked)
				{
					Chosen.AddUnique(Each);
				}
				else
				{
					Chosen.Remove(Each);
				}
				SetCultures(Chosen);
			})
			[
				SNew(STextBlock).Text(FText::FromString(Each))
			]
		];
	}
	return Box;
}

FText SDreamLocPretranslateDialog::ScopeLabel(EDreamLocPretranslateScope InScope) const
{
	const TSharedPtr<SDreamLocTranslateWindow> Owner = Window.Pin();
	switch (InScope)
	{
	case EDreamLocPretranslateScope::Shown:
		return FText::Format(LOCTEXT("ScopeShown", "What the list shows ({0})"), Owner.IsValid() ? Owner->GetShown().Num() : 0);
	case EDreamLocPretranslateScope::Selected:
		return FText::Format(LOCTEXT("ScopeSelected", "The entries selected ({0})"), Owner.IsValid() ? Owner->GetSelectedEntries().Num() : 0);
	case EDreamLocPretranslateScope::Files:
		return LOCTEXT("ScopeFiles", "Files");
	default:
		return LOCTEXT("ScopeAll", "All");
	}
}

void SDreamLocPretranslateDialog::SetCultures(TConstArrayView<FString> InCultures)
{
	Cultures = TArray<FString>(InCultures);
	Recount();
}

void SDreamLocPretranslateDialog::SetScope(EDreamLocPretranslateScope InScope)
{
	Scope = InScope;
	Recount();
}

void SDreamLocPretranslateDialog::SetFiles(TConstArrayView<FString> InFiles)
{
	Files = TArray<FString>(InFiles);
	if (FilesBox.IsValid())
	{
		FilesBox->SetText(FText::FromString(FString::Join(Files, TEXT(", "))));
	}
	Recount();
}

void SDreamLocPretranslateDialog::SetStates(bool bInUntranslated, bool bInStale, bool bInMachine)
{
	bUntranslated = bInUntranslated;
	bStale = bInStale;
	bMachine = bInMachine;
	Recount();
}

void SDreamLocPretranslateDialog::SetProfile(const FString& InProfile)
{
	Profile = InProfile;
	Recount();
}

FDreamLocTranslateRequest SDreamLocPretranslateDialog::MakeRequest() const
{
	const TSharedPtr<SDreamLocTranslateWindow> Owner = Window.Pin();
	FDreamLocTranslateRequest Request = Owner.IsValid() ? Owner->MakeBaseRequest() : FDreamLocTranslateRequest();
	Request.Cultures = Cultures;
	Request.Profile = Profile;
	Request.bIncludeUntranslated = bUntranslated;
	Request.bIncludeStale = bStale;
	Request.bIncludeMachine = bMachine;
	if (Owner.IsValid() && (Scope == EDreamLocPretranslateScope::Shown || Scope == EDreamLocPretranslateScope::Selected))
	{
		for (const TSharedPtr<FDreamLocReviewEntry>& Entry : Scope == EDreamLocPretranslateScope::Shown ? Owner->GetShown() : Owner->GetSelectedEntries())
		{
			Request.Entries.Emplace(Entry->Entry.Namespace, Entry->Entry.Key);
		}
	}
	if (Scope == EDreamLocPretranslateScope::Files)
	{
		Request.Files = Files;
	}
	return Request;
}

void SDreamLocPretranslateDialog::Recount()
{
	Estimate = FDreamLocTranslateEstimate();
	const FDreamLocTranslateRequest Request = MakeRequest();
	// No entry at all, rather than every entry: an empty list or file filter means all of them to the flow.
	if (Cultures.IsEmpty())
	{
		Estimate.Errors.Add(LOCTEXT("NoCulture", "Choose a culture.").ToString());
	}
	else if (!bUntranslated && !bStale && !bMachine)
	{
		Estimate.Errors.Add(LOCTEXT("NoState", "Choose which entries: untranslated, stale or machine.").ToString());
	}
	else if ((Scope == EDreamLocPretranslateScope::Shown || Scope == EDreamLocPretranslateScope::Selected) && Request.Entries.IsEmpty())
	{
		Estimate.Errors.Add((Scope == EDreamLocPretranslateScope::Shown ? LOCTEXT("NothingShown", "The list shows no entry.") : LOCTEXT("NothingSelected", "No entry is selected in the list.")).ToString());
	}
	else if (Scope == EDreamLocPretranslateScope::Files && Request.Files.IsEmpty())
	{
		Estimate.Errors.Add(LOCTEXT("NoFiles", "Type the files.").ToString());
	}
	else
	{
		Estimate = DreamLocTranslateFlow::Estimate(Request);
	}
	FFormatNamedArguments Args;
	Args.Add(TEXT("Entries"), Estimate.Entries);
	Args.Add(TEXT("Distinct"), Estimate.Distinct);
	Args.Add(TEXT("Characters"), Estimate.Characters);
	Args.Add(TEXT("Tokens"), Estimate.Tokens);
	Args.Add(TEXT("Batches"), Estimate.Batches);
	Args.Add(TEXT("Skipped"), Estimate.Skipped);
	const FText Counts = Estimate.bTranslatorKnown
		? FText::Format(LOCTEXT("EstimateResult",
			"{Entries} {Entries}|plural(one=entry,other=entries): {Distinct} {Distinct}|plural(one=text,other=texts) to send, {Characters} {Characters}|plural(one=character,other=characters), about {Tokens} {Tokens}|plural(one=token,other=tokens) for a large model, {Batches} {Batches}|plural(one=request,other=requests); {Skipped} left out (only for local translators, forms a machine translator cannot keep, a language not served)"),
			Args)
		: FText::Format(LOCTEXT("EstimateWithoutTranslator",
			"{Entries} {Entries}|plural(one=entry,other=entries): {Distinct} {Distinct}|plural(one=text,other=texts) to send, {Characters} {Characters}|plural(one=character,other=characters); choose a translator to count the requests and tokens"),
			Args);
	EstimateText = Estimate.Errors.IsEmpty() ? Counts : FText::Format(LOCTEXT("EstimateAndErrors", "{0}\n{1}"), Counts, FText::FromString(FString::Join(Estimate.Errors, TEXT("\n"))));
}

bool SDreamLocPretranslateDialog::CanStart() const
{
	const TSharedPtr<SDreamLocTranslateWindow> Owner = Window.Pin();
	return Owner.IsValid() && !Owner->IsRunning() && Estimate.Errors.IsEmpty() && Estimate.Distinct > 0;
}

bool SDreamLocPretranslateDialog::Start()
{
	Recount();
	const TSharedPtr<SDreamLocTranslateWindow> Owner = Window.Pin();
	if (!CanStart())
	{
		return false;
	}
	ResultText = LOCTEXT("Running", "Translating... closing this leaves it going; the review window shows how far it has got.");
	const TWeakPtr<SDreamLocTranslateWindow> WeakWindow = Owner;
	const TWeakPtr<SDreamLocPretranslateDialog> WeakDialog = SharedThis(this);
	const TArray<FString> Chosen = Cultures;
	return Owner->StartRun(MakeRequest(), [WeakWindow, WeakDialog, Chosen](const FDreamLocTranslateResult& InResult)
	{
		UE_LOG(LogDreamLocTranslate, Verbose, TEXT("pre-translation done: %d translated, %d failed, %d skipped, %d error(s); the dialog %s"), InResult.Translated,
			InResult.Failed, InResult.Skipped, InResult.Errors.Num(), WeakDialog.IsValid() ? TEXT("open") : TEXT("closed"));
		// What came back waits for review: the window shows it, the first one chosen.
		if (const TSharedPtr<SDreamLocTranslateWindow> Shown = WeakWindow.Pin())
		{
			if (!Chosen.Contains(Shown->GetCulture()) && Chosen.Num() > 0)
			{
				Shown->SetCulture(Chosen[0]);
			}
			Shown->SetFilter(EDreamLocReviewFilter::Review);
			if (Shown->GetShown().Num() > 0)
			{
				Shown->Select(Shown->GetShown()[0]->Entry.Namespace, Shown->GetShown()[0]->Entry.Key);
			}
		}
		if (const TSharedPtr<SDreamLocPretranslateDialog> Dialog = WeakDialog.Pin())
		{
			FFormatNamedArguments Args;
			Args.Add(TEXT("Translated"), InResult.Translated);
			Args.Add(TEXT("Failed"), InResult.Failed);
			Args.Add(TEXT("Skipped"), InResult.Skipped);
			Args.Add(TEXT("Cancelled"), InResult.bCancelled ? LOCTEXT("WasCancelled", " (cancelled)") : FText::GetEmpty());
			Dialog->ResultText = InResult.Errors.Num() > 0 ? FText::FromString(FString::Join(InResult.Errors, TEXT("\n")))
				: FText::Format(LOCTEXT("Done", "Done: {Translated} translated, {Failed} failed, {Skipped} skipped{Cancelled}. They wait in the review window, under To review."), Args);
			if (InResult.Errors.IsEmpty() && InResult.Failed > 0)
			{
				Dialog->ResultText = FText::Format(LOCTEXT("DoneWhy", "{0} The log below says why each failed."), Dialog->ResultText);
			}
			Dialog->Recount();
		}
	});
}

// ---------------------------------------------------------------------------------------------------------- the log

TSharedRef<SWidget> SDreamLocPretranslateDialog::MakeLogHeader()
{
	using namespace DreamLocPretranslateDialogLocal;
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			SNew(SButton)
			.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("DreamLocPretranslate.LogToggle")))
			.ButtonStyle(FAppStyle::Get(), TEXT("NoBorder"))
			.ToolTipText(LOCTEXT("LogToggleTip", "Show or fold away the log of the run"))
			.OnClicked_Lambda([this]() { SetLogOpen(!bLogOpen); return FReply::Handled(); })
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(SImage)
					.ColorAndOpacity(FSlateColor::UseForeground())
					.Image_Lambda([this]() { return FAppStyle::GetBrush(bLogOpen ? TEXT("TreeArrow_Expanded") : TEXT("TreeArrow_Collapsed")); })
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Heading(LOCTEXT("LogHeading", "Log"))]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("DreamLocPretranslate.LogCount")))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text_Lambda([this]()
					{
						return FText::Format(LOCTEXT("LogCount", "{0} {0}|plural(one=line,other=lines), {1} {1}|plural(one=failure,other=failures)"), LogLines.Num(), LogFailures);
					})
				]
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SCheckBox)
			.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("DreamLocPretranslate.LogFailuresOnly")))
			.ToolTipText(LOCTEXT("FailuresOnlyTip", "Only what went wrong: requests that failed (and were sent again), texts that failed or the checks turned down"))
			.IsChecked_Lambda([this]() { return bFailuresOnly ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this](ECheckBoxState InState) { SetFailuresOnly(InState == ECheckBoxState::Checked); })
			[
				SNew(STextBlock).Text(LOCTEXT("FailuresOnly", "Failures only"))
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SButton)
			.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("DreamLocPretranslate.LogCopy")))
			.Text(LOCTEXT("LogCopy", "Copy"))
			.ToolTipText(LOCTEXT("LogCopyTip", "Put the lines shown on the clipboard (Ctrl+C in the log: the lines selected)"))
			.IsEnabled_Lambda([this]() { return !ShownLog.IsEmpty(); })
			.OnClicked_Lambda([this]() { FPlatformApplicationMisc::ClipboardCopy(*GetLogText()); return FReply::Handled(); })
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SButton)
			.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("DreamLocPretranslate.OpenReport")))
			.Text_Lambda([this]()
			{
				return ReportPaths.Num() > 1 ? FText::Format(LOCTEXT("OpenReports", "Open the reports ({0})"), ReportPaths.Num()) : LOCTEXT("OpenReport", "Open the report");
			})
			.ToolTipText_Lambda([this]()
			{
				return ReportPaths.IsEmpty() ? LOCTEXT("NoReport", "A run writes its report (every entry, what came back and why) when it is done")
					: FText::FromString(FString::Join(ReportPaths, TEXT("\n")));
			})
			.IsEnabled_Lambda([this]() { return !ReportPaths.IsEmpty(); })
			.OnClicked_Lambda([this]() { OpenReports(); return FReply::Handled(); })
		];
}

TSharedRef<ITableRow> SDreamLocPretranslateDialog::MakeLogRow(TSharedPtr<FDreamLocTranslateLogLine> InLine, const TSharedRef<STableViewBase>& InOwner)
{
	const FSlateColor Color = InLine->Level == EDreamLocTranslateLogLevel::Error ? FSlateColor(FStyleColors::Error)
		: InLine->Level == EDreamLocTranslateLogLevel::Warning ? FSlateColor(FStyleColors::Warning) : FSlateColor::UseForeground();
	return SNew(STableRow<TSharedPtr<FDreamLocTranslateLogLine>>, InOwner)
		.Padding(FMargin(4.0f, 1.0f))
		[
			SNew(STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Mono"), 9))
			.AutoWrapText(true)
			.ColorAndOpacity(Color)
			.Text(FText::FromString(InLine->Format()))
		];
}

void SDreamLocPretranslateDialog::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	ReadLog();
}

void SDreamLocPretranslateDialog::ReadLog()
{
	const TSharedPtr<SDreamLocTranslateWindow> Owner = Window.Pin();
	if (!Owner.IsValid())
	{
		return;
	}
	bool bChanged = false;
	if (Owner->GetRunNumber() != LogRun)
	{
		LogRun = Owner->GetRunNumber();
		LogLines.Reset();
		ShownLog.Reset();
		LogFailures = 0;
		ReportPaths.Reset();
		bChanged = true;
	}
	TArray<FDreamLocTranslateLogLine> New;
	Owner->ReadRunLog(LogLines.Num(), New);
	for (FDreamLocTranslateLogLine& Line : New)
	{
		const TSharedPtr<FDreamLocTranslateLogLine> Shared = MakeShared<FDreamLocTranslateLogLine>(MoveTemp(Line));
		LogLines.Add(Shared);
		LogFailures += Shared->IsFailure() ? 1 : 0;
		if (!bFailuresOnly || Shared->IsFailure())
		{
			ShownLog.Add(Shared);
		}
	}
	// The reports once the run is done.
	if (!Owner->IsRunning() && ReportPaths != Owner->GetLastResult().ReportPaths)
	{
		ReportPaths = Owner->GetLastResult().ReportPaths;
	}
	if ((bChanged || New.Num() > 0) && LogList.IsValid())
	{
		LogList->RequestListRefresh();
		// Following the run, unless a line is selected: someone is reading.
		if (ShownLog.Num() > 0 && LogList->GetNumItemsSelected() == 0)
		{
			LogList->RequestScrollIntoView(ShownLog.Last());
		}
	}
}

void SDreamLocPretranslateDialog::RebuildShownLog()
{
	ShownLog.Reset();
	for (const TSharedPtr<FDreamLocTranslateLogLine>& Line : LogLines)
	{
		if (!bFailuresOnly || Line->IsFailure())
		{
			ShownLog.Add(Line);
		}
	}
	if (LogList.IsValid())
	{
		LogList->ClearSelection();
		LogList->RequestListRefresh();
		if (ShownLog.Num() > 0)
		{
			LogList->RequestScrollIntoView(ShownLog.Last());
		}
	}
}

void SDreamLocPretranslateDialog::SetLogOpen(bool bInOpen)
{
	bLogOpen = bInOpen;
}

void SDreamLocPretranslateDialog::SetFailuresOnly(bool bInFailuresOnly)
{
	if (bFailuresOnly != bInFailuresOnly)
	{
		bFailuresOnly = bInFailuresOnly;
		RebuildShownLog();
	}
}

FString SDreamLocPretranslateDialog::JoinLines(TConstArrayView<TSharedPtr<FDreamLocTranslateLogLine>> InLines) const
{
	TArray<FString> Out;
	for (const TSharedPtr<FDreamLocTranslateLogLine>& Line : InLines)
	{
		Out.Add(Line->Format());
	}
	return FString::Join(Out, TEXT("\n"));
}

FString SDreamLocPretranslateDialog::GetLogText() const
{
	return JoinLines(ShownLog);
}

FReply SDreamLocPretranslateDialog::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// Ctrl+C in the log: the lines selected, in the log's order.
	if (InKeyEvent.GetKey() == EKeys::C && (InKeyEvent.IsControlDown() || InKeyEvent.IsCommandDown()) && LogList.IsValid() && LogList->GetNumItemsSelected() > 0)
	{
		TArray<TSharedPtr<FDreamLocTranslateLogLine>> Selected;
		for (const TSharedPtr<FDreamLocTranslateLogLine>& Line : ShownLog)
		{
			if (LogList->IsItemSelected(Line))
			{
				Selected.Add(Line);
			}
		}
		FPlatformApplicationMisc::ClipboardCopy(*JoinLines(Selected));
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

void SDreamLocPretranslateDialog::OpenReports() const
{
	for (const FString& Path : ReportPaths)
	{
		FPlatformProcess::LaunchFileInDefaultExternalApplication(*Path);
	}
}

#undef LOCTEXT_NAMESPACE
