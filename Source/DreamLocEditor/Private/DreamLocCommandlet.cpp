// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocCommandlet.h"

#include "DreamLocDiagnostics.h"
#include "DreamLocEngine.h"
#include "DreamLocFile.h"
#include "DreamLocSettings.h"
#include "DreamLocSync.h"
#include "DreamLocTextSource.h"
#include "HAL/FileManager.h"
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

	/** What only the file's place can tell: its culture against its folder, and entries that will not load. */
	void CheckPlacement(const FSourceFile& InFile, const FDreamLocDocument& InDocument, TArray<FDreamLocDiagnostic>& OutDiagnostics)
	{
		if (!InFile.Culture.IsEmpty())
		{
			if (!FDreamLocTextSource::IsKnownCulture(InFile.Culture))
			{
				OutDiagnostics.Emplace(EDreamLocSeverity::Warning, DreamLocCode::UnknownCulture, 0, 0, FString::Printf(
					TEXT("the folder '%s' is not a culture the engine knows; the file is never loaded"), *InFile.Culture));
			}
			if (InDocument.bHasCulture && !InDocument.Culture.Equals(InFile.Culture, ESearchCase::IgnoreCase))
			{
				OutDiagnostics.Emplace(EDreamLocSeverity::Warning, DreamLocCode::CultureMismatch, 0, 0, FString::Printf(
					TEXT("the file says 'culture %s' but is in the %s folder; it is loaded as %s"), *InDocument.Culture, *InFile.Culture, *InFile.Culture));
			}
		}
		InDocument.ForEachEntry([&OutDiagnostics](const FString&, const FDreamLocEntry& InEntry, bool)
		{
			if (InEntry.IsTranslated() && !InEntry.Has(EDreamLocField::Src))
			{
				OutDiagnostics.Emplace(EDreamLocSeverity::Warning, DreamLocCode::MissingSource, InEntry.Line, InEntry.Column, FString::Printf(
					TEXT("'%s' has a text but no src; it is not loaded until it has one"), *InEntry.Key));
			}
		});
	}

	int32 RunCheck(const TArray<FSourceFile>& InFiles, const TArray<FString>& InRoots)
	{
		FRun Run;
		for (const FString& Root : InRoots)
		{
			const FString SettingsPath = Root / FDreamLocSettings::FileName;
			FString Text;
			if (IFileManager::Get().FileExists(*SettingsPath) && FFileHelper::LoadFileToString(Text, *SettingsPath))
			{
				TArray<FDreamLocDiagnostic> Diagnostics;
				FDreamLocSettings::Parse(Text, Diagnostics);
				Report(Run, SettingsPath, Diagnostics);
			}
		}
		for (const FSourceFile& File : InFiles)
		{
			++Run.Files;
			FDreamLocDocument Document;
			TArray<FDreamLocDiagnostic> Diagnostics;
			DreamLocFile::LoadFile(File.Path, Document, Diagnostics);
			CheckPlacement(File, Document, Diagnostics);
			Report(Run, File.Path, Diagnostics);
		}
		UE_LOG(LogDreamLocCommandlet, Display, TEXT("DreamLoc check: %d file(s), %d error(s), %d warning(s)"), Run.Files, Run.Errors, Run.Warnings);
		return Run.Errors > 0 ? 1 : 0;
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

	int32 RunGather()
	{
		const FDreamLocSettings Settings = FDreamLocSettings::LoadFromDirectory(FDreamLocTextSource::GetProjectDirectory());
		const DreamLocEngine::FGatherResult Result = DreamLocEngine::Gather(DreamLocEngine::MakeGatherOptions(Settings));
		ReportLines(Result.Messages);
		UE_LOG(LogDreamLocCommandlet, Display, TEXT("DreamLoc gather: %d text(s) in %d source file(s), %d manifest(s) written, %d deleted, %d error(s), %d warning(s)"),
			Result.Texts, Result.Sources, Result.ManifestsWritten, Result.ManifestsDeleted, CountErrors(Result.Messages), CountWarnings(Result.Messages));
		return Result.bSucceeded ? 0 : 1;
	}

	int32 RunSync(const FString& InCultures, bool bInDryRun)
	{
		DreamLocSync::FDirectoryOptions Options;
		Options.OwnerDirectory = FPaths::ProjectDir();
		Options.Cultures = SplitCultures(InCultures);
		Options.bDryRun = bInDryRun;
		const FDreamLocSyncOutput Output = DreamLocSync::SyncDirectory(Options);
		ReportLines(Output.Messages);
		ReportChanges(Output, FDreamLocTextSource::GetProjectDirectory(), bInDryRun);
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
		if (HasSwitch(TEXT("Gather")) && RunGather() != 0)
		{
			return 1;
		}
		return HasSwitch(TEXT("Sync")) ? RunSync(GetValue(TEXT("Culture")), HasSwitch(TEXT("DryRun"))) : 0;
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

	const TArray<FString> Roots = GetRoots(RootArgument);
	TArray<FSourceFile> Files;
	CollectFiles(GetValue(TEXT("File")), Roots, Files);
	if (HasSwitch(TEXT("Format")))
	{
		return RunFormat(Files, HasSwitch(TEXT("Write")), HasSwitch(TEXT("Verify")));
	}
	return RunCheck(Files, Roots);
}
