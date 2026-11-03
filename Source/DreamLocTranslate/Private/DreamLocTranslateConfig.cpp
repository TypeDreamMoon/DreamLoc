// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocTranslateConfig.h"

#include "Algo/StableSort.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

DEFINE_LOG_CATEGORY(LogDreamLocTranslate);

namespace DreamLocTranslateConfigLocal
{
	FCriticalSection PathLock;
	FString PathOverride;

	FString Environment(const FString& InName)
	{
		return InName.IsEmpty() ? FString() : FPlatformMisc::GetEnvironmentVariable(*InName);
	}

	FString StripComment(const FString& InLine)
	{
		FString Line = InLine;
		const FString Trimmed = Line.TrimStart();
		if (Trimmed.StartsWith(TEXT(";")) || Trimmed.StartsWith(TEXT("#")))
		{
			return FString();
		}
		const int32 Comment = Line.Find(TEXT(" ;"));
		return Comment == INDEX_NONE ? Line : Line.Left(Comment);
	}

	/** Keys of dreamdocs, the documentation translator that reads profiles from the same file. */
	bool IsDreamDocsKey(const FString& InKey)
	{
		static const TCHAR* const Keys[] = { TEXT("Api"), TEXT("MaxTokens"), TEXT("TokenField"), TEXT("ExtraBody"), TEXT("Headers"),
			TEXT("Concurrency"), TEXT("Retry"), TEXT("ChunkChars"), TEXT("Timeout") };
		for (const TCHAR* Key : Keys)
		{
			if (InKey.Equals(Key, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	bool ParseBool(const FString& InValue, bool& OutValue)
	{
		if (InValue.Equals(TEXT("true"), ESearchCase::IgnoreCase) || InValue == TEXT("1") || InValue.Equals(TEXT("yes"), ESearchCase::IgnoreCase))
		{
			OutValue = true;
			return true;
		}
		if (InValue.Equals(TEXT("false"), ESearchCase::IgnoreCase) || InValue == TEXT("0") || InValue.Equals(TEXT("no"), ESearchCase::IgnoreCase))
		{
			OutValue = false;
			return true;
		}
		return false;
	}

	/** The host of a URL: what decides whether a service is local. */
	FString HostOf(const FString& InUrl)
	{
		FString Rest = InUrl;
		int32 Scheme = Rest.Find(TEXT("://"));
		if (Scheme != INDEX_NONE)
		{
			Rest.RightChopInline(Scheme + 3);
		}
		int32 End = INDEX_NONE;
		for (const TCHAR Stop : { TEXT('/'), TEXT('?'), TEXT('#') })
		{
			int32 At = INDEX_NONE;
			if (Rest.FindChar(Stop, At) && (End == INDEX_NONE || At < End))
			{
				End = At;
			}
		}
		FString Host = End == INDEX_NONE ? Rest : Rest.Left(End);
		const int32 At = Host.Find(TEXT("@"));
		if (At != INDEX_NONE)
		{
			Host.RightChopInline(At + 1);
		}
		if (Host.StartsWith(TEXT("[")))
		{
			int32 Close = INDEX_NONE;
			return Host.FindChar(TEXT(']'), Close) ? Host.Mid(1, Close - 1) : Host;
		}
		int32 Colon = INDEX_NONE;
		return Host.FindLastChar(TEXT(':'), Colon) ? Host.Left(Colon) : Host;
	}
}

EDreamLocTranslatorKind FDreamLocTranslateProfile::GetKind() const
{
	return Type.Equals(TEXT("OpenAI"), ESearchCase::IgnoreCase) ? EDreamLocTranslatorKind::LargeModel : EDreamLocTranslatorKind::Machine;
}

bool FDreamLocTranslateProfile::IsLocal() const
{
	if (Local.IsSet())
	{
		return Local.GetValue();
	}
	if (BaseUrl.IsEmpty())
	{
		return false;
	}
	const FString Host = DreamLocTranslateConfigLocal::HostOf(BaseUrl).ToLower();
	return Host == TEXT("localhost") || Host == TEXT("127.0.0.1") || Host == TEXT("::1") || Host.EndsWith(TEXT(".local"));
}

FString FDreamLocTranslateProfile::GetApiKey() const
{
	return !ApiKey.IsEmpty() ? ApiKey : DreamLocTranslateConfigLocal::Environment(ApiKeyEnv);
}

FString FDreamLocTranslateProfile::GetAppId() const
{
	return !AppId.IsEmpty() ? AppId : DreamLocTranslateConfigLocal::Environment(AppIdEnv);
}

FString FDreamLocTranslateProfile::GetSecret() const
{
	return !Secret.IsEmpty() ? Secret : DreamLocTranslateConfigLocal::Environment(SecretEnv);
}

bool FDreamLocTranslateProfile::HasSecretInFile() const
{
	return !ApiKey.IsEmpty() || !Secret.IsEmpty();
}

TArray<FString> FDreamLocTranslateProfile::GetSecrets() const
{
	TArray<FString> Out;
	for (const FString& Value : { GetApiKey(), GetSecret() })
	{
		if (Value.Len() >= 4)
		{
			Out.AddUnique(Value);
		}
	}
	return Out;
}

FString DreamLocTranslateConfig::GetUserConfigPath()
{
	using namespace DreamLocTranslateConfigLocal;
	{
		FScopeLock ScopeLock(&PathLock);
		if (!PathOverride.IsEmpty())
		{
			return PathOverride;
		}
	}
	const FString FromEnvironment = FPlatformMisc::GetEnvironmentVariable(TEXT("DREAMLOC_TRANSLATORS_INI"));
	if (!FromEnvironment.IsEmpty())
	{
		return FromEnvironment;
	}
	FString AppData = FPlatformMisc::GetEnvironmentVariable(TEXT("APPDATA"));
	if (AppData.IsEmpty())
	{
		AppData = FPlatformProcess::UserSettingsDir();
	}
	FString Path = AppData / TEXT("DreamLoc/translators.ini");
	FPaths::NormalizeFilename(Path);
	return Path;
}

void DreamLocTranslateConfig::SetUserConfigPath(const FString& InPath)
{
	FScopeLock ScopeLock(&DreamLocTranslateConfigLocal::PathLock);
	DreamLocTranslateConfigLocal::PathOverride = InPath;
}

TArray<FDreamLocTranslateProfile> DreamLocTranslateConfig::ParseProfiles(const FString& InText, TArray<FString>& OutWarnings)
{
	using namespace DreamLocTranslateConfigLocal;
	TArray<FDreamLocTranslateProfile> Profiles;
	TArray<FString> Lines;
	InText.ParseIntoArrayLines(Lines, /*InCullEmpty*/ false);
	FDreamLocTranslateProfile* Current = nullptr;
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		FString Line = StripComment(Lines[Index]).TrimStartAndEnd();
		if (Index == 0 && Line.Len() > 0 && Line[0] == TEXT('\xFEFF'))
		{
			Line.RightChopInline(1);
		}
		if (Line.IsEmpty())
		{
			continue;
		}
		if (Line.StartsWith(TEXT("[")) && Line.EndsWith(TEXT("]")))
		{
			const FString Section = Line.Mid(1, Line.Len() - 2).TrimStartAndEnd();
			if (!Section.StartsWith(TEXT("Profile ")))
			{
				OutWarnings.Add(FString::Printf(TEXT("line %d: [%s] is not a [Profile <name>] section and is not read"), Index + 1, *Section));
				Current = nullptr;
				continue;
			}
			Current = &Profiles.AddDefaulted_GetRef();
			Current->Name = Section.RightChop(8).TrimStartAndEnd();
			continue;
		}
		FString Key;
		FString Value;
		if (!Line.Split(TEXT("="), &Key, &Value) || Current == nullptr)
		{
			OutWarnings.Add(FString::Printf(TEXT("line %d: expected key=value inside a [Profile <name>] section"), Index + 1));
			continue;
		}
		Key.TrimStartAndEndInline();
		Value.TrimStartAndEndInline();
		bool bValue = false;
		if (Key == TEXT("Type")) { Current->Type = Value; }
		else if (Key == TEXT("BaseUrl")) { Current->BaseUrl = Value; }
		else if (Key == TEXT("Model")) { Current->Model = Value; }
		else if (Key == TEXT("ApiKey")) { Current->ApiKey = Value; }
		else if (Key == TEXT("ApiKeyEnv")) { Current->ApiKeyEnv = Value; }
		else if (Key == TEXT("AppId")) { Current->AppId = Value; }
		else if (Key == TEXT("AppIdEnv")) { Current->AppIdEnv = Value; }
		else if (Key == TEXT("Secret")) { Current->Secret = Value; }
		else if (Key == TEXT("SecretEnv")) { Current->SecretEnv = Value; }
		else if (Key == TEXT("Region")) { Current->Region = Value; }
		else if (Key == TEXT("Proxy")) { Current->Proxy = Value; }
		else if (Key == TEXT("Temperature") && Value.IsNumeric()) { Current->Temperature = FCString::Atof(*Value); }
		else if (Key == TEXT("JsonMode") && ParseBool(Value, bValue)) { Current->bJsonMode = bValue; }
		else if (Key == TEXT("Local") && ParseBool(Value, bValue)) { Current->Local = bValue; }
		else
		{
			// Kept and written back as is. Keys the documentation translator reads are expected, not a typo.
			Current->Others.Emplace(Key, Value);
			if (!IsDreamDocsKey(Key))
			{
				OutWarnings.Add(FString::Printf(TEXT("line %d: '%s' is not read (kept as written)"), Index + 1, *Key));
			}
		}
	}
	return Profiles;
}

FString DreamLocTranslateConfig::WriteProfiles(TConstArrayView<FDreamLocTranslateProfile> InProfiles)
{
	FString Out = TEXT("; DreamLoc's translators: shared by every project, never in a repository. Written by Editor Preferences > Plugins > DreamLoc translators.\n");
	auto Add = [&Out](const TCHAR* InKey, const FString& InValue)
	{
		if (!InValue.IsEmpty())
		{
			Out += FString::Printf(TEXT("%s=%s\n"), InKey, *InValue);
		}
	};
	for (const FDreamLocTranslateProfile& Profile : InProfiles)
	{
		Out += FString::Printf(TEXT("\n[Profile %s]\n"), *Profile.Name);
		Add(TEXT("Type"), Profile.Type);
		Add(TEXT("BaseUrl"), Profile.BaseUrl);
		Add(TEXT("Model"), Profile.Model);
		Add(TEXT("ApiKey"), Profile.ApiKey);
		Add(TEXT("ApiKeyEnv"), Profile.ApiKeyEnv);
		Add(TEXT("AppId"), Profile.AppId);
		Add(TEXT("AppIdEnv"), Profile.AppIdEnv);
		Add(TEXT("Secret"), Profile.Secret);
		Add(TEXT("SecretEnv"), Profile.SecretEnv);
		Add(TEXT("Region"), Profile.Region);
		Add(TEXT("Proxy"), Profile.Proxy);
		if (Profile.bJsonMode)
		{
			Add(TEXT("JsonMode"), TEXT("true"));
		}
		// Written whatever the type: a profile of another tool's type may carry one DreamLoc never uses.
		if (!FMath::IsNearlyEqual(Profile.Temperature, 0.2f))
		{
			Add(TEXT("Temperature"), FString::SanitizeFloat(Profile.Temperature));
		}
		if (Profile.Local.IsSet())
		{
			Add(TEXT("Local"), Profile.Local.GetValue() ? TEXT("true") : TEXT("false"));
		}
		for (const TPair<FString, FString>& Other : Profile.Others)
		{
			Add(*Other.Key, Other.Value);
		}
	}
	return Out;
}

TArray<FDreamLocTranslateProfile> DreamLocTranslateConfig::LoadProfiles(TArray<FString>* OutWarnings)
{
	FString Text;
	const FString Path = GetUserConfigPath();
	if (!IFileManager::Get().FileExists(*Path) || !FFileHelper::LoadFileToString(Text, *Path))
	{
		return {};
	}
	TArray<FString> Warnings;
	TArray<FDreamLocTranslateProfile> Profiles = ParseProfiles(Text, Warnings);
	if (OutWarnings != nullptr)
	{
		OutWarnings->Append(Warnings);
	}
	return Profiles;
}

bool DreamLocTranslateConfig::SaveProfiles(TConstArrayView<FDreamLocTranslateProfile> InProfiles)
{
	const FString Path = GetUserConfigPath();
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), /*Tree*/ true);
	return FFileHelper::SaveStringToFile(WriteProfiles(InProfiles), *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

const FDreamLocTranslateProfile* DreamLocTranslateConfig::FindProfile(TConstArrayView<FDreamLocTranslateProfile> InProfiles, const FString& InName)
{
	for (const FDreamLocTranslateProfile& Profile : InProfiles)
	{
		if (Profile.Name.Equals(InName, ESearchCase::IgnoreCase))
		{
			return &Profile;
		}
	}
	return nullptr;
}

FString DreamLocTranslateConfig::WriteTranslateSection(const FString& InDLocText, const FDreamLocSettings& InSettings)
{
	const FDreamLocSettings Defaults;
	TArray<FString> Section;
	Section.Add(TEXT("[Translate]"));
	if (!InSettings.TranslateProfile.IsEmpty())
	{
		Section.Add(TEXT("Profile=") + InSettings.TranslateProfile);
	}
	for (const TPair<FString, FString>& Pair : InSettings.TranslateProfileByCulture)
	{
		Section.Add(FString::Printf(TEXT("Profile.%s=%s"), *Pair.Key, *Pair.Value));
	}
	if (!InSettings.TranslateFallback.IsEmpty())
	{
		Section.Add(TEXT("Fallback=") + InSettings.TranslateFallback);
	}
	if (InSettings.TranslateBatchSize != Defaults.TranslateBatchSize)
	{
		Section.Add(FString::Printf(TEXT("BatchSize=%d"), InSettings.TranslateBatchSize));
	}
	if (InSettings.TranslateConcurrency != Defaults.TranslateConcurrency)
	{
		Section.Add(FString::Printf(TEXT("Concurrency=%d"), InSettings.TranslateConcurrency));
	}
	if (InSettings.TranslateRetry != Defaults.TranslateRetry)
	{
		Section.Add(FString::Printf(TEXT("Retry=%d"), InSettings.TranslateRetry));
	}
	if (InSettings.TranslateStyle != Defaults.TranslateStyle)
	{
		Section.Add(TEXT("Style=") + InSettings.TranslateStyle);
	}
	for (int32 Index = 0; Index < InSettings.TranslateLocalOnly.Num(); ++Index)
	{
		Section.Add((Index == 0 ? TEXT("LocalOnly=") : TEXT("+LocalOnly=")) + InSettings.TranslateLocalOnly[Index]);
	}

	// The lines outside the old section as they were; the new section where the old one stood, or at the end.
	TArray<FString> Lines;
	InDLocText.ParseIntoArrayLines(Lines, /*InCullEmpty*/ false);
	TArray<FString> Out;
	bool bInSection = false;
	bool bWritten = false;
	for (const FString& Line : Lines)
	{
		const FString Trimmed = Line.TrimStartAndEnd();
		if (Trimmed.StartsWith(TEXT("[")) && Trimmed.EndsWith(TEXT("]")))
		{
			const bool bTranslate = Trimmed.Mid(1, Trimmed.Len() - 2).TrimStartAndEnd() == TEXT("Translate");
			if (bTranslate && !bWritten)
			{
				Out.Append(Section);
				bWritten = true;
			}
			if (!bTranslate && bInSection && Out.Num() > 0 && !Out.Last().TrimStartAndEnd().IsEmpty())
			{
				Out.Add(FString());
			}
			bInSection = bTranslate;
			if (bTranslate)
			{
				continue;
			}
		}
		if (bInSection)
		{
			// Comments of the old section stay with it; its settings are what the new section says.
			if (Trimmed.StartsWith(TEXT(";")) || Trimmed.StartsWith(TEXT("#")))
			{
				Out.Add(Line);
			}
			continue;
		}
		Out.Add(Line);
	}
	while (Out.Num() > 0 && Out.Last().TrimStartAndEnd().IsEmpty())
	{
		Out.Pop();
	}
	if (!bWritten)
	{
		if (Out.Num() > 0)
		{
			Out.Add(FString());
		}
		Out.Append(Section);
	}
	return FString::Join(Out, TEXT("\n")) + TEXT("\n");
}

namespace DreamLocTranslateConfigLocal
{
	/** The [Translate] lines the settings are written as, a key at a time, in the order a new section has them. */
	TArray<TPair<FString, TArray<FString>>> TranslateLines(const FDreamLocSettings& InSettings)
	{
		const FDreamLocSettings Defaults;
		TArray<TPair<FString, TArray<FString>>> Out;
		auto Add = [&Out](const FString& InKey, TArray<FString> InLines)
		{
			Out.Emplace(InKey, MoveTemp(InLines));
		};
		if (!InSettings.TranslateProfile.IsEmpty())
		{
			Add(TEXT("Profile"), { TEXT("Profile=") + InSettings.TranslateProfile });
		}
		for (const TPair<FString, FString>& Pair : InSettings.TranslateProfileByCulture)
		{
			Add(TEXT("Profile.") + Pair.Key, { FString::Printf(TEXT("Profile.%s=%s"), *Pair.Key, *Pair.Value) });
		}
		if (!InSettings.TranslateFallback.IsEmpty())
		{
			Add(TEXT("Fallback"), { TEXT("Fallback=") + InSettings.TranslateFallback });
		}
		if (InSettings.TranslateBatchSize != Defaults.TranslateBatchSize)
		{
			Add(TEXT("BatchSize"), { FString::Printf(TEXT("BatchSize=%d"), InSettings.TranslateBatchSize) });
		}
		if (InSettings.TranslateConcurrency != Defaults.TranslateConcurrency)
		{
			Add(TEXT("Concurrency"), { FString::Printf(TEXT("Concurrency=%d"), InSettings.TranslateConcurrency) });
		}
		if (InSettings.TranslateRetry != Defaults.TranslateRetry)
		{
			Add(TEXT("Retry"), { FString::Printf(TEXT("Retry=%d"), InSettings.TranslateRetry) });
		}
		if (InSettings.TranslateStyle != Defaults.TranslateStyle)
		{
			Add(TEXT("Style"), { TEXT("Style=") + InSettings.TranslateStyle });
		}
		if (InSettings.TranslateLocalOnly.Num() > 0)
		{
			TArray<FString> Lines;
			for (int32 Index = 0; Index < InSettings.TranslateLocalOnly.Num(); ++Index)
			{
				Lines.Add((Index == 0 ? TEXT("LocalOnly=") : TEXT("+LocalOnly=")) + InSettings.TranslateLocalOnly[Index]);
			}
			Add(TEXT("LocalOnly"), MoveTemp(Lines));
		}
		return Out;
	}

	/** The key a setting line sets (`+LocalOnly=a` sets LocalOnly); empty for a comment, a blank line, a section. */
	FString KeyOf(const FString& InLine)
	{
		FString Line = InLine.TrimStartAndEnd();
		if (Line.IsEmpty() || Line.StartsWith(TEXT(";")) || Line.StartsWith(TEXT("#")) || Line.StartsWith(TEXT("[")))
		{
			return FString();
		}
		while (!Line.IsEmpty() && (Line[0] == TEXT('+') || Line[0] == TEXT('-') || Line[0] == TEXT('.') || Line[0] == TEXT('!')))
		{
			Line.RightChopInline(1);
		}
		FString Key;
		return Line.Split(TEXT("="), &Key, nullptr) ? Key.TrimStartAndEnd() : Line;
	}
}

FString DreamLocTranslateConfig::WriteTranslateChanges(const FString& InDLocText, const FDreamLocSettings& InBefore, const FDreamLocSettings& InAfter)
{
	using namespace DreamLocTranslateConfigLocal;
	const TArray<TPair<FString, TArray<FString>>> Before = TranslateLines(InBefore);
	const TArray<TPair<FString, TArray<FString>>> After = TranslateLines(InAfter);
	auto Find = [](const TArray<TPair<FString, TArray<FString>>>& InLines, const FString& InKey) -> const TArray<FString>*
	{
		const TPair<FString, TArray<FString>>* Found = InLines.FindByPredicate([&InKey](const TPair<FString, TArray<FString>>& InPair) { return InPair.Key == InKey; });
		return Found != nullptr ? &Found->Value : nullptr;
	};
	// The keys whose lines differ, in the order a new section has them, then those taken out.
	TArray<FString> Changed;
	for (const TPair<FString, TArray<FString>>& Pair : After)
	{
		const TArray<FString>* Was = Find(Before, Pair.Key);
		if (Was == nullptr || *Was != Pair.Value)
		{
			Changed.Add(Pair.Key);
		}
	}
	for (const TPair<FString, TArray<FString>>& Pair : Before)
	{
		if (Find(After, Pair.Key) == nullptr)
		{
			Changed.AddUnique(Pair.Key);
		}
	}
	if (Changed.IsEmpty())
	{
		return InDLocText;
	}

	const FString LineEnd = InDLocText.Contains(TEXT("\r\n")) ? TEXT("\r\n") : TEXT("\n");
	TArray<FString> Lines;
	InDLocText.ParseIntoArrayLines(Lines, /*InCullEmpty*/ false);
	// The file's last line end makes no line of its own: one is put back at the end.
	if (Lines.Num() > 0 && Lines.Last().IsEmpty() && (InDLocText.EndsWith(TEXT("\n")) || InDLocText.EndsWith(TEXT("\r"))))
	{
		Lines.Pop();
	}
	int32 SectionStart = INDEX_NONE;
	int32 SectionEnd = INDEX_NONE;
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		const FString Trimmed = Lines[Index].TrimStartAndEnd();
		if (Trimmed.StartsWith(TEXT("[")) && Trimmed.EndsWith(TEXT("]")))
		{
			if (SectionStart != INDEX_NONE)
			{
				SectionEnd = Index;
				break;
			}
			if (Trimmed.Mid(1, Trimmed.Len() - 2).TrimStartAndEnd() == TEXT("Translate"))
			{
				SectionStart = Index;
			}
		}
	}
	if (SectionStart == INDEX_NONE)
	{
		while (Lines.Num() > 0 && Lines.Last().TrimStartAndEnd().IsEmpty())
		{
			Lines.Pop();
		}
		if (Lines.Num() > 0)
		{
			Lines.Add(FString());
		}
		Lines.Add(TEXT("[Translate]"));
		SectionStart = Lines.Num() - 1;
		SectionEnd = Lines.Num();
	}
	else if (SectionEnd == INDEX_NONE)
	{
		SectionEnd = Lines.Num();
	}

	// The changed keys' old lines out, each key's first place kept for its new lines.
	TMap<FString, int32> FirstAt;
	TArray<FString> Kept;
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		const bool bInSection = Index > SectionStart && Index < SectionEnd;
		const FString Key = bInSection ? KeyOf(Lines[Index]) : FString();
		if (!Key.IsEmpty() && Changed.Contains(Key))
		{
			if (!FirstAt.Contains(Key))
			{
				FirstAt.Add(Key, Kept.Num());
			}
			continue;
		}
		Kept.Add(Lines[Index]);
	}
	// The section's end in what is kept: after its last line that is not blank.
	int32 End = SectionStart + 1;
	for (int32 Index = SectionStart + 1; Index < Kept.Num(); ++Index)
	{
		const FString Trimmed = Kept[Index].TrimStartAndEnd();
		if (Trimmed.StartsWith(TEXT("[")) && Trimmed.EndsWith(TEXT("]")))
		{
			break;
		}
		if (!Trimmed.IsEmpty())
		{
			End = Index + 1;
		}
	}
	// Inserted from the last place to the first, so the places before stay where they are.
	TArray<TPair<int32, TArray<FString>>> Inserts;
	for (const FString& Key : Changed)
	{
		const TArray<FString>* Now = Find(After, Key);
		if (Now == nullptr)
		{
			continue;
		}
		const int32* At = FirstAt.Find(Key);
		Inserts.Emplace(At != nullptr ? *At : End, *Now);
	}
	Algo::StableSortBy(Inserts, [](const TPair<int32, TArray<FString>>& InInsert) { return InInsert.Key; });
	for (int32 Index = Inserts.Num() - 1; Index >= 0; --Index)
	{
		Kept.Insert(Inserts[Index].Value, Inserts[Index].Key);
	}
	return FString::Join(Kept, *LineEnd) + LineEnd;
}
