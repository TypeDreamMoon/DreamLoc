// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocEngine.h"
#include "DreamLocExtract.h"
#include "DreamLocTestUtil.h"
#include "Interfaces/IPluginManager.h"
#include "Internationalization/TextLocalizationResource.h"
#include "MetaDataSample/DreamLocMetaDataSample.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamLocGatherTestsLocal
{
	const FDreamLocExtractEntry* FindEntry(const FDreamLocExtractManifest& InManifest, const TCHAR* InNamespace, const FString& InKey)
	{
		for (const FDreamLocExtractNamespace& Namespace : InManifest.Namespaces)
		{
			if (Namespace.Namespace == InNamespace)
			{
				return Namespace.Entries.FindByPredicate([&InKey](const FDreamLocExtractEntry& InEntry) { return InEntry.Key == InKey; });
			}
		}
		return nullptr;
	}

	int32 CountEntries(const FDreamLocExtractManifest& InManifest, const TCHAR* InNamespace, const FString& InKey)
	{
		int32 Count = 0;
		for (const FDreamLocExtractNamespace& Namespace : InManifest.Namespaces)
		{
			for (const FDreamLocExtractEntry& Entry : Namespace.Entries)
			{
				Count += Namespace.Namespace == InNamespace && Entry.Key == InKey ? 1 : 0;
			}
		}
		return Count;
	}

	FString SourceOf(const FDreamLocExtractManifest& InManifest, const TCHAR* InNamespace, const FString& InKey)
	{
		const FDreamLocExtractEntry* Entry = FindEntry(InManifest, InNamespace, InKey);
		return Entry != nullptr ? Entry->Source : FString(TEXT("<none>"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLocGatherMetaDataTest, "DreamLoc.Gather.MetaDataUnderTheNamesAndKeysTheEditorLooksUp",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDreamLocGatherMetaDataTest::RunTest(const FString& Parameters)
{
	using namespace DreamLocGatherTestsLocal;
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DreamLoc"));
	if (!TestTrue(TEXT("DreamLoc is a plugin here"), Plugin.IsValid()))
	{
		return false;
	}
	const FString Work = DreamLocTestUtil::MakeTempDirectory(TEXT("GatherMetaData"));
	ON_SCOPE_EXIT { DreamLocTestUtil::DeleteDirectory(Work); };

	// The plugin as the owner, the sample's folder alone searched: what `dloc gather -Plugin` does with [Gather] MetaData=true.
	DreamLocEngine::FGatherOptions Options;
	Options.ProjectDirectory = FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir());
	Options.SourcePaths = { TEXT("Source/DreamLocTests/Private/MetaDataSample") };
	Options.ContentPaths.Reset();
	Options.ExcludePaths.Reset();
	Options.bMetaData = true;
	Options.bEditorOnlyData = true;
	Options.NativeCulture = TEXT("en");
	Options.ExtractDirectory = Work / TEXT("extract");
	Options.WorkDirectory = Work / TEXT("work");
	const DreamLocEngine::FGatherResult Result = DreamLocEngine::Gather(Options);
	TestTrue(TEXT("the gather ran"), Result.bSucceeded);
	TestEqual(TEXT("one source file: the sample's header"), Result.Sources, 1);

	FDreamLocExtractManifest Manifest;
	FString Error;
	const FString Path = Work / TEXT("extract/Engine/Source/DreamLocTests/Private/MetaDataSample/DreamLocMetaDataSample.h.json");
	if (!TestTrue(TEXT("the header has a manifest of its own"), DreamLocExtract::LoadFile(Path, Manifest, Error)))
	{
		AddInfo(Error);
		return false;
	}
	TestEqual(TEXT("its source, relative to the plugin"), Manifest.Source, FString(TEXT("Source/DreamLocTests/Private/MetaDataSample/DreamLocMetaDataSample.h")));
	// The engine writes a LOCTEXT's file relative to the project, `Plugins/DreamLoc/Source/...`: it is the plugin's file.
	TestEqual(TEXT("the header's LOCTEXT, in the same manifest"), SourceOf(Manifest, TEXT("DreamLocSample"), TEXT("Title")), FString(TEXT("Sample Panel Title")));
	TestEqual(TEXT("nothing under the project's path for it"), DreamLocExtract::FindManifestFiles(Work / TEXT("extract/Engine")).Num(), 1);

	// The keys the editor itself makes when it shows these: UField / FField / UEnum GetFullGroupName, as in Class.cpp,
	// Field.cpp and Enum.cpp.
	const UClass* Class = UDreamLocMetaDataSample::StaticClass();
	const FProperty* Width = FindFProperty<FProperty>(Class, GET_MEMBER_NAME_CHECKED(UDreamLocMetaDataSample, Width));
	const FProperty* ShowTitle = FindFProperty<FProperty>(Class, GET_MEMBER_NAME_CHECKED(UDreamLocMetaDataSample, bShowTitle));
	const FProperty* Fit = FindFProperty<FProperty>(Class, GET_MEMBER_NAME_CHECKED(UDreamLocMetaDataSample, Fit));
	const UEnum* Enum = StaticEnum<EDreamLocSampleFit>();
	if (!TestTrue(TEXT("the sample's fields"), Width != nullptr && ShowTitle != nullptr && Fit != nullptr && Enum != nullptr))
	{
		return false;
	}
	TestEqual(TEXT("the class's display name"), SourceOf(Manifest, TEXT("UObjectDisplayNames"), Class->GetFullGroupName(false)), FString(TEXT("Sample Panel")));
	TestEqual(TEXT("a property's own display name"), SourceOf(Manifest, TEXT("UObjectDisplayNames"), Width->GetFullGroupName(false)), FString(TEXT("Panel Width")));
	TestEqual(TEXT("a property without one gets the editor's own, a bool's without its b"),
		SourceOf(Manifest, TEXT("UObjectDisplayNames"), ShowTitle->GetFullGroupName(false)), FString(TEXT("Show Title")));
	TestEqual(TEXT("an enum value's display name"), SourceOf(Manifest, TEXT("UObjectDisplayNames"), Enum->GetFullGroupName(false) + TEXT(".") + Enum->GetNameStringByIndex(0)),
		FString(TEXT("Fill the Box")));
	TestEqual(TEXT("and one without, as the editor shows it"), SourceOf(Manifest, TEXT("UObjectDisplayNames"), Enum->GetFullGroupName(false) + TEXT(".") + Enum->GetNameStringByIndex(1)),
		FString(TEXT("Keep Aspect")));
	TestEqual(TEXT("a tooltip"), SourceOf(Manifest, TEXT("UObjectToolTips"), Width->GetFullGroupName(false)), FString(TEXT("How wide the panel is, in slate units.")));
	TestEqual(TEXT("a short tooltip"), SourceOf(Manifest, TEXT("UObjectShortTooltips"), Fit->GetFullGroupName(false)), FString(TEXT("How it fits")));
	TestEqual(TEXT("a category, keyed by itself"), SourceOf(Manifest, TEXT("UObjectCategory"), TEXT("Sample|Layout")), FString(TEXT("Sample|Layout")));
	TestEqual(TEXT("once, though two properties are in it"), CountEntries(Manifest, TEXT("UObjectCategory"), TEXT("Sample|Layout")), 1);

	// What the editor hashes a lookup's source with is the meta-data as it is: the entry's source must be that.
	TestEqual(TEXT("the source is the meta-data value"), SourceOf(Manifest, TEXT("UObjectToolTips"), Width->GetFullGroupName(false)), Width->GetMetaData(TEXT("ToolTip")));

	const FDreamLocExtractEntry* Tip = FindEntry(Manifest, TEXT("UObjectToolTips"), Width->GetFullGroupName(false));
	if (TestNotNull(TEXT("the tooltip's entry"), Tip))
	{
		TestEqual(TEXT("where it is: the field, without the module"), Tip->At, FString(TEXT("DreamLocMetaDataSample:Width")));
		TestEqual(TEXT("what it is, for the translator"), Tip->Kind, FString(TEXT("tooltip")));
	}
	const FDreamLocExtractEntry* Value = FindEntry(Manifest, TEXT("UObjectDisplayNames"), Enum->GetFullGroupName(false) + TEXT(".") + Enum->GetNameStringByIndex(0));
	if (TestNotNull(TEXT("the enum value's entry"), Value))
	{
		TestEqual(TEXT("an enum value's kind: a display name like any other, so one source is sent once"), Value->Kind, FString(TEXT("display name")));
	}

	// Without MetaData: the LOCTEXT alone.
	Options.bMetaData = false;
	const DreamLocEngine::FGatherResult Plain = DreamLocEngine::Gather(Options);
	TestTrue(TEXT("the plain gather ran"), Plain.bSucceeded);
	TestEqual(TEXT("the LOCTEXT alone then"), Plain.Texts, 1);
	FDreamLocExtractManifest PlainManifest;
	TestTrue(TEXT("its manifest reads"), DreamLocExtract::LoadFile(Path, PlainManifest, Error));
	TestEqual(TEXT("one namespace, the LOCTEXT's"), PlainManifest.Namespaces.Num(), 1);

	// dloc.ini's excludes are relative to the plugin, and leave out its LOCTEXT and meta-data alike.
	Options.bMetaData = true;
	Options.ExcludePaths = { TEXT("Source/DreamLocTests/Private/MetaDataSample/*") };
	const DreamLocEngine::FGatherResult Excluded = DreamLocEngine::Gather(Options);
	TestTrue(TEXT("the gather with the folder left out ran"), Excluded.bSucceeded);
	TestEqual(TEXT("no texts"), Excluded.Texts, 0);
	TestFalse(TEXT("and the header's manifest is gone"), IFileManager::Get().FileExists(*Path));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
