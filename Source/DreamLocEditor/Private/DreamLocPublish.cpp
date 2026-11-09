// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocPublish.h"

#include "DreamLocFile.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Internationalization/LocalizedTextSourceTypes.h"
#include "Internationalization/TextLocalizationResource.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

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
	const FString Name = InOptions.Name.IsEmpty() ? FString(FDreamLocTextSource::BakedName) : InOptions.Name;
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
	auto Wanted = [&InOptions](const FTextId& InId)
	{
		return !InOptions.NamespaceFilter || InOptions.NamespaceFilter(InId.GetNamespace().ToString());
	};

	// Everything first, then the files: a filtered bake with nothing in it writes none.
	TArray<TPair<FString, FTextLocalizationResource>> Files;
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
			if (Pair.Value.Priority >= SourceFloor || !Pair.Value.LocalizedString.IsValid() || !Wanted(Pair.Key))
			{
				continue;
			}
			Baked.AddEntry(Pair.Key.GetNamespace(), Pair.Key.GetKey(), Pair.Value.SourceStringHash, *Pair.Value.LocalizedString, 0);
			++Count;
		}
		Result.Translations.Add(Culture, Count);
		Files.Emplace(Culture, MoveTemp(Baked));
	}

	// Every text any culture's files have, to its own source: what the source language shows.
	{
		FTextLocalizationResource Loaded;
		const TArray<FString> Prioritized = { Settings.SourceCulture };
		Source.LoadLocalizedResources(ELocalizationLoadFlags::Game, Prioritized, Loaded, Loaded);
		FTextLocalizationResource Native;
		for (const TPair<FTextId, FTextLocalizationResource::FEntry>& Pair : Loaded.Entries)
		{
			if (Pair.Value.Priority >= SourceFloor && Pair.Value.LocalizedString.IsValid() && Wanted(Pair.Key))
			{
				Native.AddEntry(Pair.Key.GetNamespace(), Pair.Key.GetKey(), Pair.Value.SourceStringHash, *Pair.Value.LocalizedString, 0);
				++Result.Sources;
			}
		}
		Files.Emplace(Settings.SourceCulture, MoveTemp(Native));
	}

	const FString LocResName = Name + TEXT(".locres");
	const FString MetaPath = Output / (Name + TEXT(".locmeta"));
	if (InOptions.bSkipEmpty && Result.Sources == 0)
	{
		// Nothing for this target: what an earlier bake wrote goes.
		Result.Translations.Reset();
		if (!InOptions.bDryRun)
		{
			TArray<FString> Folders;
			IFileManager::Get().FindFiles(Folders, *(Output / TEXT("*")), /*Files*/ false, /*Directories*/ true);
			for (const FString& Folder : Folders)
			{
				IFileManager::Get().Delete(*(Output / Folder / LocResName), false, false, true);
				IFileManager::Get().DeleteDirectory(*(Output / Folder), /*RequireExists*/ false, /*Tree*/ false);
			}
			IFileManager::Get().Delete(*MetaPath, false, false, true);
			IFileManager::Get().DeleteDirectory(*Output, /*RequireExists*/ false, /*Tree*/ false);
		}
		Result.bSucceeded = true;
		return Result;
	}

	bool bAllWritten = true;
	for (const TPair<FString, FTextLocalizationResource>& File : Files)
	{
		const FString Path = Output / File.Key / LocResName;
		Result.Files.Add(Path);
		if (!InOptions.bDryRun)
		{
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), /*Tree*/ true);
			bAllWritten &= File.Value.SaveToFile(Path);
		}
		const bool bSource = &File == &Files.Last();
		AddMessage(Result.Messages, Path, EDreamLocSeverity::Info, DreamLocCode::Baked, bSource
			? FString::Printf(TEXT("%s (the source): %d text(s)"), *File.Key, Result.Sources)
			: FString::Printf(TEXT("%s: %d translation(s)"), *File.Key, Result.Translations.FindRef(File.Key)));
	}

	FTextLocalizationMetaDataResource MetaData;
	MetaData.NativeCulture = Settings.SourceCulture;
	MetaData.NativeLocRes = Settings.SourceCulture / LocResName;
	MetaData.CompiledCultures.Add(Settings.SourceCulture);
	for (const FString& Culture : Settings.TargetCultures)
	{
		MetaData.CompiledCultures.AddUnique(Culture);
	}
	Result.Files.Add(MetaPath);
	if (!InOptions.bDryRun)
	{
		bAllWritten &= MetaData.SaveToFile(MetaPath);
		// A culture baked before and not now: its file goes, or a packaged game would still offer it.
		TArray<FString> Folders;
		IFileManager::Get().FindFiles(Folders, *(Output / TEXT("*")), /*Files*/ false, /*Directories*/ true);
		for (const FString& Folder : Folders)
		{
			const FString Stale = Output / Folder / LocResName;
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

TArray<DreamLocPublish::FPluginTarget> DreamLocPublish::GetPluginTargets(const FString& InPluginName, const FDreamLocSettings& InSettings)
{
	return {
		FPluginTarget{ InPluginName, InSettings.PublishLoadingPolicy.IsEmpty() ? FString(TEXT("Editor")) : InSettings.PublishLoadingPolicy },
		FPluginTarget{ InPluginName + TEXT("PropertyNames"), TEXT("PropertyNames") },
		FPluginTarget{ InPluginName + TEXT("ToolTips"), TEXT("ToolTips") },
	};
}

bool DreamLocPublish::SetLocalizationTargets(const FString& InText, const TArray<FString>& InOwnNames, const TArray<FPluginTarget>& InTargets,
	FString& OutText, FString& OutError)
{
	using namespace DreamLocPublishLocal;
	OutText = InText;
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(InText), Root) || !Root.IsValid())
	{
		OutError = TEXT("it does not read as JSON");
		return false;
	}

	// The targets as they are, and as they are to be: the others first, as they were, then these.
	TArray<TSharedPtr<FJsonObject>> Others;
	TArray<FPluginTarget> Before;
	bool bOthersFirst = true;
	bool bSeenOwn = false;
	const TArray<TSharedPtr<FJsonValue>>* Existing = nullptr;
	if (Root->TryGetArrayField(TEXT("LocalizationTargets"), Existing))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Existing)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(Object))
			{
				continue;
			}
			FPluginTarget Target;
			(*Object)->TryGetStringField(TEXT("Name"), Target.Name);
			(*Object)->TryGetStringField(TEXT("LoadingPolicy"), Target.LoadingPolicy);
			if (Contains(InOwnNames, Target.Name))
			{
				bSeenOwn = true;
				// One of ours with more than a name and a policy is rewritten.
				if ((*Object)->Values.Num() != 2)
				{
					Target.LoadingPolicy += TEXT("\x01");
				}
				Before.Add(Target);
			}
			else
			{
				bOthersFirst &= !bSeenOwn;
				Others.Add(*Object);
			}
		}
	}
	if (bOthersFirst && Before == InTargets && (Existing != nullptr || InTargets.Num() == 0))
	{
		return true;
	}

	// The file's line ends and indentation: the first indented line's.
	const FString Newline = InText.Contains(TEXT("\r\n")) ? TEXT("\r\n") : TEXT("\n");
	FString Indent = TEXT("\t");
	{
		TArray<FString> Lines;
		InText.ParseIntoArrayLines(Lines, /*bCullEmpty*/ true);
		for (const FString& Line : Lines)
		{
			const int32 Width = Line.Len() - Line.TrimStart().Len();
			if (Width > 0 && Line.TrimStart().StartsWith(TEXT("\"")))
			{
				Indent = Line.Left(Width);
				break;
			}
		}
	}
	auto Quote = [](const FString& InValue)
	{
		return TEXT("\"") + InValue.ReplaceCharWithEscapedChar() + TEXT("\"");
	};
	TArray<FString> Items;
	for (const TSharedPtr<FJsonObject>& Other : Others)
	{
		FString Condensed;
		FJsonSerializer::Serialize(Other.ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Condensed));
		Items.Add(Indent + Indent + Condensed);
	}
	for (const FPluginTarget& Target : InTargets)
	{
		Items.Add(Indent + Indent + TEXT("{") + Newline
			+ Indent + Indent + Indent + TEXT("\"Name\": ") + Quote(Target.Name) + TEXT(",") + Newline
			+ Indent + Indent + Indent + TEXT("\"LoadingPolicy\": ") + Quote(Target.LoadingPolicy) + Newline
			+ Indent + Indent + TEXT("}"));
	}
	const FString Array = Items.Num() == 0 ? FString(TEXT("[]"))
		: TEXT("[") + Newline + FString::Join(Items, *(TEXT(",") + Newline)) + Newline + Indent + TEXT("]");
	const FString Property = TEXT("\"LocalizationTargets\": ") + Array;

	// Where the array is in the text, or where the object ends: a scan that skips strings.
	int32 KeyStart = INDEX_NONE;
	int32 ValueEnd = INDEX_NONE;
	int32 RootClose = INDEX_NONE;
	{
		int32 Depth = 0;
		int32 ArrayDepth = INDEX_NONE;
		for (int32 Index = 0; Index < InText.Len(); ++Index)
		{
			const TCHAR Char = InText[Index];
			if (Char == TEXT('"'))
			{
				const int32 Start = Index;
				for (++Index; Index < InText.Len() && InText[Index] != TEXT('"'); ++Index)
				{
					Index += InText[Index] == TEXT('\\') ? 1 : 0;
				}
				if (Depth == 1 && KeyStart == INDEX_NONE && InText.Mid(Start, Index - Start + 1) == TEXT("\"LocalizationTargets\""))
				{
					KeyStart = Start;
					ArrayDepth = 1;
				}
				continue;
			}
			if (Char == TEXT('{') || Char == TEXT('['))
			{
				++Depth;
			}
			else if (Char == TEXT('}') || Char == TEXT(']'))
			{
				--Depth;
				if (ArrayDepth != INDEX_NONE && Depth == ArrayDepth && ValueEnd == INDEX_NONE)
				{
					ValueEnd = Index + 1;
				}
				if (Depth == 0)
				{
					RootClose = Index;
					break;
				}
			}
		}
	}
	if (KeyStart != INDEX_NONE)
	{
		// Only an array is replaced: anything else there is not a .uplugin's.
		int32 Value = InText.Find(TEXT(":"), ESearchCase::CaseSensitive, ESearchDir::FromStart, KeyStart);
		if (Value != INDEX_NONE)
		{
			for (++Value; Value < InText.Len() && FChar::IsWhitespace(InText[Value]); ++Value)
			{
			}
		}
		if (Value == INDEX_NONE || Value >= InText.Len() || InText[Value] != TEXT('[') || ValueEnd == INDEX_NONE)
		{
			OutError = TEXT("its LocalizationTargets is not an array");
			return false;
		}
		OutText = InText.Left(KeyStart) + Property + InText.Mid(ValueEnd);
	}
	else if (RootClose != INDEX_NONE)
	{
		int32 Last = RootClose - 1;
		while (Last >= 0 && FChar::IsWhitespace(InText[Last]))
		{
			--Last;
		}
		const bool bEmpty = Last >= 0 && InText[Last] == TEXT('{');
		OutText = InText.Left(Last + 1) + (bEmpty ? TEXT("") : TEXT(",")) + Newline + Indent + Property + Newline + InText.Mid(RootClose);
	}
	else
	{
		OutError = TEXT("its object does not end");
		return false;
	}

	TSharedPtr<FJsonObject> Check;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(OutText), Check) || !Check.IsValid() || !Check->HasTypedField<EJson::Array>(TEXT("LocalizationTargets")))
	{
		OutError = TEXT("the targets could not be put into it");
		OutText = InText;
		return false;
	}
	return true;
}

DreamLocPublish::FPluginBakeResult DreamLocPublish::BakePlugin(const FPluginBakeOptions& InOptions)
{
	using namespace DreamLocPublishLocal;
	FPluginBakeResult Result;
	const FString Plugin = FPaths::ConvertRelativePathToFull(InOptions.PluginDirectory);
	const FString DLoc = Plugin / TEXT("DLoc");
	const FString Descriptor = Plugin / (InOptions.PluginName + TEXT(".uplugin"));
	if (!IFileManager::Get().DirectoryExists(*DLoc))
	{
		AddMessage(Result.Messages, DLoc, EDreamLocSeverity::Error, DreamLocCode::SyncBlocked,
			FString::Printf(TEXT("the plugin has no DLoc folder: dloc sync -Plugin=%s makes one"), *InOptions.PluginName));
		return Result;
	}
	const FDreamLocSettings Settings = InOptions.Settings.IsSet() ? InOptions.Settings.GetValue() : FDreamLocSettings::LoadFromDirectory(DLoc);
	const TArray<FPluginTarget> Targets = GetPluginTargets(InOptions.PluginName, Settings);

	bool bAllBaked = true;
	for (int32 Index = 0; Index < Targets.Num(); ++Index)
	{
		FBakeOptions Options;
		Options.OutputDirectory = Plugin / TEXT("Content/Localization") / Targets[Index].Name;
		Options.Name = Targets[Index].Name;
		Options.Layers = { FDreamLocLayer{ 0, TEXT("Plugin.") + InOptions.PluginName, DLoc } };
		Options.Settings = Settings;
		Options.bSkipEmpty = true;
		Options.bCheckConfig = false;
		Options.bDryRun = InOptions.bDryRun;
		Options.NamespaceFilter = [Index](const FString& InNamespace)
		{
			const bool bName = InNamespace == TEXT("UObjectDisplayNames");
			const bool bTip = InNamespace == TEXT("UObjectToolTips") || InNamespace == TEXT("UObjectShortTooltips");
			return Index == 1 ? bName : Index == 2 ? bTip : !bName && !bTip;
		};
		FBakeResult Baked = Bake(Options);
		bAllBaked &= Baked.bSucceeded;
		Result.Messages.Append(MoveTemp(Baked.Messages));
		Result.Files.Append(MoveTemp(Baked.Files));
		Result.Sources += Baked.Sources;
		for (const TPair<FString, int32>& Pair : Baked.Translations)
		{
			Result.Translations.FindOrAdd(Pair.Key) += Pair.Value;
		}
		if (Baked.Sources > 0)
		{
			Result.Targets.Add(Targets[Index]);
		}
	}
	if (!bAllBaked)
	{
		return Result;
	}
	if (Result.Targets.Num() == 0)
	{
		AddMessage(Result.Messages, DLoc, EDreamLocSeverity::Warning, DreamLocCode::Baked,
			FString::Printf(TEXT("the plugin's DLoc folder has no text to bake: dloc sync -Plugin=%s fills it"), *InOptions.PluginName));
	}

	// The .uplugin, with its byte order mark if it had one.
	TArray<uint8> Bytes;
	FString Text;
	if (!FFileHelper::LoadFileToArray(Bytes, *Descriptor) || !FFileHelper::LoadFileToString(Text, *Descriptor))
	{
		AddMessage(Result.Messages, Descriptor, EDreamLocSeverity::Error, DreamLocCode::PluginDescriptor, TEXT("the plugin's .uplugin cannot be read"));
		return Result;
	}
	const bool bByteOrderMark = Bytes.Num() >= 3 && Bytes[0] == 0xEF && Bytes[1] == 0xBB && Bytes[2] == 0xBF;
	TArray<FString> OwnNames;
	for (const FPluginTarget& Target : Targets)
	{
		OwnNames.Add(Target.Name);
	}
	FString Updated;
	FString Error;
	if (!SetLocalizationTargets(Text, OwnNames, Result.Targets, Updated, Error))
	{
		AddMessage(Result.Messages, Descriptor, EDreamLocSeverity::Error, DreamLocCode::PluginDescriptor,
			FString::Printf(TEXT("the localization targets cannot be listed in the .uplugin: %s"), *Error));
		return Result;
	}
	Result.bDescriptorChanged = Updated != Text;
	if (Result.bDescriptorChanged)
	{
		TArray<FString> Names;
		for (const FPluginTarget& Target : Result.Targets)
		{
			Names.Add(FString::Printf(TEXT("%s (%s)"), *Target.Name, *Target.LoadingPolicy));
		}
		if (!InOptions.bDryRun && !FFileHelper::SaveStringToFile(Updated, *Descriptor,
			bByteOrderMark ? FFileHelper::EEncodingOptions::ForceUTF8 : FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			AddMessage(Result.Messages, Descriptor, EDreamLocSeverity::Error, DreamLocCode::PluginDescriptor, TEXT("the .uplugin cannot be written"));
			return Result;
		}
		Result.Files.Add(Descriptor);
		AddMessage(Result.Messages, Descriptor, EDreamLocSeverity::Info, DreamLocCode::PluginDescriptor, FString::Printf(
			TEXT("LocalizationTargets: %s; the editor loads them after a restart"), Names.Num() > 0 ? *FString::Join(Names, TEXT(", ")) : TEXT("none")));
	}
	Result.bSucceeded = true;
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
