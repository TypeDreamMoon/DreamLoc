// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocFile.h"
#include "DreamLocTestUtil.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamLocFormatTestsLocal
{
	/** The example in the design, character for character: it is already in the writer's layout. */
	const TCHAR* const DesignExample = TEXT(R"dloc(// DLoc/en/DUI/UI/Settings.dloc
// at / note / max / src / prev 由 dloc sync 维护；text / state / 注释归人写。
dloc 1
culture en
namespace /Game/UI/Settings/WBP_SettingsScreen

Title.Text {
    at    DUI/UI/Settings.dui#Title
    src   "设置"
    text  "Settings"
    state done
}

Apply.Text {
    at    DUI/UI/Settings.dui#Footer/Apply
    note  "底部按钮，点了立刻生效"
    max   16
    src   "应用 {Count} 项更改"
    text  "Apply {Count} {Count}|plural(one=change,other=changes)"
    state machine
}

Hint.Text {
    at    DUI/UI/Settings.dui#Footer/Hint
    prev  "按 {Key} 返回"
    src   "按 {Key} 返回上一页"
    text  "Press {Key} to go back"
    state done
}

Row.UIButton_0.Text {
    at    DUI/UI/Settings.dui#Rows/Row
    src   "重置"
}
)dloc");

	/** A comment everywhere one can stand. */
	const TCHAR* const Commented = TEXT(R"dloc(// header comment

// after a blank line
dloc 1 // the version
culture ja // the culture

// above the namespace
namespace UI // after the namespace

// above the entry
//
// still above the entry
Title.Text { // after the brace
    // above at
    at    DUI/A.dui#Title // after at
    src   "标题"
    // above text
    text  "タイトル"
    // before the closing brace
} // after the closing brace

removed { // after removed
    namespace Old

    // above a removed entry
    Gone.Text {
        src   "旧"
        text  "古い"
    }
    // before removed closes
}

// at the end of the file
)dloc");

	const TCHAR* const Terms = TEXT(R"dloc(dloc 1
culture en
terms

term "义体" { text "Cyberware" }
term "以太" { text "Aether" note "游戏内能量单位，首字母大写" }
keep "DreamMoon" // 不翻译
)dloc");

	const FDreamLocEntry* FindEntry(const FDreamLocDocument& InDocument, const FString& InKey)
	{
		for (const FDreamLocItem& Item : InDocument.Items)
		{
			if (Item.Kind == EDreamLocItemKind::Entry && Item.Entry.Key == InKey)
			{
				return &Item.Entry;
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocFormatDesignExampleTest, "DreamLoc.Format.RoundTrip.DesignExample",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocFormatDesignExampleTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocFormatTestsLocal;
	FDreamLocDocument Document;
	TArray<FDreamLocDiagnostic> Diagnostics;
	TestTrue(TEXT("the design's example reads"), DreamLocFile::Parse(DesignExample, Document, Diagnostics));
	TestEqual(TEXT("with nothing to report"), DreamLocTestUtil::Describe(Diagnostics), FString());
	TestEqual(TEXT("two header comments"), Document.HeaderComments.Num(), 2);
	TestEqual(TEXT("culture"), Document.Culture, FString(TEXT("en")));

	TArray<FString> Keys;
	TArray<FString> Namespaces;
	Document.ForEachEntry([&Keys, &Namespaces](const FString& InNamespace, const FDreamLocEntry& InEntry, bool)
	{
		Keys.Add(InEntry.Key);
		Namespaces.AddUnique(InNamespace);
	});
	TestEqual(TEXT("four entries in file order"), FString::Join(Keys, TEXT(" ")), FString(TEXT("Title.Text Apply.Text Hint.Text Row.UIButton_0.Text")));
	TestEqual(TEXT("one namespace"), FString::Join(Namespaces, TEXT(" ")), FString(TEXT("/Game/UI/Settings/WBP_SettingsScreen")));

	if (const FDreamLocEntry* Title = FindEntry(Document, TEXT("Title.Text")))
	{
		TestEqual(TEXT("Title.Text at"), *Title->Find(EDreamLocField::At), FString(TEXT("DUI/UI/Settings.dui#Title")));
		TestEqual(TEXT("Title.Text text"), *Title->Find(EDreamLocField::Text), FString(TEXT("Settings")));
		TestTrue(TEXT("Title.Text is done"), Title->GetState() == EDreamLocState::Done);
		TestFalse(TEXT("Title.Text is not stale"), Title->IsStale());
	}
	if (const FDreamLocEntry* Apply = FindEntry(Document, TEXT("Apply.Text")))
	{
		TestEqual(TEXT("Apply.Text max"), Apply->GetMax(), 16);
		TestTrue(TEXT("Apply.Text is machine"), Apply->GetState() == EDreamLocState::Machine);
		TestEqual(TEXT("Apply.Text note"), *Apply->Find(EDreamLocField::Note), FString(TEXT("底部按钮，点了立刻生效")));
	}
	if (const FDreamLocEntry* Hint = FindEntry(Document, TEXT("Hint.Text")))
	{
		TestTrue(TEXT("Hint.Text is stale"), Hint->IsStale());
		TestEqual(TEXT("Hint.Text prev"), *Hint->Find(EDreamLocField::Prev), FString(TEXT("按 {Key} 返回")));
	}
	if (const FDreamLocEntry* Row = FindEntry(Document, TEXT("Row.UIButton_0.Text")))
	{
		TestFalse(TEXT("Row is untranslated"), Row->IsTranslated());
		TestEqual(TEXT("Row has no max"), Row->GetMax(), -1);
	}

	TestEqual(TEXT("written back byte for byte"), DreamLocFile::Write(Document), FString(DesignExample));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocFormatCommentsTest, "DreamLoc.Format.RoundTrip.Comments",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocFormatCommentsTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocFormatTestsLocal;
	FDreamLocDocument Document;
	TArray<FDreamLocDiagnostic> Diagnostics;
	TestTrue(TEXT("reads"), DreamLocFile::Parse(Commented, Document, Diagnostics));
	TestEqual(TEXT("with nothing to report"), DreamLocTestUtil::Describe(Diagnostics), FString());
	TestEqual(TEXT("header comments keep their blank line"), FString::Join(Document.HeaderComments, TEXT("|")),
		FString(TEXT("// header comment||// after a blank line")));
	TestEqual(TEXT("version comment"), Document.VersionComment, FString(TEXT("// the version")));
	TestEqual(TEXT("culture comment"), Document.CultureComment, FString(TEXT("// the culture")));

	if (const FDreamLocEntry* Title = FindEntry(Document, TEXT("Title.Text")))
	{
		TestEqual(TEXT("entry comments"), FString::Join(Title->Comments, TEXT("|")), FString(TEXT("// above the entry|//|// still above the entry")));
		TestEqual(TEXT("after the brace"), Title->HeaderComment, FString(TEXT("// after the brace")));
		TestEqual(TEXT("above at"), FString::Join(Title->Fields[(int32)EDreamLocField::At].Comments, TEXT("|")), FString(TEXT("// above at")));
		TestEqual(TEXT("after at"), Title->Fields[(int32)EDreamLocField::At].TrailingComment, FString(TEXT("// after at")));
		TestEqual(TEXT("above text"), FString::Join(Title->Fields[(int32)EDreamLocField::Text].Comments, TEXT("|")), FString(TEXT("// above text")));
		TestEqual(TEXT("before the closing brace"), FString::Join(Title->ClosingComments, TEXT("|")), FString(TEXT("// before the closing brace")));
		TestEqual(TEXT("after the closing brace"), Title->TrailingComment, FString(TEXT("// after the closing brace")));
	}

	int32 Active = 0;
	int32 Removed = 0;
	Document.ForEachEntry([&Active, &Removed](const FString& InNamespace, const FDreamLocEntry& InEntry, bool bInRemoved)
	{
		(bInRemoved ? Removed : Active) += 1;
	}, /*bInIncludeRemoved*/ true);
	TestEqual(TEXT("one active entry"), Active, 1);
	TestEqual(TEXT("one removed entry"), Removed, 1);

	int32 ActiveOnly = 0;
	Document.ForEachEntry([&ActiveOnly](const FString&, const FDreamLocEntry&, bool) { ++ActiveOnly; });
	TestEqual(TEXT("removed entries are skipped by default"), ActiveOnly, 1);

	TestEqual(TEXT("written back byte for byte"), DreamLocFile::Write(Document), FString(Commented));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocFormatCanonicalTest, "DreamLoc.Format.Canonical",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocFormatCanonicalTest::RunTest(const FString& Parameters)
{
	// Fields out of order, one-line entries, tabs, odd spacing, extra blank lines.
	const FString Loose = TEXT("dloc 1\nculture   en\nnamespace NS\nA { text \"a\" src \"源\" }\n\"Key With Space\" {\n\tstate draft\n\tsrc \"x\\ty\"\n\tat \"path with space.dui#N\"\n}\n\n\n\nB{src \"b\"}\n");
	const FString Canonical = TEXT(R"dloc(dloc 1
culture en
namespace NS

A {
    src   "源"
    text  "a"
}

"Key With Space" {
    at    "path with space.dui#N"
    src   "x\ty"
    state draft
}

B {
    src   "b"
}
)dloc");

	FDreamLocDocument Document;
	TArray<FDreamLocDiagnostic> Diagnostics;
	TestTrue(TEXT("the loose text reads"), DreamLocFile::Parse(Loose, Document, Diagnostics));
	TestEqual(TEXT("with nothing to report"), DreamLocTestUtil::Describe(Diagnostics), FString());
	TestEqual(TEXT("comes out in the writer's layout"), DreamLocFile::Write(Document), Canonical);

	FDreamLocDocument Again;
	TestTrue(TEXT("the canonical text reads"), DreamLocFile::Parse(Canonical, Again, Diagnostics));
	TestEqual(TEXT("and is stable"), DreamLocFile::Write(Again), Canonical);

	FDreamLocDocument Crlf;
	const FString CrlfText = Canonical.Replace(TEXT("\n"), TEXT("\r\n"));
	TestTrue(TEXT("CRLF reads"), DreamLocFile::Parse(TEXT("\xFEFF") + CrlfText, Crlf, Diagnostics));
	TestEqual(TEXT("CRLF and a byte order mark come out as LF without one"), DreamLocFile::Write(Crlf), Canonical);

	// Built in code rather than read: what sync will do.
	FDreamLocDocument Built;
	Built.Culture = TEXT("ja");
	Built.bHasCulture = true;
	FDreamLocItem& Namespace = Built.Items.AddDefaulted_GetRef();
	Namespace.Kind = EDreamLocItemKind::Namespace;
	Namespace.Namespace = FString();
	FDreamLocItem& Entry = Built.Items.AddDefaulted_GetRef();
	Entry.Kind = EDreamLocItemKind::Entry;
	Entry.Entry.Key = TEXT("removed");
	Entry.Entry.Set(EDreamLocField::Src, TEXT("一行\n两行"));
	Entry.Entry.Set(EDreamLocField::Max, TEXT("12"));
	const FString BuiltText = DreamLocFile::Write(Built);
	TestEqual(TEXT("an empty namespace and a keyword key are quoted"), BuiltText,
		FString(TEXT("dloc 1\nculture ja\nnamespace \"\"\n\n\"removed\" {\n    max   12\n    src   \"一行\\n两行\"\n}\n")));
	FDreamLocDocument BuiltBack;
	TestTrue(TEXT("and read back"), DreamLocFile::Parse(BuiltText, BuiltBack, Diagnostics));
	bool bFound = false;
	BuiltBack.ForEachEntry([this, &bFound](const FString& InNamespace, const FDreamLocEntry& InEntry, bool)
	{
		bFound = true;
		TestEqual(TEXT("empty namespace"), InNamespace, FString());
		TestEqual(TEXT("keyword key"), InEntry.Key, FString(TEXT("removed")));
		TestEqual(TEXT("line break"), *InEntry.Find(EDreamLocField::Src), FString(TEXT("一行\n两行")));
	});
	TestTrue(TEXT("the entry is there"), bFound);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocFormatEscapesTest, "DreamLoc.Format.Escapes",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocFormatEscapesTest::RunTest(const FString& Parameters)
{
	FDreamLocDocument Document;
	TArray<FDreamLocDiagnostic> Diagnostics;
	const FString Text = TEXT("dloc 1\nnamespace N\n\nA {\n    src   \"a\\\"b\\\\c\\nd\\re\\tf\"\n}\n");
	TestTrue(TEXT("reads"), DreamLocFile::Parse(Text, Document, Diagnostics));
	Document.ForEachEntry([this](const FString&, const FDreamLocEntry& InEntry, bool)
	{
		TestEqual(TEXT("unescaped"), *InEntry.Find(EDreamLocField::Src), FString(TEXT("a\"b\\c\nd\re\tf")));
	});
	TestEqual(TEXT("escaped again the same way"), DreamLocFile::Write(Document), Text);

	FDreamLocDocument Unknown;
	TArray<FDreamLocDiagnostic> UnknownDiagnostics;
	TestTrue(TEXT("an unknown escape is a warning, not an error"),
		DreamLocFile::Parse(TEXT("dloc 1\nnamespace N\nA {\n    src   \"\\q\"\n}\n"), Unknown, UnknownDiagnostics));
	TestEqual(TEXT("reported where the backslash is"), DreamLocTestUtil::Describe(UnknownDiagnostics), FString(TEXT("DLOC1002@4:12")));
	Unknown.ForEachEntry([this](const FString&, const FDreamLocEntry& InEntry, bool)
	{
		TestEqual(TEXT("both characters kept"), *InEntry.Find(EDreamLocField::Src), FString(TEXT("\\q")));
	});

	TestEqual(TEXT("a plain word stays bare"), DreamLocFile::QuoteWordIfNeeded(TEXT("/Game/UI/WBP_X"), false), FString(TEXT("/Game/UI/WBP_X")));
	TestEqual(TEXT("a keyword as a value stays bare"), DreamLocFile::QuoteWordIfNeeded(TEXT("namespace"), false), FString(TEXT("namespace")));
	TestEqual(TEXT("a keyword as a key is quoted"), DreamLocFile::QuoteWordIfNeeded(TEXT("namespace"), true), FString(TEXT("\"namespace\"")));
	TestEqual(TEXT("empty is quoted"), DreamLocFile::QuoteWordIfNeeded(FString(), false), FString(TEXT("\"\"")));
	TestEqual(TEXT("a space is quoted"), DreamLocFile::QuoteWordIfNeeded(TEXT("a b"), true), FString(TEXT("\"a b\"")));
	TestEqual(TEXT("a brace is quoted"), DreamLocFile::QuoteWordIfNeeded(TEXT("a{b"), true), FString(TEXT("\"a{b\"")));
	TestEqual(TEXT("a leading comment marker is quoted"), DreamLocFile::QuoteWordIfNeeded(TEXT("//x"), true), FString(TEXT("\"//x\"")));
	TestEqual(TEXT("a slash pair inside a word is part of it"), DreamLocFile::QuoteWordIfNeeded(TEXT("a//b"), true), FString(TEXT("a//b")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocFormatTermsTest, "DreamLoc.Format.RoundTrip.Terms",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocFormatTermsTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocFormatTestsLocal;
	FDreamLocDocument Document;
	TArray<FDreamLocDiagnostic> Diagnostics;
	TestTrue(TEXT("reads"), DreamLocFile::Parse(Terms, Document, Diagnostics));
	TestEqual(TEXT("with nothing to report"), DreamLocTestUtil::Describe(Diagnostics), FString());
	TestTrue(TEXT("a terms file"), Document.bTerms);
	TestEqual(TEXT("three lines"), Document.Items.Num(), 3);
	if (Document.Items.Num() == 3)
	{
		TestTrue(TEXT("a term"), Document.Items[0].Kind == EDreamLocItemKind::Term);
		TestEqual(TEXT("its word"), Document.Items[0].Entry.Key, FString(TEXT("义体")));
		TestEqual(TEXT("its translation"), *Document.Items[0].Entry.Find(EDreamLocField::Text), FString(TEXT("Cyberware")));
		TestTrue(TEXT("a keep"), Document.Items[2].Kind == EDreamLocItemKind::Keep);
		TestEqual(TEXT("the keep's comment"), Document.Items[2].Entry.TrailingComment, FString(TEXT("// 不翻译")));
	}
	int32 Entries = 0;
	Document.ForEachEntry([&Entries](const FString&, const FDreamLocEntry&, bool) { ++Entries; });
	TestEqual(TEXT("terms are not translations"), Entries, 0);
	TestEqual(TEXT("written back byte for byte"), DreamLocFile::Write(Document), FString(Terms));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocFormatDiagnosticsTest, "DreamLoc.Format.Diagnostics",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocFormatDiagnosticsTest::RunTest(const FString& Parameters)
{
	struct FCase
	{
		const TCHAR* Name;
		const TCHAR* Text;
		/** The first diagnostic, as DreamLocTestUtil::Describe spells it. */
		const TCHAR* First;
	};
	const FString Head = TEXT("dloc 1\nnamespace N\n");
	const FCase Cases[] = {
		{ TEXT("unterminated string"), TEXT("A {\n    src \"abc\n}\n"), TEXT("DLOC1001@4:9") },
		{ TEXT("unknown field"), TEXT("A {\n    txt \"x\"\n}\n"), TEXT("DLOC1006@4:5") },
		{ TEXT("duplicate field"), TEXT("A {\n    src \"x\"\n    src \"y\"\n}\n"), TEXT("DLOC1007@5:5") },
		{ TEXT("duplicate key"), TEXT("A {\n}\nA {\n}\n"), TEXT("DLOC1009@5:1") },
		{ TEXT("max not a number"), TEXT("A {\n    max big\n}\n"), TEXT("DLOC1008@4:9") },
		{ TEXT("unknown state"), TEXT("A {\n    state finished\n}\n"), TEXT("DLOC1008@4:11") },
		{ TEXT("unquoted text"), TEXT("A {\n    text hello\n}\n"), TEXT("DLOC1008@4:10") },
		{ TEXT("field without a value"), TEXT("A {\n    src\n}\n"), TEXT("DLOC1008@4:5") },
		{ TEXT("unclosed entry"), TEXT("A {\n    src \"x\"\n"), TEXT("DLOC1011@3:3") },
		{ TEXT("keyword as a bare key"), TEXT("culture {\n}\n"), TEXT("DLOC1005@3:1") },
		{ TEXT("stray closing brace"), TEXT("}\n"), TEXT("DLOC1005@3:1") },
	};
	for (const FCase& Case : Cases)
	{
		FDreamLocDocument Document;
		TArray<FDreamLocDiagnostic> Diagnostics;
		const bool bParsed = DreamLocFile::Parse(Head + Case.Text, Document, Diagnostics);
		TestFalse(FString::Printf(TEXT("%s fails"), Case.Name), bParsed);
		TestTrue(FString::Printf(TEXT("%s reports something"), Case.Name), Diagnostics.Num() > 0);
		if (Diagnostics.Num() > 0)
		{
			TestEqual(FString::Printf(TEXT("%s: the first finding"), Case.Name),
				DreamLocTestUtil::Describe(MakeArrayView(Diagnostics.GetData(), 1)), FString(Case.First));
			TestTrue(FString::Printf(TEXT("%s: an error"), Case.Name), Diagnostics[0].IsError());
		}
	}

	const FCase Whole[] = {
		{ TEXT("no header"), TEXT("namespace N\n"), TEXT("DLOC1003@1:1") },
		{ TEXT("a newer version"), TEXT("dloc 2\n"), TEXT("DLOC1004@1:6") },
		{ TEXT("an entry before any namespace"), TEXT("dloc 1\nA {\n}\n"), TEXT("DLOC1010@2:1") },
	};
	for (const FCase& Case : Whole)
	{
		FDreamLocDocument Document;
		TArray<FDreamLocDiagnostic> Diagnostics;
		TestFalse(FString::Printf(TEXT("%s fails"), Case.Name), DreamLocFile::Parse(Case.Text, Document, Diagnostics));
		if (TestTrue(FString::Printf(TEXT("%s reports something"), Case.Name), Diagnostics.Num() > 0))
		{
			TestEqual(FString::Printf(TEXT("%s: the first finding"), Case.Name),
				DreamLocTestUtil::Describe(MakeArrayView(Diagnostics.GetData(), 1)), FString(Case.First));
		}
	}

	FDreamLocDiagnostic Diagnostic(EDreamLocSeverity::Error, DreamLocCode::UnknownField, 4, 5, TEXT("'txt' is not a field"));
	TestEqual(TEXT("the compiler-style line"), Diagnostic.Format(TEXT("DLoc/en/UI.dloc")),
		FString(TEXT("DLoc/en/UI.dloc(4,5): error DLOC1006: 'txt' is not a field")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
