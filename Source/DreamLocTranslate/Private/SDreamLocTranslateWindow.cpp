// Copyright DreamMoon. All Rights Reserved.

#include "SDreamLocTranslateWindow.h"

#include "Async/Async.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "DirectoryWatcherModule.h"
#include "DreamLocChips.h"
#include "DreamLocExtract.h"
#include "DreamLocFile.h"
#include "DreamLocTextSource.h"
#include "DreamLocTranslateConfig.h"
#include "DreamLocTranslateModule.h"
#include "DreamLocTranslator.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "IDirectoryWatcher.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"
#include "SDreamLocPretranslateDialog.h"
#include "SDreamLocTranslateEditor.h"
#include "SourceCodeNavigation.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Types/ISlateMetaData.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SToolTip.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "DreamLocTranslateWindow"

struct SDreamLocTranslateWindow::FRunState
{
	TAtomic<bool> Cancel{ false };
	FCriticalSection Lock;
	FDreamLocTranslateProgress Progress;
};

namespace DreamLocTranslateWindowLocal
{
	const FName ColumnStatus(TEXT("Status"));
	const FName ColumnKey(TEXT("Key"));
	const FName ColumnSource(TEXT("Source"));
	const FName ColumnTranslation(TEXT("Translation"));
	const FName ColumnReference(TEXT("Reference"));
	const FName ColumnProblems(TEXT("Problems"));

	const FSlateBrush* DotBrush(bool bInHollow)
	{
		static const FSlateRoundedBoxBrush Filled(FLinearColor::White, 5.0f);
		static const FSlateRoundedBoxBrush Hollow(FLinearColor::Transparent, 5.0f, FLinearColor::White, 1.5f);
		return bInHollow ? &Hollow : &Filled;
	}

	/** Reviewed green, to review yellow, stale blue, untranslated an empty ring: muted, as the rest of the editor. */
	FLinearColor DotColor(EDreamLocReviewStatus InStatus)
	{
		switch (InStatus)
		{
		case EDreamLocReviewStatus::Done: return FLinearColor(0.36f, 0.62f, 0.38f);
		case EDreamLocReviewStatus::Review: return FLinearColor(0.80f, 0.66f, 0.30f);
		case EDreamLocReviewStatus::Stale: return FLinearColor(0.38f, 0.55f, 0.80f);
		default: return FLinearColor(0.55f, 0.55f, 0.55f);
		}
	}

	FText FilterName(EDreamLocReviewFilter InFilter)
	{
		switch (InFilter)
		{
		case EDreamLocReviewFilter::Untranslated: return LOCTEXT("FilterUntranslated", "Untranslated");
		case EDreamLocReviewFilter::Stale: return LOCTEXT("FilterStale", "Stale");
		case EDreamLocReviewFilter::Review: return LOCTEXT("FilterReview", "To review");
		case EDreamLocReviewFilter::Problems: return LOCTEXT("FilterProblems", "Problems");
		case EDreamLocReviewFilter::Done: return LOCTEXT("FilterDone", "Reviewed");
		default: return LOCTEXT("FilterAll", "All");
		}
	}

	const TCHAR* FilterTag(EDreamLocReviewFilter InFilter)
	{
		switch (InFilter)
		{
		case EDreamLocReviewFilter::Untranslated: return TEXT("DreamLocTranslate.Filter.Untranslated");
		case EDreamLocReviewFilter::Stale: return TEXT("DreamLocTranslate.Filter.Stale");
		case EDreamLocReviewFilter::Review: return TEXT("DreamLocTranslate.Filter.Review");
		case EDreamLocReviewFilter::Problems: return TEXT("DreamLocTranslate.Filter.Problems");
		case EDreamLocReviewFilter::Done: return TEXT("DreamLocTranslate.Filter.Done");
		default: return TEXT("DreamLocTranslate.Filter.All");
		}
	}

	class SReviewRow : public SMultiColumnTableRow<TSharedPtr<FDreamLocReviewEntry>>
	{
	public:
		SLATE_BEGIN_ARGS(SReviewRow) {}
			SLATE_ARGUMENT(TSharedPtr<FDreamLocReviewEntry>, Entry)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwner)
		{
			Entry = InArgs._Entry;
			SMultiColumnTableRow<TSharedPtr<FDreamLocReviewEntry>>::Construct(FSuperRowType::FArguments().Padding(FMargin(0.0f, 2.0f)), InOwner);
			AddMetadata<FTagMetaData>(MakeShared<FTagMetaData>(*(TEXT("DreamLocTranslate.Row.") + Entry->Entry.Key)));
		}

		virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& InColumn) override
		{
			const FDreamLocReviewEntry& Shown = *Entry;
			auto Padded = [](const TSharedRef<SWidget>& InContent) -> TSharedRef<SWidget>
			{
				return SNew(SBox).Padding(FMargin(4.0f, 1.0f)).VAlign(VAlign_Center)[InContent];
			};
			if (InColumn == ColumnStatus)
			{
				return SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(SImage)
					.Image(DotBrush(Shown.Status == EDreamLocReviewStatus::Untranslated))
					.DesiredSizeOverride(FVector2D(10.0, 10.0))
					.ColorAndOpacity(DotColor(Shown.Status))
				];
			}
			if (InColumn == ColumnKey)
			{
				return Padded(SNew(STextBlock).Text(FText::FromString(Shown.Entry.Key)).ToolTipText(FText::FromString(Shown.Entry.Namespace + TEXT(" / ") + Shown.Entry.Key)));
			}
			if (InColumn == ColumnSource)
			{
				// A stale entry's source: what changed, on hover.
				TSharedPtr<SToolTip> Tip;
				if (Shown.Status == EDreamLocReviewStatus::Stale)
				{
					Tip = SNew(SToolTip)[SNew(SDreamLocChipText).MultiLine(true).Text(Shown.Entry.Source).OldText(Shown.Entry.Prev)];
				}
				else
				{
					Tip = SNew(SToolTip)[SNew(SDreamLocChipText).MultiLine(true).Text(Shown.Entry.Source)];
				}
				return Padded(SNew(SDreamLocChipText).AddMetaData<FTagMetaData>(FTagMetaData(TEXT("DreamLocTranslate.ListSource"))).Text(Shown.Entry.Source).ToolTip(Tip));
			}
			if (InColumn == ColumnTranslation)
			{
				return Padded(SNew(SDreamLocChipText).Text(Shown.Translation)
					.ToolTip(SNew(SToolTip)[SNew(SDreamLocChipText).MultiLine(true).Text(Shown.Translation)]));
			}
			if (InColumn == ColumnReference)
			{
				return Padded(SNew(SDreamLocChipText).Text(Shown.Reference).ColorAndOpacity(FSlateColor::UseSubduedForeground()));
			}
			if (InColumn == ColumnProblems)
			{
				return SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(SImage)
					.Visibility(Shown.HasProblems() ? EVisibility::Visible : EVisibility::Hidden)
					.Image(FAppStyle::GetBrush(TEXT("Icons.Warning")))
					.ColorAndOpacity(FStyleColors::Warning)
					.DesiredSizeOverride(FVector2D(14.0, 14.0))
					.ToolTipText(FText::FromString(Shown.Problems.Replace(TEXT("; "), TEXT("\n"))))
				];
			}
			return SNullWidget::NullWidget;
		}

	private:
		TSharedPtr<FDreamLocReviewEntry> Entry;
	};
}

// ---------------------------------------------------------------------------------------------------------- set up

void SDreamLocTranslateWindow::Construct(const FArguments& InArgs)
{
	using namespace DreamLocTranslateWindowLocal;
	DLocDirectory = InArgs._DLocDirectory.IsEmpty() ? FDreamLocTextSource::GetProjectDirectory() : InArgs._DLocDirectory;
	OwnerDirectory = InArgs._OwnerDirectory.IsEmpty() ? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()) : InArgs._OwnerDirectory;

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(6.0f, 6.0f, 6.0f, 2.0f)[MakeToolbar()]
		+ SVerticalBox::Slot().AutoHeight().Padding(6.0f, 2.0f)[MakeOverview()]
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(6.0f, 2.0f)
		[
			SNew(SSplitter)
			+ SSplitter::Slot().Value(0.5f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SBox).WidthOverride(220.0f).Visibility_Lambda([this]() { return bShowFiles ? EVisibility::Visible : EVisibility::Collapsed; })
					[
						MakeFiles()
					]
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f)[MakeList()]
			]
			+ SSplitter::Slot().Value(0.5f)
			[
				SAssignNew(Editor, SDreamLocTranslateEditor, SharedThis(this))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 2.0f, 8.0f, 6.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(LOCTEXT("Shortcuts", "Ctrl+Enter pass and next   Ctrl+Shift+Enter draft and next   Alt+Up / Alt+Down previous / next   Ctrl+1/2/3 a candidate   Ctrl+R translate again   Ctrl+Shift+F AI fix   F3 the missing placeholder"))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text_Lambda([this]() { return StatusText; })
			]
		]
	];
	Refresh();
	WatchDirectory();
}

SDreamLocTranslateWindow::~SDreamLocTranslateWindow()
{
	UnwatchDirectory();
	if (Running.IsValid())
	{
		Running->Cancel = true;
	}
	if (const TSharedPtr<SWindow> Dialog = PretranslateWindow.Pin())
	{
		FSlateApplication::Get().RequestDestroyWindow(Dialog.ToSharedRef());
	}
}

TSharedRef<SWidget> SDreamLocTranslateWindow::MakeToolbar()
{
	using namespace DreamLocTranslateWindowLocal;
	auto Choices = [this](TSharedPtr<SComboBox<TSharedPtr<FString>>>& OutCombo, TArray<TSharedPtr<FString>>* InChoices, TFunction<FText(const FString&)> InLabel,
		TFunction<void(const FString&)> InChosen, TFunction<FString()> InCurrent, const TCHAR* InTag, const FText& InTip) -> TSharedRef<SWidget>
	{
		return SAssignNew(OutCombo, SComboBox<TSharedPtr<FString>>)
			.AddMetaData<FTagMetaData>(FTagMetaData(InTag))
			.ToolTipText(InTip)
			.OptionsSource(InChoices)
			.OnGenerateWidget_Lambda([InLabel](TSharedPtr<FString> InItem) -> TSharedRef<SWidget> { return SNew(STextBlock).Text(InLabel(InItem.IsValid() ? *InItem : FString())); })
			.OnSelectionChanged_Lambda([InChosen](TSharedPtr<FString> InItem, ESelectInfo::Type InHow)
			{
				if (InItem.IsValid() && InHow != ESelectInfo::Direct)
				{
					InChosen(*InItem);
				}
			})
			[
				SNew(STextBlock).Text_Lambda([InLabel, InCurrent]() { return InLabel(InCurrent()); })
			];
	};

	TSharedRef<SHorizontalBox> Filters = SNew(SHorizontalBox);
	for (int32 Index = 0; Index < int32(EDreamLocReviewFilter::Count); ++Index)
	{
		const EDreamLocReviewFilter Each = EDreamLocReviewFilter(Index);
		Filters->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 2.0f, 0.0f)
		[
			SNew(SCheckBox)
			.AddMetaData<FTagMetaData>(FTagMetaData(FilterTag(Each)))
			.Style(FAppStyle::Get(), TEXT("ToggleButtonCheckbox"))
			.IsChecked_Lambda([this, Each]() { return Filter == Each ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this, Each](ECheckBoxState) { SetFilter(Each); })
			[
				SNew(SBox).Padding(FMargin(6.0f, 2.0f))[SNew(STextBlock).Text_Lambda([this, Each]() { return FilterLabel(Each); })]
			]
		];
	}

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				Choices(CultureCombo, &CultureChoices, [](const FString& InName) { return InName.IsEmpty() ? LOCTEXT("NoCulture", "(no culture)") : FText::FromString(InName); },
					[this](const FString& InName) { SetCulture(InName); }, [this]() { return Culture; }, TEXT("DreamLocTranslate.Culture"),
					LOCTEXT("CultureTip", "The culture to review"))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)[Filters]
			+ SHorizontalBox::Slot().FillWidth(1.0f).MaxWidth(260.0f).Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SAssignNew(SearchBox, SSearchBox)
				.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("DreamLocTranslate.Search")))
				.HintText(LOCTEXT("SearchHint", "Key, source or translation"))
				.OnTextChanged_Lambda([this](const FText& InText) { SetSearch(InText.ToString()); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SCheckBox)
				.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("DreamLocTranslate.Files")))
				.Style(FAppStyle::Get(), TEXT("ToggleButtonCheckbox"))
				.ToolTipText(LOCTEXT("FilesTip", "Show the files, to look at one alone"))
				.IsChecked_Lambda([this]() { return bShowFiles ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState InState) { bShowFiles = InState == ECheckBoxState::Checked; })
				[
					SNew(SBox).Padding(FMargin(6.0f, 2.0f))[SNew(STextBlock).Text(LOCTEXT("FilesButton", "Files"))]
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				Choices(ReferenceCombo, &ReferenceChoices, [](const FString& InName) { return InName.IsEmpty() ? LOCTEXT("NoReference", "No reference") : FText::Format(LOCTEXT("Reference", "Reference: {0}"), FText::FromString(InName)); },
					[this](const FString& InName) { SetReferenceCulture(InName); }, [this]() { return ReferenceCulture; }, TEXT("DreamLocTranslate.ReferenceCulture"),
					LOCTEXT("ReferenceTip", "Another culture's translations, beside this one's"))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				Choices(ProfileCombo, &ProfileChoices, [this](const FString& InName) { return ProfileLabel(InName); },
					[this](const FString& InName) { SetProfile(InName); }, [this]() { return Profile; }, TEXT("DreamLocTranslate.Profile"),
					LOCTEXT("ProfileTip", "The translator for this window's actions"))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SButton)
				.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("DreamLocTranslate.Settings")))
				.ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
				.ToolTipText(LOCTEXT("SettingsTip", "Your translators: Editor Preferences > Plugins > DreamLoc translators"))
				.OnClicked_Lambda([]() { DreamLocTranslateWindow::OpenTranslatorsSettings(); return FReply::Handled(); })
				[
					SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.Settings"))).ColorAndOpacity(FSlateColor::UseForeground())
				]
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("DreamLocTranslate.Pretranslate")))
				.Text(LOCTEXT("Pretranslate", "AI pre-translate..."))
				.ToolTipText(LOCTEXT("PretranslateTip", "Translate many entries at once; what comes back waits here for review"))
				.OnClicked_Lambda([this]() { OpenPretranslate(); return FReply::Handled(); })
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.35f).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(SProgressBar)
				.Percent_Lambda([this]() -> TOptional<float>
				{
					const FDreamLocReviewProgress Progress = Model.GetProgress(File);
					return Progress.Total() > 0 ? float(Progress.Done) / float(Progress.Total()) : 0.0f;
				})
			]
			+ SHorizontalBox::Slot().FillWidth(0.65f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).ColorAndOpacity(FSlateColor::UseSubduedForeground()).Text_Lambda([this]() { return ProgressText(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SHorizontalBox).Visibility_Lambda([this]() { return IsRunning() ? EVisibility::Visible : EVisibility::Collapsed; })
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(SBox).WidthOverride(90.0f)[SNew(SProgressBar).Percent_Lambda([this]() -> TOptional<float> { return GetRunFraction(); })]
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton).Text(LOCTEXT("CancelRun", "Cancel")).OnClicked_Lambda([this]() { CancelRun(); return FReply::Handled(); })
				]
			]
		];
}

TSharedRef<SWidget> SDreamLocTranslateWindow::MakeList()
{
	using namespace DreamLocTranslateWindowLocal;
	SAssignNew(Header, SHeaderRow)
		+ SHeaderRow::Column(ColumnStatus).DefaultLabel(FText::GetEmpty()).FixedWidth(22.0f)
		+ SHeaderRow::Column(ColumnKey).DefaultLabel(LOCTEXT("ColumnKey", "Key")).FillWidth(0.18f)
		+ SHeaderRow::Column(ColumnSource).DefaultLabel(LOCTEXT("ColumnSource", "Source")).FillWidth(0.34f)
		+ SHeaderRow::Column(ColumnTranslation).DefaultLabel(LOCTEXT("ColumnTranslation", "Translation")).FillWidth(0.34f)
		+ SHeaderRow::Column(ColumnReference).DefaultLabel(LOCTEXT("ColumnReference", "Reference")).FillWidth(0.24f)
		+ SHeaderRow::Column(ColumnProblems).DefaultLabel(FText::GetEmpty()).FixedWidth(26.0f);
	Header->SetShowGeneratedColumn(ColumnReference, false);
	return SAssignNew(List, SListView<TSharedPtr<FDreamLocReviewEntry>>)
		.ListItemsSource(&Shown)
		.SelectionMode(ESelectionMode::Multi)
		.HeaderRow(Header)
		.OnGenerateRow(this, &SDreamLocTranslateWindow::MakeRow)
		.OnContextMenuOpening(this, &SDreamLocTranslateWindow::MakeContextMenu)
		.OnSelectionChanged_Lambda([this](TSharedPtr<FDreamLocReviewEntry> InEntry, ESelectInfo::Type)
		{
			if (bSyncingSelection || !InEntry.IsValid() || InEntry == Current)
			{
				return;
			}
			// A change to the entry being left is kept as a draft first: the files are read again, so find the entry anew.
			const FString Namespace = InEntry->Entry.Namespace;
			const FString Key = InEntry->Entry.Key;
			KeepEdit();
			ShowEntry(Model.Find(Namespace, Key));
		})
		.OnMouseButtonDoubleClick_Lambda([this](TSharedPtr<FDreamLocReviewEntry> InEntry) { OpenInFile(InEntry); });
}

TSharedRef<ITableRow> SDreamLocTranslateWindow::MakeRow(TSharedPtr<FDreamLocReviewEntry> InEntry, const TSharedRef<STableViewBase>& InOwner)
{
	return SNew(DreamLocTranslateWindowLocal::SReviewRow, InOwner).Entry(InEntry);
}

TSharedRef<SWidget> SDreamLocTranslateWindow::MakeFiles()
{
	return SNew(SBorder).BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
	[
		SAssignNew(FileTree, STreeView<TSharedPtr<FDreamLocReviewFileNode>>)
		.TreeItemsSource(&FileRoots)
		.SelectionMode(ESelectionMode::Single)
		.OnGetChildren_Lambda([](TSharedPtr<FDreamLocReviewFileNode> InNode, TArray<TSharedPtr<FDreamLocReviewFileNode>>& OutChildren) { OutChildren = InNode->Children; })
		.OnGenerateRow_Lambda([this](TSharedPtr<FDreamLocReviewFileNode> InNode, const TSharedRef<STableViewBase>& InOwner) -> TSharedRef<ITableRow>
		{
			const FDreamLocReviewProgress Progress = Model.GetProgress(InNode->Path);
			return SNew(STableRow<TSharedPtr<FDreamLocReviewFileNode>>, InOwner)
				.AddMetaData<FTagMetaData>(FTagMetaData(*(TEXT("DreamLocTranslate.File.") + (InNode->Path.IsEmpty() ? FString(TEXT("All")) : InNode->Path))))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f)[SNew(STextBlock).Text(FText::FromString(InNode->Name))]
					+ SHorizontalBox::Slot().AutoWidth().Padding(6.0f, 0.0f, 2.0f, 0.0f)
					[
						SNew(STextBlock).ColorAndOpacity(FSlateColor::UseSubduedForeground())
						.Text(FText::Format(LOCTEXT("FileProgress", "{0}/{1}"), Progress.Done, Progress.Total()))
					]
				];
		})
		.OnSelectionChanged_Lambda([this](TSharedPtr<FDreamLocReviewFileNode> InNode, ESelectInfo::Type InHow)
		{
			if (InNode.IsValid() && InHow != ESelectInfo::Direct)
			{
				SetFile(InNode->Path);
			}
		})
	];
}

TSharedPtr<SWidget> SDreamLocTranslateWindow::MakeContextMenu()
{
	const TArray<TSharedPtr<FDreamLocReviewEntry>> Selected = GetSelectedEntries();
	if (Selected.IsEmpty())
	{
		return nullptr;
	}
	const bool bAnyTranslated = Selected.ContainsByPredicate([](const TSharedPtr<FDreamLocReviewEntry>& InEntry) { return !InEntry->Translation.IsEmpty(); });
	FMenuBuilder Menu(/*bInShouldCloseWindowAfterMenuSelection*/ true, nullptr);
	Menu.AddMenuEntry(LOCTEXT("MenuPass", "Pass"), LOCTEXT("MenuPassTip", "Mark their translations reviewed (state done)"), FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, Selected]() { PassEntries(Selected); }), FCanExecuteAction::CreateLambda([bAnyTranslated]() { return bAnyTranslated; })),
		NAME_None, EUserInterfaceActionType::Button, TEXT("DreamLocTranslate.MenuPass"));
	Menu.AddMenuEntry(LOCTEXT("MenuReject", "Reject"), LOCTEXT("MenuRejectTip", "Take their translations out"), FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, Selected]() { RejectEntries(Selected); }), FCanExecuteAction::CreateLambda([bAnyTranslated]() { return bAnyTranslated; })),
		NAME_None, EUserInterfaceActionType::Button, TEXT("DreamLocTranslate.MenuReject"));
	for (const EDreamLocTranslatorKind Kind : { EDreamLocTranslatorKind::LargeModel, EDreamLocTranslatorKind::Machine })
	{
		const bool bLarge = Kind == EDreamLocTranslatorKind::LargeModel;
		Menu.AddSubMenu(bLarge ? LOCTEXT("MenuLarge", "Translate with a large model") : LOCTEXT("MenuMachine", "Translate with a machine translator"), FText::GetEmpty(),
			FNewMenuDelegate::CreateLambda([this, Selected, Kind](FMenuBuilder& InSub)
			{
				for (const FDreamLocTranslateProfile& Each : Profiles)
				{
					if (Each.GetKind() == Kind)
					{
						const FString Name = Each.Name;
						InSub.AddMenuEntry(ProfileLabel(Name), FText::GetEmpty(), FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda([this, Selected, Name]() { TranslateEntries(Selected, Name); }), FCanExecuteAction::CreateLambda([this]() { return !IsRunning(); })),
							NAME_None, EUserInterfaceActionType::Button, *(TEXT("DreamLocTranslate.MenuTranslate.") + Name));
					}
				}
			}));
	}
	Menu.AddMenuEntry(LOCTEXT("MenuCopy", "Copy key"), LOCTEXT("MenuCopyTip", "Their namespaces and keys, a line each"), FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([Selected]()
		{
			TArray<FString> Lines;
			for (const TSharedPtr<FDreamLocReviewEntry>& Entry : Selected)
			{
				Lines.Add(Entry->Entry.Namespace + TEXT(" / ") + Entry->Entry.Key);
			}
			FPlatformApplicationMisc::ClipboardCopy(*FString::Join(Lines, TEXT("\n")));
		})), NAME_None, EUserInterfaceActionType::Button, TEXT("DreamLocTranslate.MenuCopy"));
	return Menu.MakeWidget();
}

// ---------------------------------------------------------------------------------------------------------- what is shown

void SDreamLocTranslateWindow::SetDirectories(const FString& InDLocDirectory, const FString& InOwnerDirectory)
{
	UnwatchDirectory();
	DLocDirectory = InDLocDirectory;
	OwnerDirectory = InOwnerDirectory;
	Culture.Reset();
	Current.Reset();
	Refresh();
	WatchDirectory();
}

void SDreamLocTranslateWindow::Refresh()
{
	Settings = FDreamLocSettings::LoadFromDirectory(DLocDirectory);
	TArray<FString> Warnings;
	Profiles = DreamLocTranslateConfig::LoadProfiles(&Warnings);
	if (!Settings.TargetCultures.Contains(Culture))
	{
		Culture = Settings.TargetCultures.Num() > 0 ? Settings.TargetCultures[0] : FString();
	}
	if (!Settings.TargetCultures.Contains(ReferenceCulture) || ReferenceCulture == Culture)
	{
		ReferenceCulture.Reset();
	}
	if (!Profile.IsEmpty() && !DreamLocTranslateConfig::FindProfile(Profiles, Profile))
	{
		Profile.Reset();
	}
	CultureChoices.Reset();
	ReferenceChoices.Reset();
	ReferenceChoices.Add(MakeShared<FString>());
	for (const FString& Each : Settings.TargetCultures)
	{
		CultureChoices.Add(MakeShared<FString>(Each));
		ReferenceChoices.Add(MakeShared<FString>(Each));
	}
	ProfileChoices.Reset();
	ProfileChoices.Add(MakeShared<FString>());
	for (const FDreamLocTranslateProfile& Each : Profiles)
	{
		ProfileChoices.Add(MakeShared<FString>(Each.Name));
	}
	for (const TSharedPtr<SComboBox<TSharedPtr<FString>>>& Combo : { CultureCombo, ReferenceCombo, ProfileCombo })
	{
		if (Combo.IsValid())
		{
			Combo->RefreshOptions();
		}
	}
	StatusText = Warnings.Num() > 0 ? FText::FromString(FString::Join(Warnings, TEXT("; "))) : FText::GetEmpty();
	Model.Load(DLocDirectory, OwnerDirectory, Culture, ReferenceCulture);
	if (Header.IsValid())
	{
		Header->SetShowGeneratedColumn(DreamLocTranslateWindowLocal::ColumnReference, !ReferenceCulture.IsEmpty());
	}
	const TSharedPtr<FDreamLocReviewEntry> Was = Current;
	RebuildShown();
	RebuildFiles();
	RebuildOverview();
	ShowEntry(Was.IsValid() ? Model.Find(Was->Entry.Namespace, Was->Entry.Key) : nullptr);
}

void SDreamLocTranslateWindow::Reload()
{
	const FString Namespace = Current.IsValid() ? Current->Entry.Namespace : FString();
	const FString Key = Current.IsValid() ? Current->Entry.Key : FString();
	const bool bChanged = Editor.IsValid() && Editor->IsChanged();
	const FString Typed = Editor.IsValid() ? Editor->GetText() : FString();
	TSet<FString> Selected;
	for (const TSharedPtr<FDreamLocReviewEntry>& Entry : GetSelectedEntries())
	{
		Selected.Add(Entry->GetId());
	}
	const uint32 Was = Model.GetSignature();
	Model.Reload();
	const bool bSame = Model.GetSignature() == Was;
	RebuildShown();
	if (!bSame)
	{
		RebuildFiles();
	}
	RebuildOverview();
	const TSharedPtr<FDreamLocReviewEntry> Again = Current.IsValid() ? Model.Find(Namespace, Key) : nullptr;
	if (bSame && Editor.IsValid())
	{
		// The same entries (a file this window wrote, read again when the folder says it changed): the editor and the
		// list left as they are -- what is typed, the cursor, the undo, the rows selected.
		Current = Again;
		Editor->Rebind(Again);
		if (List.IsValid())
		{
			TGuardValue<bool> Guard(bSyncingSelection, true);
			List->ClearSelection();
			for (const TSharedPtr<FDreamLocReviewEntry>& Entry : Shown)
			{
				if (Selected.Contains(Entry->GetId()))
				{
					List->SetItemSelection(Entry, true, ESelectInfo::Direct);
				}
			}
		}
		return;
	}
	ShowEntry(Again);
	if (bChanged && Current.IsValid())
	{
		// What was being typed stays: the file changing underneath does not take it away.
		Editor->SetText(Typed);
	}
}

void SDreamLocTranslateWindow::RebuildShown()
{
	Shown = Model.Filter(Filter, Search, File);
	if (List.IsValid())
	{
		List->RequestListRefresh();
	}
}

void SDreamLocTranslateWindow::RebuildFiles()
{
	FileRoots.Reset();
	const TSharedPtr<FDreamLocReviewFileNode> All = MakeShared<FDreamLocReviewFileNode>();
	All->Name = LOCTEXT("AllFiles", "All files").ToString();
	FileRoots.Add(All);
	TMap<FString, TSharedPtr<FDreamLocReviewFileNode>> Folders;
	for (const FString& Each : Model.GetFiles())
	{
		TArray<FString> Parts;
		Each.ParseIntoArray(Parts, TEXT("/"));
		TSharedPtr<FDreamLocReviewFileNode> Parent = All;
		FString Path;
		for (int32 Index = 0; Index < Parts.Num(); ++Index)
		{
			const bool bFile = Index == Parts.Num() - 1;
			Path += Parts[Index] + (bFile ? TEXT("") : TEXT("/"));
			if (!bFile)
			{
				if (const TSharedPtr<FDreamLocReviewFileNode>* Found = Folders.Find(Path))
				{
					Parent = *Found;
					continue;
				}
			}
			const TSharedPtr<FDreamLocReviewFileNode> Node = MakeShared<FDreamLocReviewFileNode>();
			Node->Name = Parts[Index];
			Node->Path = Path;
			Parent->Children.Add(Node);
			if (!bFile)
			{
				Folders.Add(Path, Node);
				Parent = Node;
			}
		}
	}
	if (FileTree.IsValid())
	{
		FileTree->RequestTreeRefresh();
		FileTree->SetItemExpansion(All, true);
		for (const TPair<FString, TSharedPtr<FDreamLocReviewFileNode>>& Folder : Folders)
		{
			FileTree->SetItemExpansion(Folder.Value, true);
		}
	}
}

void SDreamLocTranslateWindow::ShowEntry(TSharedPtr<FDreamLocReviewEntry> InEntry)
{
	Current = InEntry;
	if (Editor.IsValid())
	{
		Editor->Show(InEntry);
	}
	if (List.IsValid())
	{
		TGuardValue<bool> Guard(bSyncingSelection, true);
		List->ClearSelection();
		if (InEntry.IsValid() && Shown.Contains(InEntry))
		{
			List->SetSelection(InEntry, ESelectInfo::Direct);
			List->RequestScrollIntoView(InEntry);
		}
	}
}

void SDreamLocTranslateWindow::SetCulture(const FString& InCulture)
{
	if (InCulture == Culture)
	{
		return;
	}
	KeepEdit();
	Culture = InCulture;
	Current.Reset();
	if (ReferenceCulture == Culture)
	{
		ReferenceCulture.Reset();
	}
	Refresh();
}

void SDreamLocTranslateWindow::SetReferenceCulture(const FString& InCulture)
{
	ReferenceCulture = InCulture == Culture ? FString() : InCulture;
	Refresh();
}

void SDreamLocTranslateWindow::SetFilter(EDreamLocReviewFilter InFilter)
{
	Filter = InFilter;
	RebuildShown();
	ShowEntry(Current);
}

void SDreamLocTranslateWindow::SetSearch(const FString& InSearch)
{
	Search = InSearch;
	RebuildShown();
	ShowEntry(Current);
}

void SDreamLocTranslateWindow::SetFile(const FString& InFile)
{
	File = InFile;
	RebuildShown();
	ShowEntry(Current);
}

void SDreamLocTranslateWindow::SetProfile(const FString& InProfile)
{
	Profile = InProfile;
	if (ProfileCombo.IsValid())
	{
		for (const TSharedPtr<FString>& Choice : ProfileChoices)
		{
			if (*Choice == InProfile)
			{
				ProfileCombo->SetSelectedItem(Choice);
			}
		}
	}
}

bool SDreamLocTranslateWindow::Select(const FString& InNamespace, const FString& InKey)
{
	const TSharedPtr<FDreamLocReviewEntry> Found = Model.Find(InNamespace, InKey);
	if (!Found.IsValid())
	{
		return false;
	}
	KeepEdit();
	const TSharedPtr<FDreamLocReviewEntry> Again = Model.Find(InNamespace, InKey);
	if (!Shown.Contains(Again))
	{
		Filter = EDreamLocReviewFilter::All;
		Search.Reset();
		File.Reset();
		if (SearchBox.IsValid())
		{
			SearchBox->SetText(FText::GetEmpty());
		}
		RebuildShown();
	}
	ShowEntry(Again);
	return true;
}

FText SDreamLocTranslateWindow::FilterLabel(EDreamLocReviewFilter InFilter) const
{
	return FText::Format(LOCTEXT("FilterLabel", "{0} {1}"), DreamLocTranslateWindowLocal::FilterName(InFilter), Model.Count(InFilter, Search, File));
}

FText SDreamLocTranslateWindow::ProfileLabel(const FString& InName) const
{
	if (InName.IsEmpty())
	{
		return LOCTEXT("EachCulturesOwn", "Each culture's own translator");
	}
	if (const FDreamLocTranslateProfile* Found = DreamLocTranslateConfig::FindProfile(Profiles, InName))
	{
		return FText::Format(LOCTEXT("ProfileLabel", "{0} ({1}, {2})"), FText::FromString(Found->Name), FText::FromString(Found->Type),
			Found->IsLocal() ? LOCTEXT("Local", "local") : LOCTEXT("Cloud", "cloud"));
	}
	return FText::FromString(InName);
}

FText SDreamLocTranslateWindow::ProgressText() const
{
	const FDreamLocReviewProgress Progress = Model.GetProgress(File);
	FFormatNamedArguments Args;
	Args.Add(TEXT("Done"), Progress.Done);
	Args.Add(TEXT("Total"), Progress.Total());
	Args.Add(TEXT("Review"), Progress.Review);
	Args.Add(TEXT("Untranslated"), Progress.Untranslated);
	Args.Add(TEXT("Stale"), Progress.Stale);
	return FText::Format(LOCTEXT("Progress", "{Done} of {Total} reviewed   {Review} to review, {Untranslated} untranslated, {Stale} stale"), Args);
}

TSharedPtr<SWidget> SDreamLocTranslateWindow::GetRowWidget(const TSharedPtr<FDreamLocReviewEntry>& InEntry) const
{
	const TSharedPtr<ITableRow> Row = List.IsValid() ? List->WidgetFromItem(InEntry) : nullptr;
	return Row.IsValid() ? Row->AsWidget() : TSharedPtr<SWidget>();
}

TArray<TSharedPtr<FDreamLocReviewEntry>> SDreamLocTranslateWindow::GetSelectedEntries() const
{
	return List.IsValid() ? List->GetSelectedItems() : TArray<TSharedPtr<FDreamLocReviewEntry>>();
}

FString SDreamLocTranslateWindow::GetEditText() const
{
	return Editor.IsValid() ? Editor->GetText() : FString();
}

FString SDreamLocTranslateWindow::GetEditProblems() const
{
	return Editor.IsValid() ? Editor->GetProblems() : FString();
}

FReply SDreamLocTranslateWindow::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	return Editor.IsValid() ? Editor->HandleKey(InKeyEvent) : FReply::Unhandled();
}

// ---------------------------------------------------------------------------------------------------------- decisions

bool SDreamLocTranslateWindow::WriteCurrent(const TOptional<FString>& InText, const FString& InState)
{
	if (!Current.IsValid())
	{
		return false;
	}
	DreamLocTranslateFlow::FReviewEdit Edit = Model.MakeEdit(*Current);
	if (InText.IsSet() && !InText->IsEmpty())
	{
		Edit.Text = InText.GetValue();
		Edit.State = InState;
	}
	const FString Namespace = Current->Entry.Namespace;
	const FString Key = Current->Entry.Key;
	LastErrors.Reset();
	const bool bWritten = Model.Apply({ Edit }, LastErrors);
	StatusText = bWritten ? FText::GetEmpty() : FText::FromString(FString::Join(LastErrors, TEXT("; ")));
	RebuildShown();
	RebuildFiles();
	ShowEntry(Model.Find(Namespace, Key));
	return bWritten;
}

bool SDreamLocTranslateWindow::KeepEdit()
{
	if (!Editor.IsValid() || !Editor->IsChanged())
	{
		return true;
	}
	return WriteCurrent(Editor->GetText(), TEXT("draft"));
}

TSharedPtr<FDreamLocReviewEntry> SDreamLocTranslateWindow::FindAfter(const TSharedPtr<FDreamLocReviewEntry>& InFrom, TFunctionRef<bool(const FDreamLocReviewEntry&)> InWanted) const
{
	const int32 From = InFrom.IsValid() ? Shown.IndexOfByKey(InFrom) : INDEX_NONE;
	for (int32 Step = 1; Step <= Shown.Num(); ++Step)
	{
		const TSharedPtr<FDreamLocReviewEntry>& Each = Shown[(FMath::Max(From, -1) + Step) % Shown.Num()];
		if (Each != InFrom && InWanted(*Each))
		{
			return Each;
		}
	}
	return nullptr;
}

bool SDreamLocTranslateWindow::PassAndNext()
{
	if (!Current.IsValid() || GetEditText().IsEmpty())
	{
		return false;
	}
	const TSharedPtr<FDreamLocReviewEntry> Next = FindAfter(Current, [](const FDreamLocReviewEntry& InEntry) { return InEntry.Status != EDreamLocReviewStatus::Done; });
	const FString NextNamespace = Next.IsValid() ? Next->Entry.Namespace : FString();
	const FString NextKey = Next.IsValid() ? Next->Entry.Key : FString();
	if (!WriteCurrent(GetEditText(), TEXT("done")))
	{
		return false;
	}
	if (Next.IsValid())
	{
		ShowEntry(Model.Find(NextNamespace, NextKey));
	}
	return true;
}

bool SDreamLocTranslateWindow::DraftAndNext()
{
	if (!Current.IsValid())
	{
		return false;
	}
	const TSharedPtr<FDreamLocReviewEntry> Next = FindAfter(Current, [](const FDreamLocReviewEntry&) { return true; });
	const FString NextNamespace = Next.IsValid() ? Next->Entry.Namespace : FString();
	const FString NextKey = Next.IsValid() ? Next->Entry.Key : FString();
	if (!WriteCurrent(GetEditText(), TEXT("draft")))
	{
		return false;
	}
	if (Next.IsValid())
	{
		ShowEntry(Model.Find(NextNamespace, NextKey));
	}
	return true;
}

bool SDreamLocTranslateWindow::SelectNext(int32 InDirection)
{
	if (Shown.IsEmpty())
	{
		return false;
	}
	const FString Namespace = Current.IsValid() ? Current->Entry.Namespace : FString();
	const FString Key = Current.IsValid() ? Current->Entry.Key : FString();
	int32 Index = Current.IsValid() ? Shown.IndexOfByKey(Current) : INDEX_NONE;
	const int32 Target = Index == INDEX_NONE ? 0 : FMath::Clamp(Index + InDirection, 0, Shown.Num() - 1);
	const FString TargetNamespace = Shown[Target]->Entry.Namespace;
	const FString TargetKey = Shown[Target]->Entry.Key;
	KeepEdit();
	ShowEntry(Model.Find(TargetNamespace, TargetKey));
	return true;
}

bool SDreamLocTranslateWindow::ClearCurrent()
{
	return WriteCurrent(TOptional<FString>(), FString());
}

bool SDreamLocTranslateWindow::InsertNextMissing()
{
	if (!Current.IsValid())
	{
		return false;
	}
	const TOptional<FString> Missing = DreamLocChips::NextMissing(Current->Entry.Source, GetEditText());
	return Missing.IsSet() && Editor->InsertAtCursor(Missing.GetValue());
}

bool SDreamLocTranslateWindow::PassEntries(TConstArrayView<TSharedPtr<FDreamLocReviewEntry>> InEntries)
{
	TArray<DreamLocTranslateFlow::FReviewEdit> Edits;
	for (const TSharedPtr<FDreamLocReviewEntry>& Entry : InEntries)
	{
		if (Entry.IsValid() && !Entry->Translation.IsEmpty())
		{
			DreamLocTranslateFlow::FReviewEdit Edit = Model.MakeEdit(*Entry);
			Edit.Text = Entry == Current && Editor->IsChanged() ? Editor->GetText() : Entry->Translation;
			Edit.State = TEXT("done");
			Edits.Add(MoveTemp(Edit));
		}
	}
	if (Edits.IsEmpty())
	{
		return false;
	}
	LastErrors.Reset();
	const bool bWritten = Model.Apply(Edits, LastErrors);
	Reload();
	return bWritten;
}

bool SDreamLocTranslateWindow::RejectEntries(TConstArrayView<TSharedPtr<FDreamLocReviewEntry>> InEntries)
{
	TArray<DreamLocTranslateFlow::FReviewEdit> Edits;
	for (const TSharedPtr<FDreamLocReviewEntry>& Entry : InEntries)
	{
		if (Entry.IsValid() && !Entry->Translation.IsEmpty())
		{
			Edits.Add(Model.MakeEdit(*Entry));
		}
	}
	if (Edits.IsEmpty())
	{
		return false;
	}
	LastErrors.Reset();
	const bool bWritten = Model.Apply(Edits, LastErrors);
	Reload();
	return bWritten;
}

// ---------------------------------------------------------------------------------------------------------- runs

FDreamLocTranslateRequest SDreamLocTranslateWindow::MakeBaseRequest() const
{
	FDreamLocTranslateRequest Request;
	Request.DLocDirectory = DLocDirectory;
	Request.OwnerDirectory = OwnerDirectory;
	Request.Profiles = Profiles;
	Request.Settings = Settings;
	Request.Profile = Profile;
	// The reports beside the sources' project: the project's own Saved/DLoc in the editor.
	Request.ReportDirectory = OwnerDirectory / TEXT("Saved/DLoc");
	return Request;
}

FDreamLocTranslateRequest SDreamLocTranslateWindow::MakeEntriesRequest(TConstArrayView<TSharedPtr<FDreamLocReviewEntry>> InEntries, const FString& InProfile) const
{
	FDreamLocTranslateRequest Request = MakeBaseRequest();
	Request.Cultures = { Culture };
	Request.Profile = InProfile;
	Request.bIncludeUntranslated = true;
	Request.bIncludeStale = true;
	Request.bIncludeMachine = true;
	for (const TSharedPtr<FDreamLocReviewEntry>& Entry : InEntries)
	{
		Request.Entries.Emplace(Entry->Entry.Namespace, Entry->Entry.Key);
	}
	return Request;
}

bool SDreamLocTranslateWindow::Retranslate()
{
	if (!Current.IsValid() || IsRunning())
	{
		return false;
	}
	KeepEdit();
	// A draft or a reviewed translation is the flow's to leave alone: what comes back for it is machine, so it is cleared first.
	if (Current.IsValid() && (Current->Entry.State.Equals(TEXT("draft"), ESearchCase::IgnoreCase) || Current->Status == EDreamLocReviewStatus::Done))
	{
		ClearCurrent();
	}
	return Current.IsValid() && StartRun(MakeEntriesRequest({ Current }, Profile));
}

bool SDreamLocTranslateWindow::TranslateEntries(TConstArrayView<TSharedPtr<FDreamLocReviewEntry>> InEntries, const FString& InProfile)
{
	if (InEntries.IsEmpty() || IsRunning())
	{
		return false;
	}
	KeepEdit();
	return StartRun(MakeEntriesRequest(InEntries, InProfile.IsEmpty() ? Profile : InProfile));
}

bool SDreamLocTranslateWindow::StartRun(const FDreamLocTranslateRequest& InRequest, TFunction<void(const FDreamLocTranslateResult&)> InOnDone)
{
	if (IsRunning())
	{
		return false;
	}
	const TSharedRef<FRunState> State = MakeShared<FRunState>();
	Running = State;
	StatusText = LOCTEXT("Translating", "Translating...");
	const TWeakPtr<SDreamLocTranslateWindow> Weak = SharedThis(this);
	Async(EAsyncExecution::Thread, [State, InRequest, Weak, InOnDone]()
	{
		FDreamLocTranslateResult Result = DreamLocTranslateFlow::Run(InRequest, [State](const FDreamLocTranslateProgress& InProgress)
		{
			FScopeLock Lock(&State->Lock);
			State->Progress = InProgress;
		}, State->Cancel);
		AsyncTask(ENamedThreads::GameThread, [Weak, Result = MoveTemp(Result), InOnDone]()
		{
			if (const TSharedPtr<SDreamLocTranslateWindow> Window = Weak.Pin())
			{
				Window->OnRunFinished(Result, InOnDone);
			}
		});
	});
	return true;
}

void SDreamLocTranslateWindow::OnRunFinished(const FDreamLocTranslateResult& InResult, TFunction<void(const FDreamLocTranslateResult&)> InOnDone)
{
	Running.Reset();
	LastResult = InResult;
	LastErrors = InResult.Errors;
	Reload();
	if (InResult.Errors.Num() > 0)
	{
		StatusText = FText::FromString(FString::Join(InResult.Errors, TEXT("; ")));
	}
	else
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("Translated"), InResult.Translated);
		Args.Add(TEXT("Failed"), InResult.Failed);
		Args.Add(TEXT("Skipped"), InResult.Skipped);
		Args.Add(TEXT("Cancelled"), InResult.bCancelled ? LOCTEXT("WasCancelled", " (cancelled)") : FText::GetEmpty());
		StatusText = FText::Format(LOCTEXT("RunFinished", "{Translated} translated, {Failed} failed, {Skipped} skipped{Cancelled}"), Args);
	}
	if (InOnDone)
	{
		InOnDone(InResult);
	}
}

bool SDreamLocTranslateWindow::IsRunning() const
{
	return Running.IsValid();
}

void SDreamLocTranslateWindow::CancelRun()
{
	if (Running.IsValid())
	{
		Running->Cancel = true;
	}
}

float SDreamLocTranslateWindow::GetRunFraction() const
{
	if (!Running.IsValid())
	{
		return 0.0f;
	}
	FScopeLock Lock(&Running->Lock);
	return Running->Progress.Batches > 0 ? float(Running->Progress.BatchesDone) / float(Running->Progress.Batches) : 0.0f;
}

TSharedPtr<SDreamLocPretranslateDialog> SDreamLocTranslateWindow::OpenPretranslate()
{
	if (const TSharedPtr<SWindow> Open = PretranslateWindow.Pin())
	{
		Open->BringToFront();
		return StaticCastSharedRef<SDreamLocPretranslateDialog>(Open->GetContent());
	}
	const TSharedRef<SDreamLocPretranslateDialog> Dialog = SNew(SDreamLocPretranslateDialog, SharedThis(this));
	// Beside the review window, wherever that is, and as quiet as it is (a test keeps its windows off the screens, never activated).
	const TSharedPtr<SWindow> Parent = FSlateApplication::Get().FindWidgetWindow(AsShared());
	const bool bQuiet = Parent.IsValid() && Parent->ActivationPolicy() == EWindowActivationPolicy::Never;
	const TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("PretranslateTitle", "AI pre-translate"))
		.ClientSize(FVector2D(560.0, 480.0))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		.ActivationPolicy(bQuiet ? EWindowActivationPolicy::Never : EWindowActivationPolicy::Always)
		.FocusWhenFirstShown(!bQuiet)
		[
			Dialog
		];
	if (Parent.IsValid())
	{
		FSlateApplication::Get().AddWindowAsNativeChild(Window, Parent.ToSharedRef(), /*bShowImmediately*/ false);
		const FVector2D Center = FVector2D(Parent->GetPositionInScreen()) + FVector2D(Parent->GetSizeInScreen()) * 0.5;
		Window->MoveWindowTo(Center - FVector2D(Window->GetSizeInScreen()) * 0.5);
		Window->ShowWindow();
	}
	else
	{
		FSlateApplication::Get().AddWindow(Window);
	}
	PretranslateWindow = Window;
	return Dialog;
}

// ---------------------------------------------------------------------------------------------------------- candidates

FString SDreamLocTranslateWindow::CandidateKey(const FDreamLocReviewEntry& InEntry) const
{
	FString Key = Culture;
	Key.AppendChar(TCHAR(1));
	Key += InEntry.GetId();
	Key.AppendChar(TCHAR(1));
	return Key + InEntry.Entry.Source;
}

TArray<FDreamLocCandidate> SDreamLocTranslateWindow::GetCandidates() const
{
	TArray<FDreamLocCandidate> Out;
	if (!Current.IsValid())
	{
		return Out;
	}
	auto Add = [&Out](FDreamLocCandidate InCandidate)
	{
		const bool bHad = Out.ContainsByPredicate([&InCandidate](const FDreamLocCandidate& InHad) { return InHad.Text.Equals(InCandidate.Text, ESearchCase::CaseSensitive); });
		if (!InCandidate.Text.IsEmpty() && !bHad)
		{
			Out.Add(MoveTemp(InCandidate));
		}
	};
	if (const TArray<FDreamLocCandidate>* Found = Asked.Find(CandidateKey(*Current)))
	{
		for (const FDreamLocCandidate& Each : *Found)
		{
			Add(Each);
		}
	}
	for (const FString& Reviewed : Model.FindReviewed(Current->Entry.Source, Current.Get()))
	{
		Add({ Reviewed, LOCTEXT("ReviewedElsewhere", "Reviewed elsewhere").ToString(), FString() });
	}
	if (!Current->Reference.IsEmpty())
	{
		Add({ Current->Reference, FText::Format(LOCTEXT("ReferenceCandidate", "Reference {0}"), FText::FromString(ReferenceCulture)).ToString(), FString() });
	}
	return Out;
}

bool SDreamLocTranslateWindow::ApplyCandidate(int32 InIndex)
{
	const TArray<FDreamLocCandidate> Candidates = GetCandidates();
	if (!Candidates.IsValidIndex(InIndex) || !Editor.IsValid())
	{
		return false;
	}
	Editor->SetText(Candidates[InIndex].Text);
	return true;
}

bool SDreamLocTranslateWindow::MayGoTo(const FDreamLocReviewEntry& InEntry, const FString& InProfile) const
{
	const FDreamLocTranslateProfile* Found = DreamLocTranslateConfig::FindProfile(Profiles, InProfile);
	return Found != nullptr && (Found->IsLocal() || !DreamLocTranslateFlow::IsLocalOnly(Settings.TranslateLocalOnly, InEntry.Entry.File, InEntry.Entry.Namespace));
}

TArray<FString> SDreamLocTranslateWindow::GetCandidateProfiles() const
{
	TArray<FString> Out;
	for (const FDreamLocTranslateProfile& Each : Profiles)
	{
		if (Current.IsValid() && MayGoTo(*Current, Each.Name))
		{
			Out.Add(Each.Name);
		}
	}
	return Out;
}

FString SDreamLocTranslateWindow::GetFixProfile() const
{
	if (!Current.IsValid())
	{
		return FString();
	}
	for (const FString& Name : { Profile, Settings.GetTranslateProfile(Culture), Settings.TranslateFallback })
	{
		const FDreamLocTranslateProfile* Found = Name.IsEmpty() ? nullptr : DreamLocTranslateConfig::FindProfile(Profiles, Name);
		if (Found != nullptr && Found->GetKind() == EDreamLocTranslatorKind::LargeModel && MayGoTo(*Current, Name))
		{
			return Name;
		}
	}
	return FString();
}

bool SDreamLocTranslateWindow::AskForCandidate(const FString& InProfile)
{
	return StartSuggestion(InProfile, InProfile, FString(), FString());
}

bool SDreamLocTranslateWindow::FixWithAI(const FString& InProblem)
{
	const FString Fixer = GetFixProfile();
	const FString Problems = InProblem.IsEmpty() ? GetEditProblems() : InProblem;
	if (Fixer.IsEmpty() || GetEditText().IsEmpty() || Problems.IsEmpty())
	{
		return false;
	}
	return StartSuggestion(Fixer, FText::Format(LOCTEXT("FixFrom", "AI fix ({0})"), FText::FromString(Fixer)).ToString(), GetEditText(), Problems);
}

bool SDreamLocTranslateWindow::StartSuggestion(const FString& InProfile, const FString& InFrom, const FString& InAmendText, const FString& InAmendProblems)
{
	if (!Current.IsValid() || IsRunning() || !MayGoTo(*Current, InProfile))
	{
		return false;
	}
	FDreamLocTranslateRequest Request = MakeEntriesRequest({ Current }, InProfile);
	Request.bSuggest = true;
	Request.bWrite = false;
	Request.AmendText = InAmendText;
	Request.AmendProblems = InAmendProblems;
	const FString Key = CandidateKey(*Current);
	const FString From = InFrom;
	UE_LOG(LogDreamLocTranslate, Verbose, TEXT("asking %s for %s/%s %s%s"), *InProfile, *Culture, *Current->Entry.Namespace, *Current->Entry.Key,
		InAmendText.IsEmpty() ? TEXT("") : TEXT(" (a fix)"));
	// Called by OnRunFinished, on the window: it is there.
	return StartRun(Request, [this, Key, From](const FDreamLocTranslateResult& InResult)
	{
		TArray<FString> Said;
		for (const FDreamLocTranslateRow& Row : InResult.Rows)
		{
			const FString& Text = !Row.NewText.IsEmpty() ? Row.NewText : Row.RejectedText;
			if (!Text.IsEmpty())
			{
				Asked.FindOrAdd(Key).Add({ Text, From, Row.Problems });
			}
			else if (!Row.Reason.IsEmpty())
			{
				Said.Add(Row.Reason);
			}
		}
		Said.Append(InResult.Errors);
		StatusText = Said.IsEmpty() ? FText::Format(LOCTEXT("CandidateCame", "{0}: a candidate"), FText::FromString(From))
			: FText::Format(LOCTEXT("CandidateFailed", "{0}: {1}"), FText::FromString(From), FText::FromString(FString::Join(Said, TEXT("; "))));
		if (Editor.IsValid())
		{
			Editor->RebuildCandidates();
		}
	});
}

// ---------------------------------------------------------------------------------------------------------- the overview

TSharedRef<SWidget> SDreamLocTranslateWindow::MakeOverview()
{
	return SNew(SExpandableArea)
		.InitiallyCollapsed(true)
		// Opened at once: rows still sliding in under the mouse take a click meant for another.
		.AllowAnimatedTransition(false)
		.BorderImage(FAppStyle::GetBrush(TEXT("NoBorder")))
		.HeaderContent()
		[
			SNew(STextBlock)
			.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("DreamLocTranslate.Overview")))
			.Text(LOCTEXT("Overview", "Every culture"))
		]
		.OnAreaExpansionChanged_Lambda([this](bool bInOpen) { SetOverviewOpen(bInOpen); })
		.BodyContent()
		[
			SAssignNew(OverviewBox, SVerticalBox)
		];
}

void SDreamLocTranslateWindow::SetOverviewOpen(bool bInOpen)
{
	bOverviewOpen = bInOpen;
	RebuildOverview();
}

void SDreamLocTranslateWindow::SetOverviewFilesShown(const FString& InCulture, bool bInShown)
{
	if (bInShown)
	{
		OverviewFilesShown.Add(InCulture);
	}
	else
	{
		OverviewFilesShown.Remove(InCulture);
	}
	RebuildOverview();
}

void SDreamLocTranslateWindow::RebuildOverview()
{
	if (!OverviewBox.IsValid())
	{
		return;
	}
	if (!bOverviewOpen)
	{
		OverviewBox->ClearChildren();
		OverviewShown.Reset();
		return;
	}
	Overview = FDreamLocReviewModel::Overview(DLocDirectory);
	// Built again only when what it shows changed: the files are read again often (the window's own writes too), and a
	// row built again under the mouse takes the click it was in the middle of.
	FString Shows = FString::Join(OverviewFilesShown.Array(), TEXT(","));
	auto Describe = [](const FDreamLocReviewProgress& InProgress)
	{
		return FString::Printf(TEXT("%d/%d/%d/%d"), InProgress.Done, InProgress.Review, InProgress.Stale, InProgress.Untranslated);
	};
	for (const FDreamLocCultureProgress& Each : Overview)
	{
		Shows += TEXT("|") + Each.Culture + TEXT(" ") + Describe(Each.Total);
		for (const TPair<FString, FDreamLocReviewProgress>& Part : Each.Files)
		{
			Shows += TEXT(";") + Part.Key + TEXT(" ") + Describe(Part.Value);
		}
	}
	if (Shows == OverviewShown && OverviewBox->NumSlots() > 0)
	{
		return;
	}
	OverviewShown = Shows;
	OverviewBox->ClearChildren();
	for (const FDreamLocCultureProgress& Each : Overview)
	{
		OverviewBox->AddSlot().AutoHeight().Padding(0.0f, 1.0f)[MakeProgressRow(Each.Culture, FString(), Each.Total, true)];
		if (OverviewFilesShown.Contains(Each.Culture))
		{
			for (const TPair<FString, FDreamLocReviewProgress>& Each2 : Each.Files)
			{
				OverviewBox->AddSlot().AutoHeight().Padding(18.0f, 1.0f, 0.0f, 1.0f)[MakeProgressRow(Each.Culture, Each2.Key, Each2.Value, false)];
			}
		}
	}
}

TSharedRef<SWidget> SDreamLocTranslateWindow::MakeProgressRow(const FString& InCulture, const FString& InFile, const FDreamLocReviewProgress& InProgress, bool bInCultureRow)
{
	using namespace DreamLocTranslateWindowLocal;
	TSharedRef<SHorizontalBox> Bar = SNew(SHorizontalBox);
	const FString Where = InFile.IsEmpty() ? InCulture : InCulture + TEXT(".") + InFile;
	struct FSegment
	{
		int32 Count;
		EDreamLocReviewStatus Status;
		EDreamLocReviewFilter Filter;
	};
	const FSegment Segments[] = { { InProgress.Done, EDreamLocReviewStatus::Done, EDreamLocReviewFilter::Done },
		{ InProgress.Review, EDreamLocReviewStatus::Review, EDreamLocReviewFilter::Review }, { InProgress.Stale, EDreamLocReviewStatus::Stale, EDreamLocReviewFilter::Stale },
		{ InProgress.Untranslated, EDreamLocReviewStatus::Untranslated, EDreamLocReviewFilter::Untranslated } };
	for (const FSegment& Segment : Segments)
	{
		if (Segment.Count <= 0)
		{
			continue;
		}
		const EDreamLocReviewFilter Chosen = Segment.Filter;
		const FLinearColor Color = Segment.Status == EDreamLocReviewStatus::Untranslated ? FLinearColor(0.3f, 0.3f, 0.3f) : DotColor(Segment.Status);
		Bar->AddSlot().FillWidth(float(Segment.Count))
		[
			SNew(SButton)
			.AddMetaData<FTagMetaData>(FTagMetaData(*FString::Printf(TEXT("DreamLocTranslate.Overview.%s.%s"), *Where, *FilterName(Chosen).ToString())))
			.ButtonStyle(FAppStyle::Get(), TEXT("NoBorder"))
			.ContentPadding(FMargin(0.0f, 0.0f, 1.0f, 0.0f))
			.ToolTipText(FText::Format(LOCTEXT("SegmentTip", "{0}: {1}"), FilterName(Chosen), Segment.Count))
			.OnClicked_Lambda([this, InCulture, InFile, Chosen]()
			{
				SetCulture(InCulture);
				SetFile(InFile);
				SetFilter(Chosen);
				return FReply::Handled();
			})
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush(TEXT("WhiteBrush")))
				.BorderBackgroundColor(Color)
				.Padding(0.0f)
				[
					SNew(SBox).HeightOverride(10.0f)
				]
			]
		];
	}
	FFormatNamedArguments Args;
	Args.Add(TEXT("Done"), InProgress.Done);
	Args.Add(TEXT("Total"), InProgress.Total());
	Args.Add(TEXT("Review"), InProgress.Review);
	Args.Add(TEXT("Stale"), InProgress.Stale);
	Args.Add(TEXT("Untranslated"), InProgress.Untranslated);
	const bool bFilesShown = OverviewFilesShown.Contains(InCulture);
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(18.0f)
			[
				!bInCultureRow ? SNullWidget::NullWidget :
				SNew(SButton)
				.AddMetaData<FTagMetaData>(FTagMetaData(*(TEXT("DreamLocTranslate.OverviewFiles.") + InCulture)))
				.ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
				.ContentPadding(0.0f)
				.ToolTipText(LOCTEXT("OverviewFilesTip", "Its files"))
				.OnClicked_Lambda([this, InCulture, bFilesShown]() { SetOverviewFilesShown(InCulture, !bFilesShown); return FReply::Handled(); })
				[
					SNew(SImage).Image(FAppStyle::GetBrush(bFilesShown ? TEXT("TreeArrow_Expanded") : TEXT("TreeArrow_Collapsed")))
				]
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			SNew(SBox).WidthOverride(bInCultureRow ? 90.0f : 160.0f)
			[
				SNew(STextBlock).Text(FText::FromString(bInCultureRow ? InCulture : InFile))
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[Bar]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(FText::Format(LOCTEXT("OverviewCounts", "{Done}/{Total} reviewed, {Review} to review, {Stale} stale, {Untranslated} untranslated"), Args))
		];
}

// ---------------------------------------------------------------------------------------------------------- the files

void SDreamLocTranslateWindow::WatchDirectory()
{
	FDirectoryWatcherModule& Module = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));
	IDirectoryWatcher* Watcher = Module.Get();
	if (Watcher == nullptr || DLocDirectory.IsEmpty() || !IFileManager::Get().DirectoryExists(*DLocDirectory))
	{
		return;
	}
	const TWeakPtr<SDreamLocTranslateWindow> Weak = SharedThis(this);
	if (Watcher->RegisterDirectoryChangedCallback_Handle(DLocDirectory, IDirectoryWatcher::FDirectoryChanged::CreateLambda([Weak](const TArray<FFileChangeData>& InChanges)
	{
		const TSharedPtr<SDreamLocTranslateWindow> Window = Weak.Pin();
		if (!Window.IsValid())
		{
			return;
		}
		for (const FFileChangeData& Change : InChanges)
		{
			const FString Extension = FPaths::GetExtension(Change.Filename, /*bIncludeDot*/ true);
			if (Extension.Equals(DreamLocFile::Extension, ESearchCase::IgnoreCase) || FPaths::GetCleanFilename(Change.Filename).Equals(FDreamLocSettings::FileName, ESearchCase::IgnoreCase))
			{
				// Read again once the changes stop for a moment.
				Window->PendingReload = FPlatformTime::Seconds() + 0.3;
				if (!Window->bReloadTimer)
				{
					Window->bReloadTimer = true;
					Window->RegisterActiveTimer(0.1f, FWidgetActiveTimerDelegate::CreateSP(Window.ToSharedRef(), &SDreamLocTranslateWindow::ReloadWhenQuiet));
				}
				return;
			}
		}
	}), WatchHandle, IDirectoryWatcher::WatchOptions::IncludeDirectoryChanges))
	{
		WatchedDirectory = DLocDirectory;
	}
}

void SDreamLocTranslateWindow::UnwatchDirectory()
{
	if (WatchedDirectory.IsEmpty() || !FModuleManager::Get().IsModuleLoaded(TEXT("DirectoryWatcher")))
	{
		return;
	}
	if (IDirectoryWatcher* Watcher = FModuleManager::GetModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher")).Get())
	{
		Watcher->UnregisterDirectoryChangedCallback_Handle(WatchedDirectory, WatchHandle);
	}
	WatchedDirectory.Reset();
}

EActiveTimerReturnType SDreamLocTranslateWindow::ReloadWhenQuiet(double InCurrentTime, float InDeltaTime)
{
	if (FPlatformTime::Seconds() < PendingReload)
	{
		return EActiveTimerReturnType::Continue;
	}
	bReloadTimer = false;
	Reload();
	return EActiveTimerReturnType::Stop;
}

void SDreamLocTranslateWindow::OpenInFile(const TSharedPtr<FDreamLocReviewEntry>& InEntry) const
{
	if (!InEntry.IsValid())
	{
		return;
	}
	const FString Path = FPaths::ConvertRelativePathToFull(DLocDirectory / Culture / InEntry->Entry.File);
	const int32 Line = DreamLocTranslateFlow::FindEntryLine(DLocDirectory, Culture, InEntry->Entry.File, InEntry->Entry.Namespace, InEntry->Entry.Key);
	if (!FSourceCodeNavigation::OpenSourceFile(Path, FMath::Max(1, Line), 1))
	{
		FPlatformProcess::LaunchFileInDefaultExternalApplication(*Path);
	}
}

void SDreamLocTranslateWindow::OpenSource(const TSharedPtr<FDreamLocReviewEntry>& InEntry) const
{
	if (!InEntry.IsValid() || InEntry->Entry.At.IsEmpty())
	{
		return;
	}
	const FString Source = DreamLocExtract::GetAtSource(InEntry->Entry.At);
	// An asset (/Game/...) opens in its editor; a file (a .dui, a .dna, C++) in the code editor.
	if (Source.StartsWith(TEXT("/")))
	{
		if (UObject* Asset = FSoftObjectPath(Source).TryLoad(); Asset != nullptr && GEditor != nullptr)
		{
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Asset);
		}
		return;
	}
	const FString Path = FPaths::ConvertRelativePathToFull(OwnerDirectory / Source);
	if (!FSourceCodeNavigation::OpenSourceFile(Path, 1, 1))
	{
		FPlatformProcess::LaunchFileInDefaultExternalApplication(*Path);
	}
}

#undef LOCTEXT_NAMESPACE
