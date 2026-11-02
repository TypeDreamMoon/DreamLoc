// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocEditorModule.h"
#include "DreamLocModule.h"
#include "DreamLocTextSource.h"
#include "DreamLocTestUtil.h"
#include "Internationalization/TextLocalizationManager.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamLocLiveTestsLocal
{
	const TCHAR* const Namespace = TEXT("DreamLoc.Tests.Live");
	const TCHAR* const Key = TEXT("Title.Text");
	const TCHAR* const Source = TEXT("设置");

	/** What the steps share: the folder mounted as a layer, the file in it, and one text held across every step as a widget holds it. */
	struct FState
	{
		FString Root;
		FString File;
		FText Title;
	};

	FString MakeFile(const TCHAR* InText, const TCHAR* InPrevious = nullptr)
	{
		FString Body = FString::Printf(TEXT("dloc 1\nculture en\nnamespace %s\n\n%s {\n"), Namespace, Key);
		if (InPrevious != nullptr)
		{
			Body += FString::Printf(TEXT("    prev  \"%s\"\n"), InPrevious);
		}
		Body += FString::Printf(TEXT("    src   \"%s\"\n"), Source);
		if (InText != nullptr)
		{
			Body += FString::Printf(TEXT("    text  \"%s\"\n"), InText);
		}
		return Body + TEXT("}\n");
	}

	/**
	 * Writes the file (when there is a text to write), then waits -- through the directory watcher, the editor module's
	 * reload and the engine's asynchronous load -- until the held text reads InExpected, or the time runs out.
	 */
	class FWriteAndWait : public IAutomationLatentCommand
	{
	public:
		FWriteAndWait(FAutomationTestBase* InTest, TSharedRef<FState> InState, FString InStep, TOptional<FString> InFileText, FString InExpected,
			TFunction<void()> InAction = nullptr)
			: Test(InTest), State(InState), Step(MoveTemp(InStep)), FileText(MoveTemp(InFileText)), Expected(MoveTemp(InExpected)), Action(MoveTemp(InAction))
		{
		}

		virtual bool Update() override
		{
			if (StartTime < 0.0)
			{
				StartTime = FPlatformTime::Seconds();
				if (FileText.IsSet())
				{
					DreamLocTestUtil::WriteText(State->File, FileText.GetValue());
				}
				if (Action)
				{
					Action();
				}
				return false;
			}
			const FString Shown = State->Title.ToString();
			if (Shown == Expected)
			{
				Test->AddInfo(FString::Printf(TEXT("%s: '%s' after %.2f s"), *Step, *Shown, FPlatformTime::Seconds() - StartTime));
				return true;
			}
			if (FPlatformTime::Seconds() - StartTime > 15.0)
			{
				Test->AddError(FString::Printf(TEXT("%s: still '%s' after 15 s, expected '%s'"), *Step, *Shown, *Expected));
				return true;
			}
			return false;
		}

	private:
		FAutomationTestBase* Test;
		TSharedRef<FState> State;
		FString Step;
		TOptional<FString> FileText;
		FString Expected;
		TFunction<void()> Action;
		double StartTime = -1.0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocLivePreviewTest, "DreamLoc.Live.PreviewAndHotReload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLocLivePreviewTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocLiveTestsLocal;
	FTextLocalizationManager& Manager = FTextLocalizationManager::Get();
	if (Manager.IsGameLocalizationPreviewEnabled())
	{
		AddError(TEXT("a game localization preview is already on; this test turns it on and off itself"));
		return false;
	}
	const TSharedPtr<FDreamLocTextSource> TextSource = FDreamLocModule::Get().GetTextSource();
	if (!TestTrue(TEXT("DreamLoc's text source is registered"), TextSource.IsValid()))
	{
		return false;
	}

	TSharedRef<FState> State = MakeShared<FState>();
	State->Root = DreamLocTestUtil::MakeTempDirectory(TEXT("Live"));
	State->File = State->Root / TEXT("en/UI/Live.dloc");
	DreamLocTestUtil::WriteText(State->File, MakeFile(TEXT("Settings")));
	TextSource->MountLayer(FDreamLocLayer{ 2, TEXT("Test.Live"), State->Root });
	FDreamLocEditorModule::Get().SyncWatches();
	TestTrue(TEXT("the mounted folder is watched"), FDreamLocEditorModule::Get().GetWatchedDirectories().Contains(State->Root));

	State->Title = FText::AsLocalizable_Advanced(Namespace, Key, Source);
	TestEqual(TEXT("without a preview the editor shows the source"), State->Title.ToString(), FString(Source));

	Manager.EnableGameLocalizationPreview(TEXT("en"));
	Manager.WaitForAsyncTasks();
	TestEqual(TEXT("the game preview in en shows the translation"), State->Title.ToString(), FString(TEXT("Settings")));
	TestEqual(TEXT("the preview culture is remembered for reloads"), TextSource->GetLastPreviewCulture(), FString(TEXT("en")));

	ADD_LATENT_AUTOMATION_COMMAND(FWriteAndWait(this, State, TEXT("a new translation saved"), MakeFile(TEXT("Options")), TEXT("Options")));
	ADD_LATENT_AUTOMATION_COMMAND(FWriteAndWait(this, State, TEXT("the source changed since (prev)"), MakeFile(TEXT("Options"), TEXT("设定")), Source));
	ADD_LATENT_AUTOMATION_COMMAND(FWriteAndWait(this, State, TEXT("reviewed (prev removed)"), MakeFile(TEXT("Options")), TEXT("Options")));
	ADD_LATENT_AUTOMATION_COMMAND(FWriteAndWait(this, State, TEXT("the translation deleted"), MakeFile(nullptr), Source));
	ADD_LATENT_AUTOMATION_COMMAND(FWriteAndWait(this, State, TEXT("translated again"), MakeFile(TEXT("Preferences")), TEXT("Preferences")));
	ADD_LATENT_AUTOMATION_COMMAND(FWriteAndWait(this, State, TEXT("the preview turned off"), TOptional<FString>(), Source,
		[]() { FTextLocalizationManager::Get().DisableGameLocalizationPreview(); }));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, State]()
	{
		const TSharedPtr<FDreamLocTextSource> LiveSource = FDreamLocModule::Get().GetTextSource();
		if (LiveSource.IsValid())
		{
			LiveSource->UnmountLayer(State->Root);
		}
		FDreamLocEditorModule::Get().SyncWatches();
		DreamLocTestUtil::DeleteDirectory(State->Root);
		TestFalse(TEXT("the preview is off again"), FTextLocalizationManager::Get().IsGameLocalizationPreviewEnabled());
		TestTrue(TEXT("file changes caused reloads"), FDreamLocEditorModule::Get().GetReloadCount() >= 5);
		return true;
	}));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
