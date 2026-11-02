// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocPublish.h"

#include "DreamLocFile.h"
#include "HAL/FileManager.h"
#include "Internationalization/LocalizedTextSourceTypes.h"
#include "Internationalization/TextLocalizationResource.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace DreamLocPublishLocal
{
	void AddMessage(TArray<FDreamLocReportLine>& OutMessages, const FString& InFile, EDreamLocSeverity InSeverity, int32 InCode, const FString& InText)
	{
		OutMessages.Add(FDreamLocReportLine{ InFile, FDreamLocDiagnostic(InSeverity, InCode, 0, 0, InText) });
	}

	/** The project's packaging settings, as staging will read them. */
	TArray<FString> CulturesToStage()
	{
		TArray<FString> Cultures;
		if (GConfig != nullptr)
		{
			GConfig->GetArray(TEXT("/Script/UnrealEd.ProjectPackagingSettings"), TEXT("CulturesToStage"), Cultures, GGameIni);
		}
		return Cultures;
	}

	bool Contains(const TArray<FString>& InList, const FString& InValue)
	{
		return InList.ContainsByPredicate([&InValue](const FString& InItem) { return InItem.Equals(InValue, ESearchCase::IgnoreCase); });
	}

	FString FrozenId(const FString& InNamespace, const FString& InKey)
	{
		return InNamespace + TEXT("\x01") + InKey;
	}
}

DreamLocPublish::FBakeResult DreamLocPublish::Bake(const FBakeOptions& InOptions)
{
	using namespace DreamLocPublishLocal;
	FBakeResult Result;
	const FString Output = InOptions.OutputDirectory.IsEmpty() ? FDreamLocTextSource::GetBakedDirectory() : FPaths::ConvertRelativePathToFull(InOptions.OutputDirectory);
	const FDreamLocSettings Settings = InOptions.Settings.IsSet() ? InOptions.Settings.GetValue()
		: FDreamLocSettings::LoadFromDirectory(FDreamLocTextSource::GetProjectDirectory());

	TArray<FDreamLocLayer> Layers = InOptions.Layers;
	if (Layers.Num() == 0)
	{
		for (const FDreamLocLayer& Layer : FDreamLocTextSource::FindLayers())
		{
			if (!Layer.bBaked && Layer.Index < 2)
			{
				Layers.Add(Layer);
			}
		}
	}
	if (Layers.Num() == 0)
	{
		AddMessage(Result.Messages, FDreamLocTextSource::GetProjectDirectory(), EDreamLocSeverity::Error, DreamLocCode::SyncBlocked,
			TEXT("there is no DLoc folder to bake"));
		return Result;
	}
	if (Settings.TargetCultures.Num() == 0)
	{
		AddMessage(Result.Messages, Layers.Last().Directory / FDreamLocSettings::FileName, EDreamLocSeverity::Error, DreamLocCode::SyncBlocked,
			TEXT("there is no culture to bake: list them in TargetCultures"));
		return Result;
	}

	FDreamLocTextSource Source(Layers, Settings);
	const int32 SourceFloor = FDreamLocTextSource::MakeSourceEntryPriority(63);
	auto Write = [&Result, &InOptions](const FTextLocalizationResource& InResource, const FString& InPath)
	{
		Result.Files.Add(InPath);
		if (InOptions.bDryRun)
		{
			return true;
		}
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(InPath), /*Tree*/ true);
		return InResource.SaveToFile(InPath);
	};

	bool bAllWritten = true;
	for (const FString& Culture : Settings.TargetCultures)
	{
		// One culture and the dloc.ini fallbacks after it: the engine's own parents (en-GB to en) come at run time.
		FTextLocalizationResource Loaded;
		const TArray<FString> Prioritized = { Culture };
		Source.LoadLocalizedResources(ELocalizationLoadFlags::Game, Prioritized, Loaded, Loaded);
		FTextLocalizationResource Baked;
		int32 Count = 0;
		for (const TPair<FTextId, FTextLocalizationResource::FEntry>& Pair : Loaded.Entries)
		{
			if (Pair.Value.Priority >= SourceFloor || !Pair.Value.LocalizedString.IsValid())
			{
				continue;
			}
			Baked.AddEntry(Pair.Key.GetNamespace(), Pair.Key.GetKey(), Pair.Value.SourceStringHash, *Pair.Value.LocalizedString, 0);
			++Count;
		}
		const FString Path = Output / Culture / (FString(FDreamLocTextSource::BakedName) + TEXT(".locres"));
		bAllWritten &= Write(Baked, Path);
		Result.Translations.Add(Culture, Count);
		AddMessage(Result.Messages, Path, EDreamLocSeverity::Info, DreamLocCode::Baked, FString::Printf(TEXT("%s: %d translation(s)"), *Culture, Count));
	}

	// Every text any culture's files have, to its own source: what the source language shows.
	{
		FTextLocalizationResource Loaded;
		const TArray<FString> Prioritized = { Settings.SourceCulture };
		Source.LoadLocalizedResources(ELocalizationLoadFlags::Game, Prioritized, Loaded, Loaded);
		FTextLocalizationResource Native;
		for (const TPair<FTextId, FTextLocalizationResource::FEntry>& Pair : Loaded.Entries)
		{
			if (Pair.Value.Priority >= SourceFloor && Pair.Value.LocalizedString.IsValid())
			{
				Native.AddEntry(Pair.Key.GetNamespace(), Pair.Key.GetKey(), Pair.Value.SourceStringHash, *Pair.Value.LocalizedString, 0);
				++Result.Sources;
			}
		}
		const FString Path = Output / Settings.SourceCulture / (FString(FDreamLocTextSource::BakedName) + TEXT(".locres"));
		bAllWritten &= Write(Native, Path);
		AddMessage(Result.Messages, Path, EDreamLocSeverity::Info, DreamLocCode::Baked, FString::Printf(TEXT("%s (the source): %d text(s)"),
			*Settings.SourceCulture, Result.Sources));
	}

	FTextLocalizationMetaDataResource MetaData;
	MetaData.NativeCulture = Settings.SourceCulture;
	MetaData.NativeLocRes = Settings.SourceCulture / (FString(FDreamLocTextSource::BakedName) + TEXT(".locres"));
	MetaData.CompiledCultures.Add(Settings.SourceCulture);
	for (const FString& Culture : Settings.TargetCultures)
	{
		MetaData.CompiledCultures.AddUnique(Culture);
	}
	const FString MetaPath = Output / (FString(FDreamLocTextSource::BakedName) + TEXT(".locmeta"));
	Result.Files.Add(MetaPath);
	if (!InOptions.bDryRun)
	{
		bAllWritten &= MetaData.SaveToFile(MetaPath);
		// A culture baked before and not now: its file goes, or a packaged game would still offer it.
		TArray<FString> Folders;
		IFileManager::Get().FindFiles(Folders, *(Output / TEXT("*")), /*Files*/ false, /*Directories*/ true);
		for (const FString& Folder : Folders)
		{
			const FString Stale = Output / Folder / (FString(FDreamLocTextSource::BakedName) + TEXT(".locres"));
			if (!Contains(MetaData.CompiledCultures, Folder) && IFileManager::Get().FileExists(*Stale))
			{
				IFileManager::Get().Delete(*Stale);
				IFileManager::Get().DeleteDirectory(*(Output / Folder), /*RequireExists*/ false, /*Tree*/ false);
			}
		}
	}
	if (!bAllWritten)
	{
		AddMessage(Result.Messages, Output, EDreamLocSeverity::Error, DreamLocCode::SyncBlocked, TEXT("a baked file could not be written"));
	}

	if (InOptions.bCheckConfig)
	{
		const bool bLoadedByEngine = FDreamLocTextSource::IsBakedDirectoryLoadedByEngine();
		const FString GameIni = FPaths::ProjectConfigDir() / TEXT("DefaultGame.ini");
		if (Settings.bPublishNative && !bLoadedByEngine)
		{
			AddMessage(Result.Messages, GameIni, EDreamLocSeverity::Warning, DreamLocCode::PublishConfig,
				TEXT("[Publish] Mode=Native, but the engine does not read the baked files: add +LocalizationPaths=%GAMEDIR%Content/Localization/DreamLoc to [Internationalization]"));
		}
		else if (!Settings.bPublishNative && bLoadedByEngine)
		{
			AddMessage(Result.Messages, GameIni, EDreamLocSeverity::Warning, DreamLocCode::PublishConfig,
				TEXT("[Publish] Mode=Source, but Content/Localization/DreamLoc is in [Internationalization] LocalizationPaths: the engine loads the baked files itself and DreamLoc leaves them, so mods have nothing to go over; take it out, or set Mode=Native"));
		}
		const TArray<FString> Staged = CulturesToStage();
		for (const FString& Culture : MetaData.CompiledCultures)
		{
			if (Staged.Num() > 0 && !Contains(Staged, Culture))
			{
				AddMessage(Result.Messages, GameIni, EDreamLocSeverity::Warning, DreamLocCode::CultureNotStaged, FString::Printf(
					TEXT("%s is baked but not in CulturesToStage: a packaged game does not get its file (Project Settings > Packaging > Localizations to Package)"), *Culture));
			}
		}
		if (Staged.Num() == 0)
		{
			AddMessage(Result.Messages, GameIni, EDreamLocSeverity::Warning, DreamLocCode::CultureNotStaged,
				TEXT("the project lists no CulturesToStage: packaging needs them, or -CookCultures, for the baked files to go into the package"));
		}
	}

	Result.bSucceeded = bAllWritten;
	return Result;
}

FDreamLocSyncOutput DreamLocPublish::Prune(const TMap<FString, TMap<FString, FString>>& InFiles, const FString& InDLocName)
{
	using namespace DreamLocPublishLocal;
	FDreamLocSyncOutput Output;
	for (const TPair<FString, TMap<FString, FString>>& Culture : InFiles)
	{
		for (const TPair<FString, FString>& File : Culture.Value)
		{
			FDreamLocDocument Document;
			TArray<FDreamLocDiagnostic> Diagnostics;
			const FString Name = InDLocName / Culture.Key / File.Key;
			if (!DreamLocFile::Parse(File.Value, Document, Diagnostics))
			{
				Output.bBlocked = true;
				AddMessage(Output.Messages, Name, EDreamLocSeverity::Error, DreamLocCode::SyncBlocked, TEXT("the file does not read; nothing was pruned"));
				continue;
			}
			int32 Removed = 0;
			Document.Items.RemoveAll([&Removed](const FDreamLocItem& InItem)
			{
				if (InItem.Kind != EDreamLocItemKind::Removed)
				{
					return false;
				}
				for (const FDreamLocItem& Child : InItem.Children)
				{
					Removed += Child.Kind == EDreamLocItemKind::Entry ? 1 : 0;
				}
				return true;
			});
			if (Removed == 0)
			{
				continue;
			}
			Output.Removed += Removed;
			const bool bEmpty = !Document.bTerms && !Document.Items.ContainsByPredicate([](const FDreamLocItem& InItem) { return InItem.Kind == EDreamLocItemKind::Entry; });
			Output.Changes.FindOrAdd(Culture.Key).Add(File.Key, bEmpty ? TOptional<FString>() : TOptional<FString>(DreamLocFile::Write(Document)));
			AddMessage(Output.Messages, Name, EDreamLocSeverity::Info, DreamLocCode::Pruned, FString::Printf(TEXT("%d removed entr%s deleted%s"),
				Removed, Removed == 1 ? TEXT("y") : TEXT("ies"), bEmpty ? TEXT(", and the file with them") : TEXT("")));
		}
	}
	if (Output.bBlocked)
	{
		Output.Changes.Reset();
	}
	return Output;
}

TSet<FString> DreamLocPublish::ReadFrozen(const FString& InDLocDirectory)
{
	TSet<FString> Frozen;
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *(InDLocDirectory / FrozenFileName)))
	{
		return Frozen;
	}
	for (const FString& Line : Lines)
	{
		FString Namespace;
		FString Key;
		if (!Line.StartsWith(TEXT("#")) && Line.Split(TEXT("\t"), &Namespace, &Key))
		{
			Frozen.Add(DreamLocPublishLocal::FrozenId(Namespace, Key));
		}
	}
	return Frozen;
}

bool DreamLocPublish::Freeze(const FString& InDLocDirectory, const TMap<FString, TMap<FString, FString>>& InFiles, int32& OutKeys)
{
	TSet<FString> Ids;
	TArray<FString> Lines;
	for (const TPair<FString, TMap<FString, FString>>& Culture : InFiles)
	{
		for (const TPair<FString, FString>& File : Culture.Value)
		{
			FDreamLocDocument Document;
			TArray<FDreamLocDiagnostic> Diagnostics;
			if (FPaths::GetCleanFilename(File.Key).StartsWith(TEXT("_")) || !DreamLocFile::Parse(File.Value, Document, Diagnostics) || Document.bTerms)
			{
				continue;
			}
			Document.ForEachEntry([&Ids, &Lines](const FString& InNamespace, const FDreamLocEntry& InEntry, bool)
			{
				bool bAlready = false;
				Ids.Add(DreamLocPublishLocal::FrozenId(InNamespace, InEntry.Key), &bAlready);
				if (!bAlready)
				{
					Lines.Add(InNamespace + TEXT("\t") + InEntry.Key);
				}
			});
		}
	}
	Lines.Sort();
	OutKeys = Lines.Num();
	const FString Text = TEXT("# dloc freeze: the keys shipped. A sync that finds one gone from the sources fails (DLOC3022).\n# namespace<TAB>key, one per line.\n")
		+ FString::Join(Lines, TEXT("\n")) + TEXT("\n");
	return FFileHelper::SaveStringToFile(Text, *(InDLocDirectory / FrozenFileName), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
