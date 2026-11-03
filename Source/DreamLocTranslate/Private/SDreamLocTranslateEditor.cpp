// Copyright DreamMoon. All Rights Reserved.

#include "SDreamLocTranslateEditor.h"

#include "DreamLocChips.h"
#include "SDreamLocTranslateWindow.h"
#include "Framework/Application/SlateApplication.h"
#include "Internationalization/TextChar.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Types/ISlateMetaData.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SHyperlink.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "DreamLocTranslateEditor"

namespace DreamLocTranslateEditorLocal
{
	/** The box the game has for the text and what the text needs, to scale: red where it does not fit. */
	class SFitPreview : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SFitPreview) {}
			SLATE_ATTRIBUTE(TOptional<FDreamLocFitResult>, Fit)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			Fit = InArgs._Fit;
		}

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
			int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
		{
			const TOptional<FDreamLocFitResult> Shown = Fit.Get();
			if (!Shown.IsSet())
			{
				return LayerId;
			}
			const FVector2f Size = FVector2f(AllottedGeometry.GetLocalSize());
			const float Wide = FMath::Max3(Shown->Box.X, Shown->Needed.X, 1.0f);
			const float Tall = FMath::Max3(Shown->Box.Y, Shown->Needed.Y, 1.0f);
			const float Scale = FMath::Max(0.01f, FMath::Min((Size.X - 2.0f) / Wide, (Size.Y - 2.0f) / Tall));
			const FLinearColor Needed = Shown->bOverflows ? FLinearColor(0.75f, 0.25f, 0.22f, 0.45f) : FLinearColor(0.36f, 0.62f, 0.38f, 0.35f);
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(Shown->Needed * Scale, FSlateLayoutTransform(FVector2f(1.0f, 1.0f))),
				FAppStyle::GetBrush(TEXT("WhiteBrush")), ESlateDrawEffect::None, Needed);
			const FVector2f Box = Shown->Box * Scale;
			TArray<FVector2f> Outline = { FVector2f(1.0f, 1.0f), FVector2f(Box.X + 1.0f, 1.0f), FVector2f(Box.X + 1.0f, Box.Y + 1.0f), FVector2f(1.0f, Box.Y + 1.0f), FVector2f(1.0f, 1.0f) };
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(), Outline, ESlateDrawEffect::None, FLinearColor(0.8f, 0.8f, 0.8f), true, 1.0f);
			return LayerId + 1;
		}

		virtual FVector2D ComputeDesiredSize(float) const override
		{
			return Fit.Get().IsSet() ? FVector2D(240.0, 56.0) : FVector2D::ZeroVector;
		}

	private:
		TAttribute<TOptional<FDreamLocFitResult>> Fit;
	};

	FText StatusOf(const FDreamLocReviewEntry& InEntry)
	{
		switch (InEntry.Status)
		{
		case EDreamLocReviewStatus::Untranslated: return LOCTEXT("Untranslated", "not translated");
		case EDreamLocReviewStatus::Stale: return LOCTEXT("Stale", "stale: the source changed since it was translated");
		case EDreamLocReviewStatus::Done: return LOCTEXT("Done", "reviewed");
		default:
			return InEntry.Entry.State.Equals(TEXT("draft"), ESearchCase::IgnoreCase) ? LOCTEXT("Draft", "draft, to review")
				: InEntry.Entry.State.IsEmpty() ? LOCTEXT("Unreviewed", "to review") : LOCTEXT("Machine", "machine translated, to review");
		}
	}

	/** The note, the length limit, what the source tool says (the speaker, the line before...), one a line. */
	FString DetailsOf(const FDreamLocReviewEntry& InEntry)
	{
		TArray<FString> Lines;
		const FDreamLocExchangeEntry& Entry = InEntry.Entry;
		if (!Entry.Note.IsEmpty())
		{
			Lines.Add(FText::Format(LOCTEXT("Note", "Note: {0}"), FText::FromString(Entry.Note)).ToString());
		}
		if (Entry.Max >= 0)
		{
			Lines.Add(FText::Format(LOCTEXT("Max", "At most {0} wide (a full-width character counts 2)"), Entry.Max).ToString());
		}
		if (!Entry.Kind.IsEmpty())
		{
			Lines.Add(FText::Format(LOCTEXT("Kind", "Kind: {0}"), FText::FromString(Entry.Kind)).ToString());
		}
		for (const FDreamLocExtractContext& Context : Entry.Context)
		{
			Lines.Add(Context.Name + TEXT(": ") + Context.Describe());
		}
		return FString::Join(Lines, TEXT("\n"));
	}
}

void SDreamLocTranslateEditor::Construct(const FArguments& InArgs, const TSharedRef<SDreamLocTranslateWindow>& InWindow)
{
	using namespace DreamLocTranslateEditorLocal;
	Window = InWindow;
	const FEditableTextBoxStyle& BoxStyle = FAppStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>(TEXT("NormalEditableTextBox"));
	auto Heading = [](const FText& InText) -> TSharedRef<SWidget>
	{
		return SNew(STextBlock).Text(InText).Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")));
	};
	auto Visible = [this]() { return Entry.IsValid() ? EVisibility::Visible : EVisibility::Collapsed; };

	ChildSlot
	[
		SNew(SScrollBox)
		+ SScrollBox::Slot().Padding(8.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Visibility_Lambda([this]() { return Entry.IsValid() ? EVisibility::Collapsed : EVisibility::Visible; })
				.Text(LOCTEXT("NoEntry", "Choose an entry on the left."))
			]
			// Where it is: the .dloc line, and the source it comes from.
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SNew(SHorizontalBox).Visibility_Lambda(Visible)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 12.0f, 0.0f)
				[
					SNew(SHyperlink)
					.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("DreamLocTranslate.Location")))
					.Text_Lambda([this]() { return Entry.IsValid() ? FText::FromString(Entry->Entry.Namespace + TEXT(" / ") + Entry->Entry.Key) : FText::GetEmpty(); })
					.ToolTipText(LOCTEXT("LocationTip", "Open the .dloc file at this entry"))
					.OnNavigate_Lambda([this]() { if (const TSharedPtr<SDreamLocTranslateWindow> Pinned = Window.Pin()) { Pinned->OpenInFile(Entry); } })
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SHyperlink)
					.Visibility_Lambda([this]() { return Entry.IsValid() && !Entry->Entry.At.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed; })
					.Text_Lambda([this]() { return Entry.IsValid() ? FText::Format(LOCTEXT("From", "from {0}"), FText::FromString(Entry->Entry.At)) : FText::GetEmpty(); })
					.ToolTipText(LOCTEXT("FromTip", "Open the source this text comes from"))
					.OnNavigate_Lambda([this]() { if (const TSharedPtr<SDreamLocTranslateWindow> Pinned = Window.Pin()) { Pinned->OpenSource(Entry); } })
				]
			]
			// The source: what changed, for a stale entry.
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f)
			[
				SNew(SVerticalBox).Visibility_Lambda(Visible)
				+ SVerticalBox::Slot().AutoHeight()[Heading(LOCTEXT("Source", "Source"))]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
				[
					SNew(SBorder).BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder"))).Padding(6.0f)
					[
						SNew(SDreamLocChipText)
						.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("DreamLocTranslate.EditorSource")))
						.MultiLine(true)
						.Text_Lambda([this]() { return Entry.IsValid() ? Entry->Entry.Source : FString(); })
						.OldText_Lambda([this]() { return Entry.IsValid() ? Entry->Entry.Prev : FString(); })
					]
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Visibility_Lambda([this]() { return Entry.IsValid() && !Entry->Entry.Prev.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed; })
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text(LOCTEXT("StaleHint", "Struck through: what the old source had; marked: what the new one has."))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text_Lambda([this]() { return Entry.IsValid() ? FText::FromString(DetailsOf(*Entry)) : FText::GetEmpty(); })
				]
			]
			// The translation.
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 2.0f)
			[
				SNew(SHorizontalBox).Visibility_Lambda(Visible)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)[Heading(LOCTEXT("Translation", "Translation"))]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text_Lambda([this]()
					{
						if (!Entry.IsValid())
						{
							return FText::GetEmpty();
						}
						return IsChanged() ? LOCTEXT("Changed", "changed: Ctrl+Enter to pass it, Ctrl+Shift+Enter to keep it as a draft") : StatusOf(*Entry);
					})
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox).Visibility_Lambda(Visible).MinDesiredHeight(72.0f)
				[
					SAssignNew(Box, SMultiLineEditableTextBox)
					.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("DreamLocTranslate.Translation")))
					.Marshaller(DreamLocChips::MakeMarshaller(BoxStyle.TextStyle))
					.AutoWrapText(true)
					.OnTextChanged(this, &SDreamLocTranslateEditor::OnBoxChanged)
					.OnKeyDownHandler_Lambda([this](const FGeometry&, const FKeyEvent& InKeyEvent) { return HandleKey(InKeyEvent); })
				]
			]
			// What the checks say, as it is typed.
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 2.0f)
			[
				SNew(SVerticalBox).Visibility_Lambda(Visible)
				+ SVerticalBox::Slot().AutoHeight()[Heading(LOCTEXT("Checks", "Checks"))]
				+ SVerticalBox::Slot().AutoHeight()[SAssignNew(ProblemList, SVerticalBox)]
			]
			// What the translators give, and translations already there: a click puts one in the box.
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 2.0f)
			[
				SNew(SVerticalBox).Visibility_Lambda(Visible)
				+ SVerticalBox::Slot().AutoHeight()[Heading(LOCTEXT("Candidates", "Candidates"))]
				+ SVerticalBox::Slot().AutoHeight()[SAssignNew(CandidateList, SVerticalBox)]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)[SAssignNew(AskBar, SWrapBox).UseAllottedSize(true)]
			]
			// Where the game shows it, when a tool lays it out (DreamGUI, for a .dui text).
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 2.0f)
			[
				SNew(SVerticalBox)
				.Visibility_Lambda([this]() { return Entry.IsValid() && Fit.IsSet() ? EVisibility::Visible : EVisibility::Collapsed; })
				+ SVerticalBox::Slot().AutoHeight()[Heading(LOCTEXT("Preview", "Where it is shown"))]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
				[
					SNew(DreamLocTranslateEditorLocal::SFitPreview).Fit_Lambda([this]() { return Fit; })
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("DreamLocTranslate.Preview")))
					.AutoWrapText(true)
					.ColorAndOpacity_Lambda([this]() { return Fit.IsSet() && (Fit->bOverflows || Fit->MissingGlyphs.Num() > 0) ? FSlateColor(FStyleColors::Error) : FSlateColor::UseSubduedForeground(); })
					.Text_Lambda([this]() { return GetPreviewText(); })
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 10.0f, 0.0f, 0.0f)
			[
				SNew(SHorizontalBox).Visibility_Lambda(Visible)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(SButton)
					.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("DreamLocTranslate.PassNext")))
					.Text(LOCTEXT("PassNext", "Pass and next"))
					.ToolTipText(LOCTEXT("PassNextTip", "Mark this translation reviewed (state done) and go to the next entry not done (Ctrl+Enter)"))
					.IsEnabled_Lambda([this]() { return !Text.IsEmpty(); })
					.OnClicked_Lambda([this]() { if (const TSharedPtr<SDreamLocTranslateWindow> Pinned = Window.Pin()) { Pinned->PassAndNext(); } return FReply::Handled(); })
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(SButton)
					.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("DreamLocTranslate.Retranslate")))
					.Text(LOCTEXT("Retranslate", "Translate again"))
					.ToolTipText(LOCTEXT("RetranslateTip", "Have the translator translate this entry again (Ctrl+R)"))
					.IsEnabled_Lambda([this]() { const TSharedPtr<SDreamLocTranslateWindow> Pinned = Window.Pin(); return Pinned.IsValid() && !Pinned->IsRunning(); })
					.OnClicked_Lambda([this]() { if (const TSharedPtr<SDreamLocTranslateWindow> Pinned = Window.Pin()) { Pinned->Retranslate(); } return FReply::Handled(); })
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
					.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("DreamLocTranslate.Clear")))
					.Text(LOCTEXT("Clear", "Clear"))
					.ToolTipText(LOCTEXT("ClearTip", "Take the translation out"))
					.IsEnabled_Lambda([this]() { return Entry.IsValid() && !Entry->Translation.IsEmpty(); })
					.OnClicked_Lambda([this]() { if (const TSharedPtr<SDreamLocTranslateWindow> Pinned = Window.Pin()) { Pinned->ClearCurrent(); } return FReply::Handled(); })
				]
			]
		]
	];
	Show(nullptr);
}

void SDreamLocTranslateEditor::Show(const TSharedPtr<FDreamLocReviewEntry>& InEntry)
{
	Entry = InEntry;
	Text = Entry.IsValid() ? Entry->Translation : FString();
	bFixPending = false;
	bFixing = true;
	Box->SetText(FText::FromString(Text));
	bFixing = false;
	CheckNow();
	RebuildCandidates();
	RebuildAsk();
}

FString SDreamLocTranslateEditor::GetText() const
{
	return Text;
}

void SDreamLocTranslateEditor::SetText(const FString& InText)
{
	Text = InText;
	bFixPending = false;
	bFixing = true;
	Box->SetText(FText::FromString(InText));
	bFixing = false;
	CheckNow();
}

bool SDreamLocTranslateEditor::IsChanged() const
{
	return Entry.IsValid() && !Text.Equals(Entry->Translation, ESearchCase::CaseSensitive);
}

bool SDreamLocTranslateEditor::InsertAtCursor(const FString& InText)
{
	if (!Entry.IsValid())
	{
		return false;
	}
	Box->InsertTextAtCursor(InText);
	return true;
}

void SDreamLocTranslateEditor::FocusBox()
{
	FSlateApplication::Get().SetKeyboardFocus(Box, EFocusCause::SetDirectly);
}

void SDreamLocTranslateEditor::OnBoxChanged(const FText& InText)
{
	if (bFixing)
	{
		return;
	}
	// The box joins its lines with the platform's line end (\r\n on Windows); a .dloc's are \n.
	const FString Now = InText.ToString().Replace(TEXT("\r\n"), TEXT("\n"));
	FString Fixed;
	int32 Cursor = 0;
	// While a fix waits, the box still holds the edit before it: the next edit is measured from there.
	const FString Before = bFixPending ? BeforeFix : Text;
	if (DreamLocChips::FixEdit(Before, Now, Fixed, Cursor))
	{
		// A placeholder is kept whole: the edit is put right, the cursor where the edit left off. Not from in here: the
		// box calls this in the middle of its own edit, and a live marshaller then lays out the text it had, setting
		// it back (FSlateEditableTextLayout::EndEditTransaction, ForceRefreshTextLayout). Once the edit is over.
		if (!bFixPending)
		{
			BeforeFix = Text;
		}
		Text = Fixed;
		FixCursor = Cursor;
		bFixPending = true;
		if (!bFixTimer)
		{
			bFixTimer = true;
			RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SDreamLocTranslateEditor::ApplyFix));
		}
	}
	else
	{
		Text = Now;
		bFixPending = false;
	}
	CheckAt = FPlatformTime::Seconds() + 0.25;
	if (!bCheckPending)
	{
		bCheckPending = true;
		RegisterActiveTimer(0.05f, FWidgetActiveTimerDelegate::CreateSP(this, &SDreamLocTranslateEditor::CheckLater));
	}
}

EActiveTimerReturnType SDreamLocTranslateEditor::ApplyFix(double InCurrentTime, float InDeltaTime)
{
	bFixTimer = false;
	if (bFixPending)
	{
		bFixPending = false;
		bFixing = true;
		Box->SetText(FText::FromString(Text));
		int32 Line = 0;
		int32 Column = 0;
		DreamLocChips::ToLineAndColumn(Text, FixCursor, Line, Column);
		Box->GoTo(FTextLocation(Line, Column));
		bFixing = false;
	}
	return EActiveTimerReturnType::Stop;
}

EActiveTimerReturnType SDreamLocTranslateEditor::CheckLater(double InCurrentTime, float InDeltaTime)
{
	if (FPlatformTime::Seconds() < CheckAt)
	{
		return EActiveTimerReturnType::Continue;
	}
	bCheckPending = false;
	CheckNow();
	return EActiveTimerReturnType::Stop;
}

void SDreamLocTranslateEditor::CheckNow()
{
	const TSharedPtr<SDreamLocTranslateWindow> Pinned = Window.Pin();
	Problems = Entry.IsValid() && Pinned.IsValid() ? Pinned->GetModel().Check(*Entry, Text) : FString();
	Fit = Entry.IsValid() && Pinned.IsValid() ? Pinned->GetModel().Measure(*Entry, Text) : TOptional<FDreamLocFitResult>();
	RebuildProblems();
}

FText SDreamLocTranslateEditor::GetPreviewText() const
{
	if (!Fit.IsSet())
	{
		return FText::GetEmpty();
	}
	FFormatNamedArguments Args;
	Args.Add(TEXT("NeedX"), FMath::RoundToInt(Fit->Needed.X));
	Args.Add(TEXT("NeedY"), FMath::RoundToInt(Fit->Needed.Y));
	Args.Add(TEXT("BoxX"), FMath::RoundToInt(Fit->Box.X));
	Args.Add(TEXT("BoxY"), FMath::RoundToInt(Fit->Box.Y));
	Args.Add(TEXT("Where"), FText::FromString(Fit->Where));
	FText Out = Fit->bOverflows
		? FText::Format(LOCTEXT("DoesNotFit", "Does not fit: it needs {NeedX} x {NeedY}, the box is {BoxX} x {BoxY} ({Where})"), Args)
		: FText::Format(LOCTEXT("Fits", "Fits: it needs {NeedX} x {NeedY} of the {BoxX} x {BoxY} box ({Where})"), Args);
	if (Fit->MissingGlyphs.Num() > 0)
	{
		TArray<FString> Glyphs;
		for (const uint32 Glyph : Fit->MissingGlyphs)
		{
			FString One;
			if (FTextChar::ConvertCodepointToString(Glyph, One))
			{
				Glyphs.Add(One);
			}
		}
		Out = FText::Format(LOCTEXT("FitAndGlyphs", "{0}\nThe font lacks: {1}"), Out, FText::FromString(FString::Join(Glyphs, TEXT(" "))));
	}
	return Out;
}

void SDreamLocTranslateEditor::RebuildCandidates()
{
	const TSharedPtr<SDreamLocTranslateWindow> Pinned = Window.Pin();
	if (!CandidateList.IsValid() || !Pinned.IsValid())
	{
		return;
	}
	CandidateList->ClearChildren();
	const TArray<FDreamLocCandidate> Candidates = Pinned->GetCandidates();
	if (Candidates.IsEmpty())
	{
		CandidateList->AddSlot().AutoHeight()
		[
			SNew(STextBlock).ColorAndOpacity(FSlateColor::UseSubduedForeground()).Text(LOCTEXT("NoCandidates", "None yet: ask a translator below."))
		];
	}
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		const FDreamLocCandidate& Candidate = Candidates[Index];
		CandidateList->AddSlot().AutoHeight().Padding(0.0f, 1.0f)
		[
			SNew(SButton)
			.AddMetaData<FTagMetaData>(FTagMetaData(*FString::Printf(TEXT("DreamLocTranslate.Candidate.%d"), Index + 1)))
			.ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
			.ToolTipText(LOCTEXT("CandidateTip", "Put it in the box (a change: Ctrl+Enter passes it)"))
			.OnClicked_Lambda([this, Index]() { if (const TSharedPtr<SDreamLocTranslateWindow> Owner = Window.Pin()) { Owner->ApplyCandidate(Index); } return FReply::Handled(); })
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(SBox).WidthOverride(44.0f)
					[
						SNew(STextBlock).ColorAndOpacity(FSlateColor::UseSubduedForeground())
						.Text(Index < 9 ? FText::Format(LOCTEXT("CandidateKey", "Ctrl+{0}"), Index + 1) : FText::GetEmpty())
					]
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f)[SNew(SDreamLocChipText).MultiLine(true).Text(Candidate.Text)]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(6.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock).ColorAndOpacity(FSlateColor::UseSubduedForeground()).Text(FText::FromString(Candidate.From))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SImage)
					.Visibility(Candidate.Problems.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
					.Image(FAppStyle::GetBrush(TEXT("Icons.Warning")))
					.ColorAndOpacity(FStyleColors::Warning)
					.DesiredSizeOverride(FVector2D(14.0, 14.0))
					.ToolTipText(FText::FromString(Candidate.Problems.Replace(TEXT("; "), TEXT("\n"))))
				]
			]
		];
	}
}

void SDreamLocTranslateEditor::RebuildAsk()
{
	const TSharedPtr<SDreamLocTranslateWindow> Pinned = Window.Pin();
	if (!AskBar.IsValid() || !Pinned.IsValid())
	{
		return;
	}
	AskBar->ClearChildren();
	for (const FString& Name : Entry.IsValid() ? Pinned->GetCandidateProfiles() : TArray<FString>())
	{
		AskBar->AddSlot().Padding(0.0f, 0.0f, 4.0f, 2.0f)
		[
			SNew(SButton)
			.AddMetaData<FTagMetaData>(FTagMetaData(*(TEXT("DreamLocTranslate.Ask.") + Name)))
			.Text(FText::Format(LOCTEXT("Ask", "Ask {0}"), FText::FromString(Name)))
			.ToolTipText(FText::Format(LOCTEXT("AskTip", "Send this entry to {0}; what comes back is a candidate, nothing is written"), Pinned->ProfileLabel(Name)))
			.IsEnabled_Lambda([this]() { const TSharedPtr<SDreamLocTranslateWindow> Owner = Window.Pin(); return Owner.IsValid() && !Owner->IsRunning(); })
			.OnClicked_Lambda([this, Name]() { if (const TSharedPtr<SDreamLocTranslateWindow> Owner = Window.Pin()) { Owner->AskForCandidate(Name); } return FReply::Handled(); })
		];
	}
	if (Entry.IsValid() && Pinned->GetCandidateProfiles().IsEmpty())
	{
		AskBar->AddSlot()
		[
			SNew(STextBlock).ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(LOCTEXT("NoTranslatorToAsk", "No translator this entry may go to (Editor Preferences > Plugins > DreamLoc translators)."))
		];
	}
}

void SDreamLocTranslateEditor::RebuildProblems()
{
	ProblemList->ClearChildren();
	TArray<FString> Lines;
	Problems.ParseIntoArray(Lines, TEXT("; "));
	if (Lines.IsEmpty() && Entry.IsValid())
	{
		ProblemList->AddSlot().AutoHeight()
		[
			SNew(STextBlock).ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(Text.IsEmpty() ? LOCTEXT("NothingToCheck", "Nothing to check: no translation.") : LOCTEXT("NoProblems", "Nothing to say."))
		];
	}
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		const FString Line = Lines[Index];
		// A font without a glyph is the font's to fix: no AI fix for it.
		const bool bNote = Line.StartsWith(TEXT("DLOC4002"));
		ProblemList->AddSlot().AutoHeight().Padding(0.0f, 1.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.ColorAndOpacity(bNote ? FSlateColor::UseSubduedForeground() : FSlateColor(FStyleColors::Warning))
				.Text(FText::FromString(Line))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.AddMetaData<FTagMetaData>(FTagMetaData(*FString::Printf(TEXT("DreamLocTranslate.Fix.%d"), Index + 1)))
				.Visibility(bNote ? EVisibility::Collapsed : EVisibility::Visible)
				.Text(LOCTEXT("Fix", "AI fix"))
				.ToolTipText_Lambda([this]()
				{
					const TSharedPtr<SDreamLocTranslateWindow> Owner = Window.Pin();
					const FString Fixer = Owner.IsValid() ? Owner->GetFixProfile() : FString();
					return Fixer.IsEmpty() ? LOCTEXT("NoFixer", "No large model this entry may go to: set one up in Editor Preferences > Plugins > DreamLoc translators")
						: FText::Format(LOCTEXT("FixTip", "Send the translation and this problem to {0}; what comes back is a candidate (Ctrl+Shift+F: every problem)"), FText::FromString(Fixer));
				})
				.IsEnabled_Lambda([this]()
				{
					const TSharedPtr<SDreamLocTranslateWindow> Owner = Window.Pin();
					return Owner.IsValid() && !Owner->IsRunning() && !Text.IsEmpty() && !Owner->GetFixProfile().IsEmpty();
				})
				.OnClicked_Lambda([this, Line]() { if (const TSharedPtr<SDreamLocTranslateWindow> Owner = Window.Pin()) { Owner->FixWithAI(Line); } return FReply::Handled(); })
			]
		];
	}
}

FReply SDreamLocTranslateEditor::HandleKey(const FKeyEvent& InKeyEvent)
{
	const TSharedPtr<SDreamLocTranslateWindow> Pinned = Window.Pin();
	if (!Pinned.IsValid())
	{
		return FReply::Unhandled();
	}
	const FKey Key = InKeyEvent.GetKey();
	const bool bControl = InKeyEvent.IsControlDown();
	const bool bShift = InKeyEvent.IsShiftDown();
	const bool bAlt = InKeyEvent.IsAltDown();
	if (Key == EKeys::Enter && bControl)
	{
		bShift ? Pinned->DraftAndNext() : Pinned->PassAndNext();
		return FReply::Handled();
	}
	if (bAlt && (Key == EKeys::Up || Key == EKeys::Down))
	{
		Pinned->SelectNext(Key == EKeys::Up ? -1 : 1);
		return FReply::Handled();
	}
	if (Key == EKeys::R && bControl && !bShift)
	{
		Pinned->Retranslate();
		return FReply::Handled();
	}
	if (Key == EKeys::F && bControl && bShift)
	{
		Pinned->FixWithAI();
		return FReply::Handled();
	}
	if (bControl && !bShift && !bAlt)
	{
		const FKey Digits[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine };
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Digits); ++Index)
		{
			if (Key == Digits[Index])
			{
				Pinned->ApplyCandidate(Index);
				return FReply::Handled();
			}
		}
	}
	if (Key == EKeys::F3)
	{
		Pinned->InsertNextMissing();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
