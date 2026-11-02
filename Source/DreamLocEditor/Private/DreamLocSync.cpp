// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocSync.h"

#include "DreamLocFile.h"
#include "DreamLocPublish.h"
#include "DreamLocSettings.h"
#include "DreamLocTextSource.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace DreamLocSyncLocal
{
	/** A text's identity: namespace and key, compared exactly. */
	struct FId
	{
		FString Namespace;
		FString Key;

		bool operator==(const FId& InOther) const
		{
			return Namespace.Equals(InOther.Namespace, ESearchCase::CaseSensitive) && Key.Equals(InOther.Key, ESearchCase::CaseSensitive);
		}
		bool operator<(const FId& InOther) const
		{
			const int32 Compare = Namespace.Compare(InOther.Namespace, ESearchCase::CaseSensitive);
			return Compare != 0 ? Compare < 0 : Key.Compare(InOther.Key, ESearchCase::CaseSensitive) < 0;
		}
		friend uint32 GetTypeHash(const FId& InId)
		{
			return HashCombine(GetTypeHash(InId.Namespace), GetTypeHash(InId.Key));
		}
	};

	FString Describe(const FId& InId)
	{
		return FString::Printf(TEXT("'%s' (namespace %s)"), *InId.Key, *InId.Namespace);
	}

	bool SameText(const FString& InA, const FString& InB)
	{
		return InA.Equals(InB, ESearchCase::CaseSensitive);
	}

	FString Shorten(const FString& InText)
	{
		return InText.Len() > 40 ? InText.Left(40) + TEXT("...") : InText;
	}

	/** One text a manifest has, in the order the manifests list them. */
	struct FCurrent
	{
		FId Id;
		FDreamLocExtractEntry Entry;
		int32 Unit = INDEX_NONE;
	};

	/** The .dloc one source (or several with the same mirror path) writes. */
	struct FUnit
	{
		FString Provider;
		FString Source;
		FString DlocPath;
		/** Namespaces in order, each with its texts (indices into the current texts) in order. */
		TArray<TPair<FString, TArray<int32>>> Namespaces;

		TArray<int32>& FindOrAddNamespace(const FString& InNamespace)
		{
			for (TPair<FString, TArray<int32>>& Pair : Namespaces)
			{
				if (SameText(Pair.Key, InNamespace))
				{
					return Pair.Value;
				}
			}
			return Namespaces.Emplace_GetRef(InNamespace, TArray<int32>()).Value;
		}
	};

	struct FLocation
	{
		int32 File = INDEX_NONE;
		/** In the document's items. */
		int32 Item = INDEX_NONE;
		/** In a removed block's children; none for a live entry. */
		int32 Child = INDEX_NONE;

		bool IsRemoved() const { return Child != INDEX_NONE; }
		bool operator==(const FLocation& InOther) const { return File == InOther.File && Item == InOther.Item && Child == InOther.Child; }
		friend uint32 GetTypeHash(const FLocation& InLocation)
		{
			return HashCombine(HashCombine(::GetTypeHash(InLocation.File), ::GetTypeHash(InLocation.Item)), ::GetTypeHash(InLocation.Child));
		}
	};

	struct FFile
	{
		FString Path;
		FString Text;
		FDreamLocDocument Document;
	};

	/** Where an entry is and the namespace in force there. */
	struct FIndexed
	{
		FLocation Location;
		FId Id;
	};

	struct FCultureState
	{
		FString Name;
		TArray<FFile> Files;
		TMap<FString, int32> FileByPath;
		TMap<FId, FLocation> Live;
		TMap<FId, FLocation> Removed;
		/** Every live entry in file order, duplicates included. */
		TArray<FIndexed> LiveOrder;
		/** Entries whose translation went to a current text. */
		TSet<FLocation> Consumed;
		/** Live entries that go into their file's removed block. */
		TSet<FLocation> ToRemoved;

		const FDreamLocEntry& GetEntry(const FLocation& InLocation) const
		{
			const FDreamLocItem& Item = Files[InLocation.File].Document.Items[InLocation.Item];
			return InLocation.IsRemoved() ? Item.Children[InLocation.Child].Entry : Item.Entry;
		}
	};

	/** What sync knew of a text before this run: from the first file of the first culture that has it. */
	struct FPrevious
	{
		FString Source;
		FString At;
		FString Path;
	};

	void IndexFile(FCultureState& InOutCulture, int32 InFile)
	{
		const FDreamLocDocument& Document = InOutCulture.Files[InFile].Document;
		FString Namespace;
		for (int32 ItemIndex = 0; ItemIndex < Document.Items.Num(); ++ItemIndex)
		{
			const FDreamLocItem& Item = Document.Items[ItemIndex];
			if (Item.Kind == EDreamLocItemKind::Namespace)
			{
				Namespace = Item.Namespace;
			}
			else if (Item.Kind == EDreamLocItemKind::Entry)
			{
				const FId Id{ Namespace, Item.Entry.Key };
				const FLocation Location{ InFile, ItemIndex, INDEX_NONE };
				InOutCulture.LiveOrder.Add(FIndexed{ Location, Id });
				if (!InOutCulture.Live.Contains(Id))
				{
					InOutCulture.Live.Add(Id, Location);
				}
			}
			else if (Item.Kind == EDreamLocItemKind::Removed)
			{
				FString BlockNamespace = Namespace;
				for (int32 ChildIndex = 0; ChildIndex < Item.Children.Num(); ++ChildIndex)
				{
					const FDreamLocItem& Child = Item.Children[ChildIndex];
					if (Child.Kind == EDreamLocItemKind::Namespace)
					{
						BlockNamespace = Child.Namespace;
					}
					else if (Child.Kind == EDreamLocItemKind::Entry)
					{
						const FId Id{ BlockNamespace, Child.Entry.Key };
						if (!InOutCulture.Removed.Contains(Id))
						{
							InOutCulture.Removed.Add(Id, FLocation{ InFile, ItemIndex, ChildIndex });
						}
					}
				}
			}
		}
	}

	/** A file sync never writes: a terms file, or one whose name starts with `_`. */
	bool IsSkippedPath(const FString& InPath)
	{
		return FPaths::GetCleanFilename(InPath).StartsWith(TEXT("_"));
	}

	/** Reads every culture's files. False when one does not read: the report says which, and nothing may be decided. */
	bool ReadCultures(const TMap<FString, TMap<FString, FString>>& InFiles, TConstArrayView<FString> InCultures, const FString& InDLocName,
		TArray<FCultureState>& OutCultures, TArray<FDreamLocReportLine>& OutMessages)
	{
		bool bAllRead = true;
		for (const FString& CultureName : InCultures)
		{
			FCultureState& Culture = OutCultures.AddDefaulted_GetRef();
			Culture.Name = CultureName;
			const TMap<FString, FString>* Files = InFiles.Find(CultureName);
			if (Files == nullptr)
			{
				continue;
			}
			TArray<FString> Paths;
			Files->GetKeys(Paths);
			Paths.Sort();
			for (const FString& Path : Paths)
			{
				if (IsSkippedPath(Path))
				{
					continue;
				}
				FFile File;
				File.Path = Path;
				File.Text = Files->FindChecked(Path);
				TArray<FDreamLocDiagnostic> Diagnostics;
				const bool bParsed = DreamLocFile::Parse(File.Text, File.Document, Diagnostics);
				const FString ReportName = InDLocName / CultureName / Path;
				for (const FDreamLocDiagnostic& Diagnostic : Diagnostics)
				{
					if (Diagnostic.IsError())
					{
						OutMessages.Add(FDreamLocReportLine{ ReportName, Diagnostic });
					}
				}
				if (!bParsed)
				{
					bAllRead = false;
					continue;
				}
				if (File.Document.bTerms)
				{
					continue;
				}
				const int32 Index = Culture.Files.Add(MoveTemp(File));
				Culture.FileByPath.Add(Path, Index);
				IndexFile(Culture, Index);
			}
		}
		return bAllRead;
	}

	FString LastSegment(const FString& InKey)
	{
		int32 Dot = INDEX_NONE;
		return InKey.FindLastChar(TEXT('.'), Dot) ? InKey.RightChop(Dot + 1) : InKey;
	}

	/** 1 for the same text, 0 for nothing in common: one minus the edit distance over the longer length. */
	float Similarity(const FString& InA, const FString& InB)
	{
		const int32 LengthA = InA.Len();
		const int32 LengthB = InB.Len();
		if (LengthA == 0 || LengthB == 0 || LengthA > 256 || LengthB > 256)
		{
			return 0.0f;
		}
		TArray<int32> Previous;
		TArray<int32> Row;
		Previous.SetNumUninitialized(LengthB + 1);
		Row.SetNumUninitialized(LengthB + 1);
		for (int32 Index = 0; Index <= LengthB; ++Index)
		{
			Previous[Index] = Index;
		}
		for (int32 IndexA = 1; IndexA <= LengthA; ++IndexA)
		{
			Row[0] = IndexA;
			for (int32 IndexB = 1; IndexB <= LengthB; ++IndexB)
			{
				const int32 Cost = InA[IndexA - 1] == InB[IndexB - 1] ? 0 : 1;
				Row[IndexB] = FMath::Min3(Previous[IndexB] + 1, Row[IndexB - 1] + 1, Previous[IndexB - 1] + Cost);
			}
			Swap(Previous, Row);
		}
		return 1.0f - static_cast<float>(Previous[LengthB]) / static_cast<float>(FMath::Max(LengthA, LengthB));
	}

	void SetOrRemove(FDreamLocEntry& InOutEntry, EDreamLocField InField, const FString& InValue)
	{
		if (InValue.IsEmpty())
		{
			InOutEntry.Remove(InField);
		}
		else
		{
			InOutEntry.Set(InField, InValue);
		}
	}

	void ClearPositions(FDreamLocEntry& InOutEntry)
	{
		InOutEntry.Line = 0;
		InOutEntry.Column = 0;
		for (FDreamLocFieldValue& Field : InOutEntry.Fields)
		{
			Field.Line = 0;
			Field.Column = 0;
		}
	}

	FDreamLocItem MakeNamespaceItem(const FString& InNamespace)
	{
		FDreamLocItem Item;
		Item.Kind = EDreamLocItemKind::Namespace;
		Item.Namespace = InNamespace;
		return Item;
	}

	FDreamLocItem MakeEntryItem(FDreamLocEntry&& InEntry)
	{
		FDreamLocItem Item;
		Item.Kind = EDreamLocItemKind::Entry;
		Item.Entry = MoveTemp(InEntry);
		ClearPositions(Item.Entry);
		return Item;
	}

	/** Appends entries grouped by namespace, writing a namespace line only where it changes. */
	void AppendGrouped(TArray<FDreamLocItem>& OutItems, FString& InOutLastNamespace, bool& InOutHasNamespace,
		const FString& InNamespace, FDreamLocEntry&& InEntry, const FDreamLocItem* InNamespaceItem = nullptr)
	{
		if (!InOutHasNamespace || !SameText(InOutLastNamespace, InNamespace))
		{
			FDreamLocItem Namespace = InNamespaceItem != nullptr ? *InNamespaceItem : MakeNamespaceItem(InNamespace);
			Namespace.Line = 0;
			Namespace.Column = 0;
			OutItems.Add(MoveTemp(Namespace));
			InOutLastNamespace = InNamespace;
			InOutHasNamespace = true;
		}
		OutItems.Add(MakeEntryItem(MoveTemp(InEntry)));
	}

	FDreamLocDocument MakeDocument(const FFile* InBase, const FString& InCulture)
	{
		FDreamLocDocument Document;
		if (InBase != nullptr)
		{
			Document.HeaderComments = InBase->Document.HeaderComments;
			Document.VersionComment = InBase->Document.VersionComment;
			Document.CultureComment = InBase->Document.CultureComment;
			Document.EndComments = InBase->Document.EndComments;
		}
		Document.Version = DreamLocFile::FormatVersion;
		Document.bHasCulture = true;
		Document.Culture = InCulture;
		return Document;
	}

	bool HasEntries(const TArray<FDreamLocItem>& InItems)
	{
		return InItems.ContainsByPredicate([](const FDreamLocItem& InItem) { return InItem.Kind == EDreamLocItemKind::Entry; });
	}

	FString JoinKeys(const TArray<FString>& InKeys)
	{
		TArray<FString> Shown;
		for (int32 Index = 0; Index < InKeys.Num() && Index < 5; ++Index)
		{
			Shown.Add(InKeys[Index]);
		}
		FString Joined = FString::Join(Shown, TEXT(", "));
		if (InKeys.Num() > Shown.Num())
		{
			Joined += FString::Printf(TEXT(" and %d more"), InKeys.Num() - Shown.Num());
		}
		return Joined;
	}

	void AddMessage(TArray<FDreamLocReportLine>& OutMessages, const FString& InFile, EDreamLocSeverity InSeverity, int32 InCode, const FString& InText)
	{
		OutMessages.Add(FDreamLocReportLine{ InFile, FDreamLocDiagnostic(InSeverity, InCode, 0, 0, InText) });
	}

	bool IsInScope(const FString& InSource, TConstArrayView<FString> InScope)
	{
		if (InScope.Num() == 0)
		{
			return true;
		}
		for (const FString& Prefix : InScope)
		{
			if (InSource.StartsWith(Prefix, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}
}

int32 FDreamLocSyncOutput::NumErrors() const
{
	int32 Count = 0;
	for (const FDreamLocReportLine& Line : Messages)
	{
		Count += Line.Diagnostic.Severity == EDreamLocSeverity::Error ? 1 : 0;
	}
	return Count;
}

int32 FDreamLocSyncOutput::NumWarnings() const
{
	int32 Count = 0;
	for (const FDreamLocReportLine& Line : Messages)
	{
		Count += Line.Diagnostic.Severity == EDreamLocSeverity::Warning ? 1 : 0;
	}
	return Count;
}

int32 FDreamLocSyncOutput::NumWrites() const
{
	int32 Count = 0;
	for (const TPair<FString, TMap<FString, TOptional<FString>>>& Culture : Changes)
	{
		for (const TPair<FString, TOptional<FString>>& Change : Culture.Value)
		{
			Count += Change.Value.IsSet() ? 1 : 0;
		}
	}
	return Count;
}

int32 FDreamLocSyncOutput::NumDeletes() const
{
	int32 Count = 0;
	for (const TPair<FString, TMap<FString, TOptional<FString>>>& Culture : Changes)
	{
		for (const TPair<FString, TOptional<FString>>& Change : Culture.Value)
		{
			Count += Change.Value.IsSet() ? 0 : 1;
		}
	}
	return Count;
}

FDreamLocSyncOutput DreamLocSync::Run(const FDreamLocSyncInput& InInput)
{
	using namespace DreamLocSyncLocal;
	FDreamLocSyncOutput Output;
	auto SourceName = [&InInput](const FString& InSource) { return InInput.SourceRoot.IsEmpty() ? InSource : InInput.SourceRoot / InSource; };

	if (InInput.Cultures.Num() == 0)
	{
		Output.bBlocked = true;
		AddMessage(Output.Messages, InInput.DLocName / FDreamLocSettings::FileName, EDreamLocSeverity::Error, DreamLocCode::SyncBlocked,
			TEXT("there is no culture to sync: list them in TargetCultures"));
		return Output;
	}

	// ---- What the sources have now.
	TArray<FDreamLocExtractManifest> Manifests = InInput.Manifests;
	Manifests.StableSort([](const FDreamLocExtractManifest& InA, const FDreamLocExtractManifest& InB)
	{
		// The engine's gather also finds the texts of assets other tools generated: those tools' manifests come first.
		const bool bEngineA = InA.Provider == DreamLocExtract::EngineProvider;
		const bool bEngineB = InB.Provider == DreamLocExtract::EngineProvider;
		return bEngineA != bEngineB ? bEngineB : InA.Source < InB.Source;
	});

	TArray<FUnit> Units;
	TMap<FString, int32> UnitByPath;
	TSet<FString> UnitSources;
	TArray<FCurrent> Current;
	TMap<FId, int32> CurrentById;
	TSet<FString> CurrentNamespaces;
	for (const FDreamLocExtractManifest& Manifest : Manifests)
	{
		const FString DlocPath = DreamLocExtract::GetDlocPath(Manifest);
		int32 UnitIndex = UnitByPath.FindRef(DlocPath, INDEX_NONE);
		if (UnitIndex == INDEX_NONE)
		{
			UnitIndex = Units.AddDefaulted();
			Units[UnitIndex].Provider = Manifest.Provider;
			Units[UnitIndex].Source = Manifest.Source;
			Units[UnitIndex].DlocPath = DlocPath;
			UnitByPath.Add(DlocPath, UnitIndex);
		}
		UnitSources.Add(Manifest.Source);
		for (const FDreamLocExtractNamespace& Namespace : Manifest.Namespaces)
		{
			for (const FDreamLocExtractEntry& Entry : Namespace.Entries)
			{
				const FId Id{ Namespace.Namespace, Entry.Key };
				if (const int32* Existing = CurrentById.Find(Id))
				{
					const FCurrent& First = Current[*Existing];
					if (!SameText(First.Entry.Source, Entry.Source))
					{
						AddMessage(Output.Messages, SourceName(Manifest.Source), EDreamLocSeverity::Warning, DreamLocCode::SourceConflict, FString::Printf(
							TEXT("%s is \"%s\" here but \"%s\" in %s; the one in %s is kept"), *Describe(Id), *Shorten(Entry.Source),
							*Shorten(First.Entry.Source), *Units[First.Unit].Source, *Units[First.Unit].Source));
					}
					continue;
				}
				const int32 Index = Current.Add(FCurrent{ Id, Entry, UnitIndex });
				CurrentById.Add(Id, Index);
				Units[UnitIndex].FindOrAddNamespace(Namespace.Namespace).Add(Index);
				CurrentNamespaces.Add(Namespace.Namespace);
			}
		}
	}

	// ---- What the files have.
	TArray<FCultureState> Cultures;
	if (!ReadCultures(InInput.Files, InInput.Cultures, InInput.DLocName, Cultures, Output.Messages))
	{
		Output.bBlocked = true;
		AddMessage(Output.Messages, InInput.DLocName, EDreamLocSeverity::Error, DreamLocCode::SyncBlocked,
			TEXT("nothing was written: a .dloc above does not read (dloc check shows every error)"));
		return Output;
	}

	TMap<FId, FPrevious> Previous;
	TArray<FId> PreviousOrder;
	TSet<FString> PreviousNamespaces;
	for (const FCultureState& Culture : Cultures)
	{
		for (const FIndexed& Indexed : Culture.LiveOrder)
		{
			if (Previous.Contains(Indexed.Id))
			{
				continue;
			}
			const FDreamLocEntry& Entry = Culture.GetEntry(Indexed.Location);
			FPrevious& Info = Previous.Add(Indexed.Id);
			Info.Source = Entry.Find(EDreamLocField::Src) != nullptr ? *Entry.Find(EDreamLocField::Src) : FString();
			Info.At = Entry.Find(EDreamLocField::At) != nullptr ? *Entry.Find(EDreamLocField::At) : FString();
			Info.Path = Culture.Files[Indexed.Location.File].Path;
			PreviousOrder.Add(Indexed.Id);
			PreviousNamespaces.Add(Indexed.Id.Namespace);
		}
	}

	// ---- Which vanished texts sync may decide about: those it wrote (they have an `at`), whose source was read again
	// or is gone. The translations of a source that exists but was not read stay as they are.
	TMap<FId, FString> Vanished;
	TArray<FId> VanishedOrder;
	TMap<FString, int32> NotExtracted;
	for (const FId& Id : PreviousOrder)
	{
		if (CurrentById.Contains(Id))
		{
			continue;
		}
		const FPrevious& Info = Previous.FindChecked(Id);
		if (Info.At.IsEmpty())
		{
			continue;
		}
		const FString AtSource = DreamLocExtract::GetAtSource(Info.At);
		if (!IsInScope(AtSource, InInput.SourceScope))
		{
			continue;
		}
		const bool bRead = UnitSources.Contains(AtSource);
		const bool bGone = !bRead && !(InInput.SourceExists && InInput.SourceExists(AtSource));
		const bool bComplete = Info.Path.StartsWith(TEXT("Engine/")) && InInput.CompleteProviders.Contains(DreamLocExtract::EngineProvider);
		if (bRead || bGone || bComplete)
		{
			Vanished.Add(Id, Info.Source);
			VanishedOrder.Add(Id);
		}
		else
		{
			++NotExtracted.FindOrAdd(AtSource);
		}
	}
	for (const TPair<FString, int32>& Pair : NotExtracted)
	{
		AddMessage(Output.Messages, SourceName(Pair.Key), EDreamLocSeverity::Warning, DreamLocCode::SourceNotExtracted, FString::Printf(
			TEXT("%d translation(s) of this source are left as they are: it has no extraction manifest (compile it, then sync again)"), Pair.Value));
	}

	TArray<int32> NewOrder;
	for (int32 Index = 0; Index < Current.Num(); ++Index)
	{
		if (!Previous.Contains(Current[Index].Id))
		{
			NewOrder.Add(Index);
		}
	}

	// ---- Renames: new key -> the vanished key whose translation it takes.
	TMap<FId, FId> Renames;
	TSet<FId> RenamedFrom;
	auto AddRename = [&Renames, &RenamedFrom](const FId& InNew, const FId& InOld)
	{
		Renames.Add(InNew, InOld);
		RenamedFrom.Add(InOld);
	};

	// What the tool knows for certain.
	for (const int32 Index : NewOrder)
	{
		const FCurrent& Text = Current[Index];
		if (Text.Entry.RenamedFrom.IsEmpty())
		{
			continue;
		}
		const FId Old{ Text.Id.Namespace, Text.Entry.RenamedFrom };
		if (Vanished.Contains(Old) && !RenamedFrom.Contains(Old))
		{
			AddRename(Text.Id, Old);
		}
	}

	// Whole namespaces: one that is gone, most of whose (key, text) pairs are in one that is new.
	TMap<FString, FString> NamespaceMoves;
	{
		TMap<FString, TArray<FId>> OldGroups;
		TArray<FString> OldOrder;
		for (const FId& Id : VanishedOrder)
		{
			if (RenamedFrom.Contains(Id) || CurrentNamespaces.Contains(Id.Namespace))
			{
				continue;
			}
			if (!OldGroups.Contains(Id.Namespace))
			{
				OldOrder.Add(Id.Namespace);
			}
			OldGroups.FindOrAdd(Id.Namespace).Add(Id);
		}
		TMap<FString, TMap<FString, FString>> NewGroups;
		TArray<FString> NewNamespaceOrder;
		for (const int32 Index : NewOrder)
		{
			const FCurrent& Text = Current[Index];
			if (Renames.Contains(Text.Id) || PreviousNamespaces.Contains(Text.Id.Namespace))
			{
				continue;
			}
			if (!NewGroups.Contains(Text.Id.Namespace))
			{
				NewNamespaceOrder.Add(Text.Id.Namespace);
			}
			NewGroups.FindOrAdd(Text.Id.Namespace).Add(Text.Id.Key, Text.Entry.Source);
		}

		struct FCandidate
		{
			FString Old;
			FString New;
			int32 Matches = 0;
			int32 Total = 0;
			float Ratio = 0.0f;
		};
		TArray<FCandidate> Candidates;
		for (const FString& OldNamespace : OldOrder)
		{
			const TArray<FId>& Olds = OldGroups.FindChecked(OldNamespace);
			FCandidate Best;
			for (const FString& NewNamespace : NewNamespaceOrder)
			{
				const TMap<FString, FString>& News = NewGroups.FindChecked(NewNamespace);
				int32 Matches = 0;
				for (const FId& Old : Olds)
				{
					const FString* NewSource = News.Find(Old.Key);
					Matches += NewSource != nullptr && SameText(*NewSource, Vanished.FindChecked(Old)) ? 1 : 0;
				}
				const float Ratio = static_cast<float>(Matches) / static_cast<float>(Olds.Num());
				if (Ratio > Best.Ratio)
				{
					Best = FCandidate{ OldNamespace, NewNamespace, Matches, Olds.Num(), Ratio };
				}
			}
			if (Best.Matches > 0 && Best.Ratio + KINDA_SMALL_NUMBER >= InInput.NamespaceThreshold)
			{
				Candidates.Add(Best);
			}
			else if (Best.Matches > 0)
			{
				const FCurrent& Example = Current[CurrentById.FindChecked(FId{ Best.New, NewGroups.FindChecked(Best.New).CreateConstIterator()->Key })];
				AddMessage(Output.Messages, SourceName(Units[Example.Unit].Source), EDreamLocSeverity::Warning, DreamLocCode::RenameCandidates, FString::Printf(
					TEXT("namespace %s is gone and %s is new, but only %d of its %d texts match (%.0f%% is needed); nothing moved"),
					*Best.Old, *Best.New, Best.Matches, Best.Total, InInput.NamespaceThreshold * 100.0f));
			}
		}
		Candidates.StableSort([](const FCandidate& InA, const FCandidate& InB) { return InA.Ratio > InB.Ratio; });
		TSet<FString> TakenNew;
		for (const FCandidate& Candidate : Candidates)
		{
			const FCurrent* Example = nullptr;
			for (const int32 Index : NewOrder)
			{
				if (SameText(Current[Index].Id.Namespace, Candidate.New))
				{
					Example = &Current[Index];
					break;
				}
			}
			const FString Where = Example != nullptr ? SourceName(Units[Example->Unit].Source) : InInput.DLocName;
			if (TakenNew.Contains(Candidate.New))
			{
				AddMessage(Output.Messages, Where, EDreamLocSeverity::Warning, DreamLocCode::RenameCandidates, FString::Printf(
					TEXT("namespace %s is gone and most of it is in %s, but another namespace already moved there; nothing moved"),
					*Candidate.Old, *Candidate.New));
				continue;
			}
			TakenNew.Add(Candidate.New);
			NamespaceMoves.Add(Candidate.Old, Candidate.New);
			int32 Moved = 0;
			for (const FId& Old : OldGroups.FindChecked(Candidate.Old))
			{
				const FId New{ Candidate.New, Old.Key };
				if (const int32* Index = CurrentById.Find(New); Index != nullptr && !Previous.Contains(New) && !Renames.Contains(New))
				{
					AddRename(New, Old);
					++Moved;
				}
			}
			AddMessage(Output.Messages, Where, EDreamLocSeverity::Info, DreamLocCode::NamespaceMoved, FString::Printf(
				TEXT("namespace %s is now %s (%d of its %d texts match); %d translation(s) moved"),
				*Candidate.Old, *Candidate.New, Candidate.Matches, Candidate.Total, Moved));
		}
	}

	// One vanished key and one new key of the same namespace with the same text.
	auto EffectiveNamespace = [&NamespaceMoves](const FString& InNamespace)
	{
		const FString* Moved = NamespaceMoves.Find(InNamespace);
		return Moved != nullptr ? *Moved : InNamespace;
	};
	{
		struct FGroup
		{
			TArray<FId> Old;
			TArray<int32> New;
		};
		TMap<FId, FGroup> Groups;
		TArray<FId> GroupOrder;
		for (const FId& Id : VanishedOrder)
		{
			if (RenamedFrom.Contains(Id))
			{
				continue;
			}
			const FId GroupId{ EffectiveNamespace(Id.Namespace), Vanished.FindChecked(Id) };
			if (!Groups.Contains(GroupId))
			{
				GroupOrder.Add(GroupId);
			}
			Groups.FindOrAdd(GroupId).Old.Add(Id);
		}
		for (const int32 Index : NewOrder)
		{
			const FCurrent& Text = Current[Index];
			if (Renames.Contains(Text.Id))
			{
				continue;
			}
			if (FGroup* Group = Groups.Find(FId{ Text.Id.Namespace, Text.Entry.Source }))
			{
				Group->New.Add(Index);
			}
		}
		for (const FId& GroupId : GroupOrder)
		{
			const FGroup& Group = Groups.FindChecked(GroupId);
			if (Group.New.Num() == 0)
			{
				continue;
			}
			const FCurrent& First = Current[Group.New[0]];
			const FString Where = SourceName(Units[First.Unit].Source);
			if (Group.Old.Num() == 1 && Group.New.Num() == 1)
			{
				AddRename(First.Id, Group.Old[0]);
				AddMessage(Output.Messages, Where, EDreamLocSeverity::Info, DreamLocCode::RenameInferred, FString::Printf(
					TEXT("'%s' is now '%s' (the same text \"%s\"); its translations moved"), *Group.Old[0].Key, *First.Id.Key, *Shorten(GroupId.Key)));
				continue;
			}
			TArray<FString> OldKeys;
			for (const FId& Old : Group.Old)
			{
				OldKeys.Add(Old.Key);
			}
			TArray<FString> NewKeys;
			for (const int32 Index : Group.New)
			{
				NewKeys.Add(Current[Index].Id.Key);
			}
			AddMessage(Output.Messages, Where, EDreamLocSeverity::Warning, DreamLocCode::RenameCandidates, FString::Printf(
				TEXT("the text \"%s\" disappeared as %s and appeared as %s in namespace %s; nothing moved: dloc migrate -From <old key> -To <new key> moves one"),
				*Shorten(GroupId.Key), *JoinKeys(OldKeys), *JoinKeys(NewKeys), *GroupId.Namespace));
		}
	}

	// Similar texts: only a hint.
	for (const FId& Id : VanishedOrder)
	{
		if (RenamedFrom.Contains(Id))
		{
			continue;
		}
		const FString Namespace = EffectiveNamespace(Id.Namespace);
		const FString& OldSource = Vanished.FindChecked(Id);
		TArray<FString> Similar;
		const FCurrent* Example = nullptr;
		for (const int32 Index : NewOrder)
		{
			const FCurrent& Text = Current[Index];
			if (Renames.Contains(Text.Id) || !SameText(Text.Id.Namespace, Namespace) || !SameText(LastSegment(Text.Id.Key), LastSegment(Id.Key))
				|| SameText(Text.Entry.Source, OldSource) || Similarity(Text.Entry.Source, OldSource) < 0.5f)
			{
				continue;
			}
			Similar.Add(FString::Printf(TEXT("'%s' (\"%s\")"), *Text.Id.Key, *Shorten(Text.Entry.Source)));
			Example = Example != nullptr ? Example : &Text;
		}
		if (Example != nullptr)
		{
			AddMessage(Output.Messages, SourceName(Units[Example->Unit].Source), EDreamLocSeverity::Warning, DreamLocCode::RenameCandidates, FString::Printf(
				TEXT("'%s' (\"%s\") disappeared; it may be %s; nothing moved: dloc migrate -Namespace \"%s\" -From %s -To %s moves it"),
				*Id.Key, *Shorten(OldSource), *JoinKeys(Similar), *Id.Namespace, *Id.Key, *Example->Id.Key));
		}
	}

	// ---- A shipped key that is gone and became nothing: a translation players have would stop showing.
	if (InInput.Frozen.Num() > 0)
	{
		for (const FId& Id : VanishedOrder)
		{
			if (!RenamedFrom.Contains(Id) && InInput.Frozen.Contains(Id.Namespace + TEXT("\x01") + Id.Key))
			{
				Output.bBlocked = true;
				const FPrevious& Info = Previous.FindChecked(Id);
				AddMessage(Output.Messages, SourceName(DreamLocExtract::GetAtSource(Info.At)), EDreamLocSeverity::Error, DreamLocCode::FrozenKeyGone, FString::Printf(
					TEXT("%s was frozen as shipped and is gone from the source; bring it back, rename it with (was: ...), or run dloc freeze again to let it go"),
					*Describe(Id)));
			}
		}
		if (Output.bBlocked)
		{
			AddMessage(Output.Messages, InInput.DLocName, EDreamLocSeverity::Error, DreamLocCode::SyncBlocked, TEXT("nothing was written: a frozen key is gone"));
			return Output;
		}
	}

	// ---- Each culture: who takes which entry, then the files.
	// A key numbered by position whose source changed under a translation: said once, whichever culture has it.
	TSet<FString> ShiftReported;
	for (FCultureState& Culture : Cultures)
	{
		struct FPlanned
		{
			int32 Current = INDEX_NONE;
			TOptional<FLocation> From;
		};
		TArray<TArray<FPlanned>> Plans;
		Plans.SetNum(Units.Num());
		for (int32 UnitIndex = 0; UnitIndex < Units.Num(); ++UnitIndex)
		{
			for (const TPair<FString, TArray<int32>>& Namespace : Units[UnitIndex].Namespaces)
			{
				for (const int32 Index : Namespace.Value)
				{
					FPlanned& Plan = Plans[UnitIndex].AddDefaulted_GetRef();
					Plan.Current = Index;
					const FId& Id = Current[Index].Id;
					if (const FLocation* Live = Culture.Live.Find(Id))
					{
						Plan.From = *Live;
						Output.Moved += Culture.Files[Live->File].Path != Units[UnitIndex].DlocPath ? 1 : 0;
					}
					else if (const FId* Old = Renames.Find(Id); Old != nullptr && Culture.Live.Contains(*Old))
					{
						Plan.From = Culture.Live.FindChecked(*Old);
						++Output.Migrated;
					}
					else if (const FLocation* Removed = Culture.Removed.Find(Id))
					{
						Plan.From = *Removed;
						++Output.Revived;
					}
					else
					{
						++Output.Added;
					}
					if (Plan.From.IsSet())
					{
						Culture.Consumed.Add(Plan.From.GetValue());
					}
				}
			}
		}

		// Live entries nothing took: a vanished text of sync's goes into its file's removed block, and so does a second
		// copy of a text sync writes elsewhere.
		TSet<FId> SeenLive;
		for (const FIndexed& Indexed : Culture.LiveOrder)
		{
			const bool bFirst = !SeenLive.Contains(Indexed.Id);
			SeenLive.Add(Indexed.Id);
			if (Culture.Consumed.Contains(Indexed.Location))
			{
				continue;
			}
			const bool bHasAt = Culture.GetEntry(Indexed.Location).Has(EDreamLocField::At);
			if ((bFirst && Vanished.Contains(Indexed.Id)) || (!bFirst && bHasAt && CurrentById.Contains(Indexed.Id)))
			{
				Culture.ToRemoved.Add(Indexed.Location);
			}
		}

		TArray<FString> Paths;
		for (const FFile& File : Culture.Files)
		{
			Paths.AddUnique(File.Path);
		}
		for (const FUnit& Unit : Units)
		{
			Paths.AddUnique(Unit.DlocPath);
		}
		Paths.Sort();

		for (const FString& Path : Paths)
		{
			const int32 FileIndex = Culture.FileByPath.FindRef(Path, INDEX_NONE);
			const FFile* Base = FileIndex != INDEX_NONE ? &Culture.Files[FileIndex] : nullptr;
			const int32 UnitIndex = UnitByPath.FindRef(Path, INDEX_NONE);
			const FString ReportName = InInput.DLocName / Culture.Name / Path;
			FDreamLocDocument Document = MakeDocument(Base, Culture.Name);

			// Namespace lines of the old file, to keep their comments.
			TMap<FString, const FDreamLocItem*> BaseNamespaces;
			if (Base != nullptr)
			{
				for (const FDreamLocItem& Item : Base->Document.Items)
				{
					if (Item.Kind == EDreamLocItemKind::Namespace && !BaseNamespaces.Contains(Item.Namespace))
					{
						BaseNamespaces.Add(Item.Namespace, &Item);
					}
				}
			}

			FString LastNamespace;
			bool bHasNamespace = false;
			if (UnitIndex != INDEX_NONE)
			{
				const FUnit& Unit = Units[UnitIndex];
				for (const FPlanned& Plan : Plans[UnitIndex])
				{
					const FCurrent& Text = Current[Plan.Current];
					FDreamLocEntry Entry = Plan.From.IsSet() ? Culture.GetEntry(Plan.From.GetValue()) : FDreamLocEntry();
					Entry.Key = Text.Id.Key;
					Entry.Set(EDreamLocField::At, DreamLocExtract::MakeAt(Unit.Source, Text.Entry.At));
					SetOrRemove(Entry, EDreamLocField::Note, Text.Entry.Note);
					SetOrRemove(Entry, EDreamLocField::Max, Text.Entry.Max >= 0 ? FString::FromInt(Text.Entry.Max) : FString());
					const FString* OldSource = Entry.Find(EDreamLocField::Src);
					if (OldSource != nullptr && !SameText(*OldSource, Text.Entry.Source))
					{
						if (!Entry.IsTranslated())
						{
							Entry.Remove(EDreamLocField::Prev);
						}
						else if (!Entry.Has(EDreamLocField::Prev))
						{
							Entry.Set(EDreamLocField::Prev, *OldSource);
							++Output.Outdated;
						}
					}
					if (const FString* Prev = Entry.Find(EDreamLocField::Prev); Prev != nullptr && SameText(*Prev, Text.Entry.Source))
					{
						// The source is back to what the translation was made from.
						Entry.Remove(EDreamLocField::Prev);
					}
					// A key numbered by position with a translation of another source: maybe not an edit but a shift.
					const FString* MadeFrom = Entry.Find(EDreamLocField::Prev);
					const FString ShiftId = Text.Id.Namespace + TEXT("\x01") + Text.Id.Key;
					if (!Text.Entry.bStable && Entry.IsTranslated() && MadeFrom != nullptr && !ShiftReported.Contains(ShiftId))
					{
						ShiftReported.Add(ShiftId);
						AddMessage(Output.Messages, SourceName(Unit.Source), EDreamLocSeverity::Warning, DreamLocCode::PositionalKeyChanged, FString::Printf(
							TEXT("'%s' (namespace %s) is numbered by its position, and its translation was made from \"%s\", now \"%s\": if a text was added ")
							TEXT("or removed before it, the translations after it sit on their neighbours' texts. Give the texts ids of their own in the source ")
							TEXT("tool (DreamNarrative: dnc freeze)"), *Text.Id.Key, *Text.Id.Namespace, **MadeFrom, *Text.Entry.Source));
					}
					Entry.Set(EDreamLocField::Src, Text.Entry.Source);
					AppendGrouped(Document.Items, LastNamespace, bHasNamespace, Text.Id.Namespace, MoveTemp(Entry), BaseNamespaces.FindRef(Text.Id.Namespace));
				}
			}

			// What else the old file has that stays live: entries written by hand, and texts of sources not read.
			TArray<FString> RemovedKeys;
			TArray<TPair<FString, FDreamLocEntry>> RemovedEntries;
			if (Base != nullptr)
			{
				for (const FIndexed& Indexed : Culture.LiveOrder)
				{
					if (Indexed.Location.File != FileIndex || Culture.Consumed.Contains(Indexed.Location))
					{
						continue;
					}
					if (Culture.ToRemoved.Contains(Indexed.Location))
					{
						RemovedEntries.Emplace(Indexed.Id.Namespace, Culture.GetEntry(Indexed.Location));
						RemovedKeys.Add(Indexed.Id.Key);
						continue;
					}
					AppendGrouped(Document.Items, LastNamespace, bHasNamespace, Indexed.Id.Namespace, FDreamLocEntry(Culture.GetEntry(Indexed.Location)),
						BaseNamespaces.FindRef(Indexed.Id.Namespace));
				}
			}

			// The removed block: what it had, less what came back, then what went in now.
			TArray<TPair<FString, FDreamLocEntry>> Kept;
			const FDreamLocItem* FirstRemoved = nullptr;
			if (Base != nullptr)
			{
				FString Namespace;
				for (int32 ItemIndex = 0; ItemIndex < Base->Document.Items.Num(); ++ItemIndex)
				{
					const FDreamLocItem& Item = Base->Document.Items[ItemIndex];
					if (Item.Kind == EDreamLocItemKind::Namespace)
					{
						Namespace = Item.Namespace;
						continue;
					}
					if (Item.Kind != EDreamLocItemKind::Removed)
					{
						continue;
					}
					FirstRemoved = FirstRemoved != nullptr ? FirstRemoved : &Item;
					FString BlockNamespace = Namespace;
					for (int32 ChildIndex = 0; ChildIndex < Item.Children.Num(); ++ChildIndex)
					{
						const FDreamLocItem& Child = Item.Children[ChildIndex];
						if (Child.Kind == EDreamLocItemKind::Namespace)
						{
							BlockNamespace = Child.Namespace;
						}
						else if (Child.Kind == EDreamLocItemKind::Entry && !Culture.Consumed.Contains(FLocation{ FileIndex, ItemIndex, ChildIndex }))
						{
							Kept.Emplace(BlockNamespace, Child.Entry);
						}
					}
				}
			}
			Kept.Append(MoveTemp(RemovedEntries));
			if (Kept.Num() > 0)
			{
				FDreamLocItem Block;
				Block.Kind = EDreamLocItemKind::Removed;
				if (FirstRemoved != nullptr)
				{
					Block.Comments = FirstRemoved->Comments;
					Block.TrailingComment = FirstRemoved->TrailingComment;
					Block.ClosingComments = FirstRemoved->ClosingComments;
				}
				FString BlockNamespace;
				bool bBlockHasNamespace = false;
				for (TPair<FString, FDreamLocEntry>& Pair : Kept)
				{
					AppendGrouped(Block.Children, BlockNamespace, bBlockHasNamespace, Pair.Key, MoveTemp(Pair.Value));
				}
				Document.Items.Add(MoveTemp(Block));
			}
			if (RemovedKeys.Num() > 0)
			{
				Output.Removed += RemovedKeys.Num();
				AddMessage(Output.Messages, ReportName, EDreamLocSeverity::Info, DreamLocCode::MovedToRemoved, FString::Printf(
					TEXT("%d text(s) the source no longer has moved to the removed block: %s"), RemovedKeys.Num(), *JoinKeys(RemovedKeys)));
			}

			const bool bEmpty = !HasEntries(Document.Items) && Kept.Num() == 0;
			if (bEmpty)
			{
				if (Base != nullptr)
				{
					Output.Changes.FindOrAdd(Culture.Name).Add(Path, TOptional<FString>());
				}
				continue;
			}
			FString Text = DreamLocFile::Write(Document);
			if (Base == nullptr || Text != Base->Text)
			{
				Output.Changes.FindOrAdd(Culture.Name).Add(Path, MoveTemp(Text));
			}
		}
	}
	return Output;
}

TMap<FString, TMap<FString, FString>> DreamLocSync::ReadFiles(const FString& InDLocDirectory, TConstArrayView<FString> InCultures)
{
	TMap<FString, TMap<FString, FString>> Files;
	for (const FString& Culture : InCultures)
	{
		TMap<FString, FString>& CultureFiles = Files.Add(Culture);
		const FString Directory = InDLocDirectory / Culture;
		if (!IFileManager::Get().DirectoryExists(*Directory))
		{
			continue;
		}
		TArray<FString> Paths;
		IFileManager::Get().FindFilesRecursive(Paths, *Directory, TEXT("*.dloc"), /*Files*/ true, /*Directories*/ false);
		for (FString& Path : Paths)
		{
			FPaths::NormalizeFilename(Path);
			FString Relative = Path;
			FPaths::MakePathRelativeTo(Relative, *(Directory + TEXT("/")));
			FString Text;
			if (FFileHelper::LoadFileToString(Text, *Path))
			{
				CultureFiles.Add(Relative, MoveTemp(Text));
			}
		}
	}
	return Files;
}

bool DreamLocSync::ApplyChanges(const FString& InDLocDirectory, const FDreamLocSyncOutput& InOutput, TArray<FDreamLocReportLine>& OutMessages)
{
	bool bAllWritten = true;
	for (const TPair<FString, TMap<FString, TOptional<FString>>>& Culture : InOutput.Changes)
	{
		const FString CultureDirectory = InDLocDirectory / Culture.Key;
		for (const TPair<FString, TOptional<FString>>& Change : Culture.Value)
		{
			const FString Path = CultureDirectory / Change.Key;
			if (Change.Value.IsSet())
			{
				IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), /*Tree*/ true);
				if (!FFileHelper::SaveStringToFile(Change.Value.GetValue(), *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
				{
					bAllWritten = false;
					OutMessages.Add(FDreamLocReportLine{ Path, FDreamLocDiagnostic(EDreamLocSeverity::Error, DreamLocCode::SyncBlocked, 0, 0,
						TEXT("the file cannot be written")) });
				}
				continue;
			}
			if (!IFileManager::Get().Delete(*Path, /*RequireExists*/ false, /*EvenReadOnly*/ false, /*Quiet*/ true))
			{
				bAllWritten = false;
				OutMessages.Add(FDreamLocReportLine{ Path, FDreamLocDiagnostic(EDreamLocSeverity::Error, DreamLocCode::SyncBlocked, 0, 0,
					TEXT("the file cannot be deleted")) });
				continue;
			}
			// Folders the file leaves empty go with it, up to the culture's folder.
			FString Directory = FPaths::GetPath(Path);
			while (Directory.Len() > CultureDirectory.Len() && Directory.StartsWith(CultureDirectory))
			{
				TArray<FString> Left;
				IFileManager::Get().FindFiles(Left, *(Directory / TEXT("*")), /*Files*/ true, /*Directories*/ true);
				if (Left.Num() > 0 || !IFileManager::Get().DeleteDirectory(*Directory, /*RequireExists*/ false, /*Tree*/ false))
				{
					break;
				}
				Directory = FPaths::GetPath(Directory);
			}
		}
	}
	return bAllWritten;
}

FDreamLocSyncOutput DreamLocSync::SyncDirectory(const FDirectoryOptions& InOptions)
{
	using namespace DreamLocSyncLocal;
	FDreamLocSyncOutput Output;
	FString Owner = FPaths::ConvertRelativePathToFull(InOptions.OwnerDirectory);
	FPaths::NormalizeDirectoryName(Owner);
	FString DLocDirectory = InOptions.DLocDirectory.IsEmpty() ? Owner / TEXT("DLoc") : FPaths::ConvertRelativePathToFull(InOptions.DLocDirectory);
	FPaths::NormalizeDirectoryName(DLocDirectory);
	const FString ExtractDirectory = InOptions.ExtractDirectory.IsEmpty() ? DreamLocExtract::GetExtractDirectory(Owner)
		: FPaths::ConvertRelativePathToFull(InOptions.ExtractDirectory);

	FDreamLocSyncInput Input;
	Input.DLocName = DLocDirectory;
	Input.SourceRoot = Owner;
	Input.Cultures = InOptions.Cultures;
	if (Input.Cultures.Num() == 0)
	{
		TArray<FDreamLocDiagnostic> Diagnostics;
		Input.Cultures = FDreamLocSettings::LoadFromDirectory(DLocDirectory, &Diagnostics).TargetCultures;
	}
	for (const FString& Culture : Input.Cultures)
	{
		if (!FDreamLocTextSource::IsKnownCulture(Culture))
		{
			Output.bBlocked = true;
			AddMessage(Output.Messages, DLocDirectory / FDreamLocSettings::FileName, EDreamLocSeverity::Error, DreamLocCode::SyncBlocked,
				FString::Printf(TEXT("'%s' is not a culture the engine knows; nothing was written"), *Culture));
		}
	}
	if (Output.bBlocked)
	{
		return Output;
	}

	for (const FString& Path : DreamLocExtract::FindManifestFiles(ExtractDirectory))
	{
		FDreamLocExtractManifest Manifest;
		FString Error;
		if (!DreamLocExtract::LoadFile(Path, Manifest, Error))
		{
			Output.bBlocked = true;
			AddMessage(Output.Messages, Path, EDreamLocSeverity::Error, DreamLocCode::ManifestUnreadable, Error + TEXT("; nothing was written"));
			continue;
		}
		if (!IsInScope(Manifest.Source, InOptions.SourceFilter))
		{
			continue;
		}
		const FString SourcePath = Owner / Manifest.Source;
		if (!IFileManager::Get().FileExists(*SourcePath))
		{
			// A source deleted since it was compiled: its texts are gone with it.
			continue;
		}
		if (Manifest.Provider != DreamLocExtract::EngineProvider
			&& IFileManager::Get().GetTimeStamp(*SourcePath) > Manifest.TimeStamp + FTimespan::FromSeconds(1.0))
		{
			AddMessage(Output.Messages, SourcePath, EDreamLocSeverity::Warning, DreamLocCode::ManifestOutOfDate,
				TEXT("the source changed after its extraction manifest was written; compile it again to sync what it says now"));
		}
		Input.Manifests.Add(MoveTemp(Manifest));
	}
	if (Output.bBlocked)
	{
		return Output;
	}
	if (IFileManager::Get().FileExists(*(ExtractDirectory / DreamLocExtract::EngineProvider / DreamLocExtract::GatherStampName)))
	{
		Input.CompleteProviders.Add(DreamLocExtract::EngineProvider);
	}
	Input.SourceExists = [Owner](const FString& InSource) { return IFileManager::Get().FileExists(*(Owner / InSource)); };
	Input.SourceScope = InOptions.SourceFilter;
	Input.Files = ReadFiles(DLocDirectory, Input.Cultures);
	Input.Frozen = DreamLocPublish::ReadFrozen(DLocDirectory);

	TArray<FDreamLocReportLine> Early = MoveTemp(Output.Messages);
	Output = Run(Input);
	Early.Append(MoveTemp(Output.Messages));
	Output.Messages = MoveTemp(Early);
	if (!Output.bBlocked && !InOptions.bDryRun)
	{
		ApplyChanges(DLocDirectory, Output, Output.Messages);
	}
	return Output;
}

FDreamLocSyncOutput DreamLocSync::Migrate(const TMap<FString, TMap<FString, FString>>& InFiles, const FMigrateRequest& InRequest, const FString& InDLocName)
{
	using namespace DreamLocSyncLocal;
	FDreamLocSyncOutput Output;
	TArray<FString> CultureNames;
	InFiles.GetKeys(CultureNames);
	CultureNames.Sort();
	TArray<FCultureState> Cultures;
	if (!ReadCultures(InFiles, CultureNames, InDLocName, Cultures, Output.Messages))
	{
		Output.bBlocked = true;
		AddMessage(Output.Messages, InDLocName, EDreamLocSeverity::Error, DreamLocCode::SyncBlocked,
			TEXT("nothing was written: a .dloc above does not read (dloc check shows every error)"));
		return Output;
	}

	int32 FoundIn = 0;
	for (FCultureState& Culture : Cultures)
	{
		const FString Where = FString::Printf(TEXT("%s/%s"), *InDLocName, *Culture.Name);
		// The old entry: a removed one first, since that is where a translation sync could not place waits.
		TArray<TPair<FId, FLocation>> Matches;
		for (const TMap<FId, FLocation>* Map : { &Culture.Removed, &Culture.Live })
		{
			for (const TPair<FId, FLocation>& Pair : *Map)
			{
				if (SameText(Pair.Key.Key, InRequest.FromKey) && (InRequest.FromNamespace.IsEmpty() || SameText(Pair.Key.Namespace, InRequest.FromNamespace)))
				{
					Matches.Add(Pair);
				}
			}
			if (Matches.Num() > 0)
			{
				break;
			}
		}
		if (Matches.Num() == 0)
		{
			continue;
		}
		++FoundIn;
		if (Matches.Num() > 1)
		{
			AddMessage(Output.Messages, Where, EDreamLocSeverity::Error, DreamLocCode::MigrateNotFound, FString::Printf(
				TEXT("'%s' is in %d namespaces; say which with -Namespace"), *InRequest.FromKey, Matches.Num()));
			continue;
		}
		const FId FromId = Matches[0].Key;
		const FLocation From = Matches[0].Value;
		const FId ToId{ InRequest.ToNamespace.IsEmpty() ? FromId.Namespace : InRequest.ToNamespace, InRequest.ToKey };
		const FLocation* To = Culture.Live.Find(ToId);
		if (To == nullptr)
		{
			AddMessage(Output.Messages, Where, EDreamLocSeverity::Error, DreamLocCode::MigrateNotFound, FString::Printf(
				TEXT("there is no entry %s to move the translation to (dloc sync makes it)"), *DreamLocSyncLocal::Describe(ToId)));
			continue;
		}
		const FDreamLocEntry OldEntry = Culture.GetEntry(From);
		if (!OldEntry.IsTranslated())
		{
			continue;
		}
		FDreamLocEntry& NewEntry = Culture.Files[To->File].Document.Items[To->Item].Entry;
		if (NewEntry.IsTranslated() && !InRequest.bForce)
		{
			AddMessage(Output.Messages, Where / Culture.Files[To->File].Path, EDreamLocSeverity::Warning, DreamLocCode::MigrateNotFound, FString::Printf(
				TEXT("%s already has a translation; -Force replaces it"), *DreamLocSyncLocal::Describe(ToId)));
			continue;
		}
		NewEntry.Set(EDreamLocField::Text, *OldEntry.Find(EDreamLocField::Text));
		if (const FString* State = OldEntry.Find(EDreamLocField::State))
		{
			NewEntry.Set(EDreamLocField::State, *State);
		}
		else
		{
			NewEntry.Remove(EDreamLocField::State);
		}
		const FString* OldPrev = OldEntry.Find(EDreamLocField::Prev);
		const FString* OldSource = OldEntry.Find(EDreamLocField::Src);
		const FString MadeFrom = OldPrev != nullptr ? *OldPrev : OldSource != nullptr ? *OldSource : FString();
		const FString* NewSource = NewEntry.Find(EDreamLocField::Src);
		if (!MadeFrom.IsEmpty() && (NewSource == nullptr || !SameText(MadeFrom, *NewSource)))
		{
			NewEntry.Set(EDreamLocField::Prev, MadeFrom);
		}
		else
		{
			NewEntry.Remove(EDreamLocField::Prev);
		}

		// Out with the old entry; a removed block left empty goes too.
		FDreamLocDocument& FromDocument = Culture.Files[From.File].Document;
		if (From.IsRemoved())
		{
			FDreamLocItem& Block = FromDocument.Items[From.Item];
			Block.Children.RemoveAt(From.Child);
			if (!HasEntries(Block.Children))
			{
				FromDocument.Items.RemoveAt(From.Item);
			}
		}
		else
		{
			FromDocument.Items.RemoveAt(From.Item);
		}
		++Output.Migrated;
		for (const int32 FileIndex : { From.File, To->File })
		{
			const FFile& File = Culture.Files[FileIndex];
			const FString Text = DreamLocFile::Write(File.Document);
			if (Text != File.Text)
			{
				Output.Changes.FindOrAdd(Culture.Name).Add(File.Path, Text);
			}
		}
	}
	if (FoundIn == 0)
	{
		AddMessage(Output.Messages, InDLocName, EDreamLocSeverity::Error, DreamLocCode::MigrateNotFound, FString::Printf(
			TEXT("no culture has an entry '%s'%s"), *InRequest.FromKey,
			InRequest.FromNamespace.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" in namespace %s"), *InRequest.FromNamespace)));
	}
	return Output;
}
