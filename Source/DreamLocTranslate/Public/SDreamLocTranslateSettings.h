// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamLocSettings.h"
#include "DreamLocTranslateTypes.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class SEditableTextBox;
class SMultiLineEditableTextBox;
template <typename OptionType> class SComboBox;

/**
 * Editor Preferences > Plugins > DreamLoc translators: the user's translators, a profile each, read from and written to
 * %APPDATA%/DreamLoc/translators.ini (DreamLocTranslateConfig) -- never the engine's EditorSettings.ini. The fields a type
 * has; Test sends the smallest request.
 */
class DREAMLOCTRANSLATE_API SDreamLocTranslatorSettings : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDreamLocTranslatorSettings) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Reads translators.ini again; what was not saved goes. */
	void Reload();
	/** Writes the profiles to translators.ini; false (and the status says why) when a name is missing or twice, or the file does not write. */
	bool Save();
	/** Changed since read or saved. */
	bool IsDirty() const;
	void Select(const FString& InName);
	const TArray<TSharedPtr<FDreamLocTranslateProfile>>& GetProfiles() const { return Items; }
	FText GetStatus() const { return StatusText; }
	FText GetTestText() const { return TestText; }

private:
	TSharedRef<SWidget> MakeForm();
	FDreamLocTranslateProfile* GetSelected() const { return Selected.Get(); }
	void TestSelected();
	EActiveTimerReturnType ReloadWhenChanged(double InCurrentTime, float InDeltaTime);
	FString Written() const;

	TArray<TSharedPtr<FDreamLocTranslateProfile>> Items;
	TSharedPtr<FDreamLocTranslateProfile> Selected;
	TSharedPtr<SListView<TSharedPtr<FDreamLocTranslateProfile>>> List;
	TArray<TSharedPtr<FString>> TypeChoices;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> TypeCombo;
	FString Loaded;
	/** The file as last read or written here: a change elsewhere is read again. */
	FString LoadedFile;
	FText StatusText;
	FText TestText;
};

/**
 * Project Settings > Plugins > DreamLoc: the project's translation settings, dloc.ini's [Translate]. Save writes only what
 * was changed, where it was (DreamLocTranslateConfig::WriteTranslateChanges): the file's comments and other lines stay.
 */
class DREAMLOCTRANSLATE_API SDreamLocProjectTranslateSettings : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDreamLocProjectTranslateSettings) {}
		/** The DLoc folder; the project's when empty. */
		SLATE_ARGUMENT(FString, DLocDirectory)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Reads dloc.ini (and the translators, for the choices) again. */
	void Reload();
	bool Save();
	FText GetStatus() const { return StatusText; }

private:
	void RebuildChoices();
	FText ChoiceLabel(const TSharedPtr<FString>& InChoice) const;
	EActiveTimerReturnType ReloadWhenChanged(double InCurrentTime, float InDeltaTime);

	FString DLocDirectory;
	FDreamLocSettings Settings;
	TArray<FDreamLocTranslateProfile> Profiles;
	/** The file as last read or written here: a change elsewhere is read again. */
	FString LoadedFile;
	bool bDirty = false;

	TArray<TSharedPtr<FString>> ProfileChoices;
	TArray<TSharedPtr<FString>> FallbackChoices;
	TSharedPtr<FString> ProfileChoice;
	TSharedPtr<FString> FallbackChoice;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> ProfileCombo;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> FallbackCombo;
	TSharedPtr<SMultiLineEditableTextBox> ByCulture;
	TSharedPtr<SEditableTextBox> BatchSize;
	TSharedPtr<SEditableTextBox> Concurrency;
	TSharedPtr<SEditableTextBox> Retry;
	TSharedPtr<SEditableTextBox> Style;
	TSharedPtr<SMultiLineEditableTextBox> LocalOnly;
	FText StatusText;
};
