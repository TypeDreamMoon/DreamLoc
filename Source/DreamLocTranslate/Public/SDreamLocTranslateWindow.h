// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamLocSettings.h"
#include "DreamLocTranslateFlow.h"
#include "DreamLocTranslateTypes.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STreeView.h"

class SEditableTextBox;
class SMultiLineEditableTextBox;
class SWidgetSwitcher;
class SWrapBox;
template <typename OptionType> class SComboBox;

/** One row of the results table: an entry as the run left it, and what the reviewer did with it since. */
struct FDreamLocTranslateRowItem
{
	FDreamLocTranslateRow Row;
	/** passed, rejected, edited, or empty: what has been written since the run. */
	FString Review;
	/** Being translated again. */
	bool bBusy = false;
};

/** A folder or a .dloc file in the file tree, relative to DLoc/<culture>/. */
struct FDreamLocTranslateFileNode
{
	FString Name;
	/** A file's path without .dloc; a folder's with a trailing slash. */
	FString Path;
	bool bFolder = false;
	ECheckBoxState Checked = ECheckBoxState::Checked;
	TArray<TSharedPtr<FDreamLocTranslateFileNode>> Children;
	TWeakPtr<FDreamLocTranslateFileNode> Parent;
};

/**
 * Tools > DreamLoc > Translate: the translate page (cultures, files, which entries, the translator, an estimate, a run
 * and its results to review) and the translators page (the user's profiles, a test of each, the project's settings).
 * A run is DreamLocTranslateFlow::Run on a worker thread; the window only shows it. Every change is written into the
 * .dloc files at once.
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

	// What the controls do, for a test to drive the window the way a person would.

	/** Points the window at another DLoc folder (and owner), and reads everything again. */
	void SetDirectories(const FString& InDLocDirectory, const FString& InOwnerDirectory);
	/** Reads the settings, the profiles and the files again. */
	void Refresh();
	void SetCultures(const TArray<FString>& InCultures);
	/** A profile name; empty for each culture's own ([Translate] Profile). */
	void SetProfile(const FString& InProfile);
	void SetIncludeMachine(bool bInInclude);
	FDreamLocTranslateEstimate RunEstimate();
	/** Starts a run without asking (the button asks first); false when one is running or nothing can be sent. */
	bool StartTranslate();
	void CancelTranslate();
	bool IsRunning() const;
	/** The machine translations there are, into the table for review. */
	void LoadForReview();
	const TArray<TSharedPtr<FDreamLocTranslateRowItem>>& GetRowItems() const { return RowItems; }
	/** Marks the rows done, with their text as the table has it. */
	bool PassRows(TConstArrayView<TSharedPtr<FDreamLocTranslateRowItem>> InItems);
	/** Clears the rows' translations. */
	bool RejectRows(TConstArrayView<TSharedPtr<FDreamLocTranslateRowItem>> InItems);
	/** A reviewer's own text for a row: written as draft. */
	bool EditRow(const TSharedPtr<FDreamLocTranslateRowItem>& InItem, const FString& InText);
	/** Translates that one entry again; its row changes when the run is done. */
	bool RetranslateRow(const TSharedPtr<FDreamLocTranslateRowItem>& InItem);
	void OpenRowInEditor(const TSharedPtr<FDreamLocTranslateRowItem>& InItem) const;
	const FDreamLocTranslateResult& GetLastResult() const { return LastResult; }
	const TArray<FString>& GetLastErrors() const { return LastErrors; }

private:
	struct FRunState;

	TSharedRef<SWidget> MakeTranslatePage();
	TSharedRef<SWidget> MakeBackendsPage();
	TSharedRef<SWidget> MakeProfileForm();
	TSharedRef<SWidget> MakeProjectSettings();

	FDreamLocTranslateRequest MakeRequest(bool& bOutNoFiles) const;
	void RunRequest(const FDreamLocTranslateRequest& InRequest, TSharedPtr<FDreamLocTranslateRowItem> InOnlyRow);
	void OnRunFinished(const FDreamLocTranslateResult& InResult, TSharedPtr<FDreamLocTranslateRowItem> InOnlyRow);
	FReply OnTranslateClicked();
	bool ApplyEdits(TConstArrayView<TSharedPtr<FDreamLocTranslateRowItem>> InItems,
		TFunctionRef<bool(DreamLocTranslateFlow::FReviewEdit&, FDreamLocTranslateRowItem&)> InMake, const TCHAR* InReview);
	void SetRows(TConstArrayView<FDreamLocTranslateRow> InRows);

	void RebuildCultures();
	void RebuildFileTree();
	void SetNodeChecked(const TSharedPtr<FDreamLocTranslateFileNode>& InNode, ECheckBoxState InState);
	void CollectCheckedFiles(const TSharedPtr<FDreamLocTranslateFileNode>& InNode, TArray<FString>& OutFiles, bool& bOutAllChecked, bool& bOutAnyChecked) const;
	TSharedRef<ITableRow> MakeRowWidget(TSharedPtr<FDreamLocTranslateRowItem> InItem, const TSharedRef<STableViewBase>& InOwner);
	TSharedRef<ITableRow> MakeFileWidget(TSharedPtr<FDreamLocTranslateFileNode> InNode, const TSharedRef<STableViewBase>& InOwner);

	FDreamLocTranslateProfile* GetSelectedProfile() const;
	FText ProfileLabel(const FString& InName) const;
	void RebuildProfileChoices();
	void SaveProfiles();
	void TestProfile();
	void LoadProjectSettings();
	/** The settings' profile choices from the profiles there are now, keeping what is chosen. */
	void RebuildSettingsChoices();
	FText SettingLabel(const TSharedPtr<FString>& InChoice) const;
	void SaveProjectSettings();

	FText GetProgressText() const;
	TOptional<float> GetProgressFraction() const;

	FString DLocDirectory;
	FString OwnerDirectory;
	FDreamLocSettings Settings;

	// The translate page.
	TArray<FString> AllCultures;
	TSet<FString> CheckedCultures;
	TSharedPtr<SWrapBox> CultureBox;
	TArray<TSharedPtr<FDreamLocTranslateFileNode>> FileRoots;
	TSharedPtr<STreeView<TSharedPtr<FDreamLocTranslateFileNode>>> FileTree;
	bool bIncludeUntranslated = true;
	bool bIncludeStale = true;
	bool bIncludeMachine = false;
	TArray<TSharedPtr<FString>> ProfileChoices;
	TSharedPtr<FString> ChosenProfile;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> ProfileCombo;
	FText EstimateText;
	TArray<TSharedPtr<FDreamLocTranslateRowItem>> RowItems;
	TSharedPtr<SListView<TSharedPtr<FDreamLocTranslateRowItem>>> ResultsList;
	FDreamLocTranslateResult LastResult;
	TArray<FString> LastErrors;
	FText StatusText;
	TSharedPtr<FRunState> Running;

	// The translators page.
	TArray<TSharedPtr<FDreamLocTranslateProfile>> ProfileItems;
	TSharedPtr<SListView<TSharedPtr<FDreamLocTranslateProfile>>> ProfileList;
	TSharedPtr<FDreamLocTranslateProfile> SelectedProfile;
	TArray<TSharedPtr<FString>> TypeChoices;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> TypeCombo;
	FText ProfilesText;
	FText TestText;
	/** The project's translator and Fallback, picked from the profiles (empty: none). */
	TArray<TSharedPtr<FString>> SettingsProfileChoices;
	TArray<TSharedPtr<FString>> SettingsFallbackChoices;
	TSharedPtr<FString> SettingsProfileChoice;
	TSharedPtr<FString> SettingsFallbackChoice;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> SettingsProfileCombo;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> SettingsFallbackCombo;
	TSharedPtr<SEditableTextBox> SettingsBatchSize;
	TSharedPtr<SEditableTextBox> SettingsConcurrency;
	TSharedPtr<SEditableTextBox> SettingsRetry;
	TSharedPtr<SEditableTextBox> SettingsStyle;
	TSharedPtr<SMultiLineEditableTextBox> SettingsByCulture;
	TSharedPtr<SMultiLineEditableTextBox> SettingsLocalOnly;
	FText SettingsText;

	TSharedPtr<SWidgetSwitcher> Pages;
	int32 Page = 0;
};
