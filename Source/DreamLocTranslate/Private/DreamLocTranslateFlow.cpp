// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocTranslateFlow.h"

#include "Async/Async.h"
#include "DreamLocCheck.h"
#include "DreamLocExchange.h"
#include "DreamLocFile.h"
#include "DreamLocSync.h"
#include "DreamLocTranslateConfig.h"
#include "DreamLocTranslatePrompt.h"
#include "DreamLocTranslator.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Features/IModularFeatures.h"
#include "HAL/Event.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace DreamLocTranslateFlowLocal
{
	FString Joined(TConstArrayView<FString> InParts)
	{
		FString Out;
		for (const FString& Part : InParts)
		{
			if (!Out.IsEmpty())
			{
				Out.AppendChar(TCHAR(1));
			}
			Out += Part;
		}
		return Out;
	}

	FString IdOf(const FString& InNamespace, const FString& InKey)
	{
		return Joined({ InNamespace, InKey });
	}

	/** Every secret replaced, wherever it shows: what goes into a log, the result and the report. */
	FString Scrub(FString InText, TConstArrayView<FString> InSecrets)
	{
		for (const FString& Secret : InSecrets)
		{
			if (Secret.Len() >= 4)
			{
				InText.ReplaceInline(*Secret, TEXT("***"), ESearchCase::CaseSensitive);
			}
		}
		return InText;
	}

	/** "1 text", "3 texts". */
	FString Count(int32 InCount, const TCHAR* InOne, const TCHAR* InMany)
	{
		return FString::Printf(TEXT("%d %s"), InCount, InCount == 1 ? InOne : InMany);
	}

	/** The run's log: each line kept, given to the caller and to LogDreamLocTranslate, one at a time, with no secret in it. */
	struct FLogger
	{
		const DreamLocTranslateFlow::FLog* OnLine = nullptr;
		TArray<FString> Secrets;
		FCriticalSection Lock;
		TArray<FDreamLocTranslateLogLine> Lines;

		void Add(EDreamLocTranslateLogLevel InLevel, const FString& InCulture, const FString& InText)
		{
			FDreamLocTranslateLogLine Line;
			Line.Time = FDateTime::Now();
			Line.Level = InLevel;
			Line.Culture = InCulture;
			Line.Text = Scrub(InText, Secrets);
			FScopeLock ScopeLock(&Lock);
			if (InLevel == EDreamLocTranslateLogLevel::Info)
			{
				UE_LOG(LogDreamLocTranslate, Display, TEXT("%s"), *Line.Text);
			}
			else
			{
				UE_LOG(LogDreamLocTranslate, Warning, TEXT("%s"), *Line.Text);
			}
			if (OnLine != nullptr && *OnLine)
			{
				(*OnLine)(Line);
			}
			Lines.Add(MoveTemp(Line));
		}

		TArray<FDreamLocTranslateLogLine> Of(const FString& InCulture)
		{
			FScopeLock ScopeLock(&Lock);
			return Lines.FilterByPredicate([&InCulture](const FDreamLocTranslateLogLine& InLine) { return InLine.Culture == InCulture; });
		}
	};

	/** One text to translate in a culture: the entries of the same source (and kind) sent once. */
	struct FItem
	{
		FDreamLocTranslateUnit Unit;
		/** The culture's collected entries it translates. */
		TArray<int32> Entries;
		/** Where each of them is shown: namespace and `at`, for the tools that lay texts out. */
		TArray<TPair<FString, FString>> Places;
		bool bForms = false;
		bool bLocalOnly = false;
		/** Where it goes next; null once decided without one. */
		IDreamLocTranslator* Translator = nullptr;
		/** Times sent to a large model again with what the checks said. */
		int32 Retries = 0;
		bool bDecided = false;
		EDreamLocTranslateStatus Status = EDreamLocTranslateStatus::Skipped;
		FString Text;
		FString RejectedText;
		FString Problems;
		FString Backend;
		FString Reason;
		/** What a machine translator could not do before the item went to the Fallback. */
		FString HandedOver;
	};

	struct FCulturePlan
	{
		FString Culture;
		FDreamLocExchangeBatch Collected;
		TArray<FItem> Items;
		/** Collected entry -> its item. */
		TArray<int32> ItemOfEntry;
		IDreamLocTranslator* Main = nullptr;
		IDreamLocTranslator* Fallback = nullptr;
		FString Style;
	};

	struct FTranslators
	{
		TMap<FString, TSharedPtr<IDreamLocTranslator>> ByName;

		IDreamLocTranslator* Get(TConstArrayView<FDreamLocTranslateProfile> InProfiles, const FString& InName, TArray<FString>& OutErrors)
		{
			if (const TSharedPtr<IDreamLocTranslator>* Found = ByName.Find(InName))
			{
				return Found->Get();
			}
			TSharedPtr<IDreamLocTranslator> Made;
			if (const FDreamLocTranslateProfile* Profile = DreamLocTranslateConfig::FindProfile(InProfiles, InName))
			{
				FString Error;
				Made = DreamLocTranslators::Make(*Profile, Error);
				if (!Made.IsValid())
				{
					OutErrors.AddUnique(Error);
				}
			}
			else
			{
				OutErrors.AddUnique(FString::Printf(TEXT("no translator profile named '%s'"), *InName));
			}
			ByName.Add(InName, Made);
			return Made.Get();
		}
	};

	struct FPrepared
	{
		FDreamLocSettings Settings;
		TArray<FDreamLocTranslateProfile> Profiles;
		FTranslators Translators;
		TArray<FCulturePlan> Cultures;
		FString ProjectPrompt;
		TArray<FString> Secrets;
		TArray<FString> Errors;
		TArray<FString> Warnings;
	};

	bool CanTake(const IDreamLocTranslator* InTranslator, const FItem& InItem, const FString& InSource, const FString& InCulture, FString& OutWhy)
	{
		if (InTranslator == nullptr)
		{
			return false;
		}
		if (InTranslator->GetKind() == EDreamLocTranslatorKind::Machine)
		{
			if (InItem.bForms)
			{
				OutWhy = TEXT("it has plural, ordinal or gender forms, which a machine translator cannot keep");
				return false;
			}
			if (!InTranslator->SupportsCultures(InSource, InCulture, OutWhy))
			{
				return false;
			}
		}
		if (InItem.bLocalOnly && !InTranslator->GetProfile().IsLocal())
		{
			OutWhy = FString::Printf(TEXT("it only goes to a local translator ([Translate] LocalOnly), and %s is not one"), *InTranslator->GetProfile().Name);
			return false;
		}
		return true;
	}

	/** The translator an item goes to first: the culture's, else the Fallback; none, and it is skipped. */
	void Route(FItem& InOutItem, const FCulturePlan& InPlan, const FString& InSource)
	{
		FString Why;
		if (CanTake(InPlan.Main, InOutItem, InSource, InPlan.Culture, Why))
		{
			InOutItem.Translator = InPlan.Main;
			return;
		}
		FString FallbackWhy;
		if (InPlan.Fallback != nullptr && CanTake(InPlan.Fallback, InOutItem, InSource, InPlan.Culture, FallbackWhy))
		{
			InOutItem.Translator = InPlan.Fallback;
			return;
		}
		InOutItem.Translator = nullptr;
		InOutItem.bDecided = true;
		InOutItem.Status = EDreamLocTranslateStatus::Skipped;
		InOutItem.Reason = InPlan.Fallback == nullptr && InPlan.Main->GetKind() == EDreamLocTranslatorKind::Machine && !InOutItem.bLocalOnly
			? Why + TEXT("; no Fallback large model is set") : Why;
	}

	void Prepare(const FDreamLocTranslateRequest& InRequest, FPrepared& Out)
	{
		TArray<FDreamLocDiagnostic> SettingsDiagnostics;
		Out.Settings = InRequest.Settings.IsSet() ? InRequest.Settings.GetValue() : FDreamLocSettings::LoadFromDirectory(InRequest.DLocDirectory, &SettingsDiagnostics);
		for (const FDreamLocDiagnostic& Diagnostic : SettingsDiagnostics)
		{
			Out.Warnings.Add(Diagnostic.Format(TEXT("DLoc/dloc.ini")));
		}
		TArray<FString> ProfileWarnings;
		Out.Profiles = InRequest.Profiles.Num() > 0 ? InRequest.Profiles : DreamLocTranslateConfig::LoadProfiles(&ProfileWarnings);
		Out.Warnings.Append(ProfileWarnings);
		for (const FDreamLocTranslateProfile& Profile : Out.Profiles)
		{
			Out.Secrets.Append(Profile.GetSecrets());
		}

		TArray<FDreamLocExtractManifest> Manifests;
		if (InRequest.Manifests.IsSet())
		{
			Manifests = InRequest.Manifests.GetValue();
		}
		else
		{
			for (const FString& Path : DreamLocExtract::FindManifestFiles(DreamLocExtract::GetExtractDirectory(InRequest.OwnerDirectory)))
			{
				FDreamLocExtractManifest Manifest;
				FString Error;
				if (DreamLocExtract::LoadFile(Path, Manifest, Error))
				{
					Manifests.Add(MoveTemp(Manifest));
				}
			}
		}
		const TArray<FString> Cultures = InRequest.Cultures.Num() > 0 ? InRequest.Cultures : Out.Settings.TargetCultures;
		if (Cultures.IsEmpty())
		{
			Out.Errors.Add(TEXT("no culture to translate into: choose one, or set TargetCultures in dloc.ini"));
		}
		FFileHelper::LoadFileToString(Out.ProjectPrompt, *(InRequest.DLocDirectory / TEXT("_prompt.md")));
		const FString FallbackName = !InRequest.Fallback.IsEmpty() ? InRequest.Fallback : Out.Settings.TranslateFallback;

		TSet<FString> Wanted;
		for (const TPair<FString, FString>& Entry : InRequest.Entries)
		{
			Wanted.Add(IdOf(Entry.Key, Entry.Value));
		}

		const TMap<FString, TMap<FString, FString>> Files = DreamLocSync::ReadFiles(InRequest.DLocDirectory, Cultures);
		for (const FString& Culture : Cultures)
		{
			FCulturePlan& Plan = Out.Cultures.AddDefaulted_GetRef();
			Plan.Culture = Culture;
			const FString ProfileName = !InRequest.Profile.IsEmpty() ? InRequest.Profile : Out.Settings.GetTranslateProfile(Culture);
			// Without a translator the entries are still collected: what there is to translate does not depend on it.
			if (ProfileName.IsEmpty())
			{
				Out.Errors.AddUnique(FString::Printf(TEXT("%s: no translator chosen ([Translate] Profile in dloc.ini)"), *Culture));
			}
			else
			{
				Plan.Main = Out.Translators.Get(Out.Profiles, ProfileName, Out.Errors);
			}
			if (Plan.Main != nullptr && !FallbackName.IsEmpty() && FallbackName != ProfileName)
			{
				IDreamLocTranslator* Fallback = Out.Translators.Get(Out.Profiles, FallbackName, Out.Warnings);
				if (Fallback != nullptr && Fallback->GetKind() != EDreamLocTranslatorKind::LargeModel)
				{
					Out.Warnings.AddUnique(FString::Printf(TEXT("the Fallback %s is not a large model (Type=OpenAI): not used"), *FallbackName));
				}
				else
				{
					Plan.Fallback = Fallback;
				}
			}

			// The style guide: DLoc/_style.<culture>.md before DLoc/_style.md.
			if (!Out.Settings.TranslateStyle.IsEmpty())
			{
				const FString Style = FPaths::IsRelative(Out.Settings.TranslateStyle) ? InRequest.OwnerDirectory / Out.Settings.TranslateStyle : Out.Settings.TranslateStyle;
				const FString CultureStyle = FPaths::GetPath(Style) / FPaths::GetBaseFilename(Style) + TEXT(".") + Culture + FPaths::GetExtension(Style, /*bIncludeDot*/ true);
				if (!FFileHelper::LoadFileToString(Plan.Style, *CultureStyle))
				{
					FFileHelper::LoadFileToString(Plan.Style, *Style);
				}
			}

			DreamLocExchange::FExportOptions Options;
			Options.Culture = Culture;
			for (const TPair<bool, const TCHAR*>& Category : { TPair<bool, const TCHAR*>(InRequest.bIncludeUntranslated, TEXT("untranslated")),
				TPair<bool, const TCHAR*>(InRequest.bIncludeStale, TEXT("stale")), TPair<bool, const TCHAR*>(InRequest.bIncludeMachine, TEXT("machine")) })
			{
				if (Category.Key)
				{
					Options.Only.Add(Category.Value);
				}
			}
			if (InRequest.bSuggest)
			{
				Options.Only = { TEXT("all") };
			}
			Options.Files = InRequest.Files;
			Options.Manifests = Manifests;
			TArray<FDreamLocReportLine> Messages;
			if (Options.Only.Num() > 0)
			{
				Plan.Collected = DreamLocExchange::Collect(Files, Options, Out.Settings.SourceCulture, Messages);
			}
			for (const FDreamLocReportLine& Message : Messages)
			{
				Out.Warnings.Add(Message.Format());
			}
			if (Wanted.Num() > 0)
			{
				Plan.Collected.Entries.RemoveAll([&Wanted](const FDreamLocExchangeEntry& InEntry) { return !Wanted.Contains(IdOf(InEntry.Namespace, InEntry.Key)); });
			}

			// The culture's reviewed translations, by source text: for a large model to stay with.
			DreamLocExchange::FExportOptions DoneOptions;
			DoneOptions.Culture = Culture;
			DoneOptions.Only = { TEXT("done") };
			TArray<FDreamLocReportLine> Ignored;
			TMap<FString, FString> Reviewed;
			const FDreamLocExchangeBatch Done = DreamLocExchange::Collect(Files, DoneOptions, Out.Settings.SourceCulture, Ignored);
			for (const FDreamLocExchangeEntry& Entry : Done.Entries)
			{
				if (!Entry.Text.IsEmpty() && !Reviewed.Contains(Entry.Source))
				{
					Reviewed.Add(Entry.Source, Entry.Text);
				}
			}

			// A reviewed translation whose source changed since is stale, and still a person's: it is never replaced.
			TSet<FString> ReviewedIds;
			if (const TMap<FString, FString>* CultureFiles = Files.Find(Culture))
			{
				for (const TPair<FString, FString>& File : *CultureFiles)
				{
					FDreamLocDocument Document;
					TArray<FDreamLocDiagnostic> Diagnostics;
					DreamLocFile::Parse(File.Value, Document, Diagnostics);
					Document.ForEachEntry([&ReviewedIds](const FString& InNamespace, const FDreamLocEntry& InEntry, bool)
					{
						if (InEntry.GetState() == EDreamLocState::Done)
						{
							ReviewedIds.Add(IdOf(InNamespace, InEntry.Key));
						}
					});
				}
			}

			TMap<FString, int32> ItemOfText;
			Plan.ItemOfEntry.Init(INDEX_NONE, Plan.Collected.Entries.Num());
			for (int32 Index = 0; Index < Plan.Collected.Entries.Num(); ++Index)
			{
				const FDreamLocExchangeEntry& Entry = Plan.Collected.Entries[Index];
				// A suggestion is for a person to look at: a reviewed entry may have one too.
				if (!InRequest.bSuggest && ReviewedIds.Contains(IdOf(Entry.Namespace, Entry.Key)))
				{
					const int32 ItemIndex = Plan.Items.AddDefaulted();
					Plan.ItemOfEntry[Index] = ItemIndex;
					FItem& Item = Plan.Items[ItemIndex];
					Item.Unit.Id = ItemIndex + 1;
					Item.Unit.File = Entry.File;
					Item.Unit.Source = Entry.Source;
					Item.Entries.Add(Index);
					Item.bDecided = true;
					Item.Status = EDreamLocTranslateStatus::Skipped;
					Item.Reason = TEXT("reviewed (state done) and its source changed since: for a person to look at, never replaced");
					continue;
				}
				const bool bLocalOnly = DreamLocTranslateFlow::IsLocalOnly(Out.Settings.TranslateLocalOnly, Entry.File, Entry.Namespace);
				const FString Same = Joined({ Entry.Source, Entry.Kind, FString(bLocalOnly ? TEXT("1") : TEXT("0")), Entry.Prev, Entry.PreviousText });
				if (const int32* Existing = ItemOfText.Find(Same))
				{
					FItem& Item = Plan.Items[*Existing];
					Item.Entries.Add(Index);
					Item.Places.Emplace(Entry.Namespace, Entry.At);
					if (Entry.Max >= 0)
					{
						Item.Unit.Max = Item.Unit.Max < 0 ? Entry.Max : FMath::Min(Item.Unit.Max, Entry.Max);
					}
					Plan.ItemOfEntry[Index] = *Existing;
					continue;
				}
				const int32 ItemIndex = Plan.Items.AddDefaulted();
				ItemOfText.Add(Same, ItemIndex);
				Plan.ItemOfEntry[Index] = ItemIndex;
				FItem& Item = Plan.Items[ItemIndex];
				FDreamLocTranslateUnit& Unit = Item.Unit;
				Unit.Id = ItemIndex + 1;
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
				Unit.Reference = Reviewed.FindRef(Entry.Source);
				if (InRequest.bSuggest)
				{
					// Its own reviewed translation is no reference to stay with: the person asks for another.
					if (Unit.Reference.Equals(Entry.Text, ESearchCase::CaseSensitive))
					{
						Unit.Reference.Reset();
					}
					Unit.LastText = InRequest.AmendText;
					Unit.Problems = InRequest.AmendProblems;
				}
				Item.Entries.Add(Index);
				Item.Places.Emplace(Entry.Namespace, Entry.At);
				Item.bForms = DreamLocTranslateFlow::HasForms(Entry.Source);
				Item.bLocalOnly = bLocalOnly;
			}
			for (FItem& Item : Plan.Items)
			{
				if (!Item.bDecided && Plan.Main != nullptr)
				{
					Route(Item, Plan, Out.Settings.SourceCulture);
				}
			}
		}
	}

	struct FBatch
	{
		IDreamLocTranslator* Translator = nullptr;
		TArray<int32> Items;
		int32 Characters = 0;
		bool bSent = false;
		FDreamLocTranslateReply Reply;
	};

	/** The items still to send: by translator, a file at a time, within the batch size and the translator's limits. */
	TArray<FBatch> MakeBatches(const FCulturePlan& InPlan, int32 InBatchSize)
	{
		TArray<IDreamLocTranslator*> Translators;
		for (const FItem& Item : InPlan.Items)
		{
			if (!Item.bDecided && Item.Translator != nullptr)
			{
				Translators.AddUnique(Item.Translator);
			}
		}
		TArray<FBatch> Batches;
		for (IDreamLocTranslator* Translator : Translators)
		{
			const int32 MaxUnits = FMath::Max(1, FMath::Min(InBatchSize, Translator->GetMaxUnits()));
			const int32 MaxCharacters = FMath::Max(1, Translator->GetMaxCharacters());
			int32 Current = INDEX_NONE;
			FString File;
			for (int32 Index = 0; Index < InPlan.Items.Num(); ++Index)
			{
				const FItem& Item = InPlan.Items[Index];
				if (Item.bDecided || Item.Translator != Translator)
				{
					continue;
				}
				const int32 Length = Item.Unit.Source.Len();
				if (Current == INDEX_NONE || Item.Unit.File != File || Batches[Current].Items.Num() >= MaxUnits
					|| Batches[Current].Characters + Length > MaxCharacters)
				{
					Current = Batches.AddDefaulted();
					Batches[Current].Translator = Translator;
					File = Item.Unit.File;
				}
				Batches[Current].Items.Add(Index);
				Batches[Current].Characters += Length;
			}
		}
		return Batches;
	}

	FDreamLocTranslateBatch MakeRequest(const FBatch& InBatch, const FCulturePlan& InPlan, const FPrepared& InPrepared)
	{
		FDreamLocTranslateBatch Out;
		Out.SourceCulture = InPrepared.Settings.SourceCulture;
		Out.Culture = InPlan.Culture;
		Out.Style = InPlan.Style;
		Out.ProjectPrompt = InPrepared.ProjectPrompt;
		for (const int32 Index : InBatch.Items)
		{
			FDreamLocTranslateUnit Unit = InPlan.Items[Index].Unit;
			if (InBatch.Translator->GetKind() == EDreamLocTranslatorKind::Machine)
			{
				// A machine translator reads the text alone.
				Unit.Reference.Reset();
				Unit.LastText.Reset();
				Unit.Problems.Reset();
			}
			Out.Units.Add(MoveTemp(Unit));
		}
		return Out;
	}

	struct FStats
	{
		int32 Requests = 0;
		int64 PromptTokens = 0;
		int64 CompletionTokens = 0;
		int64 Characters = 0;
	};

	/**
	 * One batch: sent again after a network error, 429 or 5xx (waiting longer each time), and once after an answer that
	 * does not read. Each try is a line of the log, InWhat first (the culture, the file, the texts, the translator).
	 */
	FDreamLocTranslateReply SendWithRetries(IDreamLocTranslator& InTranslator, const FDreamLocTranslateBatch& InBatch, int32 InCharacters,
		const FDreamLocTranslateRequest& InRequest, const TAtomic<bool>& InCancel, FCriticalSection& InLock, FStats& InOutStats, FLogger& InLog, const FString& InWhat)
	{
		FDreamLocTranslateReply Reply;
		int32 NetworkTries = 0;
		bool bResent = false;
		for (;;)
		{
			const double Sent = FPlatformTime::Seconds();
			Reply = InTranslator.Translate(InBatch, InCancel);
			const double Took = FPlatformTime::Seconds() - Sent;
			{
				FScopeLock ScopeLock(&InLock);
				++InOutStats.Requests;
				InOutStats.PromptTokens += Reply.PromptTokens;
				InOutStats.CompletionTokens += Reply.CompletionTokens;
				InOutStats.Characters += InCharacters;
			}
			// How much it took: a large model's tokens (a prompt cut to the model's context shows here), else the characters.
			const FString Usage = Reply.PromptTokens + Reply.CompletionTokens > 0 ? FString::Printf(TEXT("  %lld + %lld tokens"), Reply.PromptTokens, Reply.CompletionTokens)
				: Reply.bSucceeded ? FString::Printf(TEXT("  %d characters"), InCharacters) : FString();
			if (Reply.bSucceeded)
			{
				InLog.Add(EDreamLocTranslateLogLevel::Info, InBatch.Culture, FString::Printf(TEXT("%s  ok  %.1f s%s"), *InWhat, Took, *Usage));
				TMap<FString, int32> Said;
				for (const FString& Warning : Reply.Warnings)
				{
					++Said.FindOrAdd(Warning);
				}
				for (const TPair<FString, int32>& Warning : Said)
				{
					InLog.Add(EDreamLocTranslateLogLevel::Warning, InBatch.Culture, Warning.Value > 1 ? FString::Printf(TEXT("%s: %s (%d times)"), *InWhat, *Warning.Key, Warning.Value)
						: FString::Printf(TEXT("%s: %s"), *InWhat, *Warning.Key));
				}
				return Reply;
			}
			if (InCancel.Load())
			{
				InLog.Add(EDreamLocTranslateLogLevel::Info, InBatch.Culture, FString::Printf(TEXT("%s  cancelled  %.1f s"), *InWhat, Took));
				return Reply;
			}
			const FString Why = (Reply.Error.IsEmpty() ? FString(TEXT("the request failed")) : Reply.Error) + (Reply.Detail.IsEmpty() ? FString() : TEXT(" (") + Reply.Detail + TEXT(")"));
			const FString Failed = FString::Printf(TEXT("%s  failed  %.1f s%s  %s"), *InWhat, Took, *Usage, *Why);
			if (Reply.bMalformed && !bResent)
			{
				InLog.Add(EDreamLocTranslateLogLevel::Warning, InBatch.Culture, Failed + TEXT("; sent again"));
				bResent = true;
				continue;
			}
			if (Reply.bRetryable && NetworkTries < InRequest.NetworkRetries)
			{
				const double Delay = InRequest.RetryDelaySeconds * FMath::Pow(2.0, double(NetworkTries));
				InLog.Add(EDreamLocTranslateLogLevel::Warning, InBatch.Culture, FString::Printf(TEXT("%s; sent again in %.0f s"), *Failed, Delay));
				const double Until = FPlatformTime::Seconds() + Delay;
				++NetworkTries;
				while (FPlatformTime::Seconds() < Until && !InCancel.Load())
				{
					FPlatformProcess::Sleep(0.02f);
				}
				if (InCancel.Load())
				{
					return Reply;
				}
				continue;
			}
			InLog.Add(EDreamLocTranslateLogLevel::Error, InBatch.Culture, Failed);
			return Reply;
		}
	}

	void SendAll(TArray<FBatch>& InOutBatches, const FCulturePlan& InPlan, const FPrepared& InPrepared, const FDreamLocTranslateRequest& InRequest,
		const DreamLocTranslateFlow::FProgress& InOnProgress, FDreamLocTranslateProgress& InOutProgress, FStats& InOutStats, const TAtomic<bool>& InCancel, FLogger& InLog)
	{
		FCriticalSection Lock;
		TAtomic<int32> Next(0);
		auto Work = [&]()
		{
			for (;;)
			{
				const int32 Index = Next++;
				if (Index >= InOutBatches.Num() || InCancel.Load())
				{
					return;
				}
				FBatch& Batch = InOutBatches[Index];
				const FDreamLocTranslateBatch Request = MakeRequest(Batch, InPlan, InPrepared);
				int32 Entries = 0;
				for (const int32 Item : Batch.Items)
				{
					Entries += InPlan.Items[Item].Entries.Num();
				}
				FString What = FString::Printf(TEXT("%s  %s  %s"), *InPlan.Culture, Request.Units.Num() > 0 ? *Request.Units[0].File : TEXT(""),
					*Count(Batch.Items.Num(), TEXT("text"), TEXT("texts")));
				if (Entries != Batch.Items.Num())
				{
					What += FString::Printf(TEXT(" (%d entries)"), Entries);
				}
				What += TEXT("  ") + Batch.Translator->GetProfile().Name;
				Batch.Reply = SendWithRetries(*Batch.Translator, Request, Batch.Characters, InRequest, InCancel, Lock, InOutStats, InLog, What);
				Batch.bSent = true;
				FScopeLock ScopeLock(&Lock);
				++InOutProgress.BatchesDone;
				InOutProgress.UnitsDone += Batch.Items.Num();
				if (InOnProgress)
				{
					InOnProgress(InOutProgress);
				}
			}
		};
		const int32 Workers = FMath::Clamp(InPrepared.Settings.TranslateConcurrency, 1, FMath::Max(1, InOutBatches.Num()));
		TArray<TFuture<void>> Others;
		for (int32 Worker = 1; Worker < Workers; ++Worker)
		{
			Others.Add(Async(EAsyncExecution::Thread, [&Work]() { Work(); }));
		}
		Work();
		for (TFuture<void>& Other : Others)
		{
			Other.Wait();
		}
	}

	/** What the checks say, split: what turns the translation down, and what is only noted (glyphs the font lacks). */
	void CheckOne(const FDreamLocTranslateUnit& InUnit, TConstArrayView<TPair<FString, FString>> InPlaces, const FString& InText, const FString& InCulture,
		const FDreamLocTerms& InTerms, const FString& InOwnerDirectory, TConstArrayView<IDreamLocContextProvider*> InProviders, FString& OutFailing, FString& OutNotes)
	{
		FDreamLocEntry Entry;
		Entry.Key = InUnit.Key;
		Entry.Set(EDreamLocField::Src, InUnit.Source);
		Entry.Set(EDreamLocField::Text, InText);
		if (InUnit.Max >= 0)
		{
			Entry.Set(EDreamLocField::Max, FString::FromInt(InUnit.Max));
		}
		TArray<FDreamLocDiagnostic> Diagnostics;
		DreamLocCheck::CheckEntry(Entry, InCulture, InTerms, Diagnostics);
		if (InProviders.Num() > 0)
		{
			for (const TPair<FString, FString>& Place : InPlaces)
			{
				if (!Place.Value.IsEmpty())
				{
					Entry.Set(EDreamLocField::At, Place.Value);
					DreamLocCheck::CheckFit(InProviders, InOwnerDirectory, Place.Key, Entry, InCulture, Diagnostics);
				}
			}
		}
		TArray<FString> Failing;
		TArray<FString> Notes;
		for (const FDreamLocDiagnostic& Diagnostic : Diagnostics)
		{
			const FString Line = Diagnostic.GetCodeName() + TEXT(" ") + Diagnostic.Message;
			// A font without a glyph is the font's to fix, not the translation's.
			(Diagnostic.Code == DreamLocCode::GlyphsMissing || Diagnostic.Code == DreamLocCode::StaleTranslation ? Notes : Failing).AddUnique(Line);
		}
		OutFailing = FString::Join(Failing, TEXT("; "));
		OutNotes = FString::Join(Notes, TEXT("; "));
	}

	TArray<IDreamLocContextProvider*> GetProviders()
	{
		return IModularFeatures::Get().GetModularFeatureImplementations<IDreamLocContextProvider>(IDreamLocContextProvider::GetModularFeatureName());
	}

	/** Runs on the game thread, waited for: the tools that lay texts out load assets. */
	void OnGameThread(TFunction<void()> InWork)
	{
		if (IsInGameThread())
		{
			InWork();
			return;
		}
		FEvent* Done = FPlatformProcess::GetSynchEventFromPool(/*bIsManualReset*/ true);
		AsyncTask(ENamedThreads::GameThread, [&InWork, Done]()
		{
			InWork();
			Done->Trigger();
		});
		Done->Wait();
		FPlatformProcess::ReturnSynchEventToPool(Done);
	}

	/** A failure: a machine translator's goes to the Fallback large model with what went wrong; any other is final. */
	void HandOver(FItem& InOutItem, const FCulturePlan& InPlan, const FString& InSource, const FString& InWhy, const FString& InText, const FString& InProblems,
		bool bInCancelled)
	{
		FString FallbackWhy;
		if (!bInCancelled && InOutItem.Translator->GetKind() == EDreamLocTranslatorKind::Machine && InPlan.Fallback != nullptr
			&& CanTake(InPlan.Fallback, InOutItem, InSource, InPlan.Culture, FallbackWhy))
		{
			InOutItem.HandedOver = InOutItem.Translator->GetProfile().Name + TEXT(": ") + InWhy;
			InOutItem.Unit.LastText = InText;
			InOutItem.Unit.Problems = InProblems;
			InOutItem.Translator = InPlan.Fallback;
			InOutItem.Retries = 0;
			return;
		}
		InOutItem.bDecided = true;
		InOutItem.Status = bInCancelled ? EDreamLocTranslateStatus::Skipped : EDreamLocTranslateStatus::Failed;
		InOutItem.Reason = bInCancelled ? FString(TEXT("cancelled")) : InWhy;
		InOutItem.RejectedText = InText;
		InOutItem.Problems = InProblems;
		InOutItem.Backend = InOutItem.Translator->GetProfile().Name;
	}

	/** Where an item is, for the log: its file, its namespace and key, and how many more entries share its text. */
	FString PlaceOf(const FItem& InItem)
	{
		FString Out = FString::Printf(TEXT("%s  %s / %s"), *InItem.Unit.File, *InItem.Unit.Namespace, *InItem.Unit.Key);
		if (InItem.Entries.Num() > 1)
		{
			Out += FString::Printf(TEXT(" (and %d more)"), InItem.Entries.Num() - 1);
		}
		return Out;
	}

	/** After HandOver: the item failed for good, or went on to the Fallback. */
	void LogHandOver(FLogger& InLog, const FString& InCulture, const FItem& InItem, const FString& InWhy, const FString& InText)
	{
		FString Line = FString::Printf(TEXT("%s  %s  "), *InCulture, *PlaceOf(InItem));
		Line += InItem.bDecided ? TEXT("failed: ") + InWhy : FString::Printf(TEXT("to %s: %s"), *InItem.Translator->GetProfile().Name, *InWhy);
		Line += TEXT("; src ") + DreamLocTranslatePrompt::OneLine(InItem.Unit.Source);
		if (!InText.IsEmpty())
		{
			Line += TEXT("; turned down ") + DreamLocTranslatePrompt::OneLine(InText);
		}
		InLog.Add(InItem.bDecided ? EDreamLocTranslateLogLevel::Error : EDreamLocTranslateLogLevel::Warning, InCulture, Line);
	}

	/** What a round's answers decide: each item translated, sent again, handed over, or failed. */
	void Take(TArray<FBatch>& InBatches, FCulturePlan& InOutPlan, const FPrepared& InPrepared, const FString& InOwnerDirectory, const TAtomic<bool>& InCancel,
		TArray<FString>& OutWarnings, FLogger& InLog)
	{
		struct FCandidate
		{
			int32 Item = 0;
			FString Text;
			FString Failing;
			FString Notes;
		};
		TArray<FCandidate> Candidates;
		const FString& Source = InPrepared.Settings.SourceCulture;
		for (FBatch& Batch : InBatches)
		{
			if (!Batch.bSent)
			{
				continue;
			}
			for (const FString& Warning : Batch.Reply.Warnings)
			{
				OutWarnings.Add(FString::Printf(TEXT("%s (%s): %s"), *InOutPlan.Culture, *Batch.Translator->GetProfile().Name, *Warning));
			}
			for (const int32 Index : Batch.Items)
			{
				FItem& Item = InOutPlan.Items[Index];
				if (!Batch.Reply.bSucceeded)
				{
					// The request's line in the log says why: not a line for each of its texts.
					HandOver(Item, InOutPlan, Source, Batch.Reply.Error.IsEmpty() ? FString(TEXT("the request failed")) : Batch.Reply.Error, FString(), FString(),
						InCancel.Load());
				}
				else if (const FString* Text = Batch.Reply.Texts.Find(Item.Unit.Id))
				{
					Candidates.Add({ Index, *Text });
				}
				else
				{
					const FString* Error = Batch.Reply.UnitErrors.Find(Item.Unit.Id);
					const FString Why = Error != nullptr ? *Error : FString(TEXT("the answer has no translation for it"));
					HandOver(Item, InOutPlan, Source, Why, FString(), FString(), false);
					LogHandOver(InLog, InOutPlan.Culture, Item, Why, FString());
				}
			}
		}
		if (Candidates.IsEmpty())
		{
			return;
		}
		OnGameThread([&]()
		{
			const TArray<IDreamLocContextProvider*> Providers = GetProviders();
			for (FCandidate& Candidate : Candidates)
			{
				const FItem& Item = InOutPlan.Items[Candidate.Item];
				CheckOne(Item.Unit, Item.Places, Candidate.Text, InOutPlan.Culture, InOutPlan.Collected.Terms, InOwnerDirectory, Providers, Candidate.Failing, Candidate.Notes);
			}
		});
		for (const FCandidate& Candidate : Candidates)
		{
			FItem& Item = InOutPlan.Items[Candidate.Item];
			if (Candidate.Failing.IsEmpty())
			{
				Item.bDecided = true;
				Item.Status = EDreamLocTranslateStatus::Translated;
				Item.Text = Candidate.Text;
				Item.Problems = Candidate.Notes;
				Item.Backend = Item.Translator->GetProfile().Name;
				continue;
			}
			if (Item.Translator->GetKind() == EDreamLocTranslatorKind::LargeModel && Item.Retries < InPrepared.Settings.TranslateRetry && !InCancel.Load())
			{
				++Item.Retries;
				Item.Unit.LastText = Candidate.Text;
				Item.Unit.Problems = Candidate.Failing;
				InLog.Add(EDreamLocTranslateLogLevel::Warning, InOutPlan.Culture, FString::Printf(TEXT("%s  %s  the checks turned it down, sent again: %s; src %s; turned down %s"),
					*InOutPlan.Culture, *PlaceOf(Item), *Candidate.Failing, *DreamLocTranslatePrompt::OneLine(Item.Unit.Source), *DreamLocTranslatePrompt::OneLine(Candidate.Text)));
				continue;
			}
			HandOver(Item, InOutPlan, Source, TEXT("the checks turned it down"), Candidate.Text, Candidate.Failing, false);
			LogHandOver(InLog, InOutPlan.Culture, Item, TEXT("the checks turned it down: ") + Candidate.Failing, Candidate.Text);
		}
	}

	/** Why a culture's entries failed, the commonest first, and what the checks said (each code, in how many). */
	FString WhyFailed(TConstArrayView<FDreamLocTranslateRow> InRows)
	{
		TMap<FString, int32> ByReason;
		TMap<FString, int32> ByCode;
		for (const FDreamLocTranslateRow& Row : InRows)
		{
			if (Row.Status != EDreamLocTranslateStatus::Failed)
			{
				continue;
			}
			++ByReason.FindOrAdd(Row.Reason.IsEmpty() ? FString(TEXT("no reason given")) : Row.Reason);
			TSet<FString> Codes;
			for (int32 At = Row.Problems.Find(TEXT("DLOC"), ESearchCase::CaseSensitive); At != INDEX_NONE;
				At = Row.Problems.Find(TEXT("DLOC"), ESearchCase::CaseSensitive, ESearchDir::FromStart, At + 4))
			{
				const FString Digits = Row.Problems.Mid(At + 4, 4);
				if (Digits.Len() == 4 && FCString::IsNumeric(*Digits))
				{
					Codes.Add(TEXT("DLOC") + Digits);
				}
			}
			for (const FString& Code : Codes)
			{
				++ByCode.FindOrAdd(Code);
			}
		}
		ByReason.ValueStableSort([](int32 A, int32 B) { return A > B; });
		ByCode.ValueStableSort([](int32 A, int32 B) { return A > B; });
		TArray<FString> Parts;
		for (const TPair<FString, int32>& Reason : ByReason)
		{
			Parts.Add(FString::Printf(TEXT("%d %s"), Reason.Value, *Reason.Key));
		}
		TArray<FString> Codes;
		for (const TPair<FString, int32>& Code : ByCode)
		{
			Codes.Add(FString::Printf(TEXT("%s %d"), *Code.Key, Code.Value));
		}
		return FString::Join(Parts, TEXT(", ")) + (Codes.IsEmpty() ? FString() : TEXT("; the checks said ") + FString::Join(Codes, TEXT(", ")));
	}

	const TCHAR* StatusName(EDreamLocTranslateStatus InStatus)
	{
		switch (InStatus)
		{
		case EDreamLocTranslateStatus::Translated: return TEXT("translated");
		case EDreamLocTranslateStatus::Failed: return TEXT("failed");
		default: return TEXT("skipped");
		}
	}

	FString WriteReport(const FString& InDirectory, const FCulturePlan& InPlan, const FPrepared& InPrepared, TConstArrayView<FDreamLocTranslateRow> InRows,
		const FStats& InStats, double InSeconds, bool bInCancelled, TConstArrayView<FString> InWarnings, TConstArrayView<FDreamLocTranslateLogLine> InLog)
	{
		const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("culture"), InPlan.Culture);
		Root->SetStringField(TEXT("sourceCulture"), InPrepared.Settings.SourceCulture);
		Root->SetStringField(TEXT("finished"), FDateTime::Now().ToIso8601());
		Root->SetStringField(TEXT("profile"), InPlan.Main != nullptr ? InPlan.Main->GetProfile().Name : FString());
		Root->SetStringField(TEXT("fallback"), InPlan.Fallback != nullptr ? InPlan.Fallback->GetProfile().Name : FString());
		Root->SetNumberField(TEXT("seconds"), FMath::RoundToDouble(InSeconds * 100.0) / 100.0);
		Root->SetBoolField(TEXT("cancelled"), bInCancelled);
		Root->SetNumberField(TEXT("requests"), InStats.Requests);
		Root->SetNumberField(TEXT("promptTokens"), double(InStats.PromptTokens));
		Root->SetNumberField(TEXT("completionTokens"), double(InStats.CompletionTokens));
		Root->SetNumberField(TEXT("characters"), double(InStats.Characters));
		int32 Counts[3] = { 0, 0, 0 };
		TArray<TSharedPtr<FJsonValue>> Rows;
		for (const FDreamLocTranslateRow& Row : InRows)
		{
			++Counts[int32(Row.Status)];
			const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("status"), StatusName(Row.Status));
			Object->SetStringField(TEXT("file"), Row.File);
			Object->SetStringField(TEXT("ns"), Row.Namespace);
			Object->SetStringField(TEXT("key"), Row.Key);
			Object->SetStringField(TEXT("src"), Row.Source);
			auto SetIf = [&Object](const TCHAR* InName, const FString& InValue)
			{
				if (!InValue.IsEmpty())
				{
					Object->SetStringField(InName, InValue);
				}
			};
			SetIf(TEXT("old"), Row.OldText);
			SetIf(TEXT("text"), Row.NewText);
			SetIf(TEXT("rejected"), Row.RejectedText);
			SetIf(TEXT("problems"), Row.Problems);
			SetIf(TEXT("backend"), Row.Backend);
			SetIf(TEXT("reason"), Row.Reason);
			Rows.Add(MakeShared<FJsonValueObject>(Object));
		}
		Root->SetNumberField(TEXT("translated"), Counts[int32(EDreamLocTranslateStatus::Translated)]);
		Root->SetNumberField(TEXT("failed"), Counts[int32(EDreamLocTranslateStatus::Failed)]);
		Root->SetNumberField(TEXT("skipped"), Counts[int32(EDreamLocTranslateStatus::Skipped)]);
		Root->SetArrayField(TEXT("entries"), Rows);
		TArray<TSharedPtr<FJsonValue>> Warnings;
		for (const FString& Warning : InWarnings)
		{
			Warnings.Add(MakeShared<FJsonValueString>(Warning));
		}
		Root->SetArrayField(TEXT("warnings"), Warnings);
		TArray<TSharedPtr<FJsonValue>> Log;
		for (const FDreamLocTranslateLogLine& Line : InLog)
		{
			Log.Add(MakeShared<FJsonValueString>(Line.Format()));
		}
		Root->SetArrayField(TEXT("log"), Log);
		FString Json;
		FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Json));

		IFileManager::Get().MakeDirectory(*InDirectory, /*Tree*/ true);
		const FString Stem = InDirectory / FString::Printf(TEXT("translate-%s-%s"), *InPlan.Culture, *FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")));
		FString Path = Stem + TEXT(".json");
		for (int32 Suffix = 2; IFileManager::Get().FileExists(*Path); ++Suffix)
		{
			Path = FString::Printf(TEXT("%s-%d.json"), *Stem, Suffix);
		}
		return FFileHelper::SaveStringToFile(Scrub(Json, InPrepared.Secrets), *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) ? Path : FString();
	}
}

FString FDreamLocTranslateLogLine::Format() const
{
	return Time.ToString(TEXT("%H:%M:%S")) + TEXT("  ") + Text;
}

bool DreamLocTranslateFlow::IsLocalOnly(TConstArrayView<FString> InPrefixes, const FString& InFile, const FString& InNamespace)
{
	const FString File = InFile.Replace(TEXT("\\"), TEXT("/"));
	for (const FString& Prefix : InPrefixes)
	{
		const FString Trimmed = Prefix.TrimStartAndEnd().Replace(TEXT("\\"), TEXT("/"));
		if (!Trimmed.IsEmpty() && (File.StartsWith(Trimmed, ESearchCase::IgnoreCase) || InNamespace.StartsWith(Trimmed, ESearchCase::IgnoreCase)))
		{
			return true;
		}
	}
	return false;
}

bool DreamLocTranslateFlow::HasForms(const FString& InSource)
{
	for (const TCHAR* Modifier : { TEXT("|plural("), TEXT("|ordinal("), TEXT("|gender("), TEXT("|hpp(") })
	{
		if (InSource.Contains(Modifier, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

FString DreamLocTranslateFlow::CheckTranslation(const FDreamLocTranslateUnit& InUnit, const FString& InText, const FString& InCulture, const FDreamLocTerms& InTerms,
	const FString& InOwnerDirectory)
{
	using namespace DreamLocTranslateFlowLocal;
	FString Failing;
	FString Notes;
	const TArray<IDreamLocContextProvider*> Providers = IsInGameThread() ? GetProviders() : TArray<IDreamLocContextProvider*>();
	const TPair<FString, FString> Place(InUnit.Namespace, InUnit.At);
	CheckOne(InUnit, MakeArrayView(&Place, 1), InText, InCulture, InTerms, InOwnerDirectory, Providers, Failing, Notes);
	return Failing.IsEmpty() ? Notes : Notes.IsEmpty() ? Failing : Failing + TEXT("; ") + Notes;
}

FString DreamLocTranslateFlow::CheckContent(const FDreamLocTranslateUnit& InUnit, const FString& InText, const FString& InCulture, const FDreamLocTerms& InTerms)
{
	using namespace DreamLocTranslateFlowLocal;
	FString Failing;
	FString Notes;
	CheckOne(InUnit, {}, InText, InCulture, InTerms, FString(), {}, Failing, Notes);
	return Failing.IsEmpty() ? Notes : Notes.IsEmpty() ? Failing : Failing + TEXT("; ") + Notes;
}

FDreamLocTranslateEstimate DreamLocTranslateFlow::Estimate(const FDreamLocTranslateRequest& InRequest)
{
	using namespace DreamLocTranslateFlowLocal;
	FDreamLocTranslateEstimate Out;
	FPrepared Prepared;
	Prepare(InRequest, Prepared);
	for (const FString& Error : Prepared.Errors)
	{
		Out.Errors.Add(Scrub(Error, Prepared.Secrets));
	}
	// The system message, roughly: a token for every three or four characters of the template.
	const int32 SystemTokens = DreamLocTranslatePrompt::LoadTemplate().Len() / 3 + 200;
	for (const FCulturePlan& Plan : Prepared.Cultures)
	{
		Out.Entries += Plan.Collected.Entries.Num();
		Out.bTranslatorKnown &= Plan.Main != nullptr;
		for (const FItem& Item : Plan.Items)
		{
			if (Item.bDecided)
			{
				Out.Skipped += Item.Entries.Num();
				continue;
			}
			++Out.Distinct;
			const int32 Length = Item.Unit.Source.Len();
			Out.Characters += Length;
			if (Item.Translator != nullptr && Item.Translator->GetKind() == EDreamLocTranslatorKind::LargeModel)
			{
				int32 Context = Item.Unit.Note.Len() + Item.Unit.Reference.Len() + Item.Unit.PreviousText.Len();
				for (const FString& Near : Item.Unit.Nearby)
				{
					Context += Near.Len();
				}
				Out.Tokens += Length * 3 + Context / 2 + 40;
			}
		}
		for (const FBatch& Batch : MakeBatches(Plan, Prepared.Settings.TranslateBatchSize))
		{
			++Out.Batches;
			if (Batch.Translator->GetKind() == EDreamLocTranslatorKind::LargeModel)
			{
				Out.Tokens += SystemTokens;
			}
		}
	}
	return Out;
}

FDreamLocTranslateResult DreamLocTranslateFlow::Run(const FDreamLocTranslateRequest& InRequest, const FProgress& InProgress, const TAtomic<bool>& InCancel,
	const FLog& InLog)
{
	using namespace DreamLocTranslateFlowLocal;
	const double Started = FPlatformTime::Seconds();
	FDreamLocTranslateResult Result;
	FPrepared Prepared;
	Prepare(InRequest, Prepared);
	FLogger Log;
	Log.OnLine = &InLog;
	Log.Secrets = Prepared.Secrets;
	auto Finish = [&Result, &Prepared, &Log, Started]()
	{
		for (FString& Error : Result.Errors)
		{
			Error = Scrub(Error, Prepared.Secrets);
			Log.Add(EDreamLocTranslateLogLevel::Error, FString(), Error);
		}
		for (FString& Warning : Result.Warnings)
		{
			Warning = Scrub(Warning, Prepared.Secrets);
		}
		Result.Seconds = FPlatformTime::Seconds() - Started;
		Result.Log = MoveTemp(Log.Lines);
		return Result;
	};
	Result.Errors = Prepared.Errors;
	Result.Warnings = Prepared.Warnings;
	for (const FString& Warning : Prepared.Warnings)
	{
		Log.Add(EDreamLocTranslateLogLevel::Warning, FString(), Warning);
	}
	if (Result.Errors.Num() > 0)
	{
		return Finish();
	}

	const FString ReportDirectory = !InRequest.ReportDirectory.IsEmpty() ? InRequest.ReportDirectory : InRequest.OwnerDirectory / TEXT("Saved/DLoc");
	for (FCulturePlan& Plan : Prepared.Cultures)
	{
		const double CultureStarted = FPlatformTime::Seconds();
		FStats Stats;
		TArray<FString> Warnings;
		FDreamLocTranslateProgress Progress;
		Progress.Culture = Plan.Culture;
		{
			int32 ToSend = 0;
			int32 LeftOut = 0;
			for (const FItem& Item : Plan.Items)
			{
				if (Item.bDecided)
				{
					LeftOut += Item.Entries.Num();
				}
				else
				{
					++ToSend;
				}
			}
			FString Line = FString::Printf(TEXT("DreamLoc translate %s: %s, %s to send to %s"), *Plan.Culture, *Count(Plan.Collected.Entries.Num(), TEXT("entry"), TEXT("entries")),
				*Count(ToSend, TEXT("text"), TEXT("texts")), *Plan.Main->GetProfile().Name);
			if (Plan.Fallback != nullptr)
			{
				Line += FString::Printf(TEXT(" (Fallback %s)"), *Plan.Fallback->GetProfile().Name);
			}
			Log.Add(EDreamLocTranslateLogLevel::Info, Plan.Culture, Line + FString::Printf(TEXT(", %d left out"), LeftOut));
		}
		for (int32 Round = 0; Round < 64 && !InCancel.Load(); ++Round)
		{
			TArray<FBatch> Batches = MakeBatches(Plan, Prepared.Settings.TranslateBatchSize);
			if (Batches.IsEmpty())
			{
				break;
			}
			Progress.Batches += Batches.Num();
			for (const FBatch& Batch : Batches)
			{
				Progress.Units += Batch.Items.Num();
			}
			if (InProgress)
			{
				InProgress(Progress);
			}
			SendAll(Batches, Plan, Prepared, InRequest, InProgress, Progress, Stats, InCancel, Log);
			Take(Batches, Plan, Prepared, InRequest.OwnerDirectory, InCancel, Warnings, Log);
		}
		for (FItem& Item : Plan.Items)
		{
			if (!Item.bDecided)
			{
				Item.bDecided = true;
				Item.Status = EDreamLocTranslateStatus::Skipped;
				Item.Reason = TEXT("cancelled before it was translated");
			}
		}

		TArray<FDreamLocTranslateRow> Rows;
		FDreamLocExchangeBatch Translated;
		Translated.SourceCulture = Prepared.Settings.SourceCulture;
		Translated.Culture = Plan.Culture;
		for (int32 Index = 0; Index < Plan.Collected.Entries.Num(); ++Index)
		{
			const FDreamLocExchangeEntry& Entry = Plan.Collected.Entries[Index];
			const FItem& Item = Plan.Items[Plan.ItemOfEntry[Index]];
			FDreamLocTranslateRow& Row = Rows.AddDefaulted_GetRef();
			Row.Culture = Plan.Culture;
			Row.File = Entry.File;
			Row.Namespace = Entry.Namespace;
			Row.Key = Entry.Key;
			Row.Source = Entry.Source;
			Row.OldText = Entry.Prev.IsEmpty() ? Entry.Text : Entry.PreviousText;
			Row.NewText = Item.Status == EDreamLocTranslateStatus::Translated ? Item.Text : FString();
			Row.RejectedText = Item.RejectedText;
			Row.Problems = Item.Problems;
			Row.Backend = Item.Backend;
			Row.Reason = Item.HandedOver.IsEmpty() ? Item.Reason
				: Item.Reason.IsEmpty() ? FString::Printf(TEXT("from %s"), *Item.HandedOver) : FString::Printf(TEXT("%s (after %s)"), *Item.Reason, *Item.HandedOver);
			Row.Reason = Scrub(Row.Reason, Prepared.Secrets);
			Row.Status = Item.Status;
			if (Item.Status == EDreamLocTranslateStatus::Translated)
			{
				FDreamLocExchangeEntry& Out = Translated.Entries.AddDefaulted_GetRef();
				Out.File = Entry.File;
				Out.Namespace = Entry.Namespace;
				Out.Key = Entry.Key;
				Out.Source = Entry.Source;
				Out.Text = Item.Text;
				Out.State = TEXT("machine");
			}
		}

		// Into the .dloc files: as machine translations, a reviewed one never touched; a stale one loses its prev.
		if (InRequest.bWrite && !InRequest.bSuggest && Translated.Entries.Num() > 0)
		{
			DreamLocExchange::FImportOptions Options;
			Options.Culture = Plan.Culture;
			Options.State = TEXT("machine");
			FDreamLocSyncOutput Output = DreamLocExchange::Import(Translated, DreamLocSync::ReadFiles(InRequest.DLocDirectory, { Plan.Culture }), Options, TEXT("DLoc"));
			if (!Output.bBlocked)
			{
				DreamLocSync::ApplyChanges(InRequest.DLocDirectory, Output, Output.Messages);
			}
			for (const FDreamLocReportLine& Message : Output.Messages)
			{
				if (Message.Diagnostic.Severity == EDreamLocSeverity::Error)
				{
					Result.Errors.Add(Message.Format());
				}
				else if (Message.Diagnostic.Severity == EDreamLocSeverity::Warning)
				{
					Warnings.Add(Message.Format());
					Log.Add(EDreamLocTranslateLogLevel::Warning, Plan.Culture, Message.Format());
				}
			}
			if (Output.bBlocked)
			{
				for (FDreamLocTranslateRow& Row : Rows)
				{
					if (Row.Status == EDreamLocTranslateStatus::Translated)
					{
						Row.Status = EDreamLocTranslateStatus::Failed;
						Row.RejectedText = MoveTemp(Row.NewText);
						Row.NewText.Reset();
						Row.Reason = TEXT("not written: a file of the culture does not read");
					}
				}
			}
		}

		for (FString& Warning : Warnings)
		{
			Warning = Scrub(Warning, Prepared.Secrets);
		}
		const double Seconds = FPlatformTime::Seconds() - CultureStarted;
		int32 Counts[3] = { 0, 0, 0 };
		for (const FDreamLocTranslateRow& Row : Rows)
		{
			++Counts[int32(Row.Status)];
		}
		Log.Add(EDreamLocTranslateLogLevel::Info, Plan.Culture, FString::Printf(TEXT("DreamLoc translate %s: %d translated, %d failed, %d skipped; %d request(s) to %s in %.1f s, %lld + %lld tokens"),
			*Plan.Culture, Counts[0], Counts[1], Counts[2], Stats.Requests, *Plan.Main->GetProfile().Name, Seconds, Stats.PromptTokens, Stats.CompletionTokens));
		if (Counts[int32(EDreamLocTranslateStatus::Failed)] > 0)
		{
			Log.Add(EDreamLocTranslateLogLevel::Error, Plan.Culture, FString::Printf(TEXT("DreamLoc translate %s: why they failed: %s"), *Plan.Culture, *WhyFailed(Rows)));
		}
		const FString Report = InRequest.bSuggest ? FString() : WriteReport(ReportDirectory, Plan, Prepared, Rows, Stats, Seconds, InCancel.Load(), Warnings, Log.Of(Plan.Culture));
		if (!Report.IsEmpty())
		{
			Result.ReportPaths.Add(Report);
			Log.Add(EDreamLocTranslateLogLevel::Info, Plan.Culture, FString::Printf(TEXT("DreamLoc translate %s: the report: %s"), *Plan.Culture, *Report));
		}
		Result.Translated += Counts[int32(EDreamLocTranslateStatus::Translated)];
		Result.Failed += Counts[int32(EDreamLocTranslateStatus::Failed)];
		Result.Skipped += Counts[int32(EDreamLocTranslateStatus::Skipped)];
		Result.Requests += Stats.Requests;
		Result.PromptTokens += Stats.PromptTokens;
		Result.CompletionTokens += Stats.CompletionTokens;
		Result.Characters += Stats.Characters;
		Result.Rows.Append(MoveTemp(Rows));
		Result.Warnings.Append(Warnings);
	}
	Result.bCancelled = InCancel.Load();
	return Finish();
}

TArray<FDreamLocTranslateRow> DreamLocTranslateFlow::LoadForReview(const FDreamLocTranslateRequest& InRequest)
{
	using namespace DreamLocTranslateFlowLocal;
	const FDreamLocSettings Settings = InRequest.Settings.IsSet() ? InRequest.Settings.GetValue() : FDreamLocSettings::LoadFromDirectory(InRequest.DLocDirectory);
	const TArray<FString> Cultures = InRequest.Cultures.Num() > 0 ? InRequest.Cultures : Settings.TargetCultures;
	const TMap<FString, TMap<FString, FString>> Files = DreamLocSync::ReadFiles(InRequest.DLocDirectory, Cultures);
	TSet<FString> Wanted;
	for (const TPair<FString, FString>& Entry : InRequest.Entries)
	{
		Wanted.Add(IdOf(Entry.Key, Entry.Value));
	}
	const TArray<IDreamLocContextProvider*> Providers = IsInGameThread() ? GetProviders() : TArray<IDreamLocContextProvider*>();
	TArray<FDreamLocTranslateRow> Rows;
	for (const FString& Culture : Cultures)
	{
		DreamLocExchange::FExportOptions Options;
		Options.Culture = Culture;
		Options.Only = { TEXT("machine") };
		Options.Files = InRequest.Files;
		TArray<FDreamLocReportLine> Messages;
		const FDreamLocExchangeBatch Batch = DreamLocExchange::Collect(Files, Options, Settings.SourceCulture, Messages);
		for (const FDreamLocExchangeEntry& Entry : Batch.Entries)
		{
			if (Wanted.Num() > 0 && !Wanted.Contains(IdOf(Entry.Namespace, Entry.Key)))
			{
				continue;
			}
			FDreamLocTranslateUnit Unit;
			Unit.Namespace = Entry.Namespace;
			Unit.Key = Entry.Key;
			Unit.Source = Entry.Source;
			Unit.Max = Entry.Max;
			Unit.At = Entry.At;
			FString Failing;
			FString Notes;
			const TPair<FString, FString> Place(Entry.Namespace, Entry.At);
			CheckOne(Unit, MakeArrayView(&Place, 1), Entry.Text, Culture, Batch.Terms, InRequest.OwnerDirectory, Providers, Failing, Notes);
			FDreamLocTranslateRow& Row = Rows.AddDefaulted_GetRef();
			Row.Culture = Culture;
			Row.File = Entry.File;
			Row.Namespace = Entry.Namespace;
			Row.Key = Entry.Key;
			Row.Source = Entry.Source;
			Row.NewText = Entry.Text;
			Row.Problems = Failing.IsEmpty() ? Notes : Notes.IsEmpty() ? Failing : Failing + TEXT("; ") + Notes;
			Row.Status = EDreamLocTranslateStatus::Translated;
		}
	}
	return Rows;
}

namespace DreamLocTranslateFlowLocal
{
	/** The live entry of that namespace and key in a document; null when there is none. */
	FDreamLocEntry* FindEntry(FDreamLocDocument& InDocument, const FString& InNamespace, const FString& InKey)
	{
		FString Namespace;
		for (FDreamLocItem& Item : InDocument.Items)
		{
			if (Item.Kind == EDreamLocItemKind::Namespace)
			{
				Namespace = Item.Namespace;
			}
			else if (Item.Kind == EDreamLocItemKind::Entry && Namespace == InNamespace && Item.Entry.Key == InKey)
			{
				return &Item.Entry;
			}
		}
		return nullptr;
	}
}

bool DreamLocTranslateFlow::ApplyReview(const FString& InDLocDirectory, TConstArrayView<FReviewEdit> InEdits, TArray<FString>& OutErrors)
{
	using namespace DreamLocTranslateFlowLocal;
	TMap<FString, TArray<const FReviewEdit*>> ByFile;
	for (const FReviewEdit& Edit : InEdits)
	{
		ByFile.FindOrAdd(Edit.Culture / Edit.File).Add(&Edit);
	}
	const int32 ErrorsBefore = OutErrors.Num();
	for (const TPair<FString, TArray<const FReviewEdit*>>& File : ByFile)
	{
		const FString Path = InDLocDirectory / File.Key;
		FDreamLocDocument Document;
		TArray<FDreamLocDiagnostic> Diagnostics;
		if (!DreamLocFile::LoadFile(Path, Document, Diagnostics))
		{
			OutErrors.Add(FString::Printf(TEXT("DLoc/%s does not read; nothing in it was changed"), *File.Key));
			continue;
		}
		const FString Before = DreamLocFile::Write(Document);
		for (const FReviewEdit* Edit : File.Value)
		{
			FDreamLocEntry* Entry = FindEntry(Document, Edit->Namespace, Edit->Key);
			if (Entry == nullptr)
			{
				OutErrors.Add(FString::Printf(TEXT("DLoc/%s has no entry '%s' (namespace %s)"), *File.Key, *Edit->Key, *Edit->Namespace));
				continue;
			}
			if (Edit->Text.IsSet())
			{
				Entry->Set(EDreamLocField::Text, Edit->Text.GetValue());
				if (Edit->State.IsEmpty())
				{
					Entry->Remove(EDreamLocField::State);
				}
				else
				{
					Entry->Set(EDreamLocField::State, Edit->State);
				}
			}
			else
			{
				Entry->Remove(EDreamLocField::Text);
				Entry->Remove(EDreamLocField::State);
			}
			// Decided against the current source: no older one to compare with.
			Entry->Remove(EDreamLocField::Prev);
		}
		if (DreamLocFile::Write(Document) != Before && !DreamLocFile::SaveFile(Path, Document))
		{
			OutErrors.Add(FString::Printf(TEXT("DLoc/%s cannot be written"), *File.Key));
		}
	}
	return OutErrors.Num() == ErrorsBefore;
}

int32 DreamLocTranslateFlow::FindEntryLine(const FString& InDLocDirectory, const FString& InCulture, const FString& InFile, const FString& InNamespace,
	const FString& InKey)
{
	using namespace DreamLocTranslateFlowLocal;
	FDreamLocDocument Document;
	TArray<FDreamLocDiagnostic> Diagnostics;
	DreamLocFile::LoadFile(InDLocDirectory / InCulture / InFile, Document, Diagnostics);
	const FDreamLocEntry* Entry = FindEntry(Document, InNamespace, InKey);
	return Entry != nullptr ? Entry->Line : 0;
}
