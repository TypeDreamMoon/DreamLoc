// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamLocReviewModel.h"
#include "Widgets/SCompoundWidget.h"

class SDreamLocTranslateWindow;
class SMultiLineEditableTextBox;
class SVerticalBox;

/**
 * The review window's right side: where the entry comes from, its source (what changed, for a stale one), its translation
 * with the placeholders as chips, and what the checks say about it, checked again as it is typed.
 */
class SDreamLocTranslateEditor : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDreamLocTranslateEditor) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<SDreamLocTranslateWindow>& InWindow);

	/** The entry to edit; null for none. Its translation goes into the box, unchanged. */
	void Show(const TSharedPtr<FDreamLocReviewEntry>& InEntry);
	const TSharedPtr<FDreamLocReviewEntry>& GetEntry() const { return Entry; }
	/** The same entry read again: the box, the cursor and the undo left as they are. */
	void Rebind(const TSharedPtr<FDreamLocReviewEntry>& InEntry) { Entry = InEntry; }

	FString GetText() const;
	void SetText(const FString& InText);
	/** The box's text differs from the entry's translation. */
	bool IsChanged() const;
	FString GetProblems() const { return Problems; }
	bool InsertAtCursor(const FString& InText);
	void FocusBox();

	/** What a key does here first: the window's shortcuts, wherever the focus is in the editor. */
	FReply HandleKey(const FKeyEvent& InKeyEvent);

	/** The candidates shown again: one came, or the entry changed. */
	void RebuildCandidates();
	/** Where the game shows the box's text, laid out by the tool that shows it; unset when no tool does. */
	const TOptional<FDreamLocFitResult>& GetFit() const { return Fit; }
	/** What the preview says, as text: whether it fits, what it needs, what the font lacks. */
	FText GetPreviewText() const;

private:
	void OnBoxChanged(const FText& InText);
	void CheckNow();
	EActiveTimerReturnType CheckLater(double InCurrentTime, float InDeltaTime);
	EActiveTimerReturnType ApplyFix(double InCurrentTime, float InDeltaTime);
	void RebuildProblems();
	void RebuildAsk();

	TWeakPtr<SDreamLocTranslateWindow> Window;
	TSharedPtr<FDreamLocReviewEntry> Entry;
	TSharedPtr<SMultiLineEditableTextBox> Box;
	TSharedPtr<SVerticalBox> ProblemList;
	TSharedPtr<SVerticalBox> CandidateList;
	TSharedPtr<class SWrapBox> AskBar;
	TOptional<FDreamLocFitResult> Fit;
	FString Text;
	FString Problems;
	bool bFixing = false;
	/** A placeholder an edit cut into, put right once the box's edit is over: the text before that edit, where the cursor goes. */
	bool bFixPending = false;
	bool bFixTimer = false;
	FString BeforeFix;
	int32 FixCursor = 0;
	bool bCheckPending = false;
	double CheckAt = 0.0;
};
