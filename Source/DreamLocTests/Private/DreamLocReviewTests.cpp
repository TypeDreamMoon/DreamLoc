// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocChips.h"
#include "DreamLocReviewModel.h"
#include "DreamLocTextDiff.h"
#include "DreamLocTranslateTestUtil.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamLocReviewTestsLocal
{
	using namespace DreamLocTranslateTestUtil;

	FString DescribePieces(const FString& InText)
	{
		TArray<FString> Out;
		for (const DreamLocChips::FPiece& Piece : DreamLocChips::Split(InText))
		{
			const TCHAR* Kind = TEXT("text");
			switch (Piece.Kind)
			{
			case DreamLocChips::EKind::Argument: Kind = TEXT("arg"); break;
			case DreamLocChips::EKind::Escape: Kind = TEXT("escape"); break;
			case DreamLocChips::EKind::Tag: Kind = TEXT("tag"); break;
			case DreamLocChips::EKind::LineBreak: Kind = TEXT("break"); break;
			default: break;
			}
			Out.Add(FString::Printf(TEXT("%s(%s)"), Kind, *InText.Mid(Piece.Start, Piece.Length).Replace(TEXT("\n"), TEXT("\\n"))));
		}
		return FString::Join(Out, TEXT(" "));
	}

	FString Fixed(const FString& InBefore, const FString& InAfter)
	{
		FString Text;
		int32 Cursor = 0;
		return DreamLocChips::FixEdit(InBefore, InAfter, Text, Cursor) ? FString::Printf(TEXT("%s @%d"), *Text, Cursor) : FString(TEXT("(as typed)"));
	}

	FString Keys(TConstArrayView<TSharedPtr<FDreamLocReviewEntry>> InEntries)
	{
		TArray<FString> Out;
		for (const TSharedPtr<FDreamLocReviewEntry>& Entry : InEntries)
		{
			Out.Add(Entry->Entry.Key);
		}
		return FString::Join(Out, TEXT(" "));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocChipsTest, "DreamLoc.Translate.Review.PlaceholdersAreChipsTypedAroundNotInto",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FDreamLocChipsTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocReviewTestsLocal;
	// A backtick escapes {, } and a backtick only: the one before the line break is text.
	TestEqual(TEXT("the kinds of placeholder"), DescribePieces(TEXT("你好{Name}，<b>看</>`{这`\n下")),
		TEXT("text(你好) arg({Name}) text(，) tag(<b>) text(看) tag(</>) escape(`{) text(这`) break(\\n) text(下)"));
	TestEqual(TEXT("a < that opens no tag is text"), DescribePieces(TEXT("1 < 2")), TEXT("text(1 < 2)"));
	TestEqual(TEXT("the placeholders alone"), FString::Join(DreamLocChips::Placeholders(TEXT("{0}个<b>{1}</>\n")), TEXT(" ")).Replace(TEXT("\n"), TEXT("\\n")),
		TEXT("{0} <b> {1} </> \\n"));

	auto Missing = [](const TCHAR* InSource, const TCHAR* InTranslation)
	{
		const TOptional<FString> Next = DreamLocChips::NextMissing(InSource, InTranslation);
		return Next.IsSet() ? Next->Replace(TEXT("\n"), TEXT("\\n")) : FString(TEXT("(none)"));
	};
	TestEqual(TEXT("F3: the first one missing, in the source's order"), Missing(TEXT("{0}有{1}个"), TEXT("{1} of them")), TEXT("{0}"));
	TestEqual(TEXT("one twice in the source, once in the translation"), Missing(TEXT("{0}和{0}"), TEXT("{0} and")), TEXT("{0}"));
	TestEqual(TEXT("a line break is one"), Missing(TEXT("第一行\n第二行"), TEXT("Line one Line two")), TEXT("\\n"));
	TestEqual(TEXT("none missing"), Missing(TEXT("{0}个"), TEXT("{0} of them")), TEXT("(none)"));

	TestEqual(TEXT("a character typed inside an argument goes after it"), Fixed(TEXT("{0}Gold"), TEXT("{x0}Gold")), TEXT("{0}xGold @4"));
	TestEqual(TEXT("and inside a tag"), Fixed(TEXT("<b>Hi</>"), TEXT("<bz>Hi</>")), TEXT("<b>zHi</> @4"));
	TestEqual(TEXT("backspace on an argument's brace takes it all"), Fixed(TEXT("{0}Gold"), TEXT("{0Gold")), TEXT("Gold @0"));
	TestEqual(TEXT("a selection cutting into two takes both whole"), Fixed(TEXT("a{0}b{1}c"), TEXT("a{c")), TEXT("ac @1"));
	TestEqual(TEXT("typing between two is left alone"), Fixed(TEXT("{0}{1}"), TEXT("{0}x{1}")), TEXT("(as typed)"));
	TestEqual(TEXT("deleting a whole one is left alone"), Fixed(TEXT("x{0}y"), TEXT("xy")), TEXT("(as typed)"));
	TestEqual(TEXT("a line break is deleted like a character"), Fixed(TEXT("a\nb"), TEXT("ab")), TEXT("(as typed)"));

	int32 Line = 0;
	int32 Column = 0;
	DreamLocChips::ToLineAndColumn(TEXT("ab\ncd"), 4, Line, Column);
	TestEqual(TEXT("an offset as a line and column"), FString::Printf(TEXT("%d:%d"), Line, Column), TEXT("1:1"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocTextDiffTest, "DreamLoc.Translate.Review.ASourcesChangeIsShownByCharacterOrWord",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FDreamLocTextDiffTest::RunTest(const FString& Parameters)
{
	auto Diff = [](const TCHAR* InOld, const TCHAR* InNew) { return DreamLocTextDiff::Describe(DreamLocTextDiff::Diff(InOld, InNew)); };
	TestEqual(TEXT("Chinese by character"), Diff(TEXT("开始游戏"), TEXT("开始新游戏")), TEXT("开始+(新)游戏"));
	TestEqual(TEXT("a character taken out"), Diff(TEXT("读取存档"), TEXT("读取")), TEXT("读取-(存档)"));
	TestEqual(TEXT("other languages by word"), Diff(TEXT("Start the game"), TEXT("Start a new game")), TEXT("Start -(the)+(a) +(new )game"));
	TestEqual(TEXT("a placeholder whole"), Diff(TEXT("{0}个"), TEXT("{1}个")), TEXT("-({0})+({1})个"));
	TestEqual(TEXT("nothing changed"), Diff(TEXT("同样"), TEXT("同样")), TEXT("同样"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocReviewModelTest, "DreamLoc.Translate.Review.TheModelFiltersCountsAndWritesDecisions",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FDreamLocReviewModelTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocReviewTestsLocal;
	const FScenarioRef S = MakeShared<FScenario, ESPMode::ThreadSafe>();
	if (!S->Begin(*this, TEXT("ReviewModel")))
	{
		return false;
	}
	S->Write(TEXT("dloc.ini"), TEXT("[DreamLoc]\nSourceCulture=zh-Hans\nTargetCultures=en,ja\n"));
	S->Write(TEXT("en/UI/Menu.dloc"), MakeDloc(TEXT("en"), TEXT("Menu"), {
		{ TEXT("Start"), TEXT("开始游戏") },
		{ TEXT("Quit"), TEXT("退出"), TEXT("Quit"), TEXT("machine") },
		{ TEXT("Back"), TEXT("返回"), TEXT("Back"), TEXT("done") },
		{ TEXT("Load"), TEXT("读取存档"), TEXT("Load"), TEXT("machine"), TEXT("读取") },
		{ TEXT("Gold"), TEXT("{0}金币"), TEXT("Gold"), TEXT("draft") },
		{ TEXT("Return"), TEXT("返回") } }));
	S->Write(TEXT("en/Story/Intro.dloc"), MakeDloc(TEXT("en"), TEXT("Intro"), { { TEXT("Line"), TEXT("很久以前"), TEXT("Long ago"), TEXT("done") } }));
	S->Write(TEXT("ja/UI/Menu.dloc"), MakeDloc(TEXT("ja"), TEXT("Menu"), { { TEXT("Start"), TEXT("开始游戏"), TEXT("ゲーム開始"), TEXT("done") } }));

	FDreamLocReviewModel Model;
	Model.Load(S->DLoc, S->Root, TEXT("en"), TEXT("ja"));
	for (const FString& Message : Model.GetMessages())
	{
		AddInfo(Message);
	}
	TestEqual(TEXT("every entry, in file order"), Keys(Model.Filter(EDreamLocReviewFilter::All)), TEXT("Line Start Quit Back Load Gold Return"));
	TestEqual(TEXT("untranslated"), Keys(Model.Filter(EDreamLocReviewFilter::Untranslated)), TEXT("Start Return"));
	TestEqual(TEXT("stale"), Keys(Model.Filter(EDreamLocReviewFilter::Stale)), TEXT("Load"));
	TestEqual(TEXT("to review: machine and draft"), Keys(Model.Filter(EDreamLocReviewFilter::Review)), TEXT("Quit Gold"));
	TestEqual(TEXT("reviewed"), Keys(Model.Filter(EDreamLocReviewFilter::Done)), TEXT("Line Back"));
	TestEqual(TEXT("with problems: a placeholder lost"), Keys(Model.Filter(EDreamLocReviewFilter::Problems)), TEXT("Gold"));
	TestEqual(TEXT("counted as filtered"), Model.Count(EDreamLocReviewFilter::Review), 2);
	TestEqual(TEXT("a search: key, source or translation, any case"), Keys(Model.Filter(EDreamLocReviewFilter::All, TEXT("load"))), TEXT("Load"));
	TestEqual(TEXT("a search in the sources"), Keys(Model.Filter(EDreamLocReviewFilter::All, TEXT("返回"))), TEXT("Back Return"));
	TestEqual(TEXT("one file"), Keys(Model.Filter(EDreamLocReviewFilter::All, FString(), TEXT("Story/Intro"))), TEXT("Line"));
	TestEqual(TEXT("one folder"), Keys(Model.Filter(EDreamLocReviewFilter::Untranslated, FString(), TEXT("UI/"))), TEXT("Start Return"));
	TestEqual(TEXT("the files"), FString::Join(Model.GetFiles(), TEXT(" ")), TEXT("Story/Intro UI/Menu"));
	const FDreamLocReviewProgress Progress = Model.GetProgress();
	TestEqual(TEXT("progress"), FString::Printf(TEXT("%d done %d review %d stale %d untranslated of %d"), Progress.Done, Progress.Review, Progress.Stale,
		Progress.Untranslated, Progress.Total()), TEXT("2 done 2 review 1 stale 2 untranslated of 7"));
	TestEqual(TEXT("a file's progress"), Model.GetProgress(TEXT("Story/Intro")).Done, 1);

	const TSharedPtr<FDreamLocReviewEntry> Start = Model.Find(TEXT("Menu"), TEXT("Start"));
	const TSharedPtr<FDreamLocReviewEntry> Load = Model.Find(TEXT("Menu"), TEXT("Load"));
	const TSharedPtr<FDreamLocReviewEntry> Return = Model.Find(TEXT("Menu"), TEXT("Return"));
	TestTrue(TEXT("found by namespace and key"), Start.IsValid() && Load.IsValid() && Return.IsValid());
	if (!Start.IsValid() || !Load.IsValid() || !Return.IsValid())
	{
		S->End();
		return false;
	}
	TestEqual(TEXT("the reference culture's translation"), Start->Reference, TEXT("ゲーム開始"));
	TestEqual(TEXT("a stale entry: its translation, and what its source was"), Load->Translation + TEXT("|") + Load->Entry.Prev, TEXT("Load|读取"));
	TestEqual(TEXT("a reviewed translation of the same source elsewhere"), FString::Join(Model.FindReviewed(TEXT("返回"), Return.Get()), TEXT(",")), TEXT("Back"));
	TestTrue(TEXT("a check of a typed text"), Model.Check(*Model.Find(TEXT("Menu"), TEXT("Gold")), TEXT("{0} gold")).IsEmpty());

	// Decisions: a pass, a stale one passed (its prev goes), a reviewed one cleared.
	DreamLocTranslateFlow::FReviewEdit Pass = Model.MakeEdit(*Start);
	Pass.Text = TEXT("Start game");
	Pass.State = TEXT("done");
	DreamLocTranslateFlow::FReviewEdit Amend = Model.MakeEdit(*Load);
	Amend.Text = TEXT("Load save");
	Amend.State = TEXT("done");
	const DreamLocTranslateFlow::FReviewEdit Clear = Model.MakeEdit(*Model.Find(TEXT("Menu"), TEXT("Back")));
	TArray<FString> Errors;
	TestTrue(TEXT("written"), Model.Apply({ Pass, Amend, Clear }, Errors));
	TestEqual(TEXT("no errors"), FString::Join(Errors, TEXT("; ")), FString());
	auto Describe = [&S](const TCHAR* InKey)
	{
		const FDreamLocEntry Entry = S->Entry(TEXT("en/UI/Menu.dloc"), InKey);
		return FScenario::Field(Entry, EDreamLocField::Text) + TEXT("|") + FScenario::Field(Entry, EDreamLocField::State) + TEXT("|") + FScenario::Field(Entry, EDreamLocField::Prev);
	};
	TestEqual(TEXT("passed"), Describe(TEXT("Start")), TEXT("Start game|done|"));
	TestEqual(TEXT("a stale one decided on loses its prev"), Describe(TEXT("Load")), TEXT("Load save|done|"));
	TestEqual(TEXT("a reviewed one can be cleared by a person"), Describe(TEXT("Back")), TEXT("||"));
	TestEqual(TEXT("read again after"), Keys(Model.Filter(EDreamLocReviewFilter::Done)), TEXT("Line Start Load"));
	S->End();
	return true;
}

#endif
