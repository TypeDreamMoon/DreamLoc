// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocTextSource.h"

#include "DreamLocFile.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/LocalizedTextSourceTypes.h"
#include "Internationalization/TextLocalizationResource.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

namespace DreamLocTextSourceLocal
{
	/** The folder name, under the project or a plugin. */
	const TCHAR* const DirectoryName = TEXT("DLoc");

	/** A layer's rank, clamped to what fits in the low six bits of a priority. */
	int32 ClampLayer(int32 InLayer)
	{
		return FMath::Clamp(InLayer, 0, 63);
	}

	void LogDiagnostics(const FString& InPath, TConstArrayView<FDreamLocDiagnostic> InDiagnostics)
	{
		// Warnings, not errors, even for errors in the file: the engine loads text on every culture change, and an
		// error logged there fails whatever automation test happens to be running. The file is skipped either way.
		for (const FDreamLocDiagnostic& Diagnostic : InDiagnostics)
		{
			UE_LOG(LogDreamLoc, Warning, TEXT("%s"), *Diagnostic.Format(InPath));
		}
	}
}

FDreamLocTextSource::FDreamLocTextSource() = default;

FDreamLocTextSource::FDreamLocTextSource(TArray<FDreamLocLayer> InLayers, const FDreamLocSettings& InSettings)
	: bFixed(true)
	, FixedLayers(MoveTemp(InLayers))
	, FixedSettings(InSettings)
{
	FixedLayers.StableSort([](const FDreamLocLayer& A, const FDreamLocLayer& B) { return A.Index < B.Index; });
}

int32 FDreamLocTextSource::GetPriority() const
{
	return ELocalizedTextSourcePriority::Highest;
}

int32 FDreamLocTextSource::MakeEntryPriority(int32 InCultureIndex, int32 InLayer)
{
	// The engine flips a source's priority into its entries' base, so the highest source starts at -1000.
	return -ELocalizedTextSourcePriority::Highest + InCultureIndex * 64 + (63 - DreamLocTextSourceLocal::ClampLayer(InLayer));
}

int32 FDreamLocTextSource::MakeSourceEntryPriority(int32 InLayer)
{
	return MAX_int32 - DreamLocTextSourceLocal::ClampLayer(InLayer);
}

FString FDreamLocTextSource::GetProjectDirectory()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / DreamLocTextSourceLocal::DirectoryName);
}

FString FDreamLocTextSource::GetBakedDirectory()
{
	FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("Localization") / BakedName);
	FPaths::NormalizeDirectoryName(Directory);
	return Directory;
}

bool FDreamLocTextSource::IsBakedDirectoryLoadedByEngine()
{
	const FString Baked = GetBakedDirectory();
	for (const FString& Path : FPaths::GetGameLocalizationPaths())
	{
		FString Full = FPaths::ConvertRelativePathToFull(Path);
		FPaths::NormalizeDirectoryName(Full);
		if (Full.Equals(Baked, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

bool FDreamLocTextSource::ReadBakedNative(const FString& InDirectory, FString& OutCulture, FString& OutNativeLocRes)
{
	FTextLocalizationMetaDataResource MetaData;
	const FString Path = InDirectory / (FString(BakedName) + TEXT(".locmeta"));
	if (!IFileManager::Get().FileExists(*Path) || !MetaData.LoadFromFile(Path))
	{
		return false;
	}
	OutCulture = MetaData.NativeCulture;
	OutNativeLocRes = MetaData.NativeLocRes.IsEmpty() ? FString() : InDirectory / MetaData.NativeLocRes;
	return !OutCulture.IsEmpty();
}

bool FDreamLocTextSource::IsKnownCulture(const FString& InName)
{
	// The engine's list does not change while it runs; made once, on first use.
	static const TSet<FString> Known = []()
	{
		TArray<FString> Names;
		FInternationalization::Get().GetCultureNames(Names);
		TSet<FString> Set;
		for (const FString& Name : Names)
		{
			Set.Add(Name.ToLower());
		}
		return Set;
	}();
	return !InName.IsEmpty() && Known.Contains(InName.ToLower());
}

TArray<FDreamLocLayer> FDreamLocTextSource::FindLayers()
{
	TArray<FDreamLocLayer> Layers;
	TArray<FDreamLocLayer> Mods;
	for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPlugins())
	{
		FString Directory = FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / DreamLocTextSourceLocal::DirectoryName);
		FPaths::NormalizeDirectoryName(Directory);
		if (IFileManager::Get().DirectoryExists(*Directory))
		{
			const bool bMod = Plugin->GetType() == EPluginType::Mod;
			FDreamLocLayer& Layer = (bMod ? Mods : Layers).AddDefaulted_GetRef();
			Layer.Index = bMod ? 2 : 0;
			Layer.Name = (bMod ? TEXT("Mod.") : TEXT("Plugin.")) + Plugin->GetName();
			Layer.Directory = Directory;
		}
	}
	// By name, so two runs over the same plugins load them in the same order; each mod over the ones before it.
	Layers.Sort([](const FDreamLocLayer& A, const FDreamLocLayer& B) { return A.Name < B.Name; });
	Mods.Sort([](const FDreamLocLayer& A, const FDreamLocLayer& B) { return A.Name < B.Name; });
	for (int32 Index = 0; Index < Mods.Num(); ++Index)
	{
		Mods[Index].Index = 2 + Index;
	}

	const FString ProjectDirectory = GetProjectDirectory();
	const FString BakedDirectory = GetBakedDirectory();
	FString NativeCulture;
	FString NativeLocRes;
	if (IFileManager::Get().DirectoryExists(*ProjectDirectory))
	{
		FDreamLocLayer& Layer = Layers.AddDefaulted_GetRef();
		Layer.Index = 1;
		Layer.Name = TEXT("Project");
		Layer.Directory = ProjectDirectory;
	}
	else if (ReadBakedNative(BakedDirectory, NativeCulture, NativeLocRes) && !IsBakedDirectoryLoadedByEngine())
	{
		FDreamLocLayer& Layer = Layers.AddDefaulted_GetRef();
		Layer.Index = 1;
		Layer.Name = TEXT("Project");
		Layer.Directory = BakedDirectory;
		Layer.bBaked = true;
	}
	Layers.Append(MoveTemp(Mods));
	return Layers;
}

TArray<FDreamLocLayer> FDreamLocTextSource::GetLayers() const
{
	FScopeLock ScopeLock(&Lock);
	return GetLayers_Locked();
}

TArray<FDreamLocLayer> FDreamLocTextSource::GetLayers_Locked() const
{
	if (bFixed)
	{
		return FixedLayers;
	}
	TArray<FDreamLocLayer> Layers = FindLayers();
	for (const FDreamLocLayer& Mounted : MountedLayers)
	{
		Layers.RemoveAll([&Mounted](const FDreamLocLayer& Found) { return Found.Directory.Equals(Mounted.Directory, ESearchCase::IgnoreCase); });
		Layers.Add(Mounted);
	}
	Layers.StableSort([](const FDreamLocLayer& A, const FDreamLocLayer& B) { return A.Index < B.Index; });
	return Layers;
}

void FDreamLocTextSource::MountLayer(const FDreamLocLayer& InLayer)
{
	FScopeLock ScopeLock(&Lock);
	FDreamLocLayer Layer = InLayer;
	Layer.Directory = FPaths::ConvertRelativePathToFull(Layer.Directory);
	FPaths::NormalizeDirectoryName(Layer.Directory);
	MountedLayers.RemoveAll([&Layer](const FDreamLocLayer& Existing) { return Existing.Directory.Equals(Layer.Directory, ESearchCase::IgnoreCase); });
	MountedLayers.Add(MoveTemp(Layer));
}

void FDreamLocTextSource::UnmountLayer(const FString& InDirectory)
{
	FScopeLock ScopeLock(&Lock);
	FString Directory = FPaths::ConvertRelativePathToFull(InDirectory);
	FPaths::NormalizeDirectoryName(Directory);
	MountedLayers.RemoveAll([&Directory](const FDreamLocLayer& Existing) { return Existing.Directory.Equals(Directory, ESearchCase::IgnoreCase); });
}

FDreamLocSettings FDreamLocTextSource::GetSettings() const
{
	FScopeLock ScopeLock(&Lock);
	return ReadSettings_Locked();
}

FDreamLocSettings FDreamLocTextSource::ReadSettings_Locked() const
{
	if (bFixed)
	{
		return FixedSettings;
	}
	const FString Path = GetProjectDirectory() / FDreamLocSettings::FileName;
	const FDateTime TimeStamp = IFileManager::Get().GetTimeStamp(*Path);
	if (!bSettingsCached || TimeStamp != CachedSettingsTimeStamp)
	{
		TArray<FDreamLocDiagnostic> Diagnostics;
		CachedSettings = FDreamLocSettings::LoadFromDirectory(GetProjectDirectory(), &Diagnostics);
		CachedSettingsTimeStamp = TimeStamp;
		bSettingsCached = true;
		DreamLocTextSourceLocal::LogDiagnostics(Path, Diagnostics);
	}
	return CachedSettings;
}

bool FDreamLocTextSource::IsInUse_Locked() const
{
	return bFixed || MountedLayers.Num() > 0 || GetLayers_Locked().Num() > 0;
}

bool FDreamLocTextSource::GetNativeCultureName(const ELocalizedTextSourceCategory InCategory, FString& OutNativeCultureName)
{
	FScopeLock ScopeLock(&Lock);
	// Only for a project that uses DreamLoc: one that has a localization target of its own and no DLoc/ keeps the
	// native culture its target says, and so does one shipped the Native way, where the engine reads the baked files.
	if (InCategory != ELocalizedTextSourceCategory::Game)
	{
		return false;
	}
	if (bFixed || IFileManager::Get().DirectoryExists(*GetProjectDirectory()))
	{
		OutNativeCultureName = ReadSettings_Locked().SourceCulture;
		return !OutNativeCultureName.IsEmpty();
	}
	for (const FDreamLocLayer& Layer : GetLayers_Locked())
	{
		FString NativeLocRes;
		if (Layer.bBaked && ReadBakedNative(Layer.Directory, OutNativeCultureName, NativeLocRes))
		{
			return true;
		}
	}
	return false;
}

void FDreamLocTextSource::GetLocalizedCultureNames(const ELocalizationLoadFlags InLoadFlags, TSet<FString>& OutLocalizedCultureNames)
{
	if (!ShouldLoadGame(InLoadFlags))
	{
		return;
	}
	FScopeLock ScopeLock(&Lock);
	if (!IsInUse_Locked())
	{
		return;
	}
	if (bFixed || IFileManager::Get().DirectoryExists(*GetProjectDirectory()))
	{
		const FDreamLocSettings Settings = ReadSettings_Locked();
		OutLocalizedCultureNames.Add(Settings.SourceCulture);
		for (const FString& Culture : Settings.TargetCultures)
		{
			OutLocalizedCultureNames.Add(Culture);
		}
	}
	// And every culture a layer has a folder for: a mod can bring a language the project does not list. A baked
	// folder's cultures are the ones it has a file for.
	for (const FDreamLocLayer& Layer : GetLayers_Locked())
	{
		IFileManager::Get().IterateDirectory(*Layer.Directory, [&OutLocalizedCultureNames, &Layer](const TCHAR* InPath, bool bInIsDirectory)
		{
			if (bInIsDirectory)
			{
				const FString Name = FPaths::GetCleanFilename(InPath);
				const bool bHasFile = !Layer.bBaked || IFileManager::Get().FileExists(*(FString(InPath) / (FString(BakedName) + TEXT(".locres"))));
				if (bHasFile && IsKnownCulture(Name))
				{
					OutLocalizedCultureNames.Add(Name);
				}
			}
			return true;
		});
	}
}

void FDreamLocTextSource::LoadLocalizedResources(const ELocalizationLoadFlags InLoadFlags, TArrayView<const FString> InPrioritizedCultures,
	FTextLocalizationResource& InOutNativeResource, FTextLocalizationResource& InOutLocalizedResource)
{
	if (!ShouldLoadGame(InLoadFlags))
	{
		return;
	}
	FScopeLock ScopeLock(&Lock);
	if (bShutdown)
	{
		return;
	}

#if WITH_EDITOR
	// In the editor the only loads that carry the Game flag are the game localization preview's (the editor's own
	// refresh asks for editor and engine text): what to load again when a file changes while the preview is on.
	if (GIsEditor && EnumHasAnyFlags(InLoadFlags, ELocalizationLoadFlags::Game))
	{
		LastPreviewCulture = InPrioritizedCultures.Num() > 0 ? InPrioritizedCultures[0] : FString();
	}
#endif

	const FDreamLocSettings Settings = ReadSettings_Locked();
	const TArray<FString> Cultures = Settings.ExpandFallbacks(InPrioritizedCultures);
	const TArray<FDreamLocLayer> Layers = GetLayers_Locked();

	for (int32 CultureIndex = 0; CultureIndex < Cultures.Num(); ++CultureIndex)
	{
		const FString& Culture = Cultures[CultureIndex];
		for (const FDreamLocLayer& Layer : Layers)
		{
			if (Layer.bBaked)
			{
				// What `dloc bake` made: one file per culture, each entry already under the hash it should match.
				const FString Path = Layer.Directory / Culture / (FString(BakedName) + TEXT(".locres"));
				if (IFileManager::Get().FileExists(*Path))
				{
					InOutLocalizedResource.LoadFromFile(Path, MakeEntryPriority(CultureIndex, Layer.Index));
				}
				continue;
			}
			const FString CultureDirectory = Layer.Directory / Culture;
			if (!IFileManager::Get().DirectoryExists(*CultureDirectory))
			{
				continue;
			}
			TArray<FString> Paths;
			IFileManager::Get().FindFilesRecursive(Paths, *CultureDirectory, TEXT("*.dloc"), /*Files*/ true, /*Directories*/ false, /*bClearFileNames*/ false);
			Paths.Sort();
			for (const FString& Path : Paths)
			{
				// `_terms.dloc` and any other file a tool keeps beside the translations.
				if (FPaths::GetCleanFilename(Path).StartsWith(TEXT("_")))
				{
					continue;
				}
				const TSharedPtr<FDreamLocDocument> Document = GetDocument_Locked(Path, Culture);
				if (!Document.IsValid() || Document->bTerms)
				{
					continue;
				}
				const FTextKey LocResId(Path);
				const int32 Priority = MakeEntryPriority(CultureIndex, Layer.Index);
				Document->ForEachEntry([this, &InOutLocalizedResource, &LocResId, &Layer, Priority](const FString& InNamespace, const FDreamLocEntry& InEntry, bool)
				{
					const FString* Source = InEntry.Find(EDreamLocField::Src);
					if (Source == nullptr)
					{
						return;
					}
					const FTextId TextId{ FTextKey(InNamespace), FTextKey(InEntry.Key) };
					if (FSeenText* Seen = SeenTexts.Find(TextId))
					{
						if (Layer.Index >= Seen->Layer)
						{
							Seen->Source = *Source;
							Seen->Layer = Layer.Index;
						}
					}
					else
					{
						SeenTexts.Add(TextId, FSeenText{ *Source, Layer.Index });
					}

					const FString* Translation = InEntry.Find(EDreamLocField::Text);
					if (Translation == nullptr)
					{
						return;
					}
					// A stale translation goes in under the source it was made from, so it never matches the text on
					// screen and the engine shows that text's own source instead.
					const FString* Previous = InEntry.Find(EDreamLocField::Prev);
					const uint32 SourceHash = FTextLocalizationResource::HashString(Previous != nullptr ? *Previous : *Source);
					InOutLocalizedResource.AddEntry(TextId.GetNamespace(), TextId.GetKey(), SourceHash, *Translation, Priority, LocResId);
				});
			}
		}
	}

	for (const TPair<FTextId, FSeenText>& Seen : SeenTexts)
	{
		InOutLocalizedResource.AddEntry(Seen.Key.GetNamespace(), Seen.Key.GetKey(), Seen.Value.Source, Seen.Value.Source,
			MakeSourceEntryPriority(Seen.Value.Layer));
	}
	// A baked folder's source-to-source entries are its source culture's file.
	for (const FDreamLocLayer& Layer : Layers)
	{
		FString NativeCulture;
		FString NativeLocRes;
		if (Layer.bBaked && ReadBakedNative(Layer.Directory, NativeCulture, NativeLocRes) && IFileManager::Get().FileExists(*NativeLocRes))
		{
			InOutLocalizedResource.LoadFromFile(NativeLocRes, MakeSourceEntryPriority(Layer.Index));
		}
	}
}

TSharedPtr<FDreamLocDocument> FDreamLocTextSource::GetDocument_Locked(const FString& InPath, const FString& InCulture)
{
	const FFileStatData Stat = IFileManager::Get().GetStatData(*InPath);
	if (!Stat.bIsValid)
	{
		Files.Remove(InPath);
		return nullptr;
	}
	if (const FCachedFile* Cached = Files.Find(InPath))
	{
		if (Cached->TimeStamp == Stat.ModificationTime && Cached->Size == Stat.FileSize)
		{
			return Cached->bUsable ? Cached->Document : nullptr;
		}
	}

	TSharedPtr<FDreamLocDocument> Document = MakeShared<FDreamLocDocument>();
	TArray<FDreamLocDiagnostic> Diagnostics;
	const bool bParsed = DreamLocFile::LoadFile(InPath, *Document, Diagnostics);
	if (Document->bHasCulture && !Document->Culture.Equals(InCulture, ESearchCase::IgnoreCase))
	{
		Diagnostics.Emplace(EDreamLocSeverity::Warning, DreamLocCode::CultureMismatch, 0, 0, FString::Printf(
			TEXT("the file says 'culture %s' but is in the %s folder; it is loaded as %s"), *Document->Culture, *InCulture, *InCulture));
	}
	Document->ForEachEntry([&Diagnostics](const FString&, const FDreamLocEntry& InEntry, bool)
	{
		if (InEntry.IsTranslated() && !InEntry.Has(EDreamLocField::Src))
		{
			Diagnostics.Emplace(EDreamLocSeverity::Warning, DreamLocCode::MissingSource, InEntry.Line, InEntry.Column, FString::Printf(
				TEXT("'%s' has a text but no src; it is not loaded until it has one"), *InEntry.Key));
		}
	});
	DreamLocTextSourceLocal::LogDiagnostics(InPath, Diagnostics);
	if (!bParsed)
	{
		UE_LOG(LogDreamLoc, Warning, TEXT("%s is not loaded until its errors are fixed."), *InPath);
	}

	FCachedFile& Entry = Files.FindOrAdd(InPath);
	Entry.TimeStamp = Stat.ModificationTime;
	Entry.Size = Stat.FileSize;
	Entry.bUsable = bParsed;
	Entry.Document = bParsed ? Document : nullptr;
	return Entry.Document;
}

void FDreamLocTextSource::Invalidate()
{
	FScopeLock ScopeLock(&Lock);
	Files.Reset();
	bSettingsCached = false;
}

FString FDreamLocTextSource::GetLastPreviewCulture() const
{
	FScopeLock ScopeLock(&Lock);
	return LastPreviewCulture;
}

void FDreamLocTextSource::Shutdown()
{
	FScopeLock ScopeLock(&Lock);
	bShutdown = true;
}
