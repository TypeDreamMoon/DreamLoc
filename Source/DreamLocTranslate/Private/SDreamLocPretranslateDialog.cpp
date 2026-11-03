// Copyright DreamMoon. All Rights Reserved.

#include "SDreamLocPretranslateDialog.h"

#include "DreamLocTranslateConfig.h"
#include "SDreamLocTranslateWindow.h"
#include "Styling/AppStyle.h"
#include "Types/ISlateMetaData.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

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
			+ SVerticalBox::Slot().FillHeight(1.0f).Padding(0.0f, 4.0f, 0.0f, 10.0f)
			[
				SNew(STextBlock)
				.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("DreamLocPretranslate.Estimate")))
				.AutoWrapText(true)
				.Text_Lambda([this]() { return ResultText.IsEmpty() ? EstimateText : FText::Format(LOCTEXT("ResultAndEstimate", "{0}\n\n{1}"), ResultText, EstimateText); })
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
			Dialog->Recount();
		}
	});
}

#undef LOCTEXT_NAMESPACE
