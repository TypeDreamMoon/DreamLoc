// Copyright DreamMoon. All Rights Reserved.

#include "SDreamLocTranslateWindow.h"

#include "Async/Async.h"
#include "DreamLocSync.h"
#include "DreamLocTextSource.h"
#include "DreamLocTranslateConfig.h"
#include "DreamLocTranslator.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "SourceCodeNavigation.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Types/ISlateMetaData.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "DreamLocTranslateWindow"

struct SDreamLocTranslateWindow::FRunState
{
	TAtomic<bool> Cancel{ false };
	FCriticalSection Lock;
	FDreamLocTranslateProgress Progress;
	double Started = 0.0;
};

namespace DreamLocTranslateWindowLocal
{
	const FName ColumnFile(TEXT("File"));
	const FName ColumnKey(TEXT("Key"));
	const FName ColumnSource(TEXT("Source"));
	const FName ColumnOld(TEXT("Old"));
	const FName ColumnNew(TEXT("New"));
	const FName ColumnCheck(TEXT("Check"));
	const FName ColumnBackend(TEXT("Backend"));
	const FName ColumnActions(TEXT("Actions"));

	/** A text on one line of a table cell: line breaks shown, not followed. */
	FString OneLine(const FString& InText)
	{
		return InText.Replace(TEXT("\r\n"), TEXT("⏎")).Replace(TEXT("\n"), TEXT("⏎"));
	}

	FText StatusOf(const FDreamLocTranslateRowItem& InItem)
	{
		if (InItem.bBusy)
		{
			return LOCTEXT("StatusBusy", "translating again");
		}
		if (InItem.Review == TEXT("passed"))
		{
			return LOCTEXT("StatusPassed", "passed");
		}
		if (InItem.Review == TEXT("rejected"))
		{
			return LOCTEXT("StatusRejected", "rejected");
		}
		if (InItem.Review == TEXT("edited"))
		{
			return LOCTEXT("StatusEdited", "edited");
		}
		switch (InItem.Row.Status)
		{
		case EDreamLocTranslateStatus::Translated: return LOCTEXT("StatusTranslated", "translated");
		case EDreamLocTranslateStatus::Failed: return LOCTEXT("StatusFailed", "failed");
		default: return LOCTEXT("StatusSkipped", "skipped");
		}
	}

	/** Which fields a profile of that type has. */
	bool Shows(const FString& InType, FName InField)
	{
		const bool bOpenAI = InType.Equals(TEXT("OpenAI"), ESearchCase::IgnoreCase);
		const bool bKey = bOpenAI || InType.Equals(TEXT("DeepL"), ESearchCase::IgnoreCase) || InType.Equals(TEXT("Google"), ESearchCase::IgnoreCase);
		const bool bTencent = InType.Equals(TEXT("Tencent"), ESearchCase::IgnoreCase);
		const bool bApp = bTencent || InType.Equals(TEXT("Baidu"), ESearchCase::IgnoreCase) || InType.Equals(TEXT("Youdao"), ESearchCase::IgnoreCase);
		if (InField == TEXT("Model") || InField == TEXT("Temperature") || InField == TEXT("JsonMode")) { return bOpenAI; }
		if (InField == TEXT("ApiKey") || InField == TEXT("ApiKeyEnv")) { return bKey; }
		if (InField == TEXT("AppId") || InField == TEXT("AppIdEnv") || InField == TEXT("Secret") || InField == TEXT("SecretEnv")) { return bApp; }
		if (InField == TEXT("Region")) { return bTencent; }
		return true;
	}

	FText AppIdLabel(const FString& InType)
	{
		if (InType.Equals(TEXT("Tencent"), ESearchCase::IgnoreCase)) { return LOCTEXT("TencentSecretId", "SecretId"); }
		if (InType.Equals(TEXT("Youdao"), ESearchCase::IgnoreCase)) { return LOCTEXT("YoudaoAppKey", "App key"); }
		return LOCTEXT("BaiduAppId", "App ID");
	}

	FText SecretLabel(const FString& InType)
	{
		if (InType.Equals(TEXT("Tencent"), ESearchCase::IgnoreCase)) { return LOCTEXT("TencentSecretKey", "SecretKey"); }
		if (InType.Equals(TEXT("Youdao"), ESearchCase::IgnoreCase)) { return LOCTEXT("YoudaoAppSecret", "App secret"); }
		return LOCTEXT("BaiduKey", "Key");
	}

	class SResultRow : public SMultiColumnTableRow<TSharedPtr<FDreamLocTranslateRowItem>>
	{
	public:
		SLATE_BEGIN_ARGS(SResultRow) {}
			SLATE_ARGUMENT(TSharedPtr<FDreamLocTranslateRowItem>, Item)
			SLATE_ARGUMENT(TWeakPtr<SDreamLocTranslateWindow>, Window)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwner)
		{
			Item = InArgs._Item;
			Window = InArgs._Window;
			SMultiColumnTableRow<TSharedPtr<FDreamLocTranslateRowItem>>::Construct(FSuperRowType::FArguments().Padding(FMargin(0.0f, 1.0f)), InOwner);
		}

		virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& InColumn) override
		{
			const FDreamLocTranslateRow& Row = Item->Row;
			auto Cell = [](const FString& InText) -> TSharedRef<SWidget>
			{
				return SNew(SBox).Padding(FMargin(4.0f, 2.0f)).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(FText::FromString(OneLine(InText))).ToolTipText(FText::FromString(InText))
				];
			};
			if (InColumn == ColumnFile) { return Cell(Row.Culture / Row.File); }
			if (InColumn == ColumnKey) { return Cell(Row.Namespace.IsEmpty() ? Row.Key : Row.Namespace + TEXT(" / ") + Row.Key); }
			if (InColumn == ColumnSource) { return Cell(Row.Source); }
			if (InColumn == ColumnOld) { return Cell(Row.OldText); }
			if (InColumn == ColumnBackend) { return Cell(Row.Backend); }
			if (InColumn == ColumnNew)
			{
				const TSharedPtr<FDreamLocTranslateRowItem> Shown = Item;
				return SNew(SBox).Padding(FMargin(2.0f, 1.0f))
				[
					SNew(SEditableTextBox)
					.Text_Lambda([Shown]() { return FText::FromString(Shown->Row.NewText); })
					.HintText(FText::FromString(OneLine(Row.RejectedText)))
					.IsEnabled_Lambda([Shown]() { return !Shown->bBusy; })
					.OnTextCommitted_Lambda([this](const FText& InText, ETextCommit::Type InCommit)
					{
						const FString Text = InText.ToString();
						const TSharedPtr<SDreamLocTranslateWindow> Owner = Window.Pin();
						if (InCommit != ETextCommit::OnCleared && Owner.IsValid() && !Text.Equals(Item->Row.NewText, ESearchCase::CaseSensitive))
						{
							Owner->EditRow(Item, Text);
						}
					})
				];
			}
			if (InColumn == ColumnCheck)
			{
				const TSharedPtr<FDreamLocTranslateRowItem> Shown = Item;
				auto Says = [Shown]()
				{
					const FDreamLocTranslateRow& Now = Shown->Row;
					FString Detail = Now.Problems;
					if (!Now.Reason.IsEmpty())
					{
						Detail = Detail.IsEmpty() ? Now.Reason : Now.Reason + TEXT(" -- ") + Detail;
					}
					return Detail.IsEmpty() ? StatusOf(*Shown) : FText::Format(LOCTEXT("StatusWithDetail", "{0}: {1}"), StatusOf(*Shown), FText::FromString(OneLine(Detail)));
				};
				return SNew(SBox).Padding(FMargin(4.0f, 2.0f)).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text_Lambda(Says)
					.ToolTipText_Lambda(Says)
					.ColorAndOpacity_Lambda([Shown]()
					{
						if (Shown->Row.Status == EDreamLocTranslateStatus::Failed && Shown->Review.IsEmpty()) { return FSlateColor(FStyleColors::Error); }
						if (Shown->Row.Status == EDreamLocTranslateStatus::Skipped && Shown->Review.IsEmpty()) { return FSlateColor::UseSubduedForeground(); }
						if (!Shown->Row.Problems.IsEmpty()) { return FSlateColor(FStyleColors::Warning); }
						return FSlateColor::UseForeground();
					})
				];
			}
			if (InColumn == ColumnActions)
			{
				const TSharedPtr<FDreamLocTranslateRowItem> Shown = Item;
				const TWeakPtr<SDreamLocTranslateWindow> Owner = Window;
				auto Button = [&Shown](const TCHAR* InTag, const FText& InLabel, const FText& InTip, TFunction<bool()> InCanDo, TFunction<void()> InDo) -> TSharedRef<SWidget>
				{
					// On press, and without taking focus: a press can then not be undone before its release -- by the window being
					// activated (which releases every pointer capture), by focus moving, by the row being laid out again.
					return SNew(SButton)
						.AddMetaData<FTagMetaData>(FTagMetaData(InTag))
						.Text(InLabel)
						.ToolTipText(InTip)
						.ContentPadding(FMargin(4.0f, 0.0f))
						.ClickMethod(EButtonClickMethod::MouseDown)
						.IsFocusable(false)
						.IsEnabled_Lambda([Shown, InCanDo]() { return !Shown->bBusy && InCanDo(); })
						.OnClicked_Lambda([InDo]() { InDo(); return FReply::Handled(); });
				};
				return SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(1.0f)
					[
						Button(TEXT("DreamLocTranslate.Pass"), LOCTEXT("Pass", "Pass"),LOCTEXT("PassTip", "Mark the translation reviewed (state done)"),
							[Shown]() { return !Shown->Row.NewText.IsEmpty() && Shown->Review != TEXT("passed"); },
							[Shown, Owner]() { if (TSharedPtr<SDreamLocTranslateWindow> Pinned = Owner.Pin()) { Pinned->PassRows({ Shown }); } })
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(1.0f)
					[
						Button(TEXT("DreamLocTranslate.Reject"), LOCTEXT("Reject", "Reject"),LOCTEXT("RejectTip", "Clear the translation"),
							[Shown]() { return !Shown->Row.NewText.IsEmpty() && Shown->Review != TEXT("passed"); },
							[Shown, Owner]() { if (TSharedPtr<SDreamLocTranslateWindow> Pinned = Owner.Pin()) { Pinned->RejectRows({ Shown }); } })
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(1.0f)
					[
						Button(TEXT("DreamLocTranslate.Again"), LOCTEXT("Again", "Again"),LOCTEXT("AgainTip", "Translate this entry again"),
							[Shown, Owner]() { const TSharedPtr<SDreamLocTranslateWindow> Pinned = Owner.Pin(); return Pinned.IsValid() && !Pinned->IsRunning() && Shown->Review != TEXT("passed"); },
							[Shown, Owner]() { if (TSharedPtr<SDreamLocTranslateWindow> Pinned = Owner.Pin()) { Pinned->RetranslateRow(Shown); } })
					];
			}
			return SNullWidget::NullWidget;
		}

	private:
		TSharedPtr<FDreamLocTranslateRowItem> Item;
		TWeakPtr<SDreamLocTranslateWindow> Window;
	};
}

// ---------------------------------------------------------------------------------------------------------- set up

void SDreamLocTranslateWindow::Construct(const FArguments& InArgs)
{
	DLocDirectory = InArgs._DLocDirectory.IsEmpty() ? FDreamLocTextSource::GetProjectDirectory() : InArgs._DLocDirectory;
	OwnerDirectory = InArgs._OwnerDirectory.IsEmpty() ? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()) : InArgs._OwnerDirectory;
	for (const FString& Type : DreamLocTranslators::GetTypes())
	{
		TypeChoices.Add(MakeShared<FString>(Type));
	}

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 8.0f, 8.0f, 4.0f).HAlign(HAlign_Left)
		[
			SNew(SSegmentedControl<int32>)
			.Value_Lambda([this]() { return Page; })
			.OnValueChanged_Lambda([this](int32 InPage)
			{
				Page = InPage;
				Pages->SetActiveWidgetIndex(InPage);
			})
			+ SSegmentedControl<int32>::Slot(0).Text(LOCTEXT("TranslatePage", "Translate"))
			+ SSegmentedControl<int32>::Slot(1).Text(LOCTEXT("TranslatorsPage", "Translators"))
		]
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(8.0f, 0.0f, 8.0f, 8.0f)
		[
			SAssignNew(Pages, SWidgetSwitcher)
			+ SWidgetSwitcher::Slot()[MakeTranslatePage()]
			+ SWidgetSwitcher::Slot()[MakeBackendsPage()]
		]
	];
	Refresh();
}

SDreamLocTranslateWindow::~SDreamLocTranslateWindow()
{
	if (Running.IsValid())
	{
		Running->Cancel = true;
	}
}

void SDreamLocTranslateWindow::SetDirectories(const FString& InDLocDirectory, const FString& InOwnerDirectory)
{
	DLocDirectory = InDLocDirectory;
	OwnerDirectory = InOwnerDirectory;
	RowItems.Reset();
	if (ResultsList.IsValid())
	{
		ResultsList->RequestListRefresh();
	}
	Refresh();
}

void SDreamLocTranslateWindow::Refresh()
{
	Settings = FDreamLocSettings::LoadFromDirectory(DLocDirectory);
	TArray<FString> Warnings;
	const TArray<FDreamLocTranslateProfile> Loaded = DreamLocTranslateConfig::LoadProfiles(&Warnings);
	const FString SelectedName = SelectedProfile.IsValid() ? SelectedProfile->Name : FString();
	ProfileItems.Reset();
	SelectedProfile.Reset();
	for (const FDreamLocTranslateProfile& Profile : Loaded)
	{
		ProfileItems.Add(MakeShared<FDreamLocTranslateProfile>(Profile));
		if (Profile.Name == SelectedName)
		{
			SelectedProfile = ProfileItems.Last();
		}
	}
	if (!SelectedProfile.IsValid() && ProfileItems.Num() > 0)
	{
		SelectedProfile = ProfileItems[0];
	}
	ProfilesText = Warnings.Num() > 0 ? FText::FromString(FString::Join(Warnings, TEXT("\n")))
		: FText::Format(LOCTEXT("ProfilesRead", "{0} {0}|plural(one=profile,other=profiles) in {1}"), ProfileItems.Num(),
			FText::FromString(DreamLocTranslateConfig::GetUserConfigPath()));
	if (ProfileList.IsValid())
	{
		ProfileList->RequestListRefresh();
		if (SelectedProfile.IsValid())
		{
			ProfileList->SetSelection(SelectedProfile, ESelectInfo::Direct);
		}
	}

	AllCultures = Settings.TargetCultures;
	TSet<FString> Kept;
	for (const FString& Culture : AllCultures)
	{
		if (CheckedCultures.Contains(Culture))
		{
			Kept.Add(Culture);
		}
	}
	if (Kept.Num() == 0)
	{
		Kept.Append(AllCultures);
	}
	CheckedCultures = MoveTemp(Kept);
	RebuildCultures();
	RebuildProfileChoices();
	RebuildFileTree();
	LoadProjectSettings();
}

void SDreamLocTranslateWindow::RebuildCultures()
{
	if (!CultureBox.IsValid())
	{
		return;
	}
	CultureBox->ClearChildren();
	if (AllCultures.IsEmpty())
	{
		CultureBox->AddSlot()[SNew(STextBlock).Text(LOCTEXT("NoCultures", "dloc.ini names no TargetCultures"))];
	}
	for (const FString& Culture : AllCultures)
	{
		CultureBox->AddSlot().Padding(0.0f, 0.0f, 12.0f, 0.0f)
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([this, Culture]() { return CheckedCultures.Contains(Culture) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this, Culture](ECheckBoxState InState)
			{
				if (InState == ECheckBoxState::Checked)
				{
					CheckedCultures.Add(Culture);
				}
				else
				{
					CheckedCultures.Remove(Culture);
				}
			})
			[
				SNew(STextBlock).Text(FText::FromString(Culture))
			]
		];
	}
}

void SDreamLocTranslateWindow::RebuildProfileChoices()
{
	const FString Previous = ChosenProfile.IsValid() ? *ChosenProfile : FString();
	ProfileChoices.Reset();
	ProfileChoices.Add(MakeShared<FString>());
	ChosenProfile = ProfileChoices[0];
	for (const TSharedPtr<FDreamLocTranslateProfile>& Profile : ProfileItems)
	{
		ProfileChoices.Add(MakeShared<FString>(Profile->Name));
		if (Profile->Name == Previous)
		{
			ChosenProfile = ProfileChoices.Last();
		}
	}
	if (ProfileCombo.IsValid())
	{
		ProfileCombo->RefreshOptions();
		ProfileCombo->SetSelectedItem(ChosenProfile);
	}
}

FText SDreamLocTranslateWindow::ProfileLabel(const FString& InName) const
{
	if (InName.IsEmpty())
	{
		return LOCTEXT("EachCulturesOwn", "Each culture's own ([Translate] in dloc.ini)");
	}
	for (const TSharedPtr<FDreamLocTranslateProfile>& Profile : ProfileItems)
	{
		if (Profile->Name == InName)
		{
			return FText::Format(LOCTEXT("ProfileLabel", "{0} ({1}, {2})"), FText::FromString(Profile->Name), FText::FromString(Profile->Type),
				Profile->IsLocal() ? LOCTEXT("Local", "local") : LOCTEXT("Cloud", "cloud"));
		}
	}
	return FText::FromString(InName);
}

// ---------------------------------------------------------------------------------------------------------- the translate page

TSharedRef<SWidget> SDreamLocTranslateWindow::MakeTranslatePage()
{
	using namespace DreamLocTranslateWindowLocal;
	auto Label = [](const FText& InText) -> TSharedRef<SWidget>
	{
		return SNew(SBox).MinDesiredWidth(90.0f).VAlign(VAlign_Center)[SNew(STextBlock).Text(InText)];
	};
	auto Toggle = [](const FText& InText, bool* InFlag) -> TSharedRef<SWidget>
	{
		return SNew(SCheckBox)
			.IsChecked_Lambda([InFlag]() { return *InFlag ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([InFlag](ECheckBoxState InState) { *InFlag = InState == ECheckBoxState::Checked; })
			[
				SNew(STextBlock).Text(InText)
			];
	};

	TSharedRef<SHeaderRow> Header = SNew(SHeaderRow)
		+ SHeaderRow::Column(ColumnFile).DefaultLabel(LOCTEXT("ColumnFile", "File")).FillWidth(0.13f)
		+ SHeaderRow::Column(ColumnKey).DefaultLabel(LOCTEXT("ColumnKey", "Key")).FillWidth(0.13f)
		+ SHeaderRow::Column(ColumnSource).DefaultLabel(LOCTEXT("ColumnSource", "Source")).FillWidth(0.2f)
		+ SHeaderRow::Column(ColumnOld).DefaultLabel(LOCTEXT("ColumnOld", "Old translation")).FillWidth(0.15f)
		+ SHeaderRow::Column(ColumnNew).DefaultLabel(LOCTEXT("ColumnNew", "New translation")).FillWidth(0.22f)
		+ SHeaderRow::Column(ColumnCheck).DefaultLabel(LOCTEXT("ColumnCheck", "Checks")).FillWidth(0.17f)
		// Fixed: a row lays its fill columns out by their share alone, so a short name in a narrow window would be cut.
		+ SHeaderRow::Column(ColumnBackend).DefaultLabel(LOCTEXT("ColumnBackend", "Translator")).FixedWidth(120.0f)
		+ SHeaderRow::Column(ColumnActions).DefaultLabel(FText::GetEmpty()).FixedWidth(224.0f);

	return SNew(SSplitter)
		.Orientation(Orient_Vertical)
		+ SSplitter::Slot().Value(0.38f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()[Label(LOCTEXT("Cultures", "Cultures"))]
				+ SHorizontalBox::Slot().FillWidth(1.0f)[SAssignNew(CultureBox, SWrapBox).UseAllottedSize(true)]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()[Label(LOCTEXT("Entries", "Entries"))]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 12.0f, 0.0f)[Toggle(LOCTEXT("Untranslated", "Untranslated"), &bIncludeUntranslated)]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 12.0f, 0.0f)[Toggle(LOCTEXT("Stale", "Stale (source changed)"), &bIncludeStale)]
				+ SHorizontalBox::Slot().AutoWidth()[Toggle(LOCTEXT("Machine", "Machine translated"), &bIncludeMachine)]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()[Label(LOCTEXT("Translator", "Translator"))]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SAssignNew(ProfileCombo, SComboBox<TSharedPtr<FString>>)
					.OptionsSource(&ProfileChoices)
					.OnGenerateWidget_Lambda([this](TSharedPtr<FString> InItem) -> TSharedRef<SWidget>
					{
						return SNew(STextBlock).Text(ProfileLabel(InItem.IsValid() ? *InItem : FString()));
					})
					.OnSelectionChanged_Lambda([this](TSharedPtr<FString> InItem, ESelectInfo::Type)
					{
						if (InItem.IsValid())
						{
							ChosenProfile = InItem;
						}
					})
					[
						SNew(STextBlock).Text_Lambda([this]() { return ProfileLabel(ChosenProfile.IsValid() ? *ChosenProfile : FString()); })
					]
				]
			]
			+ SVerticalBox::Slot().FillHeight(1.0f).Padding(0.0f, 4.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).MinDesiredWidth(90.0f).VAlign(VAlign_Top)[SNew(STextBlock).Text(LOCTEXT("Files", "Files"))]]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNew(SBorder).BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
					[
						SAssignNew(FileTree, STreeView<TSharedPtr<FDreamLocTranslateFileNode>>)
						.TreeItemsSource(&FileRoots)
						.SelectionMode(ESelectionMode::None)
						.OnGenerateRow(this, &SDreamLocTranslateWindow::MakeFileWidget)
						.OnGetChildren_Lambda([](TSharedPtr<FDreamLocTranslateFileNode> InNode, TArray<TSharedPtr<FDreamLocTranslateFileNode>>& OutChildren)
						{
							OutChildren = InNode->Children;
						})
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(SButton).Text(LOCTEXT("Estimate", "Estimate")).IsEnabled_Lambda([this]() { return !IsRunning(); })
					.OnClicked_Lambda([this]() { RunEstimate(); return FReply::Handled(); })
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(SButton).Text(LOCTEXT("TranslateButton", "Translate")).IsEnabled_Lambda([this]() { return !IsRunning(); })
					.OnClicked(this, &SDreamLocTranslateWindow::OnTranslateClicked)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(SButton).Text(LOCTEXT("Cancel", "Cancel")).IsEnabled_Lambda([this]() { return IsRunning(); })
					.OnClicked_Lambda([this]() { CancelTranslate(); return FReply::Handled(); })
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text_Lambda([this]() { return EstimateText; }).AutoWrapText(true)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(0.3f).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(SProgressBar).Percent(this, &SDreamLocTranslateWindow::GetProgressFraction)
				]
				+ SHorizontalBox::Slot().FillWidth(0.7f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(this, &SDreamLocTranslateWindow::GetProgressText).AutoWrapText(true)
				]
			]
		]
		+ SSplitter::Slot().Value(0.62f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(SButton).Text(LOCTEXT("PassSelected", "Pass selected"))
					.ToolTipText(LOCTEXT("PassSelectedTip", "Mark the selected rows' translations reviewed (state done)"))
					.OnClicked_Lambda([this]() { PassRows(ResultsList->GetSelectedItems()); return FReply::Handled(); })
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 12.0f, 0.0f)
				[
					SNew(SButton).Text(LOCTEXT("RejectSelected", "Reject selected"))
					.ToolTipText(LOCTEXT("RejectSelectedTip", "Clear the selected rows' translations"))
					.OnClicked_Lambda([this]() { RejectRows(ResultsList->GetSelectedItems()); return FReply::Handled(); })
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton).Text(LOCTEXT("LoadForReview", "Load machine translations"))
					.ToolTipText(LOCTEXT("LoadForReviewTip", "Show the chosen cultures' and files' machine translations, to review them without a new run"))
					.IsEnabled_Lambda([this]() { return !IsRunning(); })
					.OnClicked_Lambda([this]() { LoadForReview(); return FReply::Handled(); })
				]
			]
			+ SVerticalBox::Slot().FillHeight(1.0f)
			[
				SAssignNew(ResultsList, SListView<TSharedPtr<FDreamLocTranslateRowItem>>)
				.ListItemsSource(&RowItems)
				.SelectionMode(ESelectionMode::Multi)
				.OnGenerateRow(this, &SDreamLocTranslateWindow::MakeRowWidget)
				.OnMouseButtonDoubleClick_Lambda([this](TSharedPtr<FDreamLocTranslateRowItem> InItem) { OpenRowInEditor(InItem); })
				.HeaderRow(Header)
			]
		];
}

TSharedRef<ITableRow> SDreamLocTranslateWindow::MakeRowWidget(TSharedPtr<FDreamLocTranslateRowItem> InItem, const TSharedRef<STableViewBase>& InOwner)
{
	return SNew(DreamLocTranslateWindowLocal::SResultRow, InOwner).Item(InItem).Window(SharedThis(this));
}

TSharedRef<ITableRow> SDreamLocTranslateWindow::MakeFileWidget(TSharedPtr<FDreamLocTranslateFileNode> InNode, const TSharedRef<STableViewBase>& InOwner)
{
	return SNew(STableRow<TSharedPtr<FDreamLocTranslateFileNode>>, InOwner)
	[
		SNew(SCheckBox)
		.IsChecked_Lambda([InNode]() { return InNode->Checked; })
		.OnCheckStateChanged_Lambda([this, InNode](ECheckBoxState InState)
		{
			SetNodeChecked(InNode, InState == ECheckBoxState::Unchecked ? ECheckBoxState::Unchecked : ECheckBoxState::Checked);
		})
		[
			SNew(STextBlock).Text(FText::FromString(InNode->Name))
		]
	];
}

void SDreamLocTranslateWindow::RebuildFileTree()
{
	FileRoots.Reset();
	TSet<FString> Stems;
	for (const TPair<FString, TMap<FString, FString>>& Culture : DreamLocSync::ReadFiles(DLocDirectory, AllCultures))
	{
		for (const TPair<FString, FString>& File : Culture.Value)
		{
			if (!FPaths::GetCleanFilename(File.Key).StartsWith(TEXT("_")))
			{
				Stems.Add(FPaths::GetBaseFilename(File.Key, /*bRemovePath*/ false));
			}
		}
	}
	TArray<FString> Sorted = Stems.Array();
	Sorted.Sort();
	TMap<FString, TSharedPtr<FDreamLocTranslateFileNode>> Folders;
	for (const FString& Stem : Sorted)
	{
		TArray<FString> Parts;
		Stem.ParseIntoArray(Parts, TEXT("/"));
		TSharedPtr<FDreamLocTranslateFileNode> Parent;
		FString Path;
		for (int32 Index = 0; Index < Parts.Num(); ++Index)
		{
			const bool bFile = Index == Parts.Num() - 1;
			Path += Parts[Index];
			TSharedPtr<FDreamLocTranslateFileNode> Node;
			if (!bFile)
			{
				Path += TEXT("/");
				if (const TSharedPtr<FDreamLocTranslateFileNode>* Found = Folders.Find(Path))
				{
					Parent = *Found;
					continue;
				}
			}
			Node = MakeShared<FDreamLocTranslateFileNode>();
			Node->Name = Parts[Index];
			Node->Path = Path;
			Node->bFolder = !bFile;
			Node->Parent = Parent;
			(Parent.IsValid() ? Parent->Children : FileRoots).Add(Node);
			if (!bFile)
			{
				Folders.Add(Path, Node);
				Parent = Node;
			}
		}
	}
	// Folders before files, as a file browser shows them.
	TFunction<void(TArray<TSharedPtr<FDreamLocTranslateFileNode>>&)> Order = [&Order](TArray<TSharedPtr<FDreamLocTranslateFileNode>>& InNodes)
	{
		InNodes.StableSort([](const TSharedPtr<FDreamLocTranslateFileNode>& A, const TSharedPtr<FDreamLocTranslateFileNode>& B) { return A->bFolder && !B->bFolder; });
		for (TSharedPtr<FDreamLocTranslateFileNode>& Node : InNodes)
		{
			Order(Node->Children);
		}
	};
	Order(FileRoots);
	if (FileTree.IsValid())
	{
		FileTree->RequestTreeRefresh();
		for (const TPair<FString, TSharedPtr<FDreamLocTranslateFileNode>>& Folder : Folders)
		{
			FileTree->SetItemExpansion(Folder.Value, true);
		}
	}
}

void SDreamLocTranslateWindow::SetNodeChecked(const TSharedPtr<FDreamLocTranslateFileNode>& InNode, ECheckBoxState InState)
{
	TFunction<void(const TSharedPtr<FDreamLocTranslateFileNode>&)> Down = [&Down, InState](const TSharedPtr<FDreamLocTranslateFileNode>& InAt)
	{
		InAt->Checked = InState;
		for (const TSharedPtr<FDreamLocTranslateFileNode>& Child : InAt->Children)
		{
			Down(Child);
		}
	};
	Down(InNode);
	for (TSharedPtr<FDreamLocTranslateFileNode> Parent = InNode->Parent.Pin(); Parent.IsValid(); Parent = Parent->Parent.Pin())
	{
		bool bAll = true;
		bool bNone = true;
		for (const TSharedPtr<FDreamLocTranslateFileNode>& Child : Parent->Children)
		{
			bAll &= Child->Checked == ECheckBoxState::Checked;
			bNone &= Child->Checked == ECheckBoxState::Unchecked;
		}
		Parent->Checked = bAll ? ECheckBoxState::Checked : bNone ? ECheckBoxState::Unchecked : ECheckBoxState::Undetermined;
	}
}

void SDreamLocTranslateWindow::CollectCheckedFiles(const TSharedPtr<FDreamLocTranslateFileNode>& InNode, TArray<FString>& OutFiles, bool& bOutAllChecked,
	bool& bOutAnyChecked) const
{
	if (!InNode->bFolder)
	{
		if (InNode->Checked == ECheckBoxState::Checked)
		{
			OutFiles.Add(InNode->Path);
			bOutAnyChecked = true;
		}
		else
		{
			bOutAllChecked = false;
		}
		return;
	}
	for (const TSharedPtr<FDreamLocTranslateFileNode>& Child : InNode->Children)
	{
		CollectCheckedFiles(Child, OutFiles, bOutAllChecked, bOutAnyChecked);
	}
}

// ---------------------------------------------------------------------------------------------------------- runs

void SDreamLocTranslateWindow::SetCultures(const TArray<FString>& InCultures)
{
	CheckedCultures.Reset();
	CheckedCultures.Append(InCultures);
}

void SDreamLocTranslateWindow::SetProfile(const FString& InProfile)
{
	for (const TSharedPtr<FString>& Choice : ProfileChoices)
	{
		if (*Choice == InProfile)
		{
			ChosenProfile = Choice;
			if (ProfileCombo.IsValid())
			{
				ProfileCombo->SetSelectedItem(Choice);
			}
			return;
		}
	}
}

void SDreamLocTranslateWindow::SetIncludeMachine(bool bInInclude)
{
	bIncludeMachine = bInInclude;
}

FDreamLocTranslateRequest SDreamLocTranslateWindow::MakeRequest(bool& bOutNoFiles) const
{
	FDreamLocTranslateRequest Request;
	Request.DLocDirectory = DLocDirectory;
	Request.OwnerDirectory = OwnerDirectory;
	for (const FString& Culture : AllCultures)
	{
		if (CheckedCultures.Contains(Culture))
		{
			Request.Cultures.Add(Culture);
		}
	}
	bool bAll = true;
	bool bAny = false;
	TArray<FString> Files;
	for (const TSharedPtr<FDreamLocTranslateFileNode>& Root : FileRoots)
	{
		CollectCheckedFiles(Root, Files, bAll, bAny);
	}
	bOutNoFiles = FileRoots.Num() > 0 && !bAny;
	if (!bAll)
	{
		Request.Files = MoveTemp(Files);
	}
	Request.bIncludeUntranslated = bIncludeUntranslated;
	Request.bIncludeStale = bIncludeStale;
	Request.bIncludeMachine = bIncludeMachine;
	Request.Profile = ChosenProfile.IsValid() ? *ChosenProfile : FString();
	for (const TSharedPtr<FDreamLocTranslateProfile>& Profile : ProfileItems)
	{
		Request.Profiles.Add(*Profile);
	}
	Request.Settings = Settings;
	return Request;
}

FDreamLocTranslateEstimate SDreamLocTranslateWindow::RunEstimate()
{
	bool bNoFiles = false;
	const FDreamLocTranslateRequest Request = MakeRequest(bNoFiles);
	FDreamLocTranslateEstimate Estimate;
	if (Request.Cultures.IsEmpty())
	{
		Estimate.Errors.Add(LOCTEXT("NoCultureChosen", "Choose a culture first.").ToString());
	}
	else if (bNoFiles)
	{
		Estimate.Errors.Add(LOCTEXT("NoFileChosen", "Choose a file first.").ToString());
	}
	else
	{
		Estimate = DreamLocTranslateFlow::Estimate(Request);
	}
	if (Request.Cultures.IsEmpty() || bNoFiles)
	{
		EstimateText = FText::FromString(FString::Join(Estimate.Errors, TEXT("\n")));
		return Estimate;
	}
	// The entries and characters stand without a translator; the requests and tokens need one.
	FFormatNamedArguments Args;
	Args.Add(TEXT("Entries"), Estimate.Entries);
	Args.Add(TEXT("Distinct"), Estimate.Distinct);
	Args.Add(TEXT("Characters"), Estimate.Characters);
	Args.Add(TEXT("Tokens"), Estimate.Tokens);
	Args.Add(TEXT("Batches"), Estimate.Batches);
	Args.Add(TEXT("Skipped"), Estimate.Skipped);
	const FText Counts = Estimate.bTranslatorKnown
		? FText::Format(LOCTEXT("EstimateResult",
			"{Entries} {Entries}|plural(one=entry,other=entries): {Distinct} {Distinct}|plural(one=text,other=texts) to send, {Characters} {Characters}|plural(one=character,other=characters), about {Tokens} {Tokens}|plural(one=token,other=tokens) for a large model, {Batches} {Batches}|plural(one=request,other=requests); {Skipped} left out"),
			Args)
		: FText::Format(LOCTEXT("EstimateWithoutTranslator",
			"{Entries} {Entries}|plural(one=entry,other=entries): {Distinct} {Distinct}|plural(one=text,other=texts) to send, {Characters} {Characters}|plural(one=character,other=characters); choose a translator to count the requests and tokens"),
			Args);
	EstimateText = Estimate.Errors.IsEmpty() ? Counts
		: FText::Format(LOCTEXT("EstimateAndErrors", "{0}\n{1}"), Counts, FText::FromString(FString::Join(Estimate.Errors, TEXT("\n"))));
	return Estimate;
}

FReply SDreamLocTranslateWindow::OnTranslateClicked()
{
	const FDreamLocTranslateEstimate Estimate = RunEstimate();
	if (Estimate.Errors.Num() > 0 || Estimate.Distinct == 0)
	{
		if (Estimate.Errors.IsEmpty())
		{
			EstimateText = LOCTEXT("NothingToSend", "Nothing to translate: every chosen entry is translated, or left out.");
		}
		return FReply::Handled();
	}
	const EAppReturnType::Type Answer = FMessageDialog::Open(EAppMsgType::OkCancel,
		FText::Format(LOCTEXT("ConfirmSend", "{0}\n\nSend them?"), EstimateText), LOCTEXT("ConfirmTitle", "DreamLoc Translate"));
	if (Answer == EAppReturnType::Ok)
	{
		StartTranslate();
	}
	return FReply::Handled();
}

bool SDreamLocTranslateWindow::StartTranslate()
{
	if (IsRunning())
	{
		return false;
	}
	bool bNoFiles = false;
	const FDreamLocTranslateRequest Request = MakeRequest(bNoFiles);
	if (Request.Cultures.IsEmpty() || bNoFiles)
	{
		StatusText = Request.Cultures.IsEmpty() ? LOCTEXT("NoCultureChosen", "Choose a culture first.") : LOCTEXT("NoFileChosen", "Choose a file first.");
		return false;
	}
	RunRequest(Request, nullptr);
	return true;
}

void SDreamLocTranslateWindow::CancelTranslate()
{
	if (Running.IsValid())
	{
		Running->Cancel = true;
	}
}

bool SDreamLocTranslateWindow::IsRunning() const
{
	return Running.IsValid();
}

void SDreamLocTranslateWindow::RunRequest(const FDreamLocTranslateRequest& InRequest, TSharedPtr<FDreamLocTranslateRowItem> InOnlyRow)
{
	const TSharedRef<FRunState> State = MakeShared<FRunState>();
	State->Started = FPlatformTime::Seconds();
	Running = State;
	StatusText = LOCTEXT("Translating", "Translating...");
	const TWeakPtr<SDreamLocTranslateWindow> Weak = SharedThis(this);
	Async(EAsyncExecution::Thread, [State, InRequest, Weak, InOnlyRow]()
	{
		FDreamLocTranslateResult Result = DreamLocTranslateFlow::Run(InRequest, [State](const FDreamLocTranslateProgress& InProgress)
		{
			FScopeLock Lock(&State->Lock);
			State->Progress = InProgress;
		}, State->Cancel);
		AsyncTask(ENamedThreads::GameThread, [Weak, Result = MoveTemp(Result), InOnlyRow]()
		{
			if (const TSharedPtr<SDreamLocTranslateWindow> Window = Weak.Pin())
			{
				Window->OnRunFinished(Result, InOnlyRow);
			}
		});
	});
}

void SDreamLocTranslateWindow::OnRunFinished(const FDreamLocTranslateResult& InResult, TSharedPtr<FDreamLocTranslateRowItem> InOnlyRow)
{
	Running.Reset();
	LastResult = InResult;
	if (InOnlyRow.IsValid())
	{
		InOnlyRow->bBusy = false;
		for (const FDreamLocTranslateRow& Row : InResult.Rows)
		{
			if (Row.Culture == InOnlyRow->Row.Culture && Row.Namespace == InOnlyRow->Row.Namespace && Row.Key == InOnlyRow->Row.Key)
			{
				InOnlyRow->Row = Row;
				InOnlyRow->Review.Reset();
			}
		}
	}
	else
	{
		SetRows(InResult.Rows);
	}
	if (InResult.Errors.Num() > 0)
	{
		StatusText = FText::FromString(FString::Join(InResult.Errors, TEXT("\n")));
	}
	else
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("Translated"), InResult.Translated);
		Args.Add(TEXT("Failed"), InResult.Failed);
		Args.Add(TEXT("Skipped"), InResult.Skipped);
		Args.Add(TEXT("Requests"), InResult.Requests);
		Args.Add(TEXT("Seconds"), FText::AsNumber(FMath::RoundToDouble(InResult.Seconds * 10.0) / 10.0));
		Args.Add(TEXT("Tokens"), InResult.PromptTokens + InResult.CompletionTokens);
		Args.Add(TEXT("Cancelled"), InResult.bCancelled ? LOCTEXT("WasCancelled", " (cancelled)") : FText::GetEmpty());
		Args.Add(TEXT("Report"), FText::FromString(InResult.ReportPaths.Num() > 0 ? InResult.ReportPaths.Last() : FString()));
		StatusText = FText::Format(LOCTEXT("RunFinished",
			"{Translated} translated, {Failed} failed, {Skipped} skipped{Cancelled}; {Requests} {Requests}|plural(one=request,other=requests), {Tokens} {Tokens}|plural(one=token,other=tokens), {Seconds} s. Report: {Report}"),
			Args);
	}
	if (ResultsList.IsValid())
	{
		ResultsList->RequestListRefresh();
	}
}

void SDreamLocTranslateWindow::SetRows(TConstArrayView<FDreamLocTranslateRow> InRows)
{
	RowItems.Reset();
	for (const FDreamLocTranslateRow& Row : InRows)
	{
		const TSharedPtr<FDreamLocTranslateRowItem> Item = MakeShared<FDreamLocTranslateRowItem>();
		Item->Row = Row;
		RowItems.Add(Item);
	}
	if (ResultsList.IsValid())
	{
		ResultsList->RequestListRefresh();
	}
}

void SDreamLocTranslateWindow::LoadForReview()
{
	bool bNoFiles = false;
	const FDreamLocTranslateRequest Request = MakeRequest(bNoFiles);
	const TArray<FDreamLocTranslateRow> Rows = bNoFiles ? TArray<FDreamLocTranslateRow>() : DreamLocTranslateFlow::LoadForReview(Request);
	SetRows(Rows);
	StatusText = FText::Format(LOCTEXT("LoadedForReview", "{0} machine {0}|plural(one=translation,other=translations) to review"), Rows.Num());
}

bool SDreamLocTranslateWindow::ApplyEdits(TConstArrayView<TSharedPtr<FDreamLocTranslateRowItem>> InItems,
	TFunctionRef<bool(DreamLocTranslateFlow::FReviewEdit&, FDreamLocTranslateRowItem&)> InMake, const TCHAR* InReview)
{
	TArray<DreamLocTranslateFlow::FReviewEdit> Edits;
	TArray<TSharedPtr<FDreamLocTranslateRowItem>> Changed;
	for (const TSharedPtr<FDreamLocTranslateRowItem>& Item : InItems)
	{
		if (!Item.IsValid() || Item->bBusy)
		{
			continue;
		}
		DreamLocTranslateFlow::FReviewEdit Edit;
		Edit.Culture = Item->Row.Culture;
		Edit.File = Item->Row.File;
		Edit.Namespace = Item->Row.Namespace;
		Edit.Key = Item->Row.Key;
		if (InMake(Edit, *Item))
		{
			Edits.Add(MoveTemp(Edit));
			Changed.Add(Item);
		}
	}
	if (Edits.IsEmpty())
	{
		return false;
	}
	LastErrors.Reset();
	const bool bWritten = DreamLocTranslateFlow::ApplyReview(DLocDirectory, Edits, LastErrors);
	for (const TSharedPtr<FDreamLocTranslateRowItem>& Item : Changed)
	{
		Item->Review = InReview;
	}
	if (!bWritten)
	{
		StatusText = FText::FromString(FString::Join(LastErrors, TEXT("\n")));
	}
	if (ResultsList.IsValid())
	{
		ResultsList->RequestListRefresh();
	}
	return bWritten;
}

bool SDreamLocTranslateWindow::PassRows(TConstArrayView<TSharedPtr<FDreamLocTranslateRowItem>> InItems)
{
	return ApplyEdits(InItems, [](DreamLocTranslateFlow::FReviewEdit& OutEdit, FDreamLocTranslateRowItem& InItem)
	{
		if (InItem.Row.NewText.IsEmpty())
		{
			return false;
		}
		OutEdit.Text = InItem.Row.NewText;
		OutEdit.State = TEXT("done");
		return true;
	}, TEXT("passed"));
}

bool SDreamLocTranslateWindow::RejectRows(TConstArrayView<TSharedPtr<FDreamLocTranslateRowItem>> InItems)
{
	return ApplyEdits(InItems, [](DreamLocTranslateFlow::FReviewEdit& OutEdit, FDreamLocTranslateRowItem& InItem)
	{
		if (InItem.Row.NewText.IsEmpty() || InItem.Review == TEXT("passed"))
		{
			return false;
		}
		OutEdit.Text.Reset();
		InItem.Row.RejectedText = InItem.Row.NewText;
		InItem.Row.NewText.Reset();
		return true;
	}, TEXT("rejected"));
}

bool SDreamLocTranslateWindow::EditRow(const TSharedPtr<FDreamLocTranslateRowItem>& InItem, const FString& InText)
{
	return ApplyEdits({ InItem }, [&InText](DreamLocTranslateFlow::FReviewEdit& OutEdit, FDreamLocTranslateRowItem& InOutItem)
	{
		if (InText.IsEmpty())
		{
			OutEdit.Text.Reset();
		}
		else
		{
			OutEdit.Text = InText;
			OutEdit.State = TEXT("draft");
		}
		InOutItem.Row.NewText = InText;
		return true;
	}, TEXT("edited"));
}

bool SDreamLocTranslateWindow::RetranslateRow(const TSharedPtr<FDreamLocTranslateRowItem>& InItem)
{
	if (IsRunning() || !InItem.IsValid() || InItem->Review == TEXT("passed"))
	{
		return false;
	}
	bool bNoFiles = false;
	FDreamLocTranslateRequest Request = MakeRequest(bNoFiles);
	Request.Cultures = { InItem->Row.Culture };
	Request.Files.Reset();
	Request.Entries = { TPair<FString, FString>(InItem->Row.Namespace, InItem->Row.Key) };
	Request.bIncludeUntranslated = true;
	Request.bIncludeStale = true;
	Request.bIncludeMachine = true;
	InItem->bBusy = true;
	RunRequest(Request, InItem);
	return true;
}

void SDreamLocTranslateWindow::OpenRowInEditor(const TSharedPtr<FDreamLocTranslateRowItem>& InItem) const
{
	if (!InItem.IsValid())
	{
		return;
	}
	const FDreamLocTranslateRow& Row = InItem->Row;
	const FString Path = FPaths::ConvertRelativePathToFull(DLocDirectory / Row.Culture / Row.File);
	const int32 Line = DreamLocTranslateFlow::FindEntryLine(DLocDirectory, Row.Culture, Row.File, Row.Namespace, Row.Key);
	if (!FSourceCodeNavigation::OpenSourceFile(Path, FMath::Max(1, Line), 1))
	{
		FPlatformProcess::LaunchFileInDefaultExternalApplication(*Path);
	}
}

FText SDreamLocTranslateWindow::GetProgressText() const
{
	if (!Running.IsValid())
	{
		return StatusText;
	}
	FScopeLock Lock(&Running->Lock);
	const FDreamLocTranslateProgress& Progress = Running->Progress;
	return FText::Format(LOCTEXT("Progress", "{0}: {1} of {2} {2}|plural(one=request,other=requests) answered, {3} s"), FText::FromString(Progress.Culture),
		Progress.BatchesDone, Progress.Batches, FText::AsNumber(int32(FPlatformTime::Seconds() - Running->Started)));
}

TOptional<float> SDreamLocTranslateWindow::GetProgressFraction() const
{
	if (!Running.IsValid())
	{
		return 0.0f;
	}
	FScopeLock Lock(&Running->Lock);
	return Running->Progress.Batches > 0 ? float(Running->Progress.BatchesDone) / float(Running->Progress.Batches) : 0.0f;
}

// ---------------------------------------------------------------------------------------------------------- the translators page

FDreamLocTranslateProfile* SDreamLocTranslateWindow::GetSelectedProfile() const
{
	return SelectedProfile.Get();
}

TSharedRef<SWidget> SDreamLocTranslateWindow::MakeBackendsPage()
{
	return SNew(SScrollBox)
		+ SScrollBox::Slot()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f)
			[
				SNew(STextBlock).Text_Lambda([this]() { return ProfilesText; }).AutoWrapText(true)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(0.3f).Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SBox).HeightOverride(220.0f)
						[
							SNew(SBorder).BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
							[
								SAssignNew(ProfileList, SListView<TSharedPtr<FDreamLocTranslateProfile>>)
								.ListItemsSource(&ProfileItems)
								.SelectionMode(ESelectionMode::Single)
								.OnGenerateRow_Lambda([this](TSharedPtr<FDreamLocTranslateProfile> InProfile, const TSharedRef<STableViewBase>& InOwner) -> TSharedRef<ITableRow>
								{
									return SNew(STableRow<TSharedPtr<FDreamLocTranslateProfile>>, InOwner)
									[
										SNew(STextBlock).Text_Lambda([this, InProfile]() { return ProfileLabel(InProfile->Name); })
									];
								})
								.OnSelectionChanged_Lambda([this](TSharedPtr<FDreamLocTranslateProfile> InProfile, ESelectInfo::Type)
								{
									if (InProfile.IsValid())
									{
										SelectedProfile = InProfile;
										TestText = FText::GetEmpty();
										for (const TSharedPtr<FString>& Type : TypeChoices)
										{
											if (Type->Equals(InProfile->Type, ESearchCase::IgnoreCase) && TypeCombo.IsValid())
											{
												TypeCombo->SetSelectedItem(Type);
											}
										}
									}
								})
							]
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)
						[
							SNew(SButton).Text(LOCTEXT("AddProfile", "Add"))
							.OnClicked_Lambda([this]()
							{
								const TSharedPtr<FDreamLocTranslateProfile> Profile = MakeShared<FDreamLocTranslateProfile>();
								Profile->Type = TEXT("OpenAI");
								for (int32 Number = ProfileItems.Num() + 1;; ++Number)
								{
									Profile->Name = FString::Printf(TEXT("profile%d"), Number);
									if (!ProfileItems.ContainsByPredicate([&Profile](const TSharedPtr<FDreamLocTranslateProfile>& InOther) { return InOther->Name == Profile->Name; }))
									{
										break;
									}
								}
								ProfileItems.Add(Profile);
								ProfileList->RequestListRefresh();
								ProfileList->SetSelection(Profile);
								return FReply::Handled();
							})
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)
						[
							SNew(SButton).Text(LOCTEXT("DeleteProfile", "Delete"))
							.IsEnabled_Lambda([this]() { return SelectedProfile.IsValid(); })
							.OnClicked_Lambda([this]()
							{
								ProfileItems.Remove(SelectedProfile);
								SelectedProfile = ProfileItems.Num() > 0 ? ProfileItems[0] : nullptr;
								ProfileList->RequestListRefresh();
								if (SelectedProfile.IsValid())
								{
									ProfileList->SetSelection(SelectedProfile);
								}
								return FReply::Handled();
							})
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(SButton).Text(LOCTEXT("SaveProfiles", "Save"))
							.ToolTipText(FText::Format(LOCTEXT("SaveProfilesTip", "Write the profiles to {0}"), FText::FromString(DreamLocTranslateConfig::GetUserConfigPath())))
							.OnClicked_Lambda([this]() { SaveProfiles(); return FReply::Handled(); })
						]
					]
				]
				+ SHorizontalBox::Slot().FillWidth(0.7f)
				[
					MakeProfileForm()
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 4.0f)
			[
				SNew(SSeparator)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				MakeProjectSettings()
			]
		];
}

TSharedRef<SWidget> SDreamLocTranslateWindow::MakeProfileForm()
{
	using namespace DreamLocTranslateWindowLocal;
	TSharedRef<SGridPanel> Grid = SNew(SGridPanel).FillColumn(1, 1.0f);
	int32 RowIndex = 0;
	auto Visible = [this](FName InField)
	{
		return TAttribute<EVisibility>::CreateLambda([this, InField]()
		{
			const FDreamLocTranslateProfile* Profile = GetSelectedProfile();
			return Profile != nullptr && Shows(Profile->Type, InField) ? EVisibility::Visible : EVisibility::Collapsed;
		});
	};
	auto AddRow = [&Grid, &RowIndex, &Visible](FName InField, const TAttribute<FText>& InLabel, const TSharedRef<SWidget>& InEditor)
	{
		Grid->AddSlot(0, RowIndex).VAlign(VAlign_Center).Padding(0.0f, 2.0f, 8.0f, 2.0f)[SNew(STextBlock).Text(InLabel).Visibility(Visible(InField))];
		Grid->AddSlot(1, RowIndex).Padding(0.0f, 2.0f)[SNew(SBox).Visibility(Visible(InField))[InEditor]];
		++RowIndex;
	};
	auto AddText = [this, &AddRow](FName InField, const TAttribute<FText>& InLabel, FString FDreamLocTranslateProfile::* InMember, bool bInPassword, const FText& InHint)
	{
		AddRow(InField, InLabel, SNew(SEditableTextBox)
			.IsPassword(bInPassword)
			.HintText(InHint)
			.Text_Lambda([this, InMember]()
			{
				const FDreamLocTranslateProfile* Profile = GetSelectedProfile();
				return Profile != nullptr ? FText::FromString(Profile->*InMember) : FText::GetEmpty();
			})
			.OnTextChanged_Lambda([this, InMember](const FText& InText)
			{
				if (FDreamLocTranslateProfile* Profile = GetSelectedProfile())
				{
					Profile->*InMember = InText.ToString().TrimStartAndEnd();
					if (ProfileList.IsValid())
					{
						ProfileList->RequestListRefresh();
					}
				}
			}));
	};
	auto TypeOf = [this]() { const FDreamLocTranslateProfile* Profile = GetSelectedProfile(); return Profile != nullptr ? Profile->Type : FString(); };

	AddText(TEXT("Name"), LOCTEXT("FieldName", "Name"), &FDreamLocTranslateProfile::Name, false, FText::GetEmpty());
	AddRow(TEXT("Type"), LOCTEXT("FieldType", "Type"),
		SAssignNew(TypeCombo, SComboBox<TSharedPtr<FString>>)
		.OptionsSource(&TypeChoices)
		.OnGenerateWidget_Lambda([](TSharedPtr<FString> InType) -> TSharedRef<SWidget> { return SNew(STextBlock).Text(FText::FromString(*InType)); })
		.OnSelectionChanged_Lambda([this](TSharedPtr<FString> InType, ESelectInfo::Type)
		{
			FDreamLocTranslateProfile* Profile = GetSelectedProfile();
			if (Profile != nullptr && InType.IsValid())
			{
				Profile->Type = *InType;
				ProfileList->RequestListRefresh();
			}
		})
		[
			SNew(STextBlock).Text_Lambda([TypeOf]() { return FText::FromString(TypeOf()); })
		]);
	AddText(TEXT("BaseUrl"), LOCTEXT("FieldBaseUrl", "Address"), &FDreamLocTranslateProfile::BaseUrl, false,
		LOCTEXT("BaseUrlHint", "OpenAI: https://api.deepseek.com/v1, http://127.0.0.1:11434/v1 ...; others: empty for the service's own"));
	AddText(TEXT("Model"), LOCTEXT("FieldModel", "Model"), &FDreamLocTranslateProfile::Model, false, LOCTEXT("ModelHint", "deepseek-chat, qwen3:14b ..."));
	AddText(TEXT("ApiKey"), LOCTEXT("FieldApiKey", "Key"), &FDreamLocTranslateProfile::ApiKey, true, LOCTEXT("ApiKeyHint", "Better: the variable below"));
	AddText(TEXT("ApiKeyEnv"), LOCTEXT("FieldApiKeyEnv", "Key from variable"), &FDreamLocTranslateProfile::ApiKeyEnv, false, LOCTEXT("ApiKeyEnvHint", "DEEPSEEK_API_KEY"));
	AddText(TEXT("AppId"), TAttribute<FText>::CreateLambda([TypeOf]() { return AppIdLabel(TypeOf()); }), &FDreamLocTranslateProfile::AppId, true, FText::GetEmpty());
	AddText(TEXT("AppIdEnv"), TAttribute<FText>::CreateLambda([TypeOf]() { return FText::Format(LOCTEXT("FromVariable", "{0} from variable"), AppIdLabel(TypeOf())); }),
		&FDreamLocTranslateProfile::AppIdEnv, false, FText::GetEmpty());
	AddText(TEXT("Secret"), TAttribute<FText>::CreateLambda([TypeOf]() { return SecretLabel(TypeOf()); }), &FDreamLocTranslateProfile::Secret, true, FText::GetEmpty());
	AddText(TEXT("SecretEnv"), TAttribute<FText>::CreateLambda([TypeOf]() { return FText::Format(LOCTEXT("FromVariable", "{0} from variable"), SecretLabel(TypeOf())); }),
		&FDreamLocTranslateProfile::SecretEnv, false, FText::GetEmpty());
	AddText(TEXT("Region"), LOCTEXT("FieldRegion", "Region"), &FDreamLocTranslateProfile::Region, false, LOCTEXT("RegionHint", "ap-shanghai"));
	AddText(TEXT("Proxy"), LOCTEXT("FieldProxy", "Proxy"), &FDreamLocTranslateProfile::Proxy, false, LOCTEXT("ProxyHint", "http://127.0.0.1:7890; empty for none"));
	AddRow(TEXT("Temperature"), LOCTEXT("FieldTemperature", "Temperature"), SNew(SEditableTextBox)
		.Text_Lambda([this]()
		{
			const FDreamLocTranslateProfile* Profile = GetSelectedProfile();
			return Profile != nullptr ? FText::FromString(FString::SanitizeFloat(Profile->Temperature)) : FText::GetEmpty();
		})
		.OnTextCommitted_Lambda([this](const FText& InText, ETextCommit::Type)
		{
			FDreamLocTranslateProfile* Profile = GetSelectedProfile();
			float Value = 0.0f;
			if (Profile != nullptr && LexTryParseString(Value, *InText.ToString()))
			{
				Profile->Temperature = Value;
			}
		}));
	AddRow(TEXT("JsonMode"), LOCTEXT("FieldJsonMode", "JSON mode"), SNew(SCheckBox)
		.ToolTipText(LOCTEXT("JsonModeTip", "Ask for a JSON object (response_format); for a service that supports it"))
		.IsChecked_Lambda([this]()
		{
			const FDreamLocTranslateProfile* Profile = GetSelectedProfile();
			return Profile != nullptr && Profile->bJsonMode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		})
		.OnCheckStateChanged_Lambda([this](ECheckBoxState InState)
		{
			if (FDreamLocTranslateProfile* Profile = GetSelectedProfile())
			{
				Profile->bJsonMode = InState == ECheckBoxState::Checked;
			}
		}));
	AddRow(TEXT("Local"), LOCTEXT("FieldLocal", "Runs on"), SNew(SSegmentedControl<int32>)
		.Value_Lambda([this]()
		{
			const FDreamLocTranslateProfile* Profile = GetSelectedProfile();
			return Profile == nullptr || !Profile->Local.IsSet() ? 0 : Profile->Local.GetValue() ? 1 : 2;
		})
		.OnValueChanged_Lambda([this](int32 InValue)
		{
			if (FDreamLocTranslateProfile* Profile = GetSelectedProfile())
			{
				Profile->Local = InValue == 0 ? TOptional<bool>() : TOptional<bool>(InValue == 1);
				ProfileList->RequestListRefresh();
			}
		})
		+ SSegmentedControl<int32>::Slot(0).Text(LOCTEXT("LocalAuto", "By its address"))
		+ SSegmentedControl<int32>::Slot(1).Text(LOCTEXT("LocalYes", "This machine"))
		+ SSegmentedControl<int32>::Slot(2).Text(LOCTEXT("LocalNo", "A cloud service")));

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock)
			.Visibility_Lambda([this]() { return GetSelectedProfile() == nullptr ? EVisibility::Visible : EVisibility::Collapsed; })
			.Text(LOCTEXT("NoProfile", "No profile yet: Add one."))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).Visibility_Lambda([this]() { return GetSelectedProfile() != nullptr ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				Grid
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.ColorAndOpacity(FStyleColors::Warning)
			.Visibility_Lambda([this]()
			{
				const FDreamLocTranslateProfile* Profile = GetSelectedProfile();
				return Profile != nullptr && Profile->HasSecretInFile() ? EVisibility::Visible : EVisibility::Collapsed;
			})
			.Text(LOCTEXT("PlainKeyWarning",
				"This profile's key is written into translators.ini as plain text. A variable (the 'from variable' fields) keeps it out of the file."))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(SButton).Text(LOCTEXT("TestProfile", "Test"))
				.ToolTipText(LOCTEXT("TestProfileTip", "Send the smallest request -- one short text -- and say how it went"))
				.IsEnabled_Lambda([this]() { return GetSelectedProfile() != nullptr; })
				.OnClicked_Lambda([this]() { TestProfile(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text_Lambda([this]() { return TestText; }).AutoWrapText(true)
			]
		];
}

void SDreamLocTranslateWindow::SaveProfiles()
{
	TArray<FDreamLocTranslateProfile> Values;
	TSet<FString> Names;
	for (const TSharedPtr<FDreamLocTranslateProfile>& Profile : ProfileItems)
	{
		if (Profile->Name.IsEmpty() || Names.Contains(Profile->Name))
		{
			ProfilesText = FText::Format(LOCTEXT("ProfileNameClash", "Not saved: every profile needs a name of its own ('{0}')."), FText::FromString(Profile->Name));
			return;
		}
		Names.Add(Profile->Name);
		Values.Add(*Profile);
	}
	const bool bSaved = DreamLocTranslateConfig::SaveProfiles(Values);
	ProfilesText = bSaved ? FText::Format(LOCTEXT("ProfilesSaved", "Saved to {0}"), FText::FromString(DreamLocTranslateConfig::GetUserConfigPath()))
		: FText::Format(LOCTEXT("ProfilesNotSaved", "{0} cannot be written"), FText::FromString(DreamLocTranslateConfig::GetUserConfigPath()));
	RebuildProfileChoices();
}

void SDreamLocTranslateWindow::TestProfile()
{
	const FDreamLocTranslateProfile* Selected = GetSelectedProfile();
	if (Selected == nullptr)
	{
		return;
	}
	const FDreamLocTranslateProfile Profile = *Selected;
	const FString Source = Settings.SourceCulture;
	const FString Target = AllCultures.Num() > 0 ? AllCultures[0] : FString(TEXT("en"));
	TestText = FText::Format(LOCTEXT("Testing", "Testing {0}..."), FText::FromString(Profile.Name));
	const TWeakPtr<SDreamLocTranslateWindow> Weak = SharedThis(this);
	Async(EAsyncExecution::Thread, [Weak, Profile, Source, Target]()
	{
		const double Started = FPlatformTime::Seconds();
		const FDreamLocTranslateReply Reply = DreamLocTranslators::Ping(Profile, Source, Target);
		const double Seconds = FPlatformTime::Seconds() - Started;
		FString Said = Reply.bSucceeded ? Reply.Texts.FindRef(1) : Reply.Error;
		for (const FString& Secret : Profile.GetSecrets())
		{
			Said.ReplaceInline(*Secret, TEXT("***"), ESearchCase::CaseSensitive);
		}
		const bool bSucceeded = Reply.bSucceeded;
		AsyncTask(ENamedThreads::GameThread, [Weak, bSucceeded, Said, Seconds, Name = Profile.Name]()
		{
			if (const TSharedPtr<SDreamLocTranslateWindow> Window = Weak.Pin())
			{
				const FText Time = FText::AsNumber(FMath::RoundToDouble(Seconds * 100.0) / 100.0);
				Window->TestText = bSucceeded
					? FText::Format(LOCTEXT("TestPassed", "{0} answered in {1} s: \"{2}\""), FText::FromString(Name), Time, FText::FromString(Said))
					: FText::Format(LOCTEXT("TestFailed", "{0} failed after {1} s: {2}"), FText::FromString(Name), Time, FText::FromString(Said));
			}
		});
	});
}

TSharedRef<SWidget> SDreamLocTranslateWindow::MakeProjectSettings()
{
	TSharedRef<SGridPanel> Grid = SNew(SGridPanel).FillColumn(1, 1.0f);
	int32 RowIndex = 0;
	auto AddRow = [&Grid, &RowIndex](const FText& InLabel, const FText& InTip, const TSharedRef<SWidget>& InEditor)
	{
		Grid->AddSlot(0, RowIndex).VAlign(VAlign_Top).Padding(0.0f, 4.0f, 8.0f, 2.0f)[SNew(STextBlock).Text(InLabel).ToolTipText(InTip)];
		Grid->AddSlot(1, RowIndex).Padding(0.0f, 2.0f)[InEditor];
		++RowIndex;
	};
	AddRow(LOCTEXT("SettingProfile", "Translator"), LOCTEXT("SettingProfileTip", "[Translate] Profile: the profile used by default"),
		SAssignNew(SettingsProfile, SEditableTextBox));
	AddRow(LOCTEXT("SettingByCulture", "Translator by culture"), LOCTEXT("SettingByCultureTip", "[Translate] Profile.<culture>: one line each, ja=deepl"),
		SNew(SBox).MinDesiredHeight(44.0f)[SAssignNew(SettingsByCulture, SMultiLineEditableTextBox).HintText(LOCTEXT("ByCultureHint", "ja=deepl"))]);
	AddRow(LOCTEXT("SettingFallback", "Fallback large model"), LOCTEXT("SettingFallbackTip", "[Translate] Fallback: what a machine translator cannot do goes to it"),
		SAssignNew(SettingsFallback, SEditableTextBox));
	AddRow(LOCTEXT("SettingBatchSize", "Entries per request"), LOCTEXT("SettingBatchSizeTip", "[Translate] BatchSize (default 40)"),
		SAssignNew(SettingsBatchSize, SEditableTextBox));
	AddRow(LOCTEXT("SettingConcurrency", "Requests at once"), LOCTEXT("SettingConcurrencyTip", "[Translate] Concurrency (default 2)"),
		SAssignNew(SettingsConcurrency, SEditableTextBox));
	AddRow(LOCTEXT("SettingRetry", "Tries after the checks"), LOCTEXT("SettingRetryTip", "[Translate] Retry: times a translation the checks turn down goes back to the large model (default 1)"),
		SAssignNew(SettingsRetry, SEditableTextBox));
	AddRow(LOCTEXT("SettingStyle", "Style guide"), LOCTEXT("SettingStyleTip", "[Translate] Style, relative to the project; DLoc/_style.<culture>.md wins when there is one"),
		SAssignNew(SettingsStyle, SEditableTextBox));
	AddRow(LOCTEXT("SettingLocalOnly", "Only to local translators"), LOCTEXT("SettingLocalOnlyTip", "[Translate] LocalOnly: file paths or namespaces, one a line"),
		SNew(SBox).MinDesiredHeight(44.0f)[SAssignNew(SettingsLocalOnly, SMultiLineEditableTextBox).HintText(LOCTEXT("LocalOnlyHint", "DNA/"))]);

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f)
		[
			SNew(STextBlock).Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold"))).Text(LOCTEXT("ProjectSettings", "This project (dloc.ini [Translate])"))
		]
		+ SVerticalBox::Slot().AutoHeight()[Grid]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(SButton).Text(LOCTEXT("SaveSettings", "Save to dloc.ini")).OnClicked_Lambda([this]() { SaveProjectSettings(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text_Lambda([this]() { return SettingsText; }).AutoWrapText(true)
			]
		];
}

void SDreamLocTranslateWindow::LoadProjectSettings()
{
	if (!SettingsProfile.IsValid())
	{
		return;
	}
	SettingsProfile->SetText(FText::FromString(Settings.TranslateProfile));
	SettingsFallback->SetText(FText::FromString(Settings.TranslateFallback));
	SettingsBatchSize->SetText(FText::AsNumber(Settings.TranslateBatchSize, &FNumberFormattingOptions::DefaultNoGrouping()));
	SettingsConcurrency->SetText(FText::AsNumber(Settings.TranslateConcurrency, &FNumberFormattingOptions::DefaultNoGrouping()));
	SettingsRetry->SetText(FText::AsNumber(Settings.TranslateRetry, &FNumberFormattingOptions::DefaultNoGrouping()));
	SettingsStyle->SetText(FText::FromString(Settings.TranslateStyle));
	TArray<FString> Lines;
	for (const TPair<FString, FString>& Culture : Settings.TranslateProfileByCulture)
	{
		Lines.Add(Culture.Key + TEXT("=") + Culture.Value);
	}
	SettingsByCulture->SetText(FText::FromString(FString::Join(Lines, TEXT("\n"))));
	SettingsLocalOnly->SetText(FText::FromString(FString::Join(Settings.TranslateLocalOnly, TEXT("\n"))));
}

void SDreamLocTranslateWindow::SaveProjectSettings()
{
	FDreamLocSettings Changed = Settings;
	Changed.TranslateProfile = SettingsProfile->GetText().ToString().TrimStartAndEnd();
	Changed.TranslateFallback = SettingsFallback->GetText().ToString().TrimStartAndEnd();
	Changed.TranslateStyle = SettingsStyle->GetText().ToString().TrimStartAndEnd();
	struct FNumber
	{
		SEditableTextBox* Box;
		int32* Value;
		int32 Least;
	};
	const FNumber Numbers[] = {
		{ SettingsBatchSize.Get(), &Changed.TranslateBatchSize, 1 }, { SettingsConcurrency.Get(), &Changed.TranslateConcurrency, 1 },
		{ SettingsRetry.Get(), &Changed.TranslateRetry, 0 } };
	for (const FNumber& Number : Numbers)
	{
		const FString Text = Number.Box->GetText().ToString().TrimStartAndEnd();
		int32 Value = 0;
		if (!Text.IsNumeric() || !LexTryParseString(Value, *Text) || Value < Number.Least)
		{
			SettingsText = FText::Format(LOCTEXT("BadNumber", "Not saved: '{0}' is not a whole number of {1} or more."), FText::FromString(Text), Number.Least);
			return;
		}
		*Number.Value = Value;
	}
	TArray<FString> Lines;
	SettingsByCulture->GetText().ToString().ParseIntoArrayLines(Lines);
	Changed.TranslateProfileByCulture.Reset();
	for (const FString& Line : Lines)
	{
		FString Culture;
		FString Profile;
		if (Line.TrimStartAndEnd().IsEmpty())
		{
			continue;
		}
		if (!Line.Split(TEXT("="), &Culture, &Profile) || Culture.TrimStartAndEnd().IsEmpty() || Profile.TrimStartAndEnd().IsEmpty())
		{
			SettingsText = FText::Format(LOCTEXT("BadByCulture", "Not saved: '{0}' is not culture=profile."), FText::FromString(Line));
			return;
		}
		Changed.TranslateProfileByCulture.Emplace(Culture.TrimStartAndEnd(), Profile.TrimStartAndEnd());
	}
	SettingsLocalOnly->GetText().ToString().ParseIntoArrayLines(Lines);
	Changed.TranslateLocalOnly.Reset();
	for (const FString& Line : Lines)
	{
		if (!Line.TrimStartAndEnd().IsEmpty())
		{
			Changed.TranslateLocalOnly.Add(Line.TrimStartAndEnd());
		}
	}
	const FString Path = DLocDirectory / FDreamLocSettings::FileName;
	FString Text;
	FFileHelper::LoadFileToString(Text, *Path);
	IFileManager::Get().MakeDirectory(*DLocDirectory, /*Tree*/ true);
	if (!FFileHelper::SaveStringToFile(DreamLocTranslateConfig::WriteTranslateSection(Text, Changed), *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		SettingsText = FText::Format(LOCTEXT("SettingsNotSaved", "{0} cannot be written"), FText::FromString(Path));
		return;
	}
	Settings = FDreamLocSettings::LoadFromDirectory(DLocDirectory);
	SettingsText = FText::Format(LOCTEXT("SettingsSaved", "Saved to {0}"), FText::FromString(Path));
}

#undef LOCTEXT_NAMESPACE
