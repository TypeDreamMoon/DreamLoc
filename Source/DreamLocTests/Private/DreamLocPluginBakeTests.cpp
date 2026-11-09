// Copyright DreamMoon. All Rights Reserved.

#include "Dom/JsonObject.h"
#include "DreamLocPublish.h"
#include "DreamLocTestUtil.h"
#include "Internationalization/TextLocalizationResource.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamLocPluginBakeTestsLocal
{
	void WriteFile(const FString& InPath, const FString& InText)
	{
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(InPath), /*Tree*/ true);
		DreamLocTestUtil::WriteText(InPath, InText);
	}

	/** A plugin's .uplugin as the editor writes one: a byte order mark, tabs, CRLF. */
	const TCHAR* const Descriptor = TEXT("{\r\n\t\"FileVersion\": 3,\r\n\t\"FriendlyName\": \"Sample\",\r\n\t\"Modules\": [\r\n\t\t{\r\n\t\t\t\"Name\": \"SampleEditor\",\r\n\t\t\t\"Type\": \"Editor\"\r\n\t\t}\r\n\t]\r\n}\r\n");

	/** A header's texts as `dloc sync -Plugin` writes them: a LOCTEXT, an untranslated one, a category, a display name, a tooltip. */
	FString PanelFile(bool bWithToolTip)
	{
		FString Text = TEXT(R"(dloc 1
culture zh-Hans
namespace SampleEditor

Title {
    src   "Panel"
    text  "面板"
    state machine
}

Hint {
    src   "Drag to resize"
}

namespace UObjectCategory

"Sample|Layout" {
    src   "Sample|Layout"
    text  "示例|布局"
}

namespace UObjectDisplayNames

"SamplePanel:Width" {
    src   "Panel Width"
    text  "面板宽度"
    state done
}
)");
		if (bWithToolTip)
		{
			Text += TEXT(R"(
namespace UObjectToolTips

"SamplePanel:Width" {
    src   "How wide the panel is."
    text  "面板有多宽。"
    state draft
}
)");
		}
		return Text;
	}

	FString TextOf(const FTextLocalizationResource& InResource, const TCHAR* InNamespace, const TCHAR* InKey)
	{
		const FTextLocalizationResource::FEntry* Entry = InResource.Entries.Find(FTextId(FTextKey(InNamespace), FTextKey(InKey)));
		return Entry != nullptr && Entry->LocalizedString.IsValid() ? *Entry->LocalizedString : FString(TEXT("<none>"));
	}

	/** The .uplugin's LocalizationTargets, as `Name:Policy` joined by commas. */
	FString TargetsOf(const FString& InPath)
	{
		FString Text;
		FFileHelper::LoadFileToString(Text, *InPath);
		TSharedPtr<FJsonObject> Root;
		const TArray<TSharedPtr<FJsonValue>>* Targets = nullptr;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid() || !Root->TryGetArrayField(TEXT("LocalizationTargets"), Targets))
		{
			return TEXT("<none>");
		}
		TArray<FString> Parts;
		for (const TSharedPtr<FJsonValue>& Value : *Targets)
		{
			const TSharedPtr<FJsonObject> Target = Value->AsObject();
			Parts.Add(Target->GetStringField(TEXT("Name")) + TEXT(":") + Target->GetStringField(TEXT("LoadingPolicy")));
		}
		return FString::Join(Parts, TEXT(","));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocPluginBakeTest, "DreamLoc.Bake.APluginsOwnTargetsAsTheEditorLoadsItsOwn",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocPluginBakeTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocPluginBakeTestsLocal;
	const FString Work = DreamLocTestUtil::MakeTempDirectory(TEXT("PluginBake"));
	ON_SCOPE_EXIT { DreamLocTestUtil::DeleteDirectory(Work); };
	const FString Plugin = Work / TEXT("Sample");
	const FString Uplugin = Plugin / TEXT("Sample.uplugin");
	IFileManager::Get().MakeDirectory(*Plugin, /*Tree*/ true);
	FFileHelper::SaveStringToFile(Descriptor, *Uplugin, FFileHelper::EEncodingOptions::ForceUTF8);
	WriteFile(Plugin / TEXT("DLoc/dloc.ini"), TEXT("[DreamLoc]\nSourceCulture=en\nTargetCultures=zh-Hans\n"));
	WriteFile(Plugin / TEXT("DLoc/zh-Hans/Source/SampleEditor/Panel.h.dloc"), PanelFile(/*bWithToolTip*/ true));

	DreamLocPublish::FPluginBakeOptions Options;
	Options.PluginDirectory = Plugin;
	Options.PluginName = TEXT("Sample");
	const DreamLocPublish::FPluginBakeResult Result = DreamLocPublish::BakePlugin(Options);
	TestTrue(TEXT("baked"), Result.bSucceeded);
	TestTrue(TEXT("the .uplugin changed"), Result.bDescriptorChanged);
	TestEqual(TEXT("three targets, as the editor splits its own"), TargetsOf(Uplugin),
		FString(TEXT("Sample:Editor,SamplePropertyNames:PropertyNames,SampleToolTips:ToolTips")));

	// The main target: LOCTEXT and categories; a machine translation too.
	const FString Main = Plugin / TEXT("Content/Localization/Sample");
	FTextLocalizationMetaDataResource MetaData;
	TestTrue(TEXT("its meta file reads"), MetaData.LoadFromFile(Main / TEXT("Sample.locmeta")));
	TestEqual(TEXT("the plugin's source culture"), MetaData.NativeCulture, FString(TEXT("en")));
	TestEqual(TEXT("and its file"), MetaData.NativeLocRes, FString(TEXT("en/Sample.locres")));
	TestTrue(TEXT("both cultures compiled"), MetaData.CompiledCultures.Contains(TEXT("en")) && MetaData.CompiledCultures.Contains(TEXT("zh-Hans")));
	FTextLocalizationResource Chinese;
	TestTrue(TEXT("the Chinese file reads"), Chinese.LoadFromFile(Main / TEXT("zh-Hans/Sample.locres"), 0));
	TestEqual(TEXT("a machine translation is baked"), TextOf(Chinese, TEXT("SampleEditor"), TEXT("Title")), FString(TEXT("面板")));
	TestEqual(TEXT("a category"), TextOf(Chinese, TEXT("UObjectCategory"), TEXT("Sample|Layout")), FString(TEXT("示例|布局")));
	TestEqual(TEXT("no display name here"), TextOf(Chinese, TEXT("UObjectDisplayNames"), TEXT("SamplePanel:Width")), FString(TEXT("<none>")));
	TestEqual(TEXT("no untranslated text"), TextOf(Chinese, TEXT("SampleEditor"), TEXT("Hint")), FString(TEXT("<none>")));
	const FTextLocalizationResource::FEntry* Title = Chinese.Entries.Find(FTextId(FTextKey(TEXT("SampleEditor")), FTextKey(TEXT("Title"))));
	TestTrue(TEXT("under the hash of its source, as the engine checks it"), Title != nullptr && Title->SourceStringHash == FTextLocalizationResource::HashString(TEXT("Panel")));
	FTextLocalizationResource English;
	English.LoadFromFile(Main / TEXT("en/Sample.locres"), 0);
	TestEqual(TEXT("the source culture's file maps a text to its source"), TextOf(English, TEXT("SampleEditor"), TEXT("Hint")), FString(TEXT("Drag to resize")));

	// The other two: one namespace each.
	FTextLocalizationResource Names;
	TestTrue(TEXT("the property names' file reads"), Names.LoadFromFile(Plugin / TEXT("Content/Localization/SamplePropertyNames/zh-Hans/SamplePropertyNames.locres"), 0));
	TestEqual(TEXT("a display name"), TextOf(Names, TEXT("UObjectDisplayNames"), TEXT("SamplePanel:Width")), FString(TEXT("面板宽度")));
	TestEqual(TEXT("only display names"), Names.Entries.Num(), 1);
	const FTextLocalizationResource::FEntry* Name = Names.Entries.Find(FTextId(FTextKey(TEXT("UObjectDisplayNames")), FTextKey(TEXT("SamplePanel:Width"))));
	TestTrue(TEXT("under the hash of the meta-data value"), Name != nullptr && Name->SourceStringHash == FTextLocalizationResource::HashString(TEXT("Panel Width")));
	FTextLocalizationResource Tips;
	TestTrue(TEXT("the tooltips' file reads"), Tips.LoadFromFile(Plugin / TEXT("Content/Localization/SampleToolTips/zh-Hans/SampleToolTips.locres"), 0));
	TestEqual(TEXT("a tooltip, a draft"), TextOf(Tips, TEXT("UObjectToolTips"), TEXT("SamplePanel:Width")), FString(TEXT("面板有多宽。")));

	// The .uplugin: only the array is new; its mark, its tabs and its line ends stay.
	TArray<uint8> Bytes;
	FFileHelper::LoadFileToArray(Bytes, *Uplugin);
	TestTrue(TEXT("the byte order mark stays"), Bytes.Num() > 3 && Bytes[0] == 0xEF && Bytes[1] == 0xBB && Bytes[2] == 0xBF);
	FString Text;
	FFileHelper::LoadFileToString(Text, *Uplugin);
	const FString Before = FString(Descriptor).LeftChop(5);
	TestTrue(TEXT("what was there is as it was"), Text.StartsWith(Before));
	TestTrue(TEXT("the array in the file's own style"), Text.Contains(TEXT("\t\"LocalizationTargets\": [\r\n\t\t{\r\n\t\t\t\"Name\": \"Sample\",\r\n\t\t\t\"LoadingPolicy\": \"Editor\"\r\n\t\t},")));

	// Again, nothing new: the .uplugin is not touched.
	const DreamLocPublish::FPluginBakeResult Again = DreamLocPublish::BakePlugin(Options);
	TestTrue(TEXT("baked again"), Again.bSucceeded);
	TestFalse(TEXT("the .uplugin as it was"), Again.bDescriptorChanged);
	FString TextAgain;
	FFileHelper::LoadFileToString(TextAgain, *Uplugin);
	TestEqual(TEXT("byte for byte"), TextAgain, Text);

	// No tooltip any more, and the main target loading in a game too: that target and its files go, the policy changes.
	WriteFile(Plugin / TEXT("DLoc/zh-Hans/Source/SampleEditor/Panel.h.dloc"), PanelFile(/*bWithToolTip*/ false));
	WriteFile(Plugin / TEXT("DLoc/dloc.ini"), TEXT("[DreamLoc]\nSourceCulture=en\nTargetCultures=zh-Hans\n[Publish]\nLoadingPolicy=Always\n"));
	const DreamLocPublish::FPluginBakeResult Fewer = DreamLocPublish::BakePlugin(Options);
	TestTrue(TEXT("baked with fewer"), Fewer.bSucceeded);
	TestEqual(TEXT("two targets, the main one always loaded"), TargetsOf(Uplugin), FString(TEXT("Sample:Always,SamplePropertyNames:PropertyNames")));
	TestFalse(TEXT("the tooltips' folder is gone"), IFileManager::Get().DirectoryExists(*(Plugin / TEXT("Content/Localization/SampleToolTips"))));

	// A plugin that has never been synced.
	DreamLocPublish::FPluginBakeOptions Nothing;
	Nothing.PluginDirectory = Work / TEXT("Empty");
	Nothing.PluginName = TEXT("Empty");
	IFileManager::Get().MakeDirectory(*Nothing.PluginDirectory, /*Tree*/ true);
	const DreamLocPublish::FPluginBakeResult None = DreamLocPublish::BakePlugin(Nothing);
	TestFalse(TEXT("no DLoc folder: not baked"), None.bSucceeded);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocPluginTargetsTest, "DreamLoc.Bake.TheUpluginKeepsOtherTargetsAndItsOwnStyle",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocPluginTargetsTest::RunTest(const FString& Parameters)
{
	const TArray<FString> Own = { TEXT("Sample"), TEXT("SamplePropertyNames"), TEXT("SampleToolTips") };
	const TArray<DreamLocPublish::FPluginTarget> Targets = { { TEXT("Sample"), TEXT("Editor") }, { TEXT("SampleToolTips"), TEXT("ToolTips") } };

	// Four spaces, LF, a target of another name and one of ours with another policy.
	const FString Spaces = TEXT("{\n    \"FileVersion\": 3,\n    \"LocalizationTargets\": [\n        { \"Name\": \"Other\", \"LoadingPolicy\": \"Game\" },\n        { \"Name\": \"Sample\", \"LoadingPolicy\": \"Game\" }\n    ],\n    \"CanContainContent\": true\n}\n");
	FString Out;
	FString Error;
	TestTrue(TEXT("set"), DreamLocPublish::SetLocalizationTargets(Spaces, Own, Targets, Out, Error));
	TestTrue(TEXT("what comes before the array stays"), Out.StartsWith(TEXT("{\n    \"FileVersion\": 3,\n    \"LocalizationTargets\": [\n        {\"Name\":\"Other\",\"LoadingPolicy\":\"Game\"},\n        {\n            \"Name\": \"Sample\",")));
	TestTrue(TEXT("and what comes after it"), Out.EndsWith(TEXT("\n    ],\n    \"CanContainContent\": true\n}\n")));
	TestFalse(TEXT("LF stays LF"), Out.Contains(TEXT("\r")));
	FString Same;
	TestTrue(TEXT("set again"), DreamLocPublish::SetLocalizationTargets(Out, Own, Targets, Same, Error));
	TestEqual(TEXT("the same targets: the text as it is"), Same, Out);

	// No targets, none there: nothing added. An object that is empty but for braces gets the array without a comma.
	FString Untouched;
	TestTrue(TEXT("nothing to list"), DreamLocPublish::SetLocalizationTargets(TEXT("{\n\t\"FileVersion\": 3\n}\n"), Own, {}, Untouched, Error));
	TestEqual(TEXT("nothing added"), Untouched, FString(TEXT("{\n\t\"FileVersion\": 3\n}\n")));
	FString Empty;
	TestTrue(TEXT("an empty object"), DreamLocPublish::SetLocalizationTargets(TEXT("{\n}\n"), Own, Targets, Empty, Error));
	TestTrue(TEXT("reads as JSON"), Empty.StartsWith(TEXT("{\n\t\"LocalizationTargets\": [")));

	// Not a .uplugin.
	TestFalse(TEXT("not JSON"), DreamLocPublish::SetLocalizationTargets(TEXT("[Section]\nKey=1\n"), Own, Targets, Out, Error));
	TestFalse(TEXT("targets that are not an array"), DreamLocPublish::SetLocalizationTargets(TEXT("{ \"LocalizationTargets\": {} }"), Own, Targets, Out, Error));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
