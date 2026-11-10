// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamLocTranslateFlow.h"
#include "Widgets/SCompoundWidget.h"

class ITableRow;
class SDreamLocTranslateWindow;
class SEditableTextBox;
class STableViewBase;
class SVerticalBox;
template <typename OptionType> class SComboBox;
template <typename ItemType> class SListView;

/** Which entries the pre-translation looks at, before the states filter them. */
enum class EDreamLocPretranslateScope : uint8
{
	/** Every entry of the cultures chosen. */
	All,
	/** What the review window's list shows now (its filter, search and file). */
	Shown,
	/** The entries chosen in the list. */
	Selected,
	/** Those under the files (or folders) typed in. */
	Files,
};

/**
 * The review window's "AI pre-translate..." dialog: which cultures, which entries, which states, which translator; what
 * that would send (entries, characters, and once the translator is known, requests and tokens) shown before Start, so no
 * prompt asks again. The run belongs to the window: closing the dialog leaves it going, the window's toolbar shows how far
 * it has got, and when it is done the window shows what came back, to review.
 *
 * Under it, the log of the window's run, whatever started it, as it is written: a line for each request as it comes back,
 * each text that failed or the checks turned down and why, what each culture came to. It folds away, shows the failures
 * alone when asked, copies (Copy: the lines shown; Ctrl+C: the ones selected), and opens the report when the run is done.
 */
class DREAMLOCTRANSLATE_API SDreamLocPretranslateDialog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDreamLocPretranslateDialog) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<SDreamLocTranslateWindow>& InWindow);

	void SetCultures(TConstArrayView<FString> InCultures);
	const TArray<FString>& GetCultures() const { return Cultures; }
	void SetScope(EDreamLocPretranslateScope InScope);
	EDreamLocPretranslateScope GetScope() const { return Scope; }
	/** For the Files scope: paths without .dloc, or folders with a trailing slash. */
	void SetFiles(TConstArrayView<FString> InFiles);
	void SetStates(bool bInUntranslated, bool bInStale, bool bInMachine);
	void SetProfile(const FString& InProfile);
	const FString& GetProfile() const { return Profile; }

	/** The request Start would send. */
	FDreamLocTranslateRequest MakeRequest() const;
	/** What it would send, as last counted (every change counts it again). */
	const FDreamLocTranslateEstimate& GetEstimate() const { return Estimate; }
	FText GetEstimateText() const { return EstimateText; }
	/** What the last run it started came to (or that one is under way); empty before any. */
	FText GetResultText() const { return ResultText; }
	/** Whether Start would send anything: no errors, something to send, no run under way. */
	bool CanStart() const;
	/** Sends it, the window running it. */
	bool Start();

	/** The log shown, or folded away to its heading. */
	void SetLogOpen(bool bInOpen);
	bool IsLogOpen() const { return bLogOpen; }
	/** Only what went wrong: requests that failed (and were sent again), texts that failed or the checks turned down. */
	void SetFailuresOnly(bool bInFailuresOnly);
	bool IsFailuresOnly() const { return bFailuresOnly; }
	/** The lines the log shows, one a line, each with its time: what Copy puts on the clipboard. */
	FString GetLogText() const;
	/** How many lines the log has, and how many of them say something went wrong. */
	int32 GetLogLineCount() const { return LogLines.Num(); }
	int32 GetLogFailureCount() const { return LogFailures; }
	/** The reports of the run the log is of, once it is done (Saved/DLoc/translate-<culture>-<time>.json): what "Open the report" opens. */
	const TArray<FString>& GetReportPaths() const { return ReportPaths; }

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
	void Recount();
	TSharedRef<SWidget> MakeCultures();
	FText ScopeLabel(EDreamLocPretranslateScope InScope) const;
	TSharedRef<SWidget> MakeLogHeader();
	TSharedRef<ITableRow> MakeLogRow(TSharedPtr<FDreamLocTranslateLogLine> InLine, const TSharedRef<STableViewBase>& InOwner);
	/** What the window's run has written since the last look; a new run starts the log again. */
	void ReadLog();
	void RebuildShownLog();
	FString JoinLines(TConstArrayView<TSharedPtr<FDreamLocTranslateLogLine>> InLines) const;
	void OpenReports() const;

	TWeakPtr<SDreamLocTranslateWindow> Window;
	TArray<FString> Cultures;
	EDreamLocPretranslateScope Scope = EDreamLocPretranslateScope::All;
	TArray<FString> Files;
	bool bUntranslated = true;
	bool bStale = true;
	bool bMachine = false;
	FString Profile;

	FDreamLocTranslateEstimate Estimate;
	FText EstimateText;
	FText ResultText;

	TArray<TSharedPtr<FString>> ProfileChoices;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> ProfileCombo;
	TSharedPtr<SEditableTextBox> FilesBox;

	/** The window's run the log is of (its GetRunNumber); every line read so far, and those shown. */
	int32 LogRun = INDEX_NONE;
	TArray<TSharedPtr<FDreamLocTranslateLogLine>> LogLines;
	TArray<TSharedPtr<FDreamLocTranslateLogLine>> ShownLog;
	int32 LogFailures = 0;
	bool bLogOpen = true;
	bool bFailuresOnly = false;
	TArray<FString> ReportPaths;
	TSharedPtr<SListView<TSharedPtr<FDreamLocTranslateLogLine>>> LogList;
};
