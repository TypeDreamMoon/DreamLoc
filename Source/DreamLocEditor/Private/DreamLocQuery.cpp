// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocQuery.h"

#include "DreamLocFile.h"
#include "DreamLocModule.h"
#include "DreamLocTextSource.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

namespace DreamLocQueryLocal
{
	struct FEntryText
	{
		FString Text;
		int32 Line = 0;
		bool bStale = false;
	};

	/** One .dloc as last read: namespace + U+0001 + key -> its translation. */
	struct FReading
	{
		FDateTime TimeStamp;
		int64 Size = -1;
		TMap<FString, FEntryText> Texts;
	};

	FCriticalSection Lock;
	TMap<FString, FReading> Readings;

	FString IdOf(const FString& InNamespace, const FString& InKey)
	{
		return InNamespace + TEXT("\x01") + InKey;
	}

	const FReading& Read(const FString& InPath)
	{
		IFileManager& Files = IFileManager::Get();
		const FDateTime TimeStamp = Files.GetTimeStamp(*InPath);
		const int64 Size = Files.FileSize(*InPath);
		FReading& Reading = Readings.FindOrAdd(InPath);
		if (Reading.TimeStamp == TimeStamp && Reading.Size == Size)
		{
			return Reading;
		}
		Reading = FReading();
		Reading.TimeStamp = TimeStamp;
		Reading.Size = Size;
		FDreamLocDocument Document;
		TArray<FDreamLocDiagnostic> Diagnostics;
		DreamLocFile::LoadFile(InPath, Document, Diagnostics);
		if (Document.bTerms)
		{
			return Reading;
		}
		Document.ForEachEntry([&Reading](const FString& InNamespace, const FDreamLocEntry& InEntry, bool)
		{
			if (const FString* Text = InEntry.Find(EDreamLocField::Text))
			{
				FEntryText& Entry = Reading.Texts.FindOrAdd(IdOf(InNamespace, InEntry.Key));
				Entry.Text = *Text;
				Entry.Line = InEntry.Fields[(int32)EDreamLocField::Text].Line;
				Entry.bStale = InEntry.IsStale();
			}
		});
		return Reading;
	}
}

TMap<FString, TArray<DreamLocQuery::FTranslation>> DreamLocQuery::FindTranslationsIn(TConstArrayView<FString> InDLocDirectories, const FString& InNamespace,
	TConstArrayView<FString> InKeys)
{
	using namespace DreamLocQueryLocal;
	TMap<FString, TArray<FTranslation>> Out;
	FScopeLock ScopeLock(&Lock);
	for (const FString& Directory : InDLocDirectories)
	{
		TArray<FString> Cultures;
		IFileManager::Get().FindFiles(Cultures, *(Directory / TEXT("*")), /*Files*/ false, /*Directories*/ true);
		Cultures.Sort();
		for (const FString& Culture : Cultures)
		{
			TArray<FString> Paths;
			IFileManager::Get().FindFilesRecursive(Paths, *(Directory / Culture), *(FString(TEXT("*")) + DreamLocFile::Extension), true, false);
			Paths.Sort();
			for (const FString& Path : Paths)
			{
				const FReading& Reading = Read(Path);
				for (const FString& Key : InKeys)
				{
					const FEntryText* Found = Reading.Texts.Find(IdOf(InNamespace, Key));
					if (Found == nullptr)
					{
						continue;
					}
					// A higher layer's (a later directory's) translation replaces a lower one's of the same culture.
					TArray<FTranslation>& Translations = Out.FindOrAdd(Key);
					FTranslation* Existing = Translations.FindByPredicate([&Culture](const FTranslation& In) { return In.Culture.Equals(Culture, ESearchCase::IgnoreCase); });
					FTranslation& Translation = Existing != nullptr ? *Existing : Translations.AddDefaulted_GetRef();
					Translation.Culture = Culture;
					Translation.Text = Found->Text;
					Translation.File = Path;
					Translation.Line = Found->Line;
					Translation.bStale = Found->bStale;
				}
			}
		}
	}
	return Out;
}

TMap<FString, TArray<DreamLocQuery::FTranslation>> DreamLocQuery::FindTranslations(const FString& InNamespace, TConstArrayView<FString> InKeys)
{
	// The text source's layers when it runs, so a DLoc folder mounted while the editor runs counts too; else what is found.
	TSharedPtr<FDreamLocTextSource> Source = FDreamLocModule::IsAvailable() ? FDreamLocModule::Get().GetTextSource() : nullptr;
	TArray<FString> Directories;
	for (const FDreamLocLayer& Layer : Source.IsValid() ? Source->GetLayers() : FDreamLocTextSource::FindLayers())
	{
		if (!Layer.bBaked)
		{
			Directories.Add(Layer.Directory);
		}
	}
	return FindTranslationsIn(Directories, InNamespace, InKeys);
}
