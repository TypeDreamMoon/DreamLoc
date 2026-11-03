// Copyright DreamMoon. All Rights Reserved.

#include "SDreamLocTranslateSettings.h"

#include "Async/Async.h"
#include "DreamLocTextSource.h"
#include "DreamLocTranslateConfig.h"
#include "DreamLocTranslator.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
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
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "DreamLocTranslateSettings"

namespace DreamLocTranslateSettingsLocal
{
	/** Whether a type of translator has the field. */
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

	FText ProfileLabel(const FDreamLocTranslateProfile& InProfile)
	{
		return FText::Format(LOCTEXT("ProfileLabel", "{0} ({1}, {2})"), FText::FromString(InProfile.Name), FText::FromString(InProfile.Type),
			InProfile.IsLocal() ? LOCTEXT("Local", "local") : LOCTEXT("Cloud", "cloud"));
	}

	/** The file's text; empty when there is none. By content, not by time: a file's time here is whole seconds. */
	FString ContentOf(const FString& InPath)
	{
		FString Text;
		FFileHelper::LoadFileToString(Text, *InPath);
		return Text;
	}
}

// ---------------------------------------------------------------------------------------------------------- the translators

void SDreamLocTranslatorSettings::Construct(const FArguments& InArgs)
{
	for (const FString& Type : DreamLocTranslators::GetTypes())
	{
		TypeChoices.Add(MakeShared<FString>(Type));
	}
	ChildSlot
	[
		SNew(SScrollBox)
		+ SScrollBox::Slot().Padding(8.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SNew(STextBlock).AutoWrapText(true).ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(FText::Format(LOCTEXT("TranslatorsIntro", "Your translators, for every project: {0}. A project chooses among them in Project Settings > Plugins > DreamLoc."),
					FText::FromString(DreamLocTranslateConfig::GetUserConfigPath())))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(0.3f).Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SBox).HeightOverride(240.0f)
						[
							SNew(SBorder).BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
							[
								SAssignNew(List, SListView<TSharedPtr<FDreamLocTranslateProfile>>)
								.ListItemsSource(&Items)
								.SelectionMode(ESelectionMode::Single)
								.OnGenerateRow_Lambda([](TSharedPtr<FDreamLocTranslateProfile> InProfile, const TSharedRef<STableViewBase>& InOwner) -> TSharedRef<ITableRow>
								{
									return SNew(STableRow<TSharedPtr<FDreamLocTranslateProfile>>, InOwner)
										.AddMetaData<FTagMetaData>(FTagMetaData(*(TEXT("DreamLocSettings.Profile.") + InProfile->Name)))
										[
											SNew(STextBlock).Text_Lambda([InProfile]() { return DreamLocTranslateSettingsLocal::ProfileLabel(*InProfile); })
										];
								})
								.OnSelectionChanged_Lambda([this](TSharedPtr<FDreamLocTranslateProfile> InProfile, ESelectInfo::Type)
								{
									if (!InProfile.IsValid())
									{
										return;
									}
									Selected = InProfile;
									TestText = FText::GetEmpty();
									for (const TSharedPtr<FString>& Type : TypeChoices)
									{
										if (Type->Equals(InProfile->Type, ESearchCase::IgnoreCase) && TypeCombo.IsValid())
										{
											TypeCombo->SetSelectedItem(Type);
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
							SNew(SButton).AddMetaData<FTagMetaData>(FTagMetaData(TEXT("DreamLocSettings.Add"))).Text(LOCTEXT("AddProfile", "Add"))
							.OnClicked_Lambda([this]()
							{
								const TSharedPtr<FDreamLocTranslateProfile> Profile = MakeShared<FDreamLocTranslateProfile>();
								Profile->Type = TEXT("OpenAI");
								for (int32 Number = Items.Num() + 1;; ++Number)
								{
									Profile->Name = FString::Printf(TEXT("profile%d"), Number);
									if (!Items.ContainsByPredicate([&Profile](const TSharedPtr<FDreamLocTranslateProfile>& InOther) { return InOther->Name == Profile->Name; }))
									{
										break;
									}
								}
								Items.Add(Profile);
								List->RequestListRefresh();
								List->SetSelection(Profile);
								return FReply::Handled();
							})
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)
						[
							SNew(SButton).AddMetaData<FTagMetaData>(FTagMetaData(TEXT("DreamLocSettings.Delete"))).Text(LOCTEXT("DeleteProfile", "Delete"))
							.IsEnabled_Lambda([this]() { return Selected.IsValid(); })
							.OnClicked_Lambda([this]()
							{
								Items.Remove(Selected);
								Selected = Items.Num() > 0 ? Items[0] : nullptr;
								List->RequestListRefresh();
								if (Selected.IsValid())
								{
									List->SetSelection(Selected);
								}
								return FReply::Handled();
							})
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(SButton).AddMetaData<FTagMetaData>(FTagMetaData(TEXT("DreamLocSettings.Save"))).Text(LOCTEXT("SaveProfiles", "Save"))
							.ToolTipText(FText::Format(LOCTEXT("SaveProfilesTip", "Write the profiles to {0}"), FText::FromString(DreamLocTranslateConfig::GetUserConfigPath())))
							.IsEnabled_Lambda([this]() { return IsDirty(); })
							.OnClicked_Lambda([this]() { Save(); return FReply::Handled(); })
						]
					]
				]
				+ SHorizontalBox::Slot().FillWidth(0.7f)[MakeForm()]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f)
			[
				SNew(STextBlock).AddMetaData<FTagMetaData>(FTagMetaData(TEXT("DreamLocSettings.Status"))).AutoWrapText(true).Text_Lambda([this]() { return StatusText; })
			]
		]
	];
	Reload();
	RegisterActiveTimer(2.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SDreamLocTranslatorSettings::ReloadWhenChanged));
}

TSharedRef<SWidget> SDreamLocTranslatorSettings::MakeForm()
{
	using namespace DreamLocTranslateSettingsLocal;
	TSharedRef<SGridPanel> Grid = SNew(SGridPanel).FillColumn(1, 1.0f);
	int32 RowIndex = 0;
	auto Visible = [this](FName InField)
	{
		return TAttribute<EVisibility>::CreateLambda([this, InField]()
		{
			const FDreamLocTranslateProfile* Profile = GetSelected();
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
			.AddMetaData<FTagMetaData>(FTagMetaData(*(TEXT("DreamLocSettings.Field.") + InField.ToString())))
			.IsPassword(bInPassword)
			.HintText(InHint)
			.Text_Lambda([this, InMember]()
			{
				const FDreamLocTranslateProfile* Profile = GetSelected();
				return Profile != nullptr ? FText::FromString(Profile->*InMember) : FText::GetEmpty();
			})
			.OnTextChanged_Lambda([this, InMember](const FText& InText)
			{
				if (FDreamLocTranslateProfile* Profile = GetSelected())
				{
					Profile->*InMember = InText.ToString().TrimStartAndEnd();
					List->RequestListRefresh();
				}
			}));
	};
	auto TypeOf = [this]() { const FDreamLocTranslateProfile* Profile = GetSelected(); return Profile != nullptr ? Profile->Type : FString(); };

	AddText(TEXT("Name"), LOCTEXT("FieldName", "Name"), &FDreamLocTranslateProfile::Name, false, FText::GetEmpty());
	AddRow(TEXT("Type"), LOCTEXT("FieldType", "Type"),
		SAssignNew(TypeCombo, SComboBox<TSharedPtr<FString>>)
		.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("DreamLocSettings.Field.Type")))
		.OptionsSource(&TypeChoices)
		.OnGenerateWidget_Lambda([](TSharedPtr<FString> InType) -> TSharedRef<SWidget> { return SNew(STextBlock).Text(FText::FromString(*InType)); })
		.OnSelectionChanged_Lambda([this](TSharedPtr<FString> InType, ESelectInfo::Type)
		{
			FDreamLocTranslateProfile* Profile = GetSelected();
			if (Profile != nullptr && InType.IsValid())
			{
				Profile->Type = *InType;
				List->RequestListRefresh();
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
		.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("DreamLocSettings.Field.Temperature")))
		.Text_Lambda([this]()
		{
			const FDreamLocTranslateProfile* Profile = GetSelected();
			return Profile != nullptr ? FText::FromString(FString::SanitizeFloat(Profile->Temperature)) : FText::GetEmpty();
		})
		.OnTextCommitted_Lambda([this](const FText& InText, ETextCommit::Type)
		{
			FDreamLocTranslateProfile* Profile = GetSelected();
			float Value = 0.0f;
			if (Profile != nullptr && LexTryParseString(Value, *InText.ToString()))
			{
				Profile->Temperature = Value;
			}
		}));
	AddRow(TEXT("JsonMode"), LOCTEXT("FieldJsonMode", "JSON mode"), SNew(SCheckBox)
		.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("DreamLocSettings.Field.JsonMode")))
		.ToolTipText(LOCTEXT("JsonModeTip", "Ask for a JSON object (response_format); for a service that supports it"))
		.IsChecked_Lambda([this]()
		{
			const FDreamLocTranslateProfile* Profile = GetSelected();
			return Profile != nullptr && Profile->bJsonMode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		})
		.OnCheckStateChanged_Lambda([this](ECheckBoxState InState)
		{
			if (FDreamLocTranslateProfile* Profile = GetSelected())
			{
				Profile->bJsonMode = InState == ECheckBoxState::Checked;
			}
		}));
	AddRow(TEXT("Local"), LOCTEXT("FieldLocal", "Runs on"), SNew(SSegmentedControl<int32>)
		.Value_Lambda([this]()
		{
			const FDreamLocTranslateProfile* Profile = GetSelected();
			return Profile == nullptr || !Profile->Local.IsSet() ? 0 : Profile->Local.GetValue() ? 1 : 2;
		})
		.OnValueChanged_Lambda([this](int32 InValue)
		{
			if (FDreamLocTranslateProfile* Profile = GetSelected())
			{
				Profile->Local = InValue == 0 ? TOptional<bool>() : TOptional<bool>(InValue == 1);
				List->RequestListRefresh();
			}
		})
		+ SSegmentedControl<int32>::Slot(0).Text(LOCTEXT("LocalAuto", "By its address"))
		+ SSegmentedControl<int32>::Slot(1).Text(LOCTEXT("LocalYes", "This machine"))
		+ SSegmentedControl<int32>::Slot(2).Text(LOCTEXT("LocalNo", "A cloud service")));

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock)
			.Visibility_Lambda([this]() { return GetSelected() == nullptr ? EVisibility::Visible : EVisibility::Collapsed; })
			.Text(LOCTEXT("NoProfile", "No translator yet: Add one."))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).Visibility_Lambda([this]() { return GetSelected() != nullptr ? EVisibility::Visible : EVisibility::Collapsed; })
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
				const FDreamLocTranslateProfile* Profile = GetSelected();
				return Profile != nullptr && Profile->HasSecretInFile() ? EVisibility::Visible : EVisibility::Collapsed;
			})
			.Text(LOCTEXT("PlainKeyWarning", "This translator's key is written into translators.ini as plain text. A variable (the 'from variable' fields) keeps it out of the file."))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(SButton).AddMetaData<FTagMetaData>(FTagMetaData(TEXT("DreamLocSettings.Test"))).Text(LOCTEXT("TestProfile", "Test"))
				.ToolTipText(LOCTEXT("TestProfileTip", "Send the smallest request -- one short text -- and say how it went"))
				.IsEnabled_Lambda([this]() { return GetSelected() != nullptr; })
				.OnClicked_Lambda([this]() { TestSelected(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).AddMetaData<FTagMetaData>(FTagMetaData(TEXT("DreamLocSettings.TestResult"))).AutoWrapText(true).Text_Lambda([this]() { return TestText; })
			]
		];
}

FString SDreamLocTranslatorSettings::Written() const
{
	TArray<FDreamLocTranslateProfile> Values;
	for (const TSharedPtr<FDreamLocTranslateProfile>& Item : Items)
	{
		Values.Add(*Item);
	}
	return DreamLocTranslateConfig::WriteProfiles(Values);
}

bool SDreamLocTranslatorSettings::IsDirty() const
{
	return !Written().Equals(Loaded, ESearchCase::CaseSensitive);
}

void SDreamLocTranslatorSettings::Reload()
{
	const FString WasSelected = Selected.IsValid() ? Selected->Name : FString();
	TArray<FString> Warnings;
	Items.Reset();
	for (const FDreamLocTranslateProfile& Profile : DreamLocTranslateConfig::LoadProfiles(&Warnings))
	{
		Items.Add(MakeShared<FDreamLocTranslateProfile>(Profile));
	}
	Loaded = Written();
	LoadedFile = DreamLocTranslateSettingsLocal::ContentOf(DreamLocTranslateConfig::GetUserConfigPath());
	StatusText = Warnings.Num() > 0 ? FText::FromString(FString::Join(Warnings, TEXT("\n"))) : FText::GetEmpty();
	Selected.Reset();
	List->RequestListRefresh();
	Select(WasSelected);
	if (!Selected.IsValid() && Items.Num() > 0)
	{
		List->SetSelection(Items[0]);
	}
}

void SDreamLocTranslatorSettings::Select(const FString& InName)
{
	for (const TSharedPtr<FDreamLocTranslateProfile>& Item : Items)
	{
		if (Item->Name == InName)
		{
			List->SetSelection(Item);
			Selected = Item;
		}
	}
}

bool SDreamLocTranslatorSettings::Save()
{
	TArray<FDreamLocTranslateProfile> Values;
	TSet<FString> Names;
	for (const TSharedPtr<FDreamLocTranslateProfile>& Profile : Items)
	{
		if (Profile->Name.IsEmpty() || Names.Contains(Profile->Name))
		{
			StatusText = FText::Format(LOCTEXT("ProfileNameClash", "Not saved: every translator needs a name of its own ('{0}')."), FText::FromString(Profile->Name));
			return false;
		}
		Names.Add(Profile->Name);
		Values.Add(*Profile);
	}
	if (!DreamLocTranslateConfig::SaveProfiles(Values))
	{
		StatusText = FText::Format(LOCTEXT("ProfilesNotSaved", "{0} cannot be written"), FText::FromString(DreamLocTranslateConfig::GetUserConfigPath()));
		return false;
	}
	Loaded = Written();
	LoadedFile = DreamLocTranslateSettingsLocal::ContentOf(DreamLocTranslateConfig::GetUserConfigPath());
	StatusText = FText::Format(LOCTEXT("ProfilesSaved", "Saved to {0}"), FText::FromString(DreamLocTranslateConfig::GetUserConfigPath()));
	return true;
}

EActiveTimerReturnType SDreamLocTranslatorSettings::ReloadWhenChanged(double InCurrentTime, float InDeltaTime)
{
	// Changed elsewhere (another editor, a hand edit) and nothing here unsaved: read again.
	if (DreamLocTranslateSettingsLocal::ContentOf(DreamLocTranslateConfig::GetUserConfigPath()) != LoadedFile && !IsDirty())
	{
		Reload();
	}
	return EActiveTimerReturnType::Continue;
}

void SDreamLocTranslatorSettings::TestSelected()
{
	const FDreamLocTranslateProfile* Chosen = GetSelected();
	if (Chosen == nullptr)
	{
		return;
	}
	const FDreamLocTranslateProfile Profile = *Chosen;
	const FDreamLocSettings Project = FDreamLocSettings::LoadFromDirectory(FDreamLocTextSource::GetProjectDirectory());
	const FString Source = Project.SourceCulture;
	const FString Target = Project.TargetCultures.Num() > 0 ? Project.TargetCultures[0] : FString(TEXT("en"));
	TestText = FText::Format(LOCTEXT("Testing", "Testing {0}..."), FText::FromString(Profile.Name));
	const TWeakPtr<SDreamLocTranslatorSettings> Weak = SharedThis(this);
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
			if (const TSharedPtr<SDreamLocTranslatorSettings> Page = Weak.Pin())
			{
				const FText Time = FText::AsNumber(FMath::RoundToDouble(Seconds * 100.0) / 100.0);
				Page->TestText = bSucceeded
					? FText::Format(LOCTEXT("TestPassed", "{0} answered in {1} s: \"{2}\""), FText::FromString(Name), Time, FText::FromString(Said))
					: FText::Format(LOCTEXT("TestFailed", "{0} failed after {1} s: {2}"), FText::FromString(Name), Time, FText::FromString(Said));
			}
		});
	});
}

// ---------------------------------------------------------------------------------------------------------- the project

void SDreamLocProjectTranslateSettings::Construct(const FArguments& InArgs)
{
	DLocDirectory = InArgs._DLocDirectory.IsEmpty() ? FDreamLocTextSource::GetProjectDirectory() : InArgs._DLocDirectory;
	TSharedRef<SGridPanel> Grid = SNew(SGridPanel).FillColumn(1, 1.0f);
	int32 RowIndex = 0;
	auto AddRow = [&Grid, &RowIndex](const FText& InLabel, const FText& InTip, const TSharedRef<SWidget>& InEditor)
	{
		Grid->AddSlot(0, RowIndex).VAlign(VAlign_Top).Padding(0.0f, 4.0f, 8.0f, 2.0f)[SNew(STextBlock).Text(InLabel).ToolTipText(InTip)];
		Grid->AddSlot(1, RowIndex).Padding(0.0f, 2.0f)[InEditor];
		++RowIndex;
	};
	auto Choose = [this](const TCHAR* InTag, TSharedPtr<SComboBox<TSharedPtr<FString>>>& OutCombo, TArray<TSharedPtr<FString>>* InChoices, TSharedPtr<FString>* InChoice) -> TSharedRef<SWidget>
	{
		return SNew(SBox).HAlign(HAlign_Left)
		[
			SAssignNew(OutCombo, SComboBox<TSharedPtr<FString>>)
			.AddMetaData<FTagMetaData>(FTagMetaData(InTag))
			.OptionsSource(InChoices)
			.OnGenerateWidget_Lambda([this](TSharedPtr<FString> InItem) -> TSharedRef<SWidget> { return SNew(STextBlock).Text(ChoiceLabel(InItem)); })
			.OnSelectionChanged_Lambda([this, InChoice](TSharedPtr<FString> InItem, ESelectInfo::Type InHow)
			{
				if (InItem.IsValid())
				{
					*InChoice = InItem;
					bDirty |= InHow != ESelectInfo::Direct;
				}
			})
			[
				SNew(STextBlock).Text_Lambda([this, InChoice]() { return ChoiceLabel(*InChoice); })
			]
		];
	};
	auto Dirty = [this](const FText&) { bDirty = true; };
	AddRow(LOCTEXT("SettingProfile", "Translator"), LOCTEXT("SettingProfileTip", "[Translate] Profile: the translator used by default"),
		Choose(TEXT("DreamLocProjectSettings.Profile"), ProfileCombo, &ProfileChoices, &ProfileChoice));
	AddRow(LOCTEXT("SettingByCulture", "Translator by culture"), LOCTEXT("SettingByCultureTip", "[Translate] Profile.<culture>: one line each, ja=deepl"),
		SNew(SBox).MinDesiredHeight(44.0f)[SAssignNew(ByCulture, SMultiLineEditableTextBox).AddMetaData<FTagMetaData>(FTagMetaData(TEXT("DreamLocProjectSettings.ByCulture")))
			.HintText(LOCTEXT("ByCultureHint", "ja=deepl")).OnTextChanged_Lambda(Dirty)]);
	AddRow(LOCTEXT("SettingFallback", "Fallback large model"), LOCTEXT("SettingFallbackTip", "[Translate] Fallback: what a machine translator cannot do goes to it"),
		Choose(TEXT("DreamLocProjectSettings.Fallback"), FallbackCombo, &FallbackChoices, &FallbackChoice));
	AddRow(LOCTEXT("SettingBatchSize", "Entries per request"), LOCTEXT("SettingBatchSizeTip", "[Translate] BatchSize (default 40)"),
		SAssignNew(BatchSize, SEditableTextBox).AddMetaData<FTagMetaData>(FTagMetaData(TEXT("DreamLocProjectSettings.BatchSize"))).OnTextChanged_Lambda(Dirty));
	AddRow(LOCTEXT("SettingConcurrency", "Requests at once"), LOCTEXT("SettingConcurrencyTip", "[Translate] Concurrency (default 2)"),
		SAssignNew(Concurrency, SEditableTextBox).AddMetaData<FTagMetaData>(FTagMetaData(TEXT("DreamLocProjectSettings.Concurrency"))).OnTextChanged_Lambda(Dirty));
	AddRow(LOCTEXT("SettingRetry", "Tries after the checks"), LOCTEXT("SettingRetryTip", "[Translate] Retry: times a translation the checks turn down goes back to the large model (default 1)"),
		SAssignNew(Retry, SEditableTextBox).AddMetaData<FTagMetaData>(FTagMetaData(TEXT("DreamLocProjectSettings.Retry"))).OnTextChanged_Lambda(Dirty));
	AddRow(LOCTEXT("SettingStyle", "Style guide"), LOCTEXT("SettingStyleTip", "[Translate] Style, relative to the project; DLoc/_style.<culture>.md wins when there is one"),
		SAssignNew(Style, SEditableTextBox).AddMetaData<FTagMetaData>(FTagMetaData(TEXT("DreamLocProjectSettings.Style"))).OnTextChanged_Lambda(Dirty));
	AddRow(LOCTEXT("SettingLocalOnly", "Only to local translators"), LOCTEXT("SettingLocalOnlyTip", "[Translate] LocalOnly: file paths or namespaces, one a line"),
		SNew(SBox).MinDesiredHeight(44.0f)[SAssignNew(LocalOnly, SMultiLineEditableTextBox).AddMetaData<FTagMetaData>(FTagMetaData(TEXT("DreamLocProjectSettings.LocalOnly")))
			.HintText(LOCTEXT("LocalOnlyHint", "DNA/")).OnTextChanged_Lambda(Dirty)]);

	ChildSlot
	[
		SNew(SScrollBox)
		+ SScrollBox::Slot().Padding(8.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SNew(STextBlock).AutoWrapText(true).ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(FText::Format(LOCTEXT("ProjectIntro", "How this project translates: {0}, [Translate]. Saving writes only what you changed; the file's comments stay. The translators themselves are yours, in Editor Preferences > Plugins > DreamLoc translators."),
					FText::FromString(DLocDirectory / FDreamLocSettings::FileName)))
			]
			+ SVerticalBox::Slot().AutoHeight()[Grid]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(SButton).AddMetaData<FTagMetaData>(FTagMetaData(TEXT("DreamLocProjectSettings.Save"))).Text(LOCTEXT("SaveSettings", "Save to dloc.ini"))
					.OnClicked_Lambda([this]() { Save(); return FReply::Handled(); })
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(SButton).Text(LOCTEXT("ReloadSettings", "Read again")).OnClicked_Lambda([this]() { Reload(); return FReply::Handled(); })
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).AddMetaData<FTagMetaData>(FTagMetaData(TEXT("DreamLocProjectSettings.Status"))).AutoWrapText(true).Text_Lambda([this]() { return StatusText; })
				]
			]
		]
	];
	Reload();
	RegisterActiveTimer(2.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SDreamLocProjectTranslateSettings::ReloadWhenChanged));
}

FText SDreamLocProjectTranslateSettings::ChoiceLabel(const TSharedPtr<FString>& InChoice) const
{
	if (!InChoice.IsValid() || InChoice->IsEmpty())
	{
		return LOCTEXT("NoProfileSetting", "(none)");
	}
	const FDreamLocTranslateProfile* Found = DreamLocTranslateConfig::FindProfile(Profiles, *InChoice);
	return Found != nullptr ? DreamLocTranslateSettingsLocal::ProfileLabel(*Found)
		: FText::Format(LOCTEXT("UnknownProfile", "{0} (not among your translators)"), FText::FromString(*InChoice));
}

void SDreamLocProjectTranslateSettings::RebuildChoices()
{
	auto Build = [this](TArray<TSharedPtr<FString>>& OutChoices, bool bInLargeModelsOnly, const FString& InChosen)
	{
		OutChoices.Reset();
		OutChoices.Add(MakeShared<FString>());
		TSharedPtr<FString> Chosen = OutChoices[0];
		for (const FDreamLocTranslateProfile& Each : Profiles)
		{
			if (!Each.Name.IsEmpty() && (!bInLargeModelsOnly || Each.GetKind() == EDreamLocTranslatorKind::LargeModel))
			{
				OutChoices.Add(MakeShared<FString>(Each.Name));
				if (Each.Name == InChosen)
				{
					Chosen = OutChoices.Last();
				}
			}
		}
		// A name dloc.ini has and the translators do not: kept, so saving does not lose it.
		if (!InChosen.IsEmpty() && Chosen == OutChoices[0])
		{
			OutChoices.Add(MakeShared<FString>(InChosen));
			Chosen = OutChoices.Last();
		}
		return Chosen;
	};
	ProfileChoice = Build(ProfileChoices, /*bInLargeModelsOnly*/ false, Settings.TranslateProfile);
	FallbackChoice = Build(FallbackChoices, /*bInLargeModelsOnly*/ true, Settings.TranslateFallback);
	ProfileCombo->RefreshOptions();
	ProfileCombo->SetSelectedItem(ProfileChoice);
	FallbackCombo->RefreshOptions();
	FallbackCombo->SetSelectedItem(FallbackChoice);
}

void SDreamLocProjectTranslateSettings::Reload()
{
	Settings = FDreamLocSettings::LoadFromDirectory(DLocDirectory);
	Profiles = DreamLocTranslateConfig::LoadProfiles();
	LoadedFile = DreamLocTranslateSettingsLocal::ContentOf(DLocDirectory / FDreamLocSettings::FileName);
	RebuildChoices();
	BatchSize->SetText(FText::AsNumber(Settings.TranslateBatchSize, &FNumberFormattingOptions::DefaultNoGrouping()));
	Concurrency->SetText(FText::AsNumber(Settings.TranslateConcurrency, &FNumberFormattingOptions::DefaultNoGrouping()));
	Retry->SetText(FText::AsNumber(Settings.TranslateRetry, &FNumberFormattingOptions::DefaultNoGrouping()));
	Style->SetText(FText::FromString(Settings.TranslateStyle));
	TArray<FString> Lines;
	for (const TPair<FString, FString>& Culture : Settings.TranslateProfileByCulture)
	{
		Lines.Add(Culture.Key + TEXT("=") + Culture.Value);
	}
	ByCulture->SetText(FText::FromString(FString::Join(Lines, TEXT("\n"))));
	LocalOnly->SetText(FText::FromString(FString::Join(Settings.TranslateLocalOnly, TEXT("\n"))));
	bDirty = false;
}

bool SDreamLocProjectTranslateSettings::Save()
{
	FDreamLocSettings Changed = Settings;
	Changed.TranslateProfile = ProfileChoice.IsValid() ? *ProfileChoice : FString();
	Changed.TranslateFallback = FallbackChoice.IsValid() ? *FallbackChoice : FString();
	Changed.TranslateStyle = Style->GetText().ToString().TrimStartAndEnd();
	struct FNumber
	{
		SEditableTextBox* Box;
		int32* Value;
		int32 Least;
	};
	const FNumber Numbers[] = {
		{ BatchSize.Get(), &Changed.TranslateBatchSize, 1 }, { Concurrency.Get(), &Changed.TranslateConcurrency, 1 }, { Retry.Get(), &Changed.TranslateRetry, 0 } };
	for (const FNumber& Number : Numbers)
	{
		const FString Text = Number.Box->GetText().ToString().TrimStartAndEnd();
		int32 Value = 0;
		if (!Text.IsNumeric() || !LexTryParseString(Value, *Text) || Value < Number.Least)
		{
			StatusText = FText::Format(LOCTEXT("BadNumber", "Not saved: '{0}' is not a whole number of {1} or more."), FText::FromString(Text), Number.Least);
			return false;
		}
		*Number.Value = Value;
	}
	TArray<FString> Lines;
	ByCulture->GetText().ToString().ParseIntoArrayLines(Lines);
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
			StatusText = FText::Format(LOCTEXT("BadByCulture", "Not saved: '{0}' is not culture=translator."), FText::FromString(Line));
			return false;
		}
		Changed.TranslateProfileByCulture.Emplace(Culture.TrimStartAndEnd(), Profile.TrimStartAndEnd());
	}
	LocalOnly->GetText().ToString().ParseIntoArrayLines(Lines);
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
	const FString Written = DreamLocTranslateConfig::WriteTranslateChanges(Text, Settings, Changed);
	if (Written.Equals(Text, ESearchCase::CaseSensitive))
	{
		StatusText = LOCTEXT("NothingChanged", "Nothing changed.");
		bDirty = false;
		return true;
	}
	IFileManager::Get().MakeDirectory(*DLocDirectory, /*Tree*/ true);
	if (!FFileHelper::SaveStringToFile(Written, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		StatusText = FText::Format(LOCTEXT("SettingsNotSaved", "{0} cannot be written"), FText::FromString(Path));
		return false;
	}
	Reload();
	StatusText = FText::Format(LOCTEXT("SettingsSaved", "Saved to {0}"), FText::FromString(Path));
	return true;
}

EActiveTimerReturnType SDreamLocProjectTranslateSettings::ReloadWhenChanged(double InCurrentTime, float InDeltaTime)
{
	if (DreamLocTranslateSettingsLocal::ContentOf(DLocDirectory / FDreamLocSettings::FileName) != LoadedFile && !bDirty)
	{
		Reload();
	}
	return EActiveTimerReturnType::Continue;
}

#undef LOCTEXT_NAMESPACE
