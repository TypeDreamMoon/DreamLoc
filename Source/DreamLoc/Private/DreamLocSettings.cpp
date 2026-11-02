// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocSettings.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace DreamLocSettingsLocal
{
	/** The line without its comment: from a `;` or `#` that starts the line or follows a blank. */
	FString StripComment(const FString& InLine)
	{
		for (int32 Index = 0; Index < InLine.Len(); ++Index)
		{
			const TCHAR Char = InLine[Index];
			if ((Char == TEXT(';') || Char == TEXT('#')) && (Index == 0 || FChar::IsWhitespace(InLine[Index - 1])))
			{
				return InLine.Left(Index);
			}
		}
		return InLine;
	}

	void SplitList(const FString& InValue, TArray<FString>& OutItems)
	{
		TArray<FString> Parts;
		InValue.ParseIntoArray(Parts, TEXT(","), /*InCullEmpty*/ true);
		for (FString& Part : Parts)
		{
			Part.TrimStartAndEndInline();
			if (!Part.IsEmpty())
			{
				OutItems.AddUnique(Part);
			}
		}
	}
}

FDreamLocSettings FDreamLocSettings::Parse(const FString& InText, TArray<FDreamLocDiagnostic>& OutDiagnostics)
{
	using namespace DreamLocSettingsLocal;
	FDreamLocSettings Settings;
	TArray<FString> Lines;
	InText.ParseIntoArrayLines(Lines, /*InCullEmpty*/ false);

	FString Section;
	bool bTargetCulturesReset = false;
	// A list key written without `+` replaces the default the first time it is seen; after that every line adds.
	TSet<FString> ListsReset;
	auto SetList = [&ListsReset](TArray<FString>& InOutList, const FString& InKey, const FString& InValue, bool bInAppend)
	{
		if (!bInAppend && !ListsReset.Contains(InKey))
		{
			InOutList.Reset();
		}
		ListsReset.Add(InKey);
		SplitList(InValue, InOutList);
	};
	for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
	{
		const int32 LineNumber = LineIndex + 1;
		FString Line = StripComment(Lines[LineIndex]);
		Line.TrimStartAndEndInline();
		if (LineIndex == 0 && Line.Len() > 0 && Line[0] == TEXT('\xFEFF'))
		{
			Line.RightChopInline(1);
		}
		if (Line.IsEmpty())
		{
			continue;
		}
		if (Line.StartsWith(TEXT("[")))
		{
			if (!Line.EndsWith(TEXT("]")))
			{
				OutDiagnostics.Emplace(EDreamLocSeverity::Error, DreamLocCode::SettingsSyntax, LineNumber, 1, TEXT("a section line ends with ']'"));
				continue;
			}
			Section = Line.Mid(1, Line.Len() - 2).TrimStartAndEnd();
			if (Section != TEXT("DreamLoc") && Section != TEXT("Fallback") && Section != TEXT("Gather") && Section != TEXT("Publish"))
			{
				OutDiagnostics.Emplace(EDreamLocSeverity::Warning, DreamLocCode::SettingsUnknownKey, LineNumber, 1,
					FString::Printf(TEXT("DreamLoc does not read a [%s] section; the sections are [DreamLoc], [Fallback], [Gather] and [Publish]"), *Section));
			}
			continue;
		}

		FString Key;
		FString Value;
		if (!Line.Split(TEXT("="), &Key, &Value))
		{
			OutDiagnostics.Emplace(EDreamLocSeverity::Error, DreamLocCode::SettingsSyntax, LineNumber, 1,
				FString::Printf(TEXT("expected key=value, not '%s'"), *Line));
			continue;
		}
		Key.TrimStartAndEndInline();
		Value.TrimStartAndEndInline();
		bool bAppend = false;
		if (Key.StartsWith(TEXT("+")))
		{
			bAppend = true;
			Key.RightChopInline(1);
		}

		if (Section == TEXT("DreamLoc"))
		{
			if (Key == TEXT("SourceCulture"))
			{
				if (Value.IsEmpty())
				{
					OutDiagnostics.Emplace(EDreamLocSeverity::Error, DreamLocCode::SettingsSyntax, LineNumber, 1, TEXT("SourceCulture is empty"));
				}
				else
				{
					Settings.SourceCulture = Value;
				}
			}
			else if (Key == TEXT("TargetCultures"))
			{
				// `TargetCultures=` replaces what came before; `+TargetCultures=` adds, as in the engine's own config files.
				if (!bAppend && !bTargetCulturesReset)
				{
					Settings.TargetCultures.Reset();
				}
				bTargetCulturesReset = true;
				SplitList(Value, Settings.TargetCultures);
			}
			else
			{
				OutDiagnostics.Emplace(EDreamLocSeverity::Warning, DreamLocCode::SettingsUnknownKey, LineNumber, 1,
					FString::Printf(TEXT("DreamLoc does not read '%s' in [DreamLoc]; it reads SourceCulture and TargetCultures"), *Key));
			}
		}
		else if (Section == TEXT("Fallback"))
		{
			TArray<FString> To;
			SplitList(Value, To);
			if (Key.IsEmpty() || To.Num() == 0)
			{
				OutDiagnostics.Emplace(EDreamLocSeverity::Error, DreamLocCode::SettingsSyntax, LineNumber, 1,
					TEXT("a fallback is <culture>=<culture it falls back to>, as in pt-PT=pt-BR"));
				continue;
			}
			for (const FString& Culture : To)
			{
				Settings.Fallbacks.Emplace(Key, Culture);
			}
		}
		else if (Section == TEXT("Gather"))
		{
			if (Key == TEXT("SourcePaths"))
			{
				SetList(Settings.GatherSourcePaths, Key, Value, bAppend);
			}
			else if (Key == TEXT("ContentPaths"))
			{
				SetList(Settings.GatherContentPaths, Key, Value, bAppend);
			}
			else if (Key == TEXT("ExcludePaths"))
			{
				SetList(Settings.GatherExcludePaths, Key, Value, bAppend);
			}
			else
			{
				OutDiagnostics.Emplace(EDreamLocSeverity::Warning, DreamLocCode::SettingsUnknownKey, LineNumber, 1,
					FString::Printf(TEXT("DreamLoc does not read '%s' in [Gather]; it reads SourcePaths, ContentPaths and ExcludePaths"), *Key));
			}
		}
		else if (Section == TEXT("Publish"))
		{
			if (Key == TEXT("Mode") && (Value.Equals(TEXT("Source"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("Native"), ESearchCase::IgnoreCase)))
			{
				Settings.bPublishNative = Value.Equals(TEXT("Native"), ESearchCase::IgnoreCase);
			}
			else if (Key == TEXT("Mode"))
			{
				OutDiagnostics.Emplace(EDreamLocSeverity::Error, DreamLocCode::SettingsSyntax, LineNumber, 1,
					FString::Printf(TEXT("[Publish] Mode is Source or Native, not '%s'"), *Value));
			}
			else
			{
				OutDiagnostics.Emplace(EDreamLocSeverity::Warning, DreamLocCode::SettingsUnknownKey, LineNumber, 1,
					FString::Printf(TEXT("DreamLoc does not read '%s' in [Publish]; it reads Mode"), *Key));
			}
		}
		else
		{
			OutDiagnostics.Emplace(EDreamLocSeverity::Warning, DreamLocCode::SettingsUnknownKey, LineNumber, 1,
				FString::Printf(TEXT("'%s' is outside the [DreamLoc], [Fallback], [Gather] and [Publish] sections and is not read"), *Key));
		}
	}
	return Settings;
}

FDreamLocSettings FDreamLocSettings::LoadFromDirectory(const FString& InDLocDirectory, TArray<FDreamLocDiagnostic>* OutDiagnostics)
{
	const FString Path = InDLocDirectory / FileName;
	FString Text;
	if (InDLocDirectory.IsEmpty() || !IFileManager::Get().FileExists(*Path) || !FFileHelper::LoadFileToString(Text, *Path))
	{
		return FDreamLocSettings();
	}
	TArray<FDreamLocDiagnostic> Diagnostics;
	FDreamLocSettings Settings = Parse(Text, Diagnostics);
	if (OutDiagnostics != nullptr)
	{
		OutDiagnostics->Append(Diagnostics);
	}
	return Settings;
}

TArray<FString> FDreamLocSettings::ExpandFallbacks(TConstArrayView<FString> InPrioritizedCultures) const
{
	TArray<FString> Cultures(InPrioritizedCultures.GetData(), InPrioritizedCultures.Num());
	// The list grows as it is walked, which is what puts a fallback's own fallbacks after it.
	for (int32 Index = 0; Index < Cultures.Num(); ++Index)
	{
		const FString Culture = Cultures[Index];
		for (const TPair<FString, FString>& Fallback : Fallbacks)
		{
			if (Fallback.Key.Equals(Culture, ESearchCase::IgnoreCase) && !Cultures.ContainsByPredicate(
				[&Fallback](const FString& InExisting) { return InExisting.Equals(Fallback.Value, ESearchCase::IgnoreCase); }))
			{
				Cultures.Add(Fallback.Value);
			}
		}
	}
	return Cultures;
}
