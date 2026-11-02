// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocCheck.h"
#include "DreamLocTestUtil.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamLocCheckTestsLocal
{
	/** The plugin's Tests/Check folder: the samples, each saying in a comment what the check must report for it. */
	FString SamplesDirectory()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DreamLoc"));
		FString Directory = Plugin.IsValid() ? FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / TEXT("Tests/Check")) : FString();
		FPaths::NormalizeDirectoryName(Directory);
		return Directory;
	}

	/** `DLOC1006@13:5` for each finding the file's `// expect:` lines name, sorted. */
	TArray<FString> ReadExpectations(const FString& InPath)
	{
		TArray<FString> Expected;
		TArray<FString> Lines;
		FFileHelper::LoadFileToStringArray(Lines, *InPath);
		for (const FString& Line : Lines)
		{
			FString Rest;
			if (Line.Split(TEXT("// expect:"), nullptr, &Rest))
			{
				TArray<FString> Words;
				Rest.ParseIntoArrayWS(Words);
				Expected.Append(Words);
			}
		}
		Expected.Sort();
		return Expected;
	}

	FString Name(const FDreamLocDiagnostic& InDiagnostic)
	{
		return FString::Printf(TEXT("%s@%d:%d"), *InDiagnostic.GetCodeName(), InDiagnostic.Line, InDiagnostic.Column);
	}

	FString Codes(const FDreamLocCheckResult& InResult)
	{
		TArray<FString> Names;
		for (const FDreamLocReportLine& Line : InResult.Lines)
		{
			Names.Add(Name(Line.Diagnostic));
		}
		Names.Sort();
		return FString::Join(Names, TEXT(" "));
	}

	void WriteFile(const FString& InPath, const FString& InText)
	{
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(InPath), /*Tree*/ true);
		DreamLocTestUtil::WriteText(InPath, InText);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocCheckSamplesTest, "DreamLoc.Check.SamplesReportWhatTheySay",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocCheckSamplesTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocCheckTestsLocal;
	const FString Root = SamplesDirectory();
	if (!TestTrue(TEXT("the samples are there"), IFileManager::Get().DirectoryExists(*Root)))
	{
		return false;
	}
	FDreamLocCheckOptions Options;
	Options.Roots.Add(FDreamLocCheckRoot{ Root, 1 });
	const FDreamLocCheckResult Result = DreamLocCheck::Run(Options);

	TMap<FString, TArray<FString>> Reported;
	TMap<FString, TArray<FString>> Messages;
	TArray<FString> FolderLines;
	for (const FDreamLocReportLine& Line : Result.Lines)
	{
		FString File = Line.File;
		FPaths::NormalizeFilename(File);
		if (File.EndsWith(TEXT(".dloc")))
		{
			Reported.FindOrAdd(File).Add(Name(Line.Diagnostic));
			Messages.FindOrAdd(File).Add(Line.Format());
		}
		else
		{
			FolderLines.Add(Line.Format());
		}
	}

	TArray<FString> Samples;
	IFileManager::Get().FindFilesRecursive(Samples, *Root, TEXT("*.dloc"), /*Files*/ true, /*Directories*/ false);
	Samples.Sort();
	TestTrue(TEXT("there are samples"), Samples.Num() >= 15);
	int32 Findings = 0;
	for (FString& Sample : Samples)
	{
		FPaths::NormalizeFilename(Sample);
		TArray<FString> Actual = Reported.FindRef(Sample);
		Actual.Sort();
		const TArray<FString> Expected = ReadExpectations(Sample);
		Findings += Expected.Num();
		FString Relative = Sample;
		FPaths::MakePathRelativeTo(Relative, *(Root + TEXT("/")));
		TestEqual(FString::Printf(TEXT("%s reports what it says"), *Relative), FString::Join(Actual, TEXT(" ")), FString::Join(Expected, TEXT(" ")));
		if (Actual != Expected)
		{
			for (const FString& Message : Messages.FindRef(Sample))
			{
				AddInfo(TEXT("    ") + Message);
			}
		}
	}
	AddInfo(FString::Printf(TEXT("%d sample file(s), %d finding(s) expected"), Samples.Num(), Findings));
	TestTrue(TEXT("the untranslated text of en is counted"), FolderLines.ContainsByPredicate([](const FString& InLine)
	{
		return InLine.Contains(TEXT("DLOC3016")) && InLine.Contains(TEXT("/en:")) && InLine.Contains(TEXT("1 of"));
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocCheckOneFileTest, "DreamLoc.Check.OneFileStillHasItsCulturesTerms",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocCheckOneFileTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocCheckTestsLocal;
	const FString Root = SamplesDirectory();
	FDreamLocCheckOptions Options;
	Options.Roots.Add(FDreamLocCheckRoot{ Root, 1 });
	Options.File = Root / TEXT("en/Terms.dloc");
	const FDreamLocCheckResult Result = DreamLocCheck::Run(Options);
	TestEqual(TEXT("one file"), Result.Files, 1);
	TestEqual(TEXT("the terms of en/_terms.dloc apply, and nothing across files is reported"), Codes(Result), FString(TEXT("DLOC2007@8:5 DLOC2008@13:5")));

	Options.File = Root / TEXT("en/DupB.dloc");
	TestEqual(TEXT("a key in another file is not looked for when one file is checked"), Codes(DreamLocCheck::Run(Options)), FString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocCheckLayersTest, "DreamLoc.Check.AHigherLayerTranslatingAKeyDifferentlyIsReported",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocCheckLayersTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocCheckTestsLocal;
	const FString Work = DreamLocTestUtil::MakeTempDirectory(TEXT("CheckLayers"));
	ON_SCOPE_EXIT { DreamLocTestUtil::DeleteDirectory(Work); };
	const TCHAR* const File = TEXT("dloc 1\nculture en\nnamespace Game\n\nQuit {\n    src   \"退出\"\n    text  \"%s\"\n}\n");
	WriteFile(Work / TEXT("Project/en/Menu.dloc"), FString(File).Replace(TEXT("%s"), TEXT("Quit")));
	WriteFile(Work / TEXT("Mod/en/Menu.dloc"), FString(File).Replace(TEXT("%s"), TEXT("Leave")));
	WriteFile(Work / TEXT("Same/en/Menu.dloc"), FString(File).Replace(TEXT("%s"), TEXT("Quit")));

	FDreamLocCheckOptions Options;
	Options.Roots.Add(FDreamLocCheckRoot{ Work / TEXT("Project"), 1 });
	Options.Roots.Add(FDreamLocCheckRoot{ Work / TEXT("Mod"), 2 });
	const FDreamLocCheckResult Result = DreamLocCheck::Run(Options);
	TestEqual(TEXT("the mod's translation is reported at its entry"), Codes(Result), FString(TEXT("DLOC3019@5:1")));
	TestTrue(TEXT("on the mod's file"), Result.Lines.Num() == 1 && Result.Lines[0].File.Contains(TEXT("/Mod/")));

	Options.Roots[1] = FDreamLocCheckRoot{ Work / TEXT("Same"), 2 };
	TestEqual(TEXT("the same translation in both: nothing"), Codes(DreamLocCheck::Run(Options)), FString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocCheckWidthTest, "DreamLoc.Check.WidthCountsWhatShows",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocCheckWidthTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("full-width counts 2"), DreamLocCheck::DisplayWidth(TEXT("设置 OK")), 7);
	TestEqual(TEXT("an argument counts nothing, a plural its widest form"),
		DreamLocCheck::ShownWidth(TEXT("Apply {Count} {Count}|plural(one=item,other=items)")), 12);
	TestEqual(TEXT("quoted forms"), DreamLocCheck::ShownWidth(TEXT("{N}|plural(one=\"a, b\",other=c)")), 4);
	TestEqual(TEXT("gender forms are positional"), DreamLocCheck::ShownWidth(TEXT("{G}|gender(he,she)")), 3);
	TestEqual(TEXT("rich-text tags count nothing"), DreamLocCheck::ShownWidth(TEXT("<b>粗体</b> <img=Icon/>")), 5);
	TestEqual(TEXT("an escaped brace is a character"), DreamLocCheck::ShownWidth(TEXT("`{x`}")), 3);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
