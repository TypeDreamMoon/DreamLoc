// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocEngine.h"

#include "AssetRegistry/IAssetRegistry.h"
#include "Commandlets/GatherTextCommandlet.h"
#include "DreamLocFile.h"
#include "DreamLocSettings.h"
#include "DreamLocTextSource.h"
#include "HAL/FileManager.h"
#include "Internationalization/TextLocalizationResource.h"
#include "LocTextHelper.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "SourceCodeNavigation.h"
#include "UObject/Package.h"

namespace DreamLocEngineLocal
{
	FString FullDirectory(const FString& InDirectory)
	{
		FString Full = FPaths::ConvertRelativePathToFull(InDirectory);
		FPaths::NormalizeDirectoryName(Full);
		return Full;
	}

	FString ProjectOrDefault(const FString& InDirectory)
	{
		return FullDirectory(InDirectory.IsEmpty() ? FPaths::ProjectDir() : InDirectory);
	}

	/** InPath relative to InDirectory, or empty when it is not inside it. */
	FString RelativeTo(const FString& InPath, const FString& InDirectory)
	{
		FString Full = FPaths::ConvertRelativePathToFull(FPaths::IsRelative(InPath) ? InDirectory / InPath : InPath);
		FPaths::NormalizeFilename(Full);
		FPaths::CollapseRelativeDirectories(Full);
		const FString Prefix = InDirectory.EndsWith(TEXT("/")) ? InDirectory : InDirectory + TEXT("/");
		return Full.StartsWith(Prefix, ESearchCase::IgnoreCase) ? Full.RightChop(Prefix.Len()) : FString();
	}

	void AddMessage(TArray<FDreamLocReportLine>& OutMessages, const FString& InFile, EDreamLocSeverity InSeverity, int32 InCode, const FString& InText)
	{
		OutMessages.Add(FDreamLocReportLine{ InFile, FDreamLocDiagnostic(InSeverity, InCode, 0, 0, InText) });
	}

	struct FGathered
	{
		FString Namespace;
		FDreamLocExtractEntry Entry;
		int32 Line = 0;
	};

	/** `Source/Game/Private/HUD.cpp(42)` -> the file and 42; anything else -> itself and 0. */
	void SplitLine(const FString& InLocation, FString& OutFile, int32& OutLine)
	{
		OutFile = InLocation;
		OutLine = 0;
		int32 Open = INDEX_NONE;
		if (InLocation.EndsWith(TEXT(")")) && InLocation.FindLastChar(TEXT('('), Open))
		{
			const FString Number = InLocation.Mid(Open + 1, InLocation.Len() - Open - 2);
			if (!Number.IsEmpty() && Number.IsNumeric())
			{
				OutFile = InLocation.Left(Open);
				OutLine = FCString::Atoi(*Number);
			}
		}
	}

	/** An asset's text location -- `/Game/UI/WBP_Main.WBP_Main:WidgetTree.Title.Text` -- as the package's file and the rest. */
	bool SplitAsset(const FString& InLocation, const FString& InProjectDirectory, FString& OutFile, FString& OutWhere)
	{
		if (!InLocation.StartsWith(TEXT("/")))
		{
			return false;
		}
		FString Package = InLocation;
		int32 Dot = INDEX_NONE;
		if (InLocation.FindChar(TEXT('.'), Dot))
		{
			Package = InLocation.Left(Dot);
			OutWhere = InLocation.RightChop(Dot + 1);
		}
		FString Filename;
		if (!FPackageName::IsValidLongPackageName(Package) || !FPackageName::DoesPackageExist(Package, &Filename))
		{
			return false;
		}
		// The engine's file names are relative to its binaries' folder, not the project's.
		OutFile = RelativeTo(FPaths::ConvertRelativePathToFull(Filename), InProjectDirectory);
		return !OutFile.IsEmpty();
	}

	/**
	 * A meta-data text's location -- `/Script/DreamGUI.DreamCanvas:SortOrder [IntProperty ToolTip meta-data]`, an enum
	 * value's `/Script/DreamGUI.EDreamFit:Fill [EnumEntry DisplayName meta-data]` -- as the header the type is declared
	 * in, the field (`DreamCanvas:SortOrder`) and what the text is (`tooltip`).
	 */
	bool SplitScript(const FString& InLocation, const FString& InProjectDirectory, FString& OutFile, FString& OutWhere, FString& OutKind)
	{
		if (!InLocation.StartsWith(TEXT("/Script/")))
		{
			return false;
		}
		FString Path = InLocation;
		int32 Bracket = INDEX_NONE;
		if (Path.FindChar(TEXT('['), Bracket))
		{
			FString What = Path.Mid(Bracket + 1);
			What.RemoveFromEnd(TEXT("]"));
			What.RemoveFromEnd(TEXT(" meta-data"));
			// What it is, not what type the field is: the translation flow sends a source once per kind, and "Visibility"
			// is one display name for a bool property, an enum property and an enum value alike.
			FString Field;
			FString Key;
			if (!What.Split(TEXT(" "), &Field, &Key))
			{
				Key = What;
			}
			OutKind = Key == TEXT("DisplayName") ? TEXT("display name") : Key == TEXT("ToolTip") ? TEXT("tooltip")
				: Key == TEXT("ShortTooltip") ? TEXT("short tooltip") : Key == TEXT("Category") ? TEXT("category") : Key;
			Path.LeftInline(Bracket);
			Path.TrimEndInline();
		}
		// The type is the object the package holds: everything up to the first ':' (a property's, a function's, an
		// enum value's own name comes after it).
		int32 Colon = INDEX_NONE;
		const FString TypePath = Path.FindChar(TEXT(':'), Colon) ? Path.Left(Colon) : Path;
		const UField* Type = FindObject<UField>(nullptr, *TypePath);
		FString Header;
		if (Type == nullptr || !FSourceCodeNavigation::FindClassHeaderPath(Type, Header))
		{
			return false;
		}
		OutFile = RelativeTo(FPaths::ConvertRelativePathToFull(Header), InProjectDirectory);
		int32 Dot = INDEX_NONE;
		OutWhere = Path.FindChar(TEXT('.'), Dot) ? Path.RightChop(Dot + 1) : Path;
		return !OutFile.IsEmpty();
	}

	/**
	 * The engine's own meta-data steps for its Editor, PropertyNames, ToolTips and Category targets
	 * (Engine/Config/Localization/PropertyNames.ini, ToolTips.ini, Category.ini), with the folders searched and the
	 * excludes of dloc.ini for the engine's folder list, and enum values' display names gathered too (the engine's
	 * PropertyNames.ini leaves enums out).
	 */
	void AppendMetaDataSteps(FString& InOutConfig, int32& InOutStep, const TArray<FString>& InDirectories, const TArray<FString>& InExcludes)
	{
		auto Begin = [&]()
		{
			InOutConfig += FString::Printf(TEXT("\n[GatherTextStep%d]\nCommandletClass=GatherTextFromMetaData\n"), InOutStep++);
			for (const FString& Directory : InDirectories)
			{
				InOutConfig += FString::Printf(TEXT("IncludePathFilters=%s/*\n"), *Directory);
			}
			for (const FString& Exclude : InExcludes)
			{
				InOutConfig += FString::Printf(TEXT("ExcludePathFilters=%s\n"), *Exclude);
			}
			InOutConfig += TEXT("ExcludePathFilters=*/NoRedist/*\nExcludePathFilters=*/NotForLicensees/*\nExcludePathFilters=*/LimitedAccess/*\n");
		};
		// PropertyNames.ini, less FieldTypesToExclude=Enum.
		Begin();
		InOutConfig += TEXT("FieldTypesToExclude=Function\nFieldOwnerTypesToExclude=K2Node\nFieldOuterTypesToExclude=Function\n");
		InOutConfig += TEXT("InputKeys=DisplayName\nOutputNamespaces=UObjectDisplayNames\nOutputKeys=\"{FieldPath}\"\nShouldGatherFromEditorOnlyData=true\n");
		// ToolTips.ini.
		Begin();
		InOutConfig += TEXT("InputKeys=ToolTip\nOutputNamespaces=UObjectToolTips\nOutputKeys=\"{FieldPath}\"\n");
		InOutConfig += TEXT("InputKeys=ShortTooltip\nOutputNamespaces=UObjectShortTooltips\nOutputKeys=\"{FieldPath}\"\nShouldGatherFromEditorOnlyData=true\n");
		// Category.ini.
		Begin();
		InOutConfig += TEXT("InputKeys=Category\nOutputNamespaces=UObjectCategory\nOutputKeys=\"{MetaDataValue}\"\nShouldGatherFromEditorOnlyData=true\n");
	}

	FString WriteGatherConfig(const DreamLocEngine::FGatherOptions& InOptions, const FString& InProject, const FString& InWork, const FString& InManifestName)
	{
		auto Absolute = [&InProject](const FString& InPath)
		{
			FString Path = FPaths::IsRelative(InPath) ? InProject / InPath : InPath;
			FPaths::NormalizeFilename(Path);
			return Path;
		};
		FString Config;
		Config += TEXT("[CommonSettings]\n");
		Config += FString::Printf(TEXT("SourcePath=%s\n"), *InWork);
		Config += FString::Printf(TEXT("DestinationPath=%s\n"), *InWork);
		Config += FString::Printf(TEXT("ManifestName=%s\n"), *InManifestName);
		Config += TEXT("ArchiveName=DreamLoc.archive\n");
		Config += FString::Printf(TEXT("NativeCulture=%s\n"), *InOptions.NativeCulture);
		Config += FString::Printf(TEXT("CulturesToGenerate=%s\n"), *InOptions.NativeCulture);

		int32 Step = 0;
		TArray<FString> SourceDirectories;
		for (const FString& Path : InOptions.SourcePaths)
		{
			if (IFileManager::Get().DirectoryExists(*Absolute(Path)))
			{
				SourceDirectories.Add(Absolute(Path));
			}
		}
		if (SourceDirectories.Num() > 0)
		{
			Config += FString::Printf(TEXT("\n[GatherTextStep%d]\nCommandletClass=GatherTextFromSource\n"), Step++);
			for (const FString& Directory : SourceDirectories)
			{
				Config += FString::Printf(TEXT("SearchDirectoryPaths=%s\n"), *Directory);
			}
			for (const FString& Exclude : InOptions.ExcludePaths)
			{
				Config += FString::Printf(TEXT("ExcludePathFilters=%s\n"), *Absolute(Exclude));
			}
			Config += TEXT("FileNameFilters=*.h\nFileNameFilters=*.cpp\nFileNameFilters=*.inl\n");
			Config += FString::Printf(TEXT("ShouldGatherFromEditorOnlyData=%s\n"), InOptions.bEditorOnlyData ? TEXT("true") : TEXT("false"));
			if (InOptions.bMetaData)
			{
				TArray<FString> Excludes;
				for (const FString& Exclude : InOptions.ExcludePaths)
				{
					Excludes.Add(Absolute(Exclude));
				}
				AppendMetaDataSteps(Config, Step, SourceDirectories, Excludes);
			}
		}
		TArray<FString> ContentDirectories;
		for (const FString& Path : InOptions.ContentPaths)
		{
			if (IFileManager::Get().DirectoryExists(*Absolute(Path)))
			{
				ContentDirectories.Add(Absolute(Path));
			}
		}
		if (ContentDirectories.Num() > 0)
		{
			Config += FString::Printf(TEXT("\n[GatherTextStep%d]\nCommandletClass=GatherTextFromAssets\n"), Step++);
			for (const FString& Directory : ContentDirectories)
			{
				Config += FString::Printf(TEXT("IncludePathFilters=%s/*\n"), *Directory);
			}
			for (const FString& Exclude : InOptions.ExcludePaths)
			{
				Config += FString::Printf(TEXT("ExcludePathFilters=%s\n"), *Absolute(Exclude));
			}
			Config += TEXT("PackageFileNameFilters=*.umap\nPackageFileNameFilters=*.uasset\nbFixBroken=false\n");
			Config += FString::Printf(TEXT("ShouldGatherFromEditorOnlyData=%s\n"), InOptions.bEditorOnlyData ? TEXT("true") : TEXT("false"));
		}
		Config += FString::Printf(TEXT("\n[GatherTextStep%d]\nCommandletClass=GenerateGatherManifest\n"), Step++);
		return Config;
	}

	struct FCultureFiles
	{
		TArray<FString> Paths;
		TArray<FString> Texts;
		TArray<FDreamLocDocument> Documents;
		TArray<bool> Changed;
		TMap<FString, int32> ByPath;
	};

	FString MakeIdKey(const FString& InNamespace, const FString& InKey)
	{
		// A separator neither a namespace nor a key has.
		return InNamespace + TEXT("\x1F") + InKey;
	}
}

DreamLocEngine::FGatherOptions DreamLocEngine::MakeGatherOptions(const FDreamLocSettings& InSettings)
{
	FGatherOptions Options;
	Options.SourcePaths = InSettings.GatherSourcePaths;
	Options.ContentPaths = InSettings.GatherContentPaths;
	Options.ExcludePaths = InSettings.GatherExcludePaths;
	Options.bEditorOnlyData = InSettings.bGatherEditorOnlyData;
	Options.bMetaData = InSettings.bGatherMetaData;
	Options.NativeCulture = InSettings.SourceCulture;
	return Options;
}

bool DreamLocEngine::ReadGatherManifest(const FString& InManifestPath, const FString& InProjectDirectory, const FString& InNativeCulture,
	TArray<FDreamLocExtractManifest>& OutManifests, FString& OutError)
{
	using namespace DreamLocEngineLocal;
	OutManifests.Reset();
	const FString Project = FullDirectory(InProjectDirectory);
	FLocTextHelper Helper(FPaths::GetPath(InManifestPath), FPaths::GetCleanFilename(InManifestPath), TEXT("DreamLoc.archive"),
		InNativeCulture, TArray<FString>(), nullptr);
	FText Error;
	if (!Helper.LoadManifest(ELocTextHelperLoadFlags::Load, &Error))
	{
		OutError = Error.ToString();
		return false;
	}

	// The engine writes a C++ file's path relative to its own base, the project's folder, whoever owns the file: a
	// plugin's are `Plugins/<name>/Source/...`, and become relative to the plugin below.
	const FString SourceBase = FullDirectory(UGatherTextCommandletBase::GetProjectBasePath());
	TMap<FString, TArray<FGathered>> ByFile;
	TArray<FString> FileOrder;
	// A text once per file: a category is one text however many fields of a header are in it. (The same key with
	// another source stays, for sync to warn of.)
	TSet<FString> Seen;
	Helper.EnumerateSourceTexts([&](TSharedRef<FManifestEntry> InEntry) -> bool
	{
		for (const FManifestContext& Context : InEntry->Contexts)
		{
			FString File;
			FString Where;
			FString Kind;
			int32 Line = 0;
			if (!SplitAsset(Context.SourceLocation, Project, File, Where) && !SplitScript(Context.SourceLocation, Project, File, Where, Kind))
			{
				SplitLine(Context.SourceLocation, File, Line);
				File = RelativeTo(FPaths::IsRelative(File) ? SourceBase / File : File, Project);
				Where.Reset();
			}
			if (File.IsEmpty())
			{
				continue;
			}
			FGathered Gathered;
			Gathered.Namespace = InEntry->Namespace.GetString();
			Gathered.Entry.Key = Context.Key.GetString();
			bool bSeen = false;
			Seen.Add(File + TEXT("\x1F") + MakeIdKey(Gathered.Namespace, Gathered.Entry.Key) + TEXT("\x1F") + InEntry->Source.Text, &bSeen);
			if (bSeen)
			{
				continue;
			}
			Gathered.Entry.Source = InEntry->Source.Text;
			Gathered.Entry.At = Where;
			Gathered.Entry.Note = Context.DevNotes;
			Gathered.Entry.Kind = Kind;
			Gathered.Line = Line;
			if (!ByFile.Contains(File))
			{
				FileOrder.Add(File);
			}
			ByFile.FindOrAdd(File).Add(MoveTemp(Gathered));
		}
		return true;
	}, /*bCheckDependencies*/ false);

	FileOrder.Sort();
	for (const FString& File : FileOrder)
	{
		TArray<FGathered>& Texts = ByFile.FindChecked(File);
		Texts.StableSort([](const FGathered& InA, const FGathered& InB)
		{
			if (InA.Line != InB.Line)
			{
				return InA.Line < InB.Line;
			}
			if (InA.Entry.At != InB.Entry.At)
			{
				return InA.Entry.At < InB.Entry.At;
			}
			return InA.Namespace != InB.Namespace ? InA.Namespace < InB.Namespace : InA.Entry.Key < InB.Entry.Key;
		});
		FDreamLocExtractManifest& Manifest = OutManifests.AddDefaulted_GetRef();
		Manifest.Provider = DreamLocExtract::EngineProvider;
		Manifest.Source = File;
		for (FGathered& Text : Texts)
		{
			Manifest.FindOrAddNamespace(Text.Namespace).Entries.Add(MoveTemp(Text.Entry));
		}
	}
	return true;
}

DreamLocEngine::FGatherResult DreamLocEngine::Gather(const FGatherOptions& InOptions)
{
	using namespace DreamLocEngineLocal;
	FGatherResult Result;
	const FString Project = ProjectOrDefault(InOptions.ProjectDirectory);
	const FString Work = InOptions.WorkDirectory.IsEmpty() ? Project / TEXT("Intermediate/DLoc/gather") : FullDirectory(InOptions.WorkDirectory);
	const FString Extract = InOptions.ExtractDirectory.IsEmpty() ? DreamLocExtract::GetExtractDirectory(Project) : FullDirectory(InOptions.ExtractDirectory);
	IFileManager::Get().MakeDirectory(*Work, /*Tree*/ true);

	// A name of its own per run: the engine's config cache keeps a file it read once, and a second gather in the same
	// editor would read the first one's settings.
	const FString RunName = FString::Printf(TEXT("DreamLoc-%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString ConfigPath = Work / (RunName + TEXT(".ini"));
	const FString ManifestName = RunName + TEXT(".manifest");
	const FString ManifestPath = Work / ManifestName;
	if (!FFileHelper::SaveStringToFile(WriteGatherConfig(InOptions, Project, Work, ManifestName), *ConfigPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		AddMessage(Result.Messages, ConfigPath, EDreamLocSeverity::Error, DreamLocCode::GatherFailed, TEXT("the gather configuration cannot be written"));
		return Result;
	}

	// The gather takes assets from the asset registry's view of the disk, and in a running editor that view learns of a
	// package saved a moment ago only when the directory watcher gets to it. Scanning the folders first puts every
	// saved asset in.
	{
		TArray<FString> PackagePaths;
		for (const FString& Path : InOptions.ContentPaths)
		{
			FString PackagePath;
			const FString Directory = FPaths::IsRelative(Path) ? Project / Path : Path;
			if (IFileManager::Get().DirectoryExists(*Directory) && FPackageName::TryConvertFilenameToLongPackageName(Directory / TEXT(""), PackagePath))
			{
				PackagePath.RemoveFromEnd(TEXT("/"));
				PackagePaths.Add(PackagePath);
			}
		}
		if (PackagePaths.Num() > 0)
		{
			IAssetRegistry::GetChecked().ScanPathsSynchronous(PackagePaths, /*bForceRescan*/ true);
		}
	}

	UGatherTextCommandlet* Commandlet = NewObject<UGatherTextCommandlet>(GetTransientPackage());
	Commandlet->AddToRoot();
	const int32 ExitCode = Commandlet->Execute(FString::Printf(TEXT("-config=\"%s\""), *ConfigPath), nullptr);
	Commandlet->RemoveFromRoot();
	if (ExitCode != 0 || !IFileManager::Get().FileExists(*ManifestPath))
	{
		AddMessage(Result.Messages, ConfigPath, EDreamLocSeverity::Error, DreamLocCode::GatherFailed, FString::Printf(
			TEXT("the engine's gather did not finish (exit %d); its log says why (LogGatherTextCommandlet)"), ExitCode));
		return Result;
	}

	TArray<FDreamLocExtractManifest> Manifests;
	FString Error;
	if (!ReadGatherManifest(ManifestPath, Project, InOptions.NativeCulture, Manifests, Error))
	{
		AddMessage(Result.Messages, ManifestPath, EDreamLocSeverity::Error, DreamLocCode::GatherFailed, Error);
		return Result;
	}
	IFileManager::Get().Delete(*ConfigPath, false, false, true);
	IFileManager::Get().Delete(*ManifestPath, false, false, true);

	// The excludes once more, on the files the texts came from: the engine ranks its path filters by depth less
	// wildcards, so `Source/*Editor/*` ties with the `Source/*` include its search folder makes, and the include can win.
	Manifests.RemoveAll([&InOptions](const FDreamLocExtractManifest& InManifest)
	{
		return InOptions.ExcludePaths.ContainsByPredicate([&InManifest](const FString& InPattern)
		{
			FString Pattern = InPattern;
			FPaths::NormalizeFilename(Pattern);
			return InManifest.Source.MatchesWildcard(Pattern);
		});
	});

	const FString EngineDirectory = Extract / DreamLocExtract::EngineProvider;
	TSet<FString> Written;
	for (const FDreamLocExtractManifest& Manifest : Manifests)
	{
		const FString Path = EngineDirectory / (Manifest.Source + TEXT(".json"));
		FString Before;
		const bool bExisted = FFileHelper::LoadFileToString(Before, *Path);
		if (!DreamLocExtract::SaveFileIfChanged(Path, Manifest))
		{
			AddMessage(Result.Messages, Path, EDreamLocSeverity::Error, DreamLocCode::GatherFailed, TEXT("the manifest cannot be written"));
			continue;
		}
		Result.ManifestsWritten += !bExisted || Before != DreamLocExtract::Write(Manifest) ? 1 : 0;
		Result.Texts += Manifest.NumEntries();
		Written.Add(FPaths::ConvertRelativePathToFull(Path));
	}
	Result.Sources = Manifests.Num();
	// Manifests of files that have no texts any more go: a missing manifest is how sync learns the texts are gone.
	for (const FString& Path : DreamLocExtract::FindManifestFiles(EngineDirectory))
	{
		if (!Written.Contains(FPaths::ConvertRelativePathToFull(Path)) && IFileManager::Get().Delete(*Path, false, false, true))
		{
			++Result.ManifestsDeleted;
		}
	}
	const FString Stamp = FString::Printf(TEXT("{\n  \"version\": 1,\n  \"provider\": \"Engine\",\n  \"gathered\": \"%s\",\n  \"sources\": %d,\n  \"texts\": %d\n}\n"),
		*FDateTime::UtcNow().ToIso8601(), Result.Sources, Result.Texts);
	FFileHelper::SaveStringToFile(Stamp, *(EngineDirectory / DreamLocExtract::GatherStampName), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	Result.bSucceeded = Result.Messages.Num() == 0;
	return Result;
}

bool DreamLocEngine::ReadTarget(const FString& InProjectDirectory, const FString& InTarget, const FString& InNativeCulture,
	TMap<FString, TArray<FImportedText>>& OutTexts, TArray<FDreamLocReportLine>& OutMessages)
{
	using namespace DreamLocEngineLocal;
	const FString Project = ProjectOrDefault(InProjectDirectory);
	const FString TargetDirectory = Project / TEXT("Content/Localization") / InTarget;
	if (!IFileManager::Get().DirectoryExists(*TargetDirectory))
	{
		AddMessage(OutMessages, TargetDirectory, EDreamLocSeverity::Error, DreamLocCode::MigrateNotFound,
			FString::Printf(TEXT("there is no localization target '%s' here"), *InTarget));
		return false;
	}

	FString Native = InNativeCulture;
	if (Native.IsEmpty())
	{
		FTextLocalizationMetaDataResource MetaData;
		const FString MetaPath = TargetDirectory / (InTarget + TEXT(".locmeta"));
		if (IFileManager::Get().FileExists(*MetaPath) && MetaData.LoadFromFile(MetaPath))
		{
			Native = MetaData.NativeCulture;
		}
	}
	if (Native.IsEmpty())
	{
		FString Config;
		if (FFileHelper::LoadFileToString(Config, *(Project / TEXT("Config/Localization") / (InTarget + TEXT("_Gather.ini")))))
		{
			TArray<FString> Lines;
			Config.ParseIntoArrayLines(Lines);
			for (const FString& Line : Lines)
			{
				FString Key;
				FString Value;
				if (Line.Split(TEXT("="), &Key, &Value) && Key.TrimStartAndEnd() == TEXT("NativeCulture"))
				{
					Native = Value.TrimStartAndEnd();
					break;
				}
			}
		}
	}
	if (Native.IsEmpty())
	{
		AddMessage(OutMessages, TargetDirectory, EDreamLocSeverity::Error, DreamLocCode::MigrateNotFound,
			TEXT("the target's native culture is not written anywhere (no .locmeta, no _Gather.ini); give it with -NativeCulture"));
		return false;
	}

	const FString ArchiveName = InTarget + TEXT(".archive");
	TArray<FString> Cultures;
	TArray<FString> Folders;
	IFileManager::Get().FindFiles(Folders, *(TargetDirectory / TEXT("*")), /*Files*/ false, /*Directories*/ true);
	Folders.Sort();
	for (const FString& Folder : Folders)
	{
		if (!Folder.Equals(Native, ESearchCase::IgnoreCase) && IFileManager::Get().FileExists(*(TargetDirectory / Folder / ArchiveName)))
		{
			Cultures.Add(Folder);
		}
	}

	FLocTextHelper Helper(TargetDirectory, InTarget + TEXT(".manifest"), ArchiveName, Native, Cultures, nullptr);
	FText Error;
	if (!Helper.LoadManifest(ELocTextHelperLoadFlags::Load, &Error))
	{
		AddMessage(OutMessages, TargetDirectory / (InTarget + TEXT(".manifest")), EDreamLocSeverity::Error, DreamLocCode::MigrateNotFound, Error.ToString());
		return false;
	}
	for (const FString& Culture : Cultures)
	{
		if (!Helper.LoadArchive(Culture, ELocTextHelperLoadFlags::Load, &Error))
		{
			AddMessage(OutMessages, TargetDirectory / Culture / ArchiveName, EDreamLocSeverity::Error, DreamLocCode::MigrateNotFound, Error.ToString());
			continue;
		}
		TArray<FImportedText>& Texts = OutTexts.FindOrAdd(Culture);
		Helper.EnumerateTranslations(Culture, [&Texts](TSharedRef<FArchiveEntry> InEntry) -> bool
		{
			if (!InEntry->Translation.Text.IsEmpty())
			{
				Texts.Add(FImportedText{ InEntry->Namespace.GetString(), InEntry->Key.GetString(), InEntry->Source.Text, InEntry->Translation.Text });
			}
			return true;
		}, /*bCheckDependencies*/ false);
	}
	return true;
}

FDreamLocSyncOutput DreamLocEngine::ApplyImport(const TMap<FString, TArray<FImportedText>>& InTexts, const TMap<FString, TMap<FString, FString>>& InFiles,
	const FString& InTarget, bool bInForce, const FString& InDLocName)
{
	using namespace DreamLocEngineLocal;
	FDreamLocSyncOutput Output;
	const FString ImportedPath = FString::Printf(TEXT("Engine/Imported/%s.dloc"), *InTarget);
	TArray<FString> CultureNames;
	InTexts.GetKeys(CultureNames);
	CultureNames.Sort();

	for (const FString& Culture : CultureNames)
	{
		FCultureFiles Files;
		if (const TMap<FString, FString>* Existing = InFiles.Find(Culture))
		{
			TArray<FString> Paths;
			Existing->GetKeys(Paths);
			Paths.Sort();
			for (const FString& Path : Paths)
			{
				if (FPaths::GetCleanFilename(Path).StartsWith(TEXT("_")))
				{
					continue;
				}
				FDreamLocDocument Document;
				TArray<FDreamLocDiagnostic> Diagnostics;
				if (!DreamLocFile::Parse(Existing->FindChecked(Path), Document, Diagnostics))
				{
					for (const FDreamLocDiagnostic& Diagnostic : Diagnostics)
					{
						if (Diagnostic.IsError())
						{
							Output.Messages.Add(FDreamLocReportLine{ InDLocName / Culture / Path, Diagnostic });
						}
					}
					Output.bBlocked = true;
					continue;
				}
				Files.ByPath.Add(Path, Files.Paths.Num());
				Files.Paths.Add(Path);
				Files.Texts.Add(Existing->FindChecked(Path));
				Files.Documents.Add(MoveTemp(Document));
				Files.Changed.Add(false);
			}
		}
		if (Output.bBlocked)
		{
			continue;
		}

		// Every live entry by namespace and key: the first one wins, as it does when the translations load.
		TMap<FString, TPair<int32, int32>> Live;
		for (int32 FileIndex = 0; FileIndex < Files.Documents.Num(); ++FileIndex)
		{
			FString Namespace;
			const TArray<FDreamLocItem>& Items = Files.Documents[FileIndex].Items;
			for (int32 ItemIndex = 0; ItemIndex < Items.Num(); ++ItemIndex)
			{
				if (Items[ItemIndex].Kind == EDreamLocItemKind::Namespace)
				{
					Namespace = Items[ItemIndex].Namespace;
				}
				else if (Items[ItemIndex].Kind == EDreamLocItemKind::Entry)
				{
					const FString Id = MakeIdKey(Namespace, Items[ItemIndex].Entry.Key);
					if (!Live.Contains(Id))
					{
						Live.Add(Id, TPair<int32, int32>(FileIndex, ItemIndex));
					}
				}
			}
		}

		int32 Imported = 0;
		int32 Stale = 0;
		int32 Kept = 0;
		TArray<const FImportedText*> Unplaced;
		for (const FImportedText& Text : InTexts.FindChecked(Culture))
		{
			const TPair<int32, int32>* Where = Live.Find(MakeIdKey(Text.Namespace, Text.Key));
			if (Where == nullptr)
			{
				Unplaced.Add(&Text);
				continue;
			}
			FDreamLocEntry& Entry = Files.Documents[Where->Key].Items[Where->Value].Entry;
			const FString* Source = Entry.Find(EDreamLocField::Src);
			const bool bMadeFromCurrent = Source == nullptr || Source->Equals(Text.Source, ESearchCase::CaseSensitive);
			if (Entry.IsTranslated())
			{
				const bool bSame = Entry.Find(EDreamLocField::Text)->Equals(Text.Translation, ESearchCase::CaseSensitive)
					&& bMadeFromCurrent == !Entry.Has(EDreamLocField::Prev);
				if (bSame)
				{
					continue;
				}
				if (Entry.GetState() == EDreamLocState::Done && !bInForce)
				{
					++Kept;
					continue;
				}
			}
			Entry.Set(EDreamLocField::Text, Text.Translation);
			// What review the imported text had is not known.
			Entry.Remove(EDreamLocField::State);
			if (bMadeFromCurrent)
			{
				Entry.Remove(EDreamLocField::Prev);
			}
			else
			{
				Entry.Set(EDreamLocField::Prev, Text.Source);
				++Stale;
			}
			Files.Changed[Where->Key] = true;
			++Imported;
		}

		if (Unplaced.Num() > 0)
		{
			int32 FileIndex = Files.ByPath.FindRef(ImportedPath, INDEX_NONE);
			if (FileIndex == INDEX_NONE)
			{
				FileIndex = Files.Paths.Add(ImportedPath);
				Files.Texts.Add(FString());
				FDreamLocDocument& Document = Files.Documents.AddDefaulted_GetRef();
				Document.bHasCulture = true;
				Document.Culture = Culture;
				Document.HeaderComments.Add(FString::Printf(TEXT("// Translations imported from the localization target %s whose keys no .dloc had."), *InTarget));
				Document.HeaderComments.Add(TEXT("// dloc sync moves each one to its file once a manifest has its key."));
				Files.Changed.Add(true);
			}
			FDreamLocDocument& Document = Files.Documents[FileIndex];
			FString LastNamespace;
			bool bHasNamespace = false;
			for (const FImportedText* Text : Unplaced)
			{
				if (!bHasNamespace || !LastNamespace.Equals(Text->Namespace, ESearchCase::CaseSensitive))
				{
					FDreamLocItem& Namespace = Document.Items.AddDefaulted_GetRef();
					Namespace.Kind = EDreamLocItemKind::Namespace;
					Namespace.Namespace = Text->Namespace;
					LastNamespace = Text->Namespace;
					bHasNamespace = true;
				}
				FDreamLocItem& Item = Document.Items.AddDefaulted_GetRef();
				Item.Kind = EDreamLocItemKind::Entry;
				Item.Entry.Key = Text->Key;
				Item.Entry.Set(EDreamLocField::Src, Text->Source);
				Item.Entry.Set(EDreamLocField::Text, Text->Translation);
			}
			Files.Changed[FileIndex] = true;
		}

		for (int32 FileIndex = 0; FileIndex < Files.Paths.Num(); ++FileIndex)
		{
			if (!Files.Changed[FileIndex])
			{
				continue;
			}
			FString Text = DreamLocFile::Write(Files.Documents[FileIndex]);
			if (Text != Files.Texts[FileIndex])
			{
				Output.Changes.FindOrAdd(Culture).Add(Files.Paths[FileIndex], MoveTemp(Text));
			}
		}

		const FString CultureName = InDLocName / Culture;
		Output.Added += Imported;
		Output.Outdated += Stale;
		AddMessage(Output.Messages, CultureName, EDreamLocSeverity::Info, DreamLocCode::ImportedStale, FString::Printf(
			TEXT("%d translation(s) imported from %s, %d of them made from another source text than the current one (they have a prev)"),
			Imported + Unplaced.Num(), *InTarget, Stale));
		if (Kept > 0)
		{
			AddMessage(Output.Messages, CultureName, EDreamLocSeverity::Info, DreamLocCode::ImportKept, FString::Printf(
				TEXT("%d imported translation(s) left out: the entry has a reviewed one (state done); -Force replaces it"), Kept));
		}
		if (Unplaced.Num() > 0)
		{
			AddMessage(Output.Messages, CultureName / ImportedPath, EDreamLocSeverity::Info, DreamLocCode::ImportUnplaced, FString::Printf(
				TEXT("%d translation(s) of keys no .dloc has went here; dloc sync moves each to its file once a manifest has the key"), Unplaced.Num()));
		}
	}
	if (Output.bBlocked)
	{
		Output.Changes.Reset();
		AddMessage(Output.Messages, InDLocName, EDreamLocSeverity::Error, DreamLocCode::SyncBlocked,
			TEXT("nothing was written: a .dloc above does not read (dloc check shows every error)"));
	}
	return Output;
}

FDreamLocSyncOutput DreamLocEngine::ImportTarget(const FImportOptions& InOptions)
{
	using namespace DreamLocEngineLocal;
	FDreamLocSyncOutput Output;
	const FString Project = ProjectOrDefault(InOptions.ProjectDirectory);
	const FString DLocDirectory = InOptions.DLocDirectory.IsEmpty() ? Project / TEXT("DLoc") : FullDirectory(InOptions.DLocDirectory);
	TArray<FString> Cultures = InOptions.Cultures;
	if (Cultures.Num() == 0)
	{
		Cultures = FDreamLocSettings::LoadFromDirectory(DLocDirectory).TargetCultures;
	}

	TMap<FString, TArray<FImportedText>> Texts;
	if (!ReadTarget(Project, InOptions.Target, InOptions.NativeCulture, Texts, Output.Messages))
	{
		Output.bBlocked = true;
		return Output;
	}
	TArray<FString> Found;
	Texts.GetKeys(Found);
	for (const FString& Culture : Found)
	{
		if (!Cultures.ContainsByPredicate([&Culture](const FString& InCulture) { return InCulture.Equals(Culture, ESearchCase::IgnoreCase); }))
		{
			AddMessage(Output.Messages, Project / TEXT("Content/Localization") / InOptions.Target / Culture, EDreamLocSeverity::Warning, DreamLocCode::ImportKept,
				FString::Printf(TEXT("%s is not a target culture of this project (dloc.ini TargetCultures); its %d translation(s) were not imported"),
					*Culture, Texts.FindChecked(Culture).Num()));
			Texts.Remove(Culture);
		}
	}
	TArray<FString> Imported;
	Texts.GetKeys(Imported);
	FDreamLocSyncOutput Applied = ApplyImport(Texts, DreamLocSync::ReadFiles(DLocDirectory, Imported), InOptions.Target, InOptions.bForce, DLocDirectory);
	Applied.Messages.Insert(Output.Messages, 0);
	if (!Applied.bBlocked && !InOptions.bDryRun)
	{
		DreamLocSync::ApplyChanges(DLocDirectory, Applied, Applied.Messages);
	}
	return Applied;
}

TArray<FString> DreamLocEngine::GetDefaultPrefillTargets()
{
	return { TEXT("PropertyNames"), TEXT("ToolTips"), TEXT("Category"), TEXT("Editor"), TEXT("Engine") };
}

DreamLocEngine::FEngineTranslations DreamLocEngine::ReadEngineTranslations(const FString& InLocalizationDirectory, const TArray<FString>& InTargets,
	const FString& InNativeCulture, const FString& InCulture, TArray<FDreamLocReportLine>& OutMessages)
{
	using namespace DreamLocEngineLocal;
	const FString Directory = InLocalizationDirectory.IsEmpty() ? FullDirectory(FPaths::EngineContentDir() / TEXT("Localization")) : FullDirectory(InLocalizationDirectory);

	// Per source (and per namespace and source): each translation, how many entries have it, and when it was first read.
	struct FCount
	{
		int32 Count = 0;
		int32 First = 0;
	};
	TMap<FString, TMap<FString, FCount>> Counts;
	TMap<FString, TMap<FString, FCount>> NamespaceCounts;
	int32 Order = 0;
	FEngineTranslations Out;
	for (const FString& Target : InTargets)
	{
		const FString NativePath = Directory / Target / InNativeCulture / (Target + TEXT(".locres"));
		const FString CulturePath = Directory / Target / InCulture / (Target + TEXT(".locres"));
		FTextLocalizationResource Native;
		FTextLocalizationResource Translated;
		if (!IFileManager::Get().FileExists(*NativePath) || !IFileManager::Get().FileExists(*CulturePath)
			|| !Native.LoadFromFile(NativePath, 0) || !Translated.LoadFromFile(CulturePath, 0))
		{
			AddMessage(OutMessages, CulturePath, EDreamLocSeverity::Warning, DreamLocCode::ImportUnreadable, FString::Printf(
				TEXT("the engine's %s target has no %s and %s files to read here; left out"), *Target, *InNativeCulture, *InCulture));
			continue;
		}
		for (const TPair<FTextId, FTextLocalizationResource::FEntry>& Pair : Translated.Entries)
		{
			const FTextLocalizationResource::FEntry* Source = Native.Entries.Find(Pair.Key);
			if (Source == nullptr || !Source->LocalizedString.IsValid() || !Pair.Value.LocalizedString.IsValid())
			{
				continue;
			}
			const FString& Text = *Source->LocalizedString;
			const FString& Translation = *Pair.Value.LocalizedString;
			// A translation made from another source text (the engine's own stale one) is no translation of this one.
			if (Text.IsEmpty() || Translation.IsEmpty() || Pair.Value.SourceStringHash != FTextLocalizationResource::HashString(Text))
			{
				continue;
			}
			++Out.Pairs;
			auto Add = [Order](TMap<FString, FCount>& InOutCounts, const FString& InTranslation)
			{
				FCount& Count = InOutCounts.FindOrAdd(InTranslation, FCount{ 0, Order });
				++Count.Count;
			};
			Add(Counts.FindOrAdd(Text), Translation);
			Add(NamespaceCounts.FindOrAdd(MakeIdKey(Pair.Key.GetNamespace().ToString(), Text)), Translation);
			++Order;
		}
	}
	auto Best = [](const TMap<FString, FCount>& InCounts)
	{
		const TPair<FString, FCount>* Found = nullptr;
		for (const TPair<FString, FCount>& Each : InCounts)
		{
			if (Found == nullptr || Each.Value.Count > Found->Value.Count || (Each.Value.Count == Found->Value.Count && Each.Value.First < Found->Value.First))
			{
				Found = &Each;
			}
		}
		return Found != nullptr ? Found->Key : FString();
	};
	for (const TPair<FString, TMap<FString, FCount>>& Pair : Counts)
	{
		Out.BySource.Add(Pair.Key, Best(Pair.Value));
	}
	for (const TPair<FString, TMap<FString, FCount>>& Pair : NamespaceCounts)
	{
		Out.ByNamespaceAndSource.Add(Pair.Key, Best(Pair.Value));
	}
	return Out;
}

FString DreamLocEngine::GetPrefillMarker(const FString& InCulture)
{
	return FString::Printf(TEXT("// from the engine's %s"), *InCulture);
}

FDreamLocSyncOutput DreamLocEngine::ApplyPrefill(const FEngineTranslations& InTranslations, const TMap<FString, FString>& InFiles, const FString& InCulture,
	const FString& InState, const FString& InDLocName, int32* OutUntranslated)
{
	using namespace DreamLocEngineLocal;
	FDreamLocSyncOutput Output;
	TArray<FString> Paths;
	InFiles.GetKeys(Paths);
	Paths.Sort();
	const FString Marker = GetPrefillMarker(InCulture);
	for (const FString& Path : Paths)
	{
		if (FPaths::GetCleanFilename(Path).StartsWith(TEXT("_")))
		{
			continue;
		}
		FDreamLocDocument Document;
		TArray<FDreamLocDiagnostic> Diagnostics;
		if (!DreamLocFile::Parse(InFiles.FindChecked(Path), Document, Diagnostics))
		{
			for (const FDreamLocDiagnostic& Diagnostic : Diagnostics)
			{
				if (Diagnostic.IsError())
				{
					Output.Messages.Add(FDreamLocReportLine{ InDLocName / InCulture / Path, Diagnostic });
				}
			}
			Output.bBlocked = true;
			continue;
		}
		int32 Filled = 0;
		FString Namespace;
		for (FDreamLocItem& Item : Document.Items)
		{
			if (Item.Kind == EDreamLocItemKind::Namespace)
			{
				Namespace = Item.Namespace;
				continue;
			}
			const FString* Source = Item.Kind == EDreamLocItemKind::Entry ? Item.Entry.Find(EDreamLocField::Src) : nullptr;
			if (Source == nullptr || Item.Entry.IsTranslated())
			{
				continue;
			}
			if (OutUntranslated != nullptr)
			{
				++*OutUntranslated;
			}
			const FString* Translation = InTranslations.ByNamespaceAndSource.Find(MakeIdKey(Namespace, *Source));
			if (Translation == nullptr)
			{
				Translation = InTranslations.BySource.Find(*Source);
			}
			if (Translation == nullptr || Translation->IsEmpty())
			{
				continue;
			}
			Item.Entry.Set(EDreamLocField::Text, *Translation);
			Item.Entry.Fields[(int32)EDreamLocField::Text].Comments = { Marker };
			if (InState.IsEmpty())
			{
				Item.Entry.Remove(EDreamLocField::State);
			}
			else
			{
				Item.Entry.Set(EDreamLocField::State, InState);
			}
			++Filled;
		}
		if (Filled > 0)
		{
			Output.Changes.FindOrAdd(InCulture).Add(Path, DreamLocFile::Write(Document));
			Output.Added += Filled;
		}
	}
	if (Output.bBlocked)
	{
		Output.Changes.Reset();
		Output.Added = 0;
	}
	return Output;
}

FDreamLocSyncOutput DreamLocEngine::Prefill(const FPrefillOptions& InOptions)
{
	using namespace DreamLocEngineLocal;
	FDreamLocSyncOutput Output;
	const FString DLocDirectory = FullDirectory(InOptions.DLocDirectory);
	const FDreamLocSettings Settings = FDreamLocSettings::LoadFromDirectory(DLocDirectory);
	if (!Settings.SourceCulture.Equals(InOptions.NativeCulture, ESearchCase::IgnoreCase))
	{
		AddMessage(Output.Messages, DLocDirectory / FDreamLocSettings::FileName, EDreamLocSeverity::Error, DreamLocCode::SyncBlocked, FString::Printf(
			TEXT("the source culture here is %s, the engine's texts are %s: their translations are of other texts"), *Settings.SourceCulture, *InOptions.NativeCulture));
		Output.bBlocked = true;
		return Output;
	}
	const TArray<FString> Targets = InOptions.Targets.Num() > 0 ? InOptions.Targets : GetDefaultPrefillTargets();
	const TArray<FString> Cultures = InOptions.Cultures.Num() > 0 ? InOptions.Cultures : Settings.TargetCultures;
	const TMap<FString, TMap<FString, FString>> Files = DreamLocSync::ReadFiles(DLocDirectory, Cultures);
	for (const FString& Culture : Cultures)
	{
		const FEngineTranslations Translations = ReadEngineTranslations(InOptions.LocalizationDirectory, Targets, InOptions.NativeCulture, Culture, Output.Messages);
		const TMap<FString, FString>* CultureFiles = Files.Find(Culture);
		if (CultureFiles == nullptr)
		{
			continue;
		}
		int32 Untranslated = 0;
		FDreamLocSyncOutput Filled = ApplyPrefill(Translations, *CultureFiles, Culture, InOptions.State, FPaths::GetCleanFilename(DLocDirectory), &Untranslated);
		Output.Messages.Append(MoveTemp(Filled.Messages));
		if (Filled.bBlocked)
		{
			Output.bBlocked = true;
			continue;
		}
		AddMessage(Output.Messages, DLocDirectory / Culture, EDreamLocSeverity::Info, DreamLocCode::Prefilled, FString::Printf(
			TEXT("%s: %d of %d untranslated entr(ies) %s from the engine's %d translation(s) of %d source text(s) (%s)"), *Culture, Filled.Added,
			Untranslated, InOptions.bDryRun ? TEXT("to fill") : TEXT("filled"), Translations.Pairs, Translations.BySource.Num(), *FString::Join(Targets, TEXT(", "))));
		Output.Added += Filled.Added;
		for (TPair<FString, TMap<FString, TOptional<FString>>>& Changed : Filled.Changes)
		{
			Output.Changes.FindOrAdd(Changed.Key).Append(MoveTemp(Changed.Value));
		}
	}
	if (!Output.bBlocked && !InOptions.bDryRun)
	{
		DreamLocSync::ApplyChanges(DLocDirectory, Output, Output.Messages);
	}
	return Output;
}
