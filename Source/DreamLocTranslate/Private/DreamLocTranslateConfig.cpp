// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocTranslateConfig.h"

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
			OutWarnings.Add(FString::Printf(TEXT("line %d: '%s' is not read"), Index + 1, *Key));
		}
	}
	return Profiles;
}

FString DreamLocTranslateConfig::WriteProfiles(TConstArrayView<FDreamLocTranslateProfile> InProfiles)
{
	FString Out = TEXT("; DreamLoc's translators: shared by every project, never in a repository. Written by the editor's translation window.\n");
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
		if (!FMath::IsNearlyEqual(Profile.Temperature, 0.2f) && Profile.GetKind() == EDreamLocTranslatorKind::LargeModel)
		{
			Add(TEXT("Temperature"), FString::SanitizeFloat(Profile.Temperature));
		}
		if (Profile.Local.IsSet())
		{
			Add(TEXT("Local"), Profile.Local.GetValue() ? TEXT("true") : TEXT("false"));
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
