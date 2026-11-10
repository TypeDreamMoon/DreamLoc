// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Async/Async.h"
#include "DreamLocFile.h"
#include "DreamLocTestUtil.h"
#include "DreamLocTranslateConfig.h"
#include "DreamLocTranslateFake.h"
#include "DreamLocTranslateFlow.h"
#include "Misc/AutomationTest.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/ScopeLock.h"

namespace DreamLocTranslateTestUtil
{
	struct FEntry
	{
		FString Key;
		FString Src;
		FString Text;
		FString State;
		FString Prev;
		int32 Max = -1;
	};

	/** A .dloc of one namespace. */
	inline FString MakeDloc(const FString& InCulture, const FString& InNamespace, TConstArrayView<FEntry> InEntries)
	{
		FString Out = FString::Printf(TEXT("dloc 1\nculture %s\nnamespace %s\n"), *InCulture, *DreamLocFile::QuoteWordIfNeeded(InNamespace, false));
		for (const FEntry& Entry : InEntries)
		{
			Out += FString::Printf(TEXT("\n%s {\n"), *DreamLocFile::QuoteWordIfNeeded(Entry.Key, true));
			if (Entry.Max >= 0)
			{
				Out += FString::Printf(TEXT("    max   %d\n"), Entry.Max);
			}
			if (!Entry.Prev.IsEmpty())
			{
				Out += TEXT("    prev  ") + DreamLocFile::Quote(Entry.Prev) + TEXT("\n");
			}
			Out += TEXT("    src   ") + DreamLocFile::Quote(Entry.Src) + TEXT("\n");
			if (!Entry.Text.IsEmpty())
			{
				Out += TEXT("    text  ") + DreamLocFile::Quote(Entry.Text) + TEXT("\n");
			}
			if (!Entry.State.IsEmpty())
			{
				Out += TEXT("    state ") + Entry.State + TEXT("\n");
			}
			Out += TEXT("}\n");
		}
		return Out;
	}

	/** Every log line written while it lives, from any thread. */
	class FLogCapture : public FOutputDevice
	{
	public:
		FLogCapture() { GLog->AddOutputDevice(this); }
		virtual ~FLogCapture() override { GLog->RemoveOutputDevice(this); }

		virtual void Serialize(const TCHAR* InText, ELogVerbosity::Type, const FName& InCategory) override
		{
			FScopeLock ScopeLock(&Lock);
			Text += InCategory.ToString() + TEXT(": ") + InText + TEXT("\n");
		}
		virtual bool CanBeUsedOnAnyThread() const override { return true; }
		virtual bool CanBeUsedOnMultipleThreads() const override { return true; }

		FString Get() const
		{
			FScopeLock ScopeLock(&Lock);
			return Text;
		}

	private:
		mutable FCriticalSection Lock;
		FString Text;
	};

	/**
	 * A project in a temporary folder -- DLoc/dloc.ini, DLoc/<culture>/*.dloc -- the fake services, and the user's
	 * translators.ini moved into the folder, so nothing reads or writes the real one.
	 */
	struct FScenario : public TSharedFromThis<FScenario, ESPMode::ThreadSafe>
	{
		FString Root;
		FString DLoc;
		TSharedPtr<DreamLocTranslateFake::FServices, ESPMode::ThreadSafe> Fake;
		TFuture<FDreamLocTranslateResult> Future;
		FDreamLocTranslateResult Result;
		TAtomic<bool> Cancel{ false };
		/** Called from the run's threads. */
		TFunction<void(FScenario&, const FDreamLocTranslateProgress&)> OnProgress;
		TUniquePtr<FLogCapture> Log;
		FString LogText;
		/** The run's log lines as they were written, from the run's threads. */
		TArray<FDreamLocTranslateLogLine> LogLines;
		mutable FCriticalSection LogLinesLock;

		bool Begin(FAutomationTestBase& InTest, const TCHAR* InName)
		{
			Root = DreamLocTestUtil::MakeTempDirectory(InName);
			DLoc = Root / TEXT("DLoc");
			DreamLocTranslateConfig::SetUserConfigPath(Root / TEXT("translators.ini"));
			Fake = MakeShared<DreamLocTranslateFake::FServices, ESPMode::ThreadSafe>();
			FString Error;
			if (!Fake->Start(Error))
			{
				InTest.AddError(Error);
				End();
				return false;
			}
			return true;
		}

		void Write(const FString& InRelative, const FString& InText) const
		{
			DreamLocTestUtil::WriteText(DLoc / InRelative, InText);
		}

		FString Read(const FString& InRelative) const
		{
			FString Text;
			FFileHelper::LoadFileToString(Text, *(DLoc / InRelative));
			return Text;
		}

		/** The entry of that key in DLoc/<InRelative> as the file has it now; an empty one when there is none. */
		FDreamLocEntry Entry(const FString& InRelative, const FString& InKey) const
		{
			FDreamLocDocument Document;
			TArray<FDreamLocDiagnostic> Diagnostics;
			DreamLocFile::Parse(Read(InRelative), Document, Diagnostics);
			FDreamLocEntry Found;
			Document.ForEachEntry([&Found, &InKey](const FString&, const FDreamLocEntry& InEntry, bool)
			{
				if (InEntry.Key == InKey)
				{
					Found = InEntry;
				}
			});
			return Found;
		}

		static FString Field(const FDreamLocEntry& InEntry, EDreamLocField InField)
		{
			const FString* Value = InEntry.Find(InField);
			return Value != nullptr ? *Value : FString();
		}

		FDreamLocTranslateRequest MakeRequest() const
		{
			FDreamLocTranslateRequest Request;
			Request.DLocDirectory = DLoc;
			Request.OwnerDirectory = Root;
			Request.Manifests = TArray<FDreamLocExtractManifest>();
			Request.ReportDirectory = Root / TEXT("Saved/DLoc");
			Request.RetryDelaySeconds = 0.01f;
			return Request;
		}

		void Run(const FDreamLocTranslateRequest& InRequest, bool bInCaptureLog = false)
		{
			if (bInCaptureLog)
			{
				Log = MakeUnique<FLogCapture>();
			}
			const TSharedRef<FScenario, ESPMode::ThreadSafe> Self = AsShared();
			Future = Async(EAsyncExecution::Thread, [Self, InRequest]()
			{
				return DreamLocTranslateFlow::Run(InRequest, [Self](const FDreamLocTranslateProgress& InProgress)
				{
					if (Self->OnProgress)
					{
						Self->OnProgress(*Self, InProgress);
					}
				}, Self->Cancel, [Self](const FDreamLocTranslateLogLine& InLine)
				{
					FScopeLock ScopeLock(&Self->LogLinesLock);
					Self->LogLines.Add(InLine);
				});
			});
		}

		/** The log lines written so far, each with its time, one a line. */
		FString LogLinesText() const
		{
			FScopeLock ScopeLock(&LogLinesLock);
			TArray<FString> Out;
			for (const FDreamLocTranslateLogLine& Line : LogLines)
			{
				Out.Add(Line.Format());
			}
			return FString::Join(Out, TEXT("\n"));
		}

		/** The log line of that level that has every one of these in it; null when none does. */
		const FDreamLocTranslateLogLine* FindLogLine(EDreamLocTranslateLogLevel InLevel, TConstArrayView<FString> InParts) const
		{
			FScopeLock ScopeLock(&LogLinesLock);
			return LogLines.FindByPredicate([InLevel, &InParts](const FDreamLocTranslateLogLine& InLine)
			{
				if (InLine.Level != InLevel)
				{
					return false;
				}
				for (const FString& Part : InParts)
				{
					if (!InLine.Text.Contains(Part, ESearchCase::CaseSensitive))
					{
						return false;
					}
				}
				return true;
			});
		}

		const FDreamLocTranslateRow* Row(const FString& InKey, const FString& InCulture = TEXT("en")) const
		{
			return Result.Rows.FindByPredicate([&InKey, &InCulture](const FDreamLocTranslateRow& InRow) { return InRow.Key == InKey && InRow.Culture == InCulture; });
		}

		/** Every entry the large model was sent, across its requests. */
		TArray<TSharedPtr<FJsonObject>> SentToModel(const FString& InModel = FString()) const
		{
			TArray<TSharedPtr<FJsonObject>> Out;
			for (const DreamLocTranslateFake::FCall& Call : Fake->GetCalls(TEXT("openai")))
			{
				const TSharedPtr<FJsonObject> Body = DreamLocTranslateFake::Parse(Call.Body);
				if (!Body.IsValid() || (!InModel.IsEmpty() && Body->GetStringField(TEXT("model")) != InModel))
				{
					continue;
				}
				const TArray<TSharedPtr<FJsonValue>>& Messages = Body->GetArrayField(TEXT("messages"));
				const TSharedPtr<FJsonObject> User = Messages.Num() == 2 ? DreamLocTranslateFake::Parse(Messages[1]->AsObject()->GetStringField(TEXT("content"))) : nullptr;
				const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
				if (User.IsValid() && User->TryGetArrayField(TEXT("entries"), Entries))
				{
					for (const TSharedPtr<FJsonValue>& Entry : *Entries)
					{
						Out.Add(Entry->AsObject());
					}
				}
			}
			return Out;
		}

		static FString StatusName(const FDreamLocTranslateRow* InRow)
		{
			if (InRow == nullptr)
			{
				return TEXT("(no row)");
			}
			switch (InRow->Status)
			{
			case EDreamLocTranslateStatus::Translated: return TEXT("translated");
			case EDreamLocTranslateStatus::Failed: return TEXT("failed: ") + InRow->Reason + TEXT(" ") + InRow->Problems;
			default: return TEXT("skipped: ") + InRow->Reason;
			}
		}

		void End()
		{
			if (Log.IsValid())
			{
				LogText = Log->Get();
				Log.Reset();
			}
			if (Fake.IsValid())
			{
				Fake->Stop();
			}
			DreamLocTranslateConfig::SetUserConfigPath(FString());
			if (!Root.IsEmpty())
			{
				DreamLocTestUtil::DeleteDirectory(Root);
			}
		}
	};

	using FScenarioRef = TSharedRef<FScenario, ESPMode::ThreadSafe>;

	/** Waits for the scenario's run, then checks it, then ends the scenario. */
	class FAfterRun : public IAutomationLatentCommand
	{
	public:
		FAfterRun(FAutomationTestBase* InTest, FScenarioRef InScenario, TFunction<void(FScenario&)> InCheck, double InTimeout = 60.0)
			: Test(InTest), Scenario(InScenario), Check(MoveTemp(InCheck)), Timeout(InTimeout)
		{
		}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Began == 0.0)
			{
				Began = Now;
			}
			if (!Scenario->Future.IsReady())
			{
				if (Now - Began > Timeout && !bTimedOut)
				{
					Test->AddError(TEXT("the translation run did not finish in time; cancelled"));
					bTimedOut = true;
					Scenario->Cancel = true;
				}
				return Now - Began > Timeout + 30.0;
			}
			Scenario->Result = Scenario->Future.Get();
			if (Scenario->Log.IsValid())
			{
				Scenario->LogText = Scenario->Log->Get();
				Scenario->Log.Reset();
			}
			Check(*Scenario);
			Scenario->End();
			return true;
		}

	private:
		FAutomationTestBase* Test;
		FScenarioRef Scenario;
		TFunction<void(FScenario&)> Check;
		double Timeout;
		double Began = 0.0;
		bool bTimedOut = false;
	};

	/** The ids and sources of a request's entries. */
	inline TArray<TPair<int32, FString>> Sources(const DreamLocTranslateFake::FChat& InChat)
	{
		TArray<TPair<int32, FString>> Out;
		for (const TSharedPtr<FJsonObject>& Entry : InChat.Entries)
		{
			Out.Emplace(int32(Entry->GetNumberField(TEXT("id"))), Entry->GetStringField(TEXT("src")));
		}
		return Out;
	}
}
