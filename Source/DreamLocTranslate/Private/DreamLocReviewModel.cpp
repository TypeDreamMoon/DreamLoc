// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocReviewModel.h"

#include "DreamLocExtract.h"
#include "DreamLocSync.h"
#include "Features/IModularFeatures.h"
#include "Misc/Paths.h"

namespace DreamLocReviewModelLocal
{
	FString Stem(const FString& InFile)
	{
		return FPaths::GetBaseFilename(InFile, /*bRemovePath*/ false);
	}

	bool IsInFile(const FDreamLocReviewEntry& InEntry, const FString& InFile)
	{
		if (InFile.IsEmpty())
		{
			return true;
		}
		const FString File = Stem(InEntry.Entry.File);
		return InFile.EndsWith(TEXT("/")) ? File.StartsWith(InFile, ESearchCase::IgnoreCase) : File.Equals(InFile, ESearchCase::IgnoreCase);
	}

	bool Matches(const FDreamLocReviewEntry& InEntry, const FString& InSearch)
	{
		return InSearch.IsEmpty() || InEntry.Entry.Key.Contains(InSearch) || InEntry.Entry.Source.Contains(InSearch) || InEntry.Translation.Contains(InSearch)
			|| InEntry.Entry.Namespace.Contains(InSearch);
	}
}

FString FDreamLocReviewEntry::GetId() const
{
	FString Id = Entry.Namespace;
	Id.AppendChar(TCHAR(1));
	return Id + Entry.Key;
}

FDreamLocTranslateUnit FDreamLocReviewEntry::MakeUnit() const
{
	FDreamLocTranslateUnit Unit;
	Unit.Id = 1;
	Unit.File = Entry.File;
	Unit.Namespace = Entry.Namespace;
	Unit.Key = Entry.Key;
	Unit.Source = Entry.Source;
	Unit.Note = Entry.Note;
	Unit.Kind = Entry.Kind;
	Unit.At = Entry.At;
	Unit.Max = Entry.Max;
	Unit.Nearby = Entry.Nearby;
	Unit.Context = Entry.Context;
	Unit.Terms = Entry.Terms;
	Unit.Keeps = Entry.Keeps;
	Unit.Prev = Entry.Prev;
	Unit.PreviousText = Entry.PreviousText;
	return Unit;
}

void FDreamLocReviewProgress::Add(EDreamLocReviewStatus InStatus)
{
	switch (InStatus)
	{
	case EDreamLocReviewStatus::Done: ++Done; break;
	case EDreamLocReviewStatus::Review: ++Review; break;
	case EDreamLocReviewStatus::Stale: ++Stale; break;
	default: ++Untranslated; break;
	}
}

void FDreamLocReviewModel::Load(const FString& InDLocDirectory, const FString& InOwnerDirectory, const FString& InCulture, const FString& InReferenceCulture)
{
	DLocDirectory = InDLocDirectory;
	OwnerDirectory = InOwnerDirectory;
	Culture = InCulture;
	ReferenceCulture = InReferenceCulture == InCulture ? FString() : InReferenceCulture;
	Reload();
}

void FDreamLocReviewModel::Reload()
{
	Entries.Reset();
	ById.Reset();
	Messages.Reset();
	Terms = FDreamLocTerms();
	TArray<FDreamLocDiagnostic> Diagnostics;
	Settings = FDreamLocSettings::LoadFromDirectory(DLocDirectory, &Diagnostics);
	if (Culture.IsEmpty())
	{
		return;
	}
	TArray<FString> Cultures = { Culture };
	if (!ReferenceCulture.IsEmpty())
	{
		Cultures.Add(ReferenceCulture);
	}
	DreamLocExchange::FExportOptions Options;
	Options.Culture = Culture;
	Options.Only = { TEXT("all") };
	if (!ReferenceCulture.IsEmpty())
	{
		Options.References = { ReferenceCulture };
	}
	TArray<FDreamLocReportLine> Lines;
	const FDreamLocExchangeBatch Batch = DreamLocExchange::Collect(DreamLocSync::ReadFiles(DLocDirectory, Cultures), Options, Settings.SourceCulture, Lines);
	for (const FDreamLocReportLine& Line : Lines)
	{
		Messages.Add(Line.Format());
	}
	Terms = Batch.Terms;
	for (const FDreamLocExchangeEntry& Collected : Batch.Entries)
	{
		const TSharedPtr<FDreamLocReviewEntry> Item = MakeShared<FDreamLocReviewEntry>();
		Item->Entry = Collected;
		Item->Status = StatusOf(Collected);
		Item->Translation = Item->Status == EDreamLocReviewStatus::Stale ? Collected.PreviousText : Collected.Text;
		for (const TPair<FString, FString>& Reference : Collected.References)
		{
			if (Reference.Key == ReferenceCulture)
			{
				Item->Reference = Reference.Value;
			}
		}
		if (!Item->Translation.IsEmpty())
		{
			Item->Problems = DreamLocTranslateFlow::CheckContent(Item->MakeUnit(), Item->Translation, Culture, Terms);
		}
		ById.Add(Item->GetId(), Item);
		Entries.Add(Item);
	}
}

uint32 FDreamLocReviewModel::GetSignature() const
{
	uint32 Crc = FCrc::StrCrc32(*Culture, FCrc::StrCrc32(*ReferenceCulture));
	for (const TSharedPtr<FDreamLocReviewEntry>& Item : Entries)
	{
		const FDreamLocExchangeEntry& Entry = Item->Entry;
		const FString* const Fields[] = { &Entry.File, &Entry.Namespace, &Entry.Key, &Entry.Source, &Entry.Prev, &Entry.State, &Entry.Note, &Entry.At, &Entry.Kind,
			&Item->Translation, &Item->Reference };
		for (const FString* Field : Fields)
		{
			Crc = FCrc::StrCrc32(**Field, Crc);
			Crc = FCrc::MemCrc32("|", 1, Crc);
		}
		const int32 Max = Entry.Max;
		const uint8 Status = uint8(Item->Status);
		Crc = FCrc::MemCrc32(&Max, sizeof(Max), Crc);
		Crc = FCrc::MemCrc32(&Status, sizeof(Status), Crc);
	}
	return Crc;
}

EDreamLocReviewStatus FDreamLocReviewModel::StatusOf(const FDreamLocExchangeEntry& InEntry)
{
	if (!InEntry.Prev.IsEmpty())
	{
		return EDreamLocReviewStatus::Stale;
	}
	if (InEntry.Text.IsEmpty())
	{
		return EDreamLocReviewStatus::Untranslated;
	}
	return InEntry.State.Equals(TEXT("done"), ESearchCase::IgnoreCase) ? EDreamLocReviewStatus::Done : EDreamLocReviewStatus::Review;
}

TArray<FDreamLocCultureProgress> FDreamLocReviewModel::Overview(const FString& InDLocDirectory)
{
	TArray<FDreamLocCultureProgress> Out;
	const FDreamLocSettings Read = FDreamLocSettings::LoadFromDirectory(InDLocDirectory);
	const TMap<FString, TMap<FString, FString>> Files = DreamLocSync::ReadFiles(InDLocDirectory, Read.TargetCultures);
	for (const FString& Each : Read.TargetCultures)
	{
		DreamLocExchange::FExportOptions Options;
		Options.Culture = Each;
		Options.Only = { TEXT("all") };
		TArray<FDreamLocReportLine> Lines;
		const FDreamLocExchangeBatch Batch = DreamLocExchange::Collect(Files, Options, Read.SourceCulture, Lines);
		FDreamLocCultureProgress& Progress = Out.AddDefaulted_GetRef();
		Progress.Culture = Each;
		TMap<FString, FDreamLocReviewProgress> ByFile;
		for (const FDreamLocExchangeEntry& Entry : Batch.Entries)
		{
			const EDreamLocReviewStatus Status = StatusOf(Entry);
			Progress.Total.Add(Status);
			ByFile.FindOrAdd(DreamLocReviewModelLocal::Stem(Entry.File)).Add(Status);
		}
		ByFile.KeySort([](const FString& A, const FString& B) { return A < B; });
		for (const TPair<FString, FDreamLocReviewProgress>& File : ByFile)
		{
			Progress.Files.Emplace(File.Key, File.Value);
		}
	}
	return Out;
}

TOptional<FDreamLocFitResult> FDreamLocReviewModel::Measure(const FDreamLocReviewEntry& InEntry, const FString& InText) const
{
	if (InEntry.Entry.At.IsEmpty() || InText.IsEmpty() || !IsInGameThread())
	{
		return {};
	}
	const FString Source = DreamLocExtract::GetAtSource(InEntry.Entry.At);
	for (IDreamLocContextProvider* Provider : IModularFeatures::Get().GetModularFeatureImplementations<IDreamLocContextProvider>(IDreamLocContextProvider::GetModularFeatureName()))
	{
		if (Provider == nullptr || !Provider->HandlesSource(Source))
		{
			continue;
		}
		FDreamLocFitRequest Request;
		Request.Namespace = InEntry.Entry.Namespace;
		Request.Key = InEntry.Entry.Key;
		Request.At = InEntry.Entry.At;
		Request.OwnerDirectory = OwnerDirectory;
		Request.Culture = Culture;
		Request.Text = InText;
		FDreamLocFitResult Result;
		if (Provider->MeasureTranslation(Request, Result) && Result.bMeasured)
		{
			return Result;
		}
	}
	return {};
}

bool FDreamLocReviewModel::Passes(const FDreamLocReviewEntry& InEntry, EDreamLocReviewFilter InFilter)
{
	switch (InFilter)
	{
	case EDreamLocReviewFilter::Untranslated: return InEntry.Status == EDreamLocReviewStatus::Untranslated;
	case EDreamLocReviewFilter::Stale: return InEntry.Status == EDreamLocReviewStatus::Stale;
	case EDreamLocReviewFilter::Review: return InEntry.Status == EDreamLocReviewStatus::Review;
	case EDreamLocReviewFilter::Problems: return InEntry.HasProblems();
	case EDreamLocReviewFilter::Done: return InEntry.Status == EDreamLocReviewStatus::Done;
	default: return true;
	}
}

TArray<TSharedPtr<FDreamLocReviewEntry>> FDreamLocReviewModel::Filter(EDreamLocReviewFilter InFilter, const FString& InSearch, const FString& InFile) const
{
	using namespace DreamLocReviewModelLocal;
	return Entries.FilterByPredicate([&](const TSharedPtr<FDreamLocReviewEntry>& InEntry)
	{
		return Passes(*InEntry, InFilter) && IsInFile(*InEntry, InFile) && Matches(*InEntry, InSearch);
	});
}

int32 FDreamLocReviewModel::Count(EDreamLocReviewFilter InFilter, const FString& InSearch, const FString& InFile) const
{
	using namespace DreamLocReviewModelLocal;
	int32 Out = 0;
	for (const TSharedPtr<FDreamLocReviewEntry>& Entry : Entries)
	{
		Out += Passes(*Entry, InFilter) && IsInFile(*Entry, InFile) && Matches(*Entry, InSearch) ? 1 : 0;
	}
	return Out;
}

FDreamLocReviewProgress FDreamLocReviewModel::GetProgress(const FString& InFile) const
{
	FDreamLocReviewProgress Out;
	for (const TSharedPtr<FDreamLocReviewEntry>& Entry : Entries)
	{
		if (DreamLocReviewModelLocal::IsInFile(*Entry, InFile))
		{
			Out.Add(Entry->Status);
		}
	}
	return Out;
}

TArray<FString> FDreamLocReviewModel::GetFiles() const
{
	TSet<FString> Files;
	for (const TSharedPtr<FDreamLocReviewEntry>& Entry : Entries)
	{
		Files.Add(DreamLocReviewModelLocal::Stem(Entry->Entry.File));
	}
	TArray<FString> Out = Files.Array();
	Out.Sort();
	return Out;
}

TSharedPtr<FDreamLocReviewEntry> FDreamLocReviewModel::Find(const FString& InNamespace, const FString& InKey) const
{
	FString Id = InNamespace;
	Id.AppendChar(TCHAR(1));
	const TSharedPtr<FDreamLocReviewEntry>* Found = ById.Find(Id + InKey);
	return Found != nullptr ? *Found : nullptr;
}

TArray<FString> FDreamLocReviewModel::FindReviewed(const FString& InSource, const FDreamLocReviewEntry* InExcept) const
{
	TArray<FString> Out;
	for (const TSharedPtr<FDreamLocReviewEntry>& Entry : Entries)
	{
		if (Entry.Get() != InExcept && Entry->Status == EDreamLocReviewStatus::Done && Entry->Entry.Source.Equals(InSource, ESearchCase::CaseSensitive))
		{
			Out.AddUnique(Entry->Translation);
		}
	}
	return Out;
}

FString FDreamLocReviewModel::Check(const FDreamLocReviewEntry& InEntry, const FString& InText) const
{
	return InText.IsEmpty() ? FString() : DreamLocTranslateFlow::CheckTranslation(InEntry.MakeUnit(), InText, Culture, Terms, OwnerDirectory);
}

DreamLocTranslateFlow::FReviewEdit FDreamLocReviewModel::MakeEdit(const FDreamLocReviewEntry& InEntry) const
{
	DreamLocTranslateFlow::FReviewEdit Edit;
	Edit.Culture = Culture;
	Edit.File = InEntry.Entry.File;
	Edit.Namespace = InEntry.Entry.Namespace;
	Edit.Key = InEntry.Entry.Key;
	return Edit;
}

bool FDreamLocReviewModel::Apply(TConstArrayView<DreamLocTranslateFlow::FReviewEdit> InEdits, TArray<FString>& OutErrors)
{
	const bool bWritten = DreamLocTranslateFlow::ApplyReview(DLocDirectory, InEdits, OutErrors);
	Reload();
	return bWritten;
}
