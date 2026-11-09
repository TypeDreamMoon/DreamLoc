// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocCommandlet.h"

#include "DreamLocCheck.h"
#include "DreamLocDiagnostics.h"
#include "DreamLocEngine.h"
#include "DreamLocExchange.h"
#include "DreamLocFile.h"
#include "DreamLocPublish.h"
#include "DreamLocSettings.h"
#include "DreamLocSync.h"
#include "DreamLocTextSource.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/LocalizedTextSourceTypes.h"
#include "Internationalization/TextLocalizationResource.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamLocCommandlet)

DEFINE_LOG_CATEGORY_STATIC(LogDreamLocCommandlet, Log, All);

namespace DreamLocCommandletLocal
{
	struct FRun
	{
		int32 Files = 0;
		int32 Errors = 0;
		int32 Warnings = 0;
	};

	struct FSourceFile
	{
		FString Path;
		/** The culture folder the file is in; empty for a file outside every DLoc folder. */
		FString Culture;
	};

	/**
	 * Straight to stdout rather than through the log: a log line carries a category and maybe a time, and the log
	 * reaches stdout only with the engine's -stdout. The log is flushed first so the text lands after it.
	 */
	void WriteStdout(const FString& InText)
	{
		GLog->Flush();
		const FTCHARToUTF8 Utf8(*InText);
		fwrite(Utf8.Get(), 1, static_cast<size_t>(Utf8.Length()), stdout);
		fflush(stdout);
	}

	void PrintTextBlock(const FString& InName, const FString& InText)
	{
		FString Block = FString::Printf(TEXT("===== begin %s =====\n"), *InName);
		Block += InText;
		if (!InText.IsEmpty() && !InText.EndsWith(TEXT("\n")))
		{
			Block += TEXT("\n");
		}
		Block += FString::Printf(TEXT("===== end %s =====\n"), *InName);
		WriteStdout(Block);
	}

	void Report(FRun& InOutRun, const FString& InFile, TConstArrayView<FDreamLocDiagnostic> InDiagnostics)
	{
		for (const FDreamLocDiagnostic& Diagnostic : InDiagnostics)
		{
			UE_LOG(LogDreamLocCommandlet, Display, TEXT("%s"), *Diagnostic.Format(InFile));
		}
		InOutRun.Errors += FDreamLocDiagnostic::CountErrors(InDiagnostics);
		InOutRun.Warnings += FDreamLocDiagnostic::CountWarnings(InDiagnostics);
	}

	TArray<FString> GetRoots(const FString& InRootArgument)
	{
		TArray<FString> Roots;
		if (!InRootArgument.IsEmpty())
		{
			TArray<FString> Parts;
			InRootArgument.ParseIntoArray(Parts, TEXT(","), /*InCullEmpty*/ true);
			for (FString& Part : Parts)
			{
				Part.TrimStartAndEndInline();
				FString Full = FPaths::IsRelative(Part) ? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / Part) : FPaths::ConvertRelativePathToFull(Part);
				FPaths::NormalizeDirectoryName(Full);
				Roots.AddUnique(Full);
			}
			return Roots;
		}
		for (const FDreamLocLayer& Layer : FDreamLocTextSource::FindLayers())
		{
			Roots.AddUnique(Layer.Directory);
		}
		return Roots;
	}

	/** The culture folder InPath is in under one of InRoots, or empty. */
	FString FindCultureOf(const FString& InPath, const TArray<FString>& InRoots)
	{
		for (const FString& Root : InRoots)
		{
			FString Relative = InPath;
			const FString RootDirectory = Root.EndsWith(TEXT("/")) ? Root : Root + TEXT("/");
			if (FPaths::MakePathRelativeTo(Relative, *RootDirectory) && !Relative.StartsWith(TEXT("..")))
			{
				FString Culture;
				if (Relative.Split(TEXT("/"), &Culture, nullptr))
				{
					return Culture;
				}
			}
		}
		return FString();
	}

	void CollectFiles(const FString& InFileArgument, const TArray<FString>& InRoots, TArray<FSourceFile>& OutFiles)
	{
		if (!InFileArgument.IsEmpty())
		{
			FString Path = FPaths::IsRelative(InFileArgument) ? FPaths::ProjectDir() / InFileArgument : InFileArgument;
			Path = FPaths::ConvertRelativePathToFull(Path);
			OutFiles.Add(FSourceFile{ Path, FindCultureOf(Path, InRoots) });
			return;
		}
		for (const FString& Root : InRoots)
		{
			TArray<FString> CultureDirectories;
			IFileManager::Get().FindFiles(CultureDirectories, *(Root / TEXT("*")), /*Files*/ false, /*Directories*/ true);
			CultureDirectories.Sort();
			for (const FString& Culture : CultureDirectories)
			{
				TArray<FString> Paths;
				IFileManager::Get().FindFilesRecursive(Paths, *(Root / Culture), TEXT("*.dloc"), /*Files*/ true, /*Directories*/ false);
				Paths.Sort();
				for (const FString& Path : Paths)
				{
					OutFiles.Add(FSourceFile{ Path, Culture });
				}
			}
		}
	}

	int32 RunFormat(const TArray<FSourceFile>& InFiles, bool bInWrite, bool bInVerify)
	{
		FRun Run;
		int32 Changed = 0;
		for (const FSourceFile& File : InFiles)
		{
			++Run.Files;
			TArray<uint8> Bytes;
			FString Text;
			if (!FFileHelper::LoadFileToArray(Bytes, *File.Path) || !FFileHelper::LoadFileToString(Text, *File.Path))
			{
				UE_LOG(LogDreamLocCommandlet, Display, TEXT("%s: error: the file cannot be read"), *File.Path);
				++Run.Errors;
				continue;
			}
			FDreamLocDocument Document;
			TArray<FDreamLocDiagnostic> Diagnostics;
			const bool bParsed = DreamLocFile::Parse(Text, Document, Diagnostics);
			Report(Run, File.Path, Diagnostics);
			if (!bParsed)
			{
				continue;
			}
			const FString Canonical = DreamLocFile::Write(Document);
			const FTCHARToUTF8 CanonicalUtf8(*Canonical);
			const bool bSame = Bytes.Num() == CanonicalUtf8.Length()
				&& FMemory::Memcmp(Bytes.GetData(), CanonicalUtf8.Get(), Bytes.Num()) == 0;
			if (bInWrite)
			{
				if (!bSame)
				{
					if (FFileHelper::SaveStringToFile(Canonical, *File.Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
					{
						++Changed;
						UE_LOG(LogDreamLocCommandlet, Display, TEXT("Formatted %s"), *File.Path);
					}
					else
					{
						++Run.Errors;
						UE_LOG(LogDreamLocCommandlet, Display, TEXT("%s: error: the file cannot be written"), *File.Path);
					}
				}
			}
			else if (bInVerify)
			{
				if (!bSame)
				{
					++Changed;
					++Run.Errors;
					UE_LOG(LogDreamLocCommandlet, Display, TEXT("%s: error: not in canonical form (dloc fmt -Write rewrites it)"), *File.Path);
				}
			}
			else
			{
				PrintTextBlock(File.Path, Canonical);
			}
		}
		if (bInWrite)
		{
			UE_LOG(LogDreamLocCommandlet, Display, TEXT("DreamLoc format: %d file(s), %d rewritten, %d error(s), %d warning(s)"), Run.Files, Changed, Run.Errors, Run.Warnings);
		}
		else if (bInVerify)
		{
			UE_LOG(LogDreamLocCommandlet, Display, TEXT("DreamLoc format: %d file(s), %d not canonical, %d error(s), %d warning(s)"), Run.Files, Changed, Run.Errors, Run.Warnings);
		}
		else
		{
			UE_LOG(LogDreamLocCommandlet, Display, TEXT("DreamLoc format: %d file(s), %d error(s), %d warning(s)"), Run.Files, Run.Errors, Run.Warnings);
		}
		return Run.Errors > 0 ? 1 : 0;
	}

	int32 RunShow(const FString& InCulture, const FString& InRootArgument)
	{
		if (!FDreamLocTextSource::IsKnownCulture(InCulture))
		{
			UE_LOG(LogDreamLocCommandlet, Display, TEXT("error: -Show needs -Culture=<a culture the engine knows>, not '%s'"), *InCulture);
			UE_LOG(LogDreamLocCommandlet, Display, TEXT("DreamLoc show: nothing done, 1 error(s)"));
			return 1;
		}
		TArray<FDreamLocLayer> Layers;
		if (InRootArgument.IsEmpty())
		{
			Layers = FDreamLocTextSource::FindLayers();
		}
		else
		{
			for (const FString& Root : GetRoots(InRootArgument))
			{
				FDreamLocLayer& Layer = Layers.AddDefaulted_GetRef();
				Layer.Index = 1;
				Layer.Name = FPaths::GetCleanFilename(Root);
				Layer.Directory = Root;
			}
		}
		const FDreamLocSettings Settings = FDreamLocSettings::LoadFromDirectory(FDreamLocTextSource::GetProjectDirectory());
		FDreamLocTextSource Source(Layers, Settings);
		const TArray<FString> Prioritized = FInternationalization::Get().GetPrioritizedCultureNames(InCulture);
		FTextLocalizationResource Resource;
		Source.LoadLocalizedResources(ELocalizationLoadFlags::Game, Prioritized, Resource, Resource);

		struct FRow
		{
			FString Namespace;
			FString Key;
			FString Text;
			FString File;
		};
		TArray<FRow> Rows;
		const int32 Floor = FDreamLocTextSource::MakeSourceEntryPriority(63);
		for (const TPair<FTextId, FTextLocalizationResource::FEntry>& Pair : Resource.Entries)
		{
			if (Pair.Value.Priority >= Floor || !Pair.Value.LocalizedString.IsValid())
			{
				continue;
			}
			Rows.Add(FRow{ Pair.Key.GetNamespace().ToString(), Pair.Key.GetKey().ToString(), *Pair.Value.LocalizedString, Pair.Value.LocResID.ToString() });
		}
		Rows.Sort([](const FRow& A, const FRow& B) { return A.Namespace != B.Namespace ? A.Namespace < B.Namespace : A.Key < B.Key; });
		for (const FRow& Row : Rows)
		{
			UE_LOG(LogDreamLocCommandlet, Display, TEXT("%s %s = %s    (%s)"), *DreamLocFile::QuoteWordIfNeeded(Row.Namespace, false),
				*DreamLocFile::QuoteWordIfNeeded(Row.Key, true), *DreamLocFile::Quote(Row.Text), *Row.File);
		}
		const TArray<FString> Chain = Settings.ExpandFallbacks(Prioritized);
		UE_LOG(LogDreamLocCommandlet, Display, TEXT("DreamLoc show: %d translation(s) for %s (cultures tried: %s)"), Rows.Num(), *InCulture,
			*FString::Join(Chain, TEXT(", ")));
		return 0;
	}

	void ReportLines(const TArray<FDreamLocReportLine>& InLines)
	{
		for (const FDreamLocReportLine& Line : InLines)
		{
			UE_LOG(LogDreamLocCommandlet, Display, TEXT("%s"), *Line.Format());
		}
	}

	int32 CountErrors(const TArray<FDreamLocReportLine>& InLines)
	{
		int32 Count = 0;
		for (const FDreamLocReportLine& Line : InLines)
		{
			Count += Line.Diagnostic.IsError() ? 1 : 0;
		}
		return Count;
	}

	int32 CountWarnings(const TArray<FDreamLocReportLine>& InLines)
	{
		int32 Count = 0;
		for (const FDreamLocReportLine& Line : InLines)
		{
			Count += Line.Diagnostic.Severity == EDreamLocSeverity::Warning ? 1 : 0;
		}
		return Count;
	}

	/** One line per file written or deleted, in path order. */
	void ReportChanges(const FDreamLocSyncOutput& InOutput, const FString& InDLocDirectory, bool bInDryRun)
	{
		TArray<FString> Lines;
		for (const TPair<FString, TMap<FString, TOptional<FString>>>& Culture : InOutput.Changes)
		{
			for (const TPair<FString, TOptional<FString>>& Change : Culture.Value)
			{
				const TCHAR* Verb = Change.Value.IsSet() ? (bInDryRun ? TEXT("would write") : TEXT("wrote")) : (bInDryRun ? TEXT("would delete") : TEXT("deleted"));
				Lines.Add(FString::Printf(TEXT("%s %s"), Verb, *(InDLocDirectory / Culture.Key / Change.Key)));
			}
		}
		Lines.Sort();
		for (const FString& Line : Lines)
		{
			UE_LOG(LogDreamLocCommandlet, Display, TEXT("%s"), *Line);
		}
	}

	TArray<FString> SplitCultures(const FString& InValue)
	{
		TArray<FString> Cultures;
		InValue.ParseIntoArray(Cultures, TEXT(","), /*InCullEmpty*/ true);
		for (FString& Culture : Cultures)
		{
			Culture.TrimStartAndEndInline();
		}
		return Cultures;
	}

	/** Whose sources and translations a command works on: the project's, or with -Plugin=<name> a plugin's own. */
	struct FOwner
	{
		FString Directory;
		FString DLocDirectory;
		FDreamLocSettings Settings;
		/** The plugin's name; empty for the project. */
		FString PluginName;
	};

	/**
	 * A plugin's own DLoc/dloc.ini when it has one; else the project's cultures, with the gather's folders the default
	 * ones (the project's are relative to the project).
	 */
	bool ResolveOwner(const FString& InPlugin, FOwner& OutOwner)
	{
		const FDreamLocSettings ProjectSettings = FDreamLocSettings::LoadFromDirectory(FDreamLocTextSource::GetProjectDirectory());
		if (InPlugin.IsEmpty())
		{
			OutOwner.Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
			OutOwner.DLocDirectory = FDreamLocTextSource::GetProjectDirectory();
			OutOwner.Settings = ProjectSettings;
			return true;
		}
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(InPlugin);
		if (!Plugin.IsValid())
		{
			UE_LOG(LogDreamLocCommandlet, Display, TEXT("error: there is no plugin '%s'"), *InPlugin);
			return false;
		}
		OutOwner.Directory = FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir());
		OutOwner.DLocDirectory = OutOwner.Directory / TEXT("DLoc");
		OutOwner.PluginName = Plugin->GetName();
		if (IFileManager::Get().FileExists(*(OutOwner.DLocDirectory / FDreamLocSettings::FileName)))
		{
			OutOwner.Settings = FDreamLocSettings::LoadFromDirectory(OutOwner.DLocDirectory);
		}
		else
		{
			const FDreamLocSettings Defaults;
			OutOwner.Settings = ProjectSettings;
			OutOwner.Settings.GatherSourcePaths = Defaults.GatherSourcePaths;
			OutOwner.Settings.GatherContentPaths = Defaults.GatherContentPaths;
			OutOwner.Settings.GatherExcludePaths = Defaults.GatherExcludePaths;
			OutOwner.Settings.bGatherEditorOnlyData = Defaults.bGatherEditorOnlyData;
			OutOwner.Settings.bGatherMetaData = Defaults.bGatherMetaData;
			OutOwner.Settings.PublishLoadingPolicy = Defaults.PublishLoadingPolicy;
		}
		return true;
	}

	int32 RunGather(const FOwner& InOwner)
	{
		DreamLocEngine::FGatherOptions Options = DreamLocEngine::MakeGatherOptions(InOwner.Settings);
		Options.ProjectDirectory = InOwner.Directory;
		const DreamLocEngine::FGatherResult Result = DreamLocEngine::Gather(Options);
		ReportLines(Result.Messages);
		UE_LOG(LogDreamLocCommandlet, Display, TEXT("DreamLoc gather: %d text(s) in %d source file(s), %d manifest(s) written, %d deleted, %d error(s), %d warning(s)"),
			Result.Texts, Result.Sources, Result.ManifestsWritten, Result.ManifestsDeleted, CountErrors(Result.Messages), CountWarnings(Result.Messages));
		return Result.bSucceeded ? 0 : 1;
	}

	int32 RunSync(const FOwner& InOwner, const FString& InCultures, bool bInDryRun)
	{
		DreamLocSync::FDirectoryOptions Options;
		Options.OwnerDirectory = InOwner.Directory;
		Options.DLocDirectory = InOwner.DLocDirectory;
		Options.Cultures = SplitCultures(InCultures);
		if (Options.Cultures.Num() == 0)
		{
			Options.Cultures = InOwner.Settings.TargetCultures;
		}
		Options.bDryRun = bInDryRun;
		const FDreamLocSyncOutput Output = DreamLocSync::SyncDirectory(Options);
		ReportLines(Output.Messages);
		ReportChanges(Output, InOwner.DLocDirectory, bInDryRun);
		UE_LOG(LogDreamLocCommandlet, Display, TEXT("DreamLoc sync: %d file(s) %s, %d deleted; %d added, %d outdated, %d migrated, %d moved, %d revived, %d removed; %d error(s), %d warning(s)"),
			Output.NumWrites(), bInDryRun ? TEXT("to write") : TEXT("written"), Output.NumDeletes(), Output.Added, Output.Outdated, Output.Migrated,
			Output.Moved, Output.Revived, Output.Removed, Output.NumErrors(), Output.NumWarnings());
		return Output.bBlocked || Output.NumErrors() > 0 ? 1 : 0;
	}

	int32 RunMigrate(const DreamLocSync::FMigrateRequest& InRequest)
	{
		const FString DLocDirectory = FDreamLocTextSource::GetProjectDirectory();
		const FDreamLocSettings Settings = FDreamLocSettings::LoadFromDirectory(DLocDirectory);
		FDreamLocSyncOutput Output;
		if (InRequest.FromKey.IsEmpty() || InRequest.ToKey.IsEmpty())
		{
			Output.Messages.Add(FDreamLocReportLine{ DLocDirectory, FDreamLocDiagnostic(EDreamLocSeverity::Error, DreamLocCode::MigrateNotFound, 0, 0,
				TEXT("-Migrate needs -From=<old key> and -To=<new key>")) });
		}
		else
		{
			Output = DreamLocSync::Migrate(DreamLocSync::ReadFiles(DLocDirectory, Settings.TargetCultures), InRequest, DLocDirectory);
			if (!Output.bBlocked)
			{
				DreamLocSync::ApplyChanges(DLocDirectory, Output, Output.Messages);
			}
		}
		ReportLines(Output.Messages);
		ReportChanges(Output, DLocDirectory, /*bInDryRun*/ false);
		UE_LOG(LogDreamLocCommandlet, Display, TEXT("DreamLoc migrate: %d translation(s) moved, %d file(s) written, %d error(s), %d warning(s)"),
			Output.Migrated, Output.NumWrites(), Output.NumErrors(), Output.NumWarnings());
		return Output.NumErrors() > 0 ? 1 : 0;
	}

	int32 RunImport(const DreamLocEngine::FImportOptions& InOptions)
	{
		const FDreamLocSyncOutput Output = DreamLocEngine::ImportTarget(InOptions);
		ReportLines(Output.Messages);
		ReportChanges(Output, FDreamLocTextSource::GetProjectDirectory(), InOptions.bDryRun);
		UE_LOG(LogDreamLocCommandlet, Display, TEXT("DreamLoc import: %d translation(s) into existing entries (%d stale), %d file(s) %s, %d error(s), %d warning(s)"),
			Output.Added, Output.Outdated, Output.NumWrites(), InOptions.bDryRun ? TEXT("to write") : TEXT("written"), Output.NumErrors(), Output.NumWarnings());
		return Output.bBlocked || Output.NumErrors() > 0 ? 1 : 0;
	}

	int32 RunBake(bool bInDryRun)
	{
		DreamLocPublish::FBakeOptions Options;
		Options.bDryRun = bInDryRun;
		const DreamLocPublish::FBakeResult Result = DreamLocPublish::Bake(Options);
		ReportLines(Result.Messages);
		int32 Translations = 0;
		for (const TPair<FString, int32>& Pair : Result.Translations)
		{
			Translations += Pair.Value;
		}
		UE_LOG(LogDreamLocCommandlet, Display, TEXT("DreamLoc bake: %d culture(s), %d translation(s), %d source text(s), %d file(s) %s, %d error(s), %d warning(s)"),
			Result.Translations.Num(), Translations, Result.Sources, Result.Files.Num(), bInDryRun ? TEXT("to write") : TEXT("written"),
			CountErrors(Result.Messages), CountWarnings(Result.Messages));
		return Result.bSucceeded && CountErrors(Result.Messages) == 0 ? 0 : 1;
	}

	int32 RunBakePlugin(const FOwner& InOwner, bool bInDryRun)
	{
		DreamLocPublish::FPluginBakeOptions Options;
		Options.PluginDirectory = InOwner.Directory;
		Options.PluginName = InOwner.PluginName;
		Options.Settings = InOwner.Settings;
		Options.bDryRun = bInDryRun;
		const DreamLocPublish::FPluginBakeResult Result = DreamLocPublish::BakePlugin(Options);
		ReportLines(Result.Messages);
		int32 Translations = 0;
		for (const TPair<FString, int32>& Pair : Result.Translations)
		{
			Translations += Pair.Value;
		}
		UE_LOG(LogDreamLocCommandlet, Display, TEXT("DreamLoc bake: %s, %d target(s), %d culture(s), %d translation(s), %d source text(s), %d file(s) %s, %d error(s), %d warning(s)"),
			*InOwner.PluginName, Result.Targets.Num(), Result.Translations.Num(), Translations, Result.Sources, Result.Files.Num(),
			bInDryRun ? TEXT("to write") : TEXT("written"), CountErrors(Result.Messages), CountWarnings(Result.Messages));
		return Result.bSucceeded && CountErrors(Result.Messages) == 0 ? 0 : 1;
	}

	int32 RunPrune(bool bInDryRun)
	{
		const FString DLocDirectory = FDreamLocTextSource::GetProjectDirectory();
		const FDreamLocSettings Settings = FDreamLocSettings::LoadFromDirectory(DLocDirectory);
		FDreamLocSyncOutput Output = DreamLocPublish::Prune(DreamLocSync::ReadFiles(DLocDirectory, Settings.TargetCultures), DLocDirectory);
		if (!Output.bBlocked && !bInDryRun)
		{
			DreamLocSync::ApplyChanges(DLocDirectory, Output, Output.Messages);
		}
		ReportLines(Output.Messages);
		ReportChanges(Output, DLocDirectory, bInDryRun);
		UE_LOG(LogDreamLocCommandlet, Display, TEXT("DreamLoc prune: %d removed entr(ies), %d file(s) %s, %d deleted, %d error(s), %d warning(s)"),
			Output.Removed, Output.NumWrites(), bInDryRun ? TEXT("to write") : TEXT("written"), Output.NumDeletes(), Output.NumErrors(), Output.NumWarnings());
		return Output.bBlocked || Output.NumErrors() > 0 ? 1 : 0;
	}

	int32 RunExport(const FString& InCulture, const FString& InOnly, const FString& InFormat, const FString& InOut, const FString& InFiles, const FString& InReferences)
	{
		const FString DLocDirectory = FDreamLocTextSource::GetProjectDirectory();
		const FDreamLocSettings Settings = FDreamLocSettings::LoadFromDirectory(DLocDirectory);
		TArray<FDreamLocReportLine> Messages;
		const TOptional<EDreamLocExchangeFormat> Format = DreamLocExchange::FormatOfPath(TEXT("x.") + (InFormat.IsEmpty() ? FString(TEXT("json")) : InFormat));
		if (InCulture.IsEmpty() || !Format.IsSet())
		{
			UE_LOG(LogDreamLocCommandlet, Display, TEXT("error: -Export needs -Culture=<culture> and a -Format of json, xliff, po or csv"));
			UE_LOG(LogDreamLocCommandlet, Display, TEXT("DreamLoc export: nothing done, 1 error(s), 0 warning(s)"));
			return 1;
		}
		DreamLocExchange::FExportOptions Options;
		Options.Culture = InCulture;
		Options.Only = SplitCultures(InOnly);
		Options.Files = SplitCultures(InFiles);
		Options.References = SplitCultures(InReferences);
		const FString ProjectDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
		// The manifests say what each text is and what the source tool knows about it (a line's speaker...).
		for (const FString& Path : DreamLocExtract::FindManifestFiles(DreamLocExtract::GetExtractDirectory(ProjectDirectory)))
		{
			FDreamLocExtractManifest Manifest;
			FString Error;
			if (DreamLocExtract::LoadFile(Path, Manifest, Error))
			{
				Options.Manifests.Add(MoveTemp(Manifest));
			}
		}
		TArray<FString> Cultures = Options.References;
		Cultures.AddUnique(InCulture);
		const FDreamLocExchangeBatch Batch = DreamLocExchange::Collect(DreamLocSync::ReadFiles(DLocDirectory, Cultures), Options, Settings.SourceCulture, Messages);
		const FString Default = FString::Printf(TEXT("Saved/DLoc/%s-batch.%s"), *InCulture, DreamLocExchange::ExtensionOf(Format.GetValue()));
		FString Out = InOut.IsEmpty() ? Default : InOut;
		Out = FPaths::ConvertRelativePathToFull(FPaths::IsRelative(Out) ? ProjectDirectory / Out : Out);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Out), /*Tree*/ true);
		const bool bWritten = FFileHelper::SaveStringToFile(DreamLocExchange::Write(Batch, Format.GetValue()), *Out, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		Messages.Add(FDreamLocReportLine{ Out, FDreamLocDiagnostic(bWritten ? EDreamLocSeverity::Info : EDreamLocSeverity::Error, bWritten ? DreamLocCode::Exported : DreamLocCode::SyncBlocked,
			0, 0, bWritten ? FString::Printf(TEXT("%d entr(ies) of %s"), Batch.Entries.Num(), *InCulture) : FString(TEXT("the file cannot be written"))) });
		ReportLines(Messages);
		UE_LOG(LogDreamLocCommandlet, Display, TEXT("DreamLoc export: %d entr(ies) to %s, %d error(s), %d warning(s)"), Batch.Entries.Num(), *Out, CountErrors(Messages), CountWarnings(Messages));
		return CountErrors(Messages) > 0 ? 1 : 0;
	}

	int32 RunImportFile(const FString& InPath, const FString& InCulture, const FString& InState, bool bInForce, bool bInDryRun)
	{
		const FString DLocDirectory = FDreamLocTextSource::GetProjectDirectory();
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::IsRelative(InPath) ? FPaths::ProjectDir() / InPath : InPath);
		const TOptional<EDreamLocExchangeFormat> Format = DreamLocExchange::FormatOfPath(Path);
		FDreamLocExchangeBatch Batch;
		FString Text;
		FString Error;
		FDreamLocSyncOutput Output;
		if (!Format.IsSet() || !FFileHelper::LoadFileToString(Text, *Path) || !DreamLocExchange::Read(Text, Format.GetValue(), Batch, Error))
		{
			Output.Messages.Add(FDreamLocReportLine{ Path, FDreamLocDiagnostic(EDreamLocSeverity::Error, DreamLocCode::ImportUnreadable, 0, 0,
				!Format.IsSet() ? FString(TEXT("not a .json, .xlf, .po or .csv file")) : Error.IsEmpty() ? FString(TEXT("the file cannot be read")) : Error) });
		}
		else
		{
			DreamLocExchange::FImportOptions Options;
			Options.Culture = InCulture;
			Options.State = InState;
			Options.bForce = bInForce;
			const FString Culture = InCulture.IsEmpty() ? Batch.Culture : InCulture;
			Output = DreamLocExchange::Import(Batch, DreamLocSync::ReadFiles(DLocDirectory, { Culture }), Options, DLocDirectory);
			if (!Output.bBlocked && !bInDryRun)
			{
				DreamLocSync::ApplyChanges(DLocDirectory, Output, Output.Messages);
			}
		}
		ReportLines(Output.Messages);
		ReportChanges(Output, DLocDirectory, bInDryRun);
		UE_LOG(LogDreamLocCommandlet, Display, TEXT("DreamLoc import: %d translation(s) into existing entries (%d stale), %d file(s) %s, %d error(s), %d warning(s)"),
			Output.Added, Output.Outdated, Output.NumWrites(), bInDryRun ? TEXT("to write") : TEXT("written"), Output.NumErrors(), Output.NumWarnings());
		return Output.bBlocked || Output.NumErrors() > 0 ? 1 : 0;
	}

	int32 RunFreeze()
	{
		const FString DLocDirectory = FDreamLocTextSource::GetProjectDirectory();
		const FDreamLocSettings Settings = FDreamLocSettings::LoadFromDirectory(DLocDirectory);
		int32 Keys = 0;
		const bool bWritten = DreamLocPublish::Freeze(DLocDirectory, DreamLocSync::ReadFiles(DLocDirectory, Settings.TargetCultures), Keys);
		UE_LOG(LogDreamLocCommandlet, Display, TEXT("DreamLoc freeze: %d key(s) recorded in %s, %d error(s), 0 warning(s)"), Keys,
			*(DLocDirectory / DreamLocPublish::FrozenFileName), bWritten ? 0 : 1);
		return bWritten ? 0 : 1;
	}

	int32 RunCheck(const FString& InFileArgument, const FString& InRootArgument, bool bInWithSync)
	{
		FDreamLocCheckOptions Options;
		if (InRootArgument.IsEmpty())
		{
			for (const FDreamLocLayer& Layer : FDreamLocTextSource::FindLayers())
			{
				Options.Roots.Add(FDreamLocCheckRoot{ Layer.Directory, Layer.Index });
			}
		}
		else
		{
			for (const FString& Root : GetRoots(InRootArgument))
			{
				Options.Roots.Add(FDreamLocCheckRoot{ Root, 1 });
			}
		}
		if (!InFileArgument.IsEmpty())
		{
			Options.File = FPaths::ConvertRelativePathToFull(FPaths::IsRelative(InFileArgument) ? FPaths::ProjectDir() / InFileArgument : InFileArgument);
		}
		const FDreamLocCheckResult Result = DreamLocCheck::Run(Options);
		TArray<FDreamLocReportLine> Lines = Result.Lines;

		// What sync would say: rename candidates, conflicting sources, sources not compiled, and files it would change.
		if (bInWithSync && InFileArgument.IsEmpty() && InRootArgument.IsEmpty())
		{
			DreamLocSync::FDirectoryOptions SyncOptions;
			SyncOptions.OwnerDirectory = FPaths::ProjectDir();
			SyncOptions.bDryRun = true;
			const FDreamLocSyncOutput Sync = DreamLocSync::SyncDirectory(SyncOptions);
			for (const FDreamLocReportLine& Line : Sync.Messages)
			{
				if (Line.Diagnostic.Severity != EDreamLocSeverity::Info)
				{
					Lines.Add(Line);
				}
			}
			const int32 Changes = Sync.NumWrites() + Sync.NumDeletes();
			if (!Sync.bBlocked && Changes > 0)
			{
				Lines.Add(FDreamLocReportLine{ FDreamLocTextSource::GetProjectDirectory(), FDreamLocDiagnostic(EDreamLocSeverity::Warning,
					DreamLocCode::NotSynced, 0, 0, FString::Printf(TEXT("%d file(s) are not up to date with the sources; dloc sync -DryRun lists them"), Changes)) });
			}
		}

		ReportLines(Lines);
		const int32 Errors = CountErrors(Lines);
		UE_LOG(LogDreamLocCommandlet, Display, TEXT("DreamLoc check: %d file(s), %d error(s), %d warning(s)"), Result.Files, Errors, CountWarnings(Lines));
		return Errors > 0 ? 1 : 0;
	}
}

UDreamLocCommandlet::UDreamLocCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UDreamLocCommandlet::Main(const FString& Params)
{
	using namespace DreamLocCommandletLocal;

	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> Values;
	ParseCommandLine(*Params, Tokens, Switches, Values);
	auto HasSwitch = [&Switches](const TCHAR* InName)
	{
		return Switches.ContainsByPredicate([InName](const FString& InSwitch) { return InSwitch.Equals(InName, ESearchCase::IgnoreCase); });
	};
	auto GetValue = [&Values](const TCHAR* InName)
	{
		for (const TPair<FString, FString>& Pair : Values)
		{
			if (Pair.Key.Equals(InName, ESearchCase::IgnoreCase))
			{
				return Pair.Value.TrimQuotes();
			}
		}
		return FString();
	};

	const FString RootArgument = GetValue(TEXT("Root"));
	if (HasSwitch(TEXT("Show")))
	{
		return RunShow(GetValue(TEXT("Culture")), RootArgument);
	}
	if (HasSwitch(TEXT("Sync")) || HasSwitch(TEXT("Gather")))
	{
		FOwner Owner;
		if (!ResolveOwner(GetValue(TEXT("Plugin")), Owner))
		{
			UE_LOG(LogDreamLocCommandlet, Display, TEXT("DreamLoc sync: nothing done, 1 error(s)"));
			return 1;
		}
		if (HasSwitch(TEXT("Gather")) && RunGather(Owner) != 0)
		{
			return 1;
		}
		return HasSwitch(TEXT("Sync")) ? RunSync(Owner, GetValue(TEXT("Culture")), HasSwitch(TEXT("DryRun"))) : 0;
	}
	if (HasSwitch(TEXT("Bake")))
	{
		const FString PluginName = GetValue(TEXT("Plugin"));
		if (PluginName.IsEmpty())
		{
			return RunBake(HasSwitch(TEXT("DryRun")));
		}
		FOwner Owner;
		if (!ResolveOwner(PluginName, Owner))
		{
			UE_LOG(LogDreamLocCommandlet, Display, TEXT("DreamLoc bake: nothing done, 1 error(s)"));
			return 1;
		}
		return RunBakePlugin(Owner, HasSwitch(TEXT("DryRun")));
	}
	if (HasSwitch(TEXT("Prune")))
	{
		return RunPrune(HasSwitch(TEXT("DryRun")));
	}
	if (HasSwitch(TEXT("Freeze")))
	{
		return RunFreeze();
	}
	if (HasSwitch(TEXT("Export")))
	{
		return RunExport(GetValue(TEXT("Culture")), GetValue(TEXT("Only")), GetValue(TEXT("Format")), GetValue(TEXT("Out")), GetValue(TEXT("Files")), GetValue(TEXT("Reference")));
	}
	if (HasSwitch(TEXT("Import")))
	{
		return RunImportFile(GetValue(TEXT("In")), GetValue(TEXT("Culture")), GetValue(TEXT("State")), HasSwitch(TEXT("Force")), HasSwitch(TEXT("DryRun")));
	}
	if (HasSwitch(TEXT("Migrate")))
	{
		DreamLocSync::FMigrateRequest Request;
		Request.FromNamespace = GetValue(TEXT("Namespace"));
		Request.FromKey = GetValue(TEXT("From"));
		Request.ToNamespace = GetValue(TEXT("ToNamespace"));
		Request.ToKey = GetValue(TEXT("To"));
		Request.bForce = HasSwitch(TEXT("Force"));
		return RunMigrate(Request);
	}
	if (HasSwitch(TEXT("ImportEngine")))
	{
		DreamLocEngine::FImportOptions Options;
		const FString Target = GetValue(TEXT("Target"));
		Options.Target = Target.IsEmpty() ? TEXT("Game") : Target;
		Options.NativeCulture = GetValue(TEXT("NativeCulture"));
		Options.Cultures = SplitCultures(GetValue(TEXT("Culture")));
		Options.bForce = HasSwitch(TEXT("Force"));
		Options.bDryRun = HasSwitch(TEXT("DryRun"));
		return RunImport(Options);
	}

	if (HasSwitch(TEXT("Format")))
	{
		const TArray<FString> Roots = GetRoots(RootArgument);
		TArray<FSourceFile> Files;
		CollectFiles(GetValue(TEXT("File")), Roots, Files);
		return RunFormat(Files, HasSwitch(TEXT("Write")), HasSwitch(TEXT("Verify")));
	}
	return RunCheck(GetValue(TEXT("File")), RootArgument, HasSwitch(TEXT("WithSync")));
}
