// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamLocTranslateFlow.h"
#include "Widgets/SCompoundWidget.h"

class SDreamLocTranslateWindow;
class SEditableTextBox;
class SVerticalBox;
template <typename OptionType> class SComboBox;

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

private:
	void Recount();
	TSharedRef<SWidget> MakeCultures();
	FText ScopeLabel(EDreamLocPretranslateScope InScope) const;

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
};
