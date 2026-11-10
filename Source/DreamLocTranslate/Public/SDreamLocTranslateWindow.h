// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamLocReviewModel.h"
#include "DreamLocTranslateFlow.h"
#include "DreamLocTranslateTypes.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STreeView.h"

class SDreamLocTranslateEditor;
class SHeaderRow;
class SSearchBox;
class SVerticalBox;
template <typename OptionType> class SComboBox;

/** A folder or a .dloc file in the file tree: a path without .dloc, a folder's with a trailing slash; empty for all. */
struct FDreamLocReviewFileNode
{
	FString Name;
	FString Path;
	TArray<TSharedPtr<FDreamLocReviewFileNode>> Children;
};

/**
 * Tools > DreamLoc > Translate: a review desk for one culture at a time. The toolbar picks the culture, a filter (with how
 * many each has), a search, a reference culture, the translator; the list on the left shows the entries, the editor on the
 * right the one chosen -- its source, its translation, what the checks say. Ctrl+Enter passes it and goes to the next one
 * not done. Batch translation is the "AI pre-translate..." dialog; what it brings in waits in the list for review.
 *
 * Every decision is written into the .dloc files at once; the list follows the files, whoever changes them.
 */
class DREAMLOCTRANSLATE_API SDreamLocTranslateWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDreamLocTranslateWindow) {}
		/** The DLoc folder, and the folder its sources are relative to; the project's when empty. */
		SLATE_ARGUMENT(FString, DLocDirectory)
		SLATE_ARGUMENT(FString, OwnerDirectory)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SDreamLocTranslateWindow() override;

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }

	/** Points the window at another DLoc folder (and owner), and reads everything again. */
	void SetDirectories(const FString& InDLocDirectory, const FString& InOwnerDirectory);
	/**
	 * What the toolbar's first list offers: empty for the project's DLoc folder, then, by name, every enabled plugin
	 * with a DLoc/dloc.ini of its own -- a plugin translating its own UI.
	 */
	static TArray<FString> FindOwners();
	/** The project's DLoc folder (empty) or a plugin's, by name, the box's change kept as a draft first. False when there is no such plugin. */
	bool SetOwner(const FString& InPluginName);
	/** The plugin whose DLoc folder is shown; empty for the project's, or a folder SetDirectories was given. */
	const FString& GetOwner() const { return OwnerName; }
	const FString& GetDLocDirectory() const { return DLocDirectory; }
	/** Reads the settings, the profiles and the files again, keeping the culture, the filter and the entry chosen. */
	void Refresh();

	void SetCulture(const FString& InCulture);
	const FString& GetCulture() const { return Culture; }
	void SetReferenceCulture(const FString& InCulture);
	void SetFilter(EDreamLocReviewFilter InFilter);
	EDreamLocReviewFilter GetFilter() const { return Filter; }
	void SetSearch(const FString& InSearch);
	/** A file (path without .dloc) or a folder (with a trailing slash) to show alone; empty for all. */
	void SetFile(const FString& InFile);
	const FString& GetFile() const { return File; }
	const FString& GetSearch() const { return Search; }
	const FDreamLocSettings& GetSettings() const { return Settings; }
	/** The translator for one entry's actions, and the pre-translation's to begin with; empty for each culture's own. */
	void SetProfile(const FString& InProfile);
	const FString& GetProfile() const { return Profile; }
	const TArray<FDreamLocTranslateProfile>& GetProfiles() const { return Profiles; }

	const FDreamLocReviewModel& GetModel() const { return Model; }
	/** What the list shows now, in order. */
	const TArray<TSharedPtr<FDreamLocReviewEntry>>& GetShown() const { return Shown; }
	TSharedPtr<FDreamLocReviewEntry> GetCurrent() const { return Current; }
	/** Chooses an entry; one the filter hides makes the filter All. False when the culture has no such entry. */
	bool Select(const FString& InNamespace, const FString& InKey);
	/** Select in the culture shown, else in the first other target culture that has the entry (the window goes there). False when none has it. */
	bool SelectInAnyCulture(const FString& InNamespace, const FString& InKey);

	/** The editor's translation box, as typed so far. */
	FString GetEditText() const;
	/** What the checks say about the box's text now. */
	FString GetEditProblems() const;

	/** Ctrl+Enter: the box's text reviewed (done), and on to the next entry shown that is not done. */
	bool PassAndNext();
	/** Ctrl+Shift+Enter: the box's text kept as a draft, and on to the next entry. */
	bool DraftAndNext();
	/** Alt+Up / Alt+Down: the entry before or after, the box's change kept as a draft first. */
	bool SelectNext(int32 InDirection);
	/** Ctrl+R: this entry translated again by the translator. */
	bool Retranslate();
	/** No translation for this entry. */
	bool ClearCurrent();
	/** F3: the next placeholder of the source the translation lacks, at the cursor. */
	bool InsertNextMissing();

	bool PassEntries(TConstArrayView<TSharedPtr<FDreamLocReviewEntry>> InEntries);
	bool RejectEntries(TConstArrayView<TSharedPtr<FDreamLocReviewEntry>> InEntries);
	/** The entries translated by that profile (empty: the window's), machine translations included. */
	bool TranslateEntries(TConstArrayView<TSharedPtr<FDreamLocReviewEntry>> InEntries, const FString& InProfile);
	TArray<TSharedPtr<FDreamLocReviewEntry>> GetSelectedEntries() const;

	/**
	 * The candidates for the entry chosen: what the translators were asked for it (AI fixes too), then the reviewed
	 * translations of its source elsewhere, then the reference culture's; each text once.
	 */
	TArray<FDreamLocCandidate> GetCandidates() const;
	/** A click on a candidate, Ctrl+1/2/3: it goes into the box, a change (a draft unless passed). */
	bool ApplyCandidate(int32 InIndex);
	/** The translators the entry chosen may be asked, by name: a LocalOnly entry's are the local ones alone. */
	TArray<FString> GetCandidateProfiles() const;
	/** Asks a translator for the entry chosen: what it gives is a candidate; nothing is written. */
	bool AskForCandidate(const FString& InProfile);
	/** The large model an AI fix goes to: the window's translator, else the culture's, else [Translate] Fallback, the first that is one and may see the entry; empty for none. */
	FString GetFixProfile() const;
	/**
	 * Ctrl+Shift+F, or a problem's "AI fix": the box's text and what is wrong with it (InProblem, or everything the checks
	 * say when empty) to the large model; what it gives is a candidate, not put in the box.
	 */
	bool FixWithAI(const FString& InProblem = FString());

	/** The overview at the top: every culture's progress, read while it is open. */
	void SetOverviewOpen(bool bInOpen);
	const TArray<FDreamLocCultureProgress>& GetOverview() const { return Overview; }
	/** A culture's files shown under it in the overview. */
	void SetOverviewFilesShown(const FString& InCulture, bool bInShown);

	/** A request with the window's folders, settings and profiles; the rest for the caller to fill in. */
	FDreamLocTranslateRequest MakeBaseRequest() const;
	/** Runs the flow on a worker thread; InOnDone on the game thread after the window has read the files again. */
	bool StartRun(const FDreamLocTranslateRequest& InRequest, TFunction<void(const FDreamLocTranslateResult&)> InOnDone = nullptr);
	bool IsRunning() const;
	void CancelRun();
	/** How far the run has got: 0 to 1. */
	float GetRunFraction() const;
	const FDreamLocTranslateResult& GetLastResult() const { return LastResult; }
	const TArray<FString>& GetLastErrors() const { return LastErrors; }
	/** Which run the log is of: each run started, whatever started it, has the next number (0: none yet). */
	int32 GetRunNumber() const { return RunNumber; }
	/** The log of the run under way, else of the last one: its lines from InFrom on, added to OutLines. */
	void ReadRunLog(int32 InFrom, TArray<FDreamLocTranslateLogLine>& OutLines) const;

	/** Opens the AI pre-translate dialog; the dialog, for a test to drive. */
	TSharedPtr<class SDreamLocPretranslateDialog> OpenPretranslate();

	/** The list's right-click menu, for the entries selected; null when none is. */
	TSharedPtr<SWidget> MakeContextMenu();

	/** The list's row widget for an entry; null when it is not generated (out of view, not painted yet). */
	TSharedPtr<SWidget> GetRowWidget(const TSharedPtr<FDreamLocReviewEntry>& InEntry) const;

private:
	struct FRunState;
	friend class SDreamLocTranslateEditor;

	TSharedRef<SWidget> MakeToolbar();
	TSharedRef<SWidget> MakeList();
	TSharedRef<SWidget> MakeFiles();
	TSharedRef<SWidget> MakeOverview();
	TSharedRef<SWidget> MakeProgressRow(const FString& InCulture, const FString& InFile, const FDreamLocReviewProgress& InProgress, bool bInCultureRow);
	void RebuildOverview();
	/** Where an entry's asked-for candidates are kept: its culture, namespace, key and source (a new source, new candidates). */
	FString CandidateKey(const FDreamLocReviewEntry& InEntry) const;
	bool StartSuggestion(const FString& InProfile, const FString& InFrom, const FString& InAmendText, const FString& InAmendProblems);
	bool MayGoTo(const FDreamLocReviewEntry& InEntry, const FString& InProfile) const;
	TSharedRef<ITableRow> MakeRow(TSharedPtr<FDreamLocReviewEntry> InEntry, const TSharedRef<STableViewBase>& InOwner);

	/** The model read again and the list rebuilt, the entry chosen kept when it is still there. */
	void Reload();
	void RebuildShown();
	void RebuildFiles();
	void ShowEntry(TSharedPtr<FDreamLocReviewEntry> InEntry);
	/** The box's change to the entry being edited written as a draft; true when there was none or it was written. */
	bool KeepEdit();
	bool WriteCurrent(const TOptional<FString>& InText, const FString& InState);
	/** The entry the list shows after InFrom (wrapping), the first that passes InWanted; null when none does. */
	TSharedPtr<FDreamLocReviewEntry> FindAfter(const TSharedPtr<FDreamLocReviewEntry>& InFrom, TFunctionRef<bool(const FDreamLocReviewEntry&)> InWanted) const;
	void OnRunFinished(const FDreamLocTranslateResult& InResult, TFunction<void(const FDreamLocTranslateResult&)> InOnDone);
	void WatchDirectory();
	void UnwatchDirectory();
	EActiveTimerReturnType ReloadWhenQuiet(double InCurrentTime, float InDeltaTime);
	void OpenInFile(const TSharedPtr<FDreamLocReviewEntry>& InEntry) const;
	void OpenSource(const TSharedPtr<FDreamLocReviewEntry>& InEntry) const;
	FDreamLocTranslateRequest MakeEntriesRequest(TConstArrayView<TSharedPtr<FDreamLocReviewEntry>> InEntries, const FString& InProfile) const;

	FText FilterLabel(EDreamLocReviewFilter InFilter) const;
	FText ProfileLabel(const FString& InName) const;
	FText ProgressText() const;

	FString DLocDirectory;
	FString OwnerDirectory;
	FString OwnerName;
	FDreamLocSettings Settings;
	TArray<FDreamLocTranslateProfile> Profiles;
	FDreamLocReviewModel Model;

	FString Culture;
	FString ReferenceCulture;
	EDreamLocReviewFilter Filter = EDreamLocReviewFilter::All;
	FString Search;
	FString File;
	FString Profile;

	TArray<TSharedPtr<FDreamLocReviewEntry>> Shown;
	TSharedPtr<FDreamLocReviewEntry> Current;
	TSharedPtr<SListView<TSharedPtr<FDreamLocReviewEntry>>> List;
	TSharedPtr<SHeaderRow> Header;
	TSharedPtr<SDreamLocTranslateEditor> Editor;

	TArray<TSharedPtr<FString>> OwnerChoices;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> OwnerCombo;
	TArray<TSharedPtr<FString>> CultureChoices;
	TArray<TSharedPtr<FString>> ReferenceChoices;
	TArray<TSharedPtr<FString>> ProfileChoices;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> CultureCombo;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> ReferenceCombo;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> ProfileCombo;
	TSharedPtr<SSearchBox> SearchBox;

	bool bShowFiles = false;
	TArray<TSharedPtr<FDreamLocReviewFileNode>> FileRoots;
	TSharedPtr<STreeView<TSharedPtr<FDreamLocReviewFileNode>>> FileTree;

	TSharedPtr<FRunState> Running;
	int32 RunNumber = 0;
	FDreamLocTranslateResult LastResult;
	TArray<FString> LastErrors;
	FText StatusText;
	TWeakPtr<SWindow> PretranslateWindow;

	TMap<FString, TArray<FDreamLocCandidate>> Asked;
	TArray<FDreamLocCultureProgress> Overview;
	bool bOverviewOpen = false;
	TSet<FString> OverviewFilesShown;
	TSharedPtr<SVerticalBox> OverviewBox;
	/** What the overview's rows were built for. */
	FString OverviewShown;

	FDelegateHandle WatchHandle;
	FString WatchedDirectory;
	double PendingReload = 0.0;
	bool bReloadTimer = false;
	/** The list's selection being set from here, not by a person. */
	bool bSyncingSelection = false;
};
