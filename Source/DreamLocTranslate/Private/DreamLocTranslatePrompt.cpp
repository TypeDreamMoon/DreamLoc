// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocTranslatePrompt.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Interfaces/IPluginManager.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"

namespace DreamLocTranslatePromptLocal
{
	/** What Resources/Prompts/translate.md says, for a plugin installed without it. */
	const TCHAR* const BuiltIn =
		TEXT("You translate the text of a video game from {{source_language}} ({{source_culture}}) into {{target_language}} ({{target_culture}}).\n\n")
		TEXT("The user message is a JSON object whose \"entries\" each hold one text to translate, \"src\". Answer with one JSON object and nothing else: ")
		TEXT("{\"entries\": [{\"id\": <id>, \"ns\": \"<ns>\", \"key\": \"<key>\", \"text\": \"<translation>\"}]}, one for every entry given.\n\n")
		TEXT("Keep every {argument} as written and add none; keep rich-text tags the same and in pairs; keep blanks at either end and line breaks; ")
		TEXT("stay within max; use the translations in terms and leave the words in keep as they are. An entry with prev and previousText: amend ")
		TEXT("previousText to the new source. An entry with reference: keep to it unless the context calls for something else. An entry with ")
		TEXT("lastText and problems: translate it again without those problems.\n{{style}}{{terms}}\n");

	FString LanguageName(const FString& InCulture)
	{
		const FCulturePtr Culture = FInternationalization::Get().GetCulture(InCulture);
		return Culture.IsValid() ? Culture->GetEnglishName() : InCulture;
	}

	FString Serialize(const TSharedRef<FJsonObject>& InObject)
	{
		FString Out;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(InObject, Writer);
		return Out;
	}

	TArray<TSharedPtr<FJsonValue>> Strings(const TArray<FString>& InValues)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FString& Value : InValues)
		{
			Out.Add(MakeShared<FJsonValueString>(Value));
		}
		return Out;
	}

	/** The answer's JSON object: the text from its first `{` to its last `}`, so a code fence or a word around it is no matter. */
	FString JsonPart(const FString& InContent)
	{
		int32 Open = INDEX_NONE;
		int32 Close = INDEX_NONE;
		if (!InContent.FindChar(TEXT('{'), Open) || !InContent.FindLastChar(TEXT('}'), Close) || Close < Open)
		{
			return FString();
		}
		return InContent.Mid(Open, Close - Open + 1);
	}
}

FString DreamLocTranslatePrompt::LoadTemplate()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DreamLoc"));
	FString Text;
	if (Plugin.IsValid() && FFileHelper::LoadFileToString(Text, *(Plugin->GetBaseDir() / TEXT("Resources/Prompts/translate.md"))) && !Text.IsEmpty())
	{
		return Text;
	}
	return DreamLocTranslatePromptLocal::BuiltIn;
}

FString DreamLocTranslatePrompt::System(const FDreamLocTranslateBatch& InBatch, const FString& InTemplate)
{
	using namespace DreamLocTranslatePromptLocal;
	FString Template = InTemplate;
	FString Project = InBatch.ProjectPrompt.TrimStartAndEnd();
	if (Project.StartsWith(TEXT("<!-- replace -->")))
	{
		Template = Project.RightChop(16).TrimStart();
		Project.Reset();
	}

	FString Style;
	if (!InBatch.Style.TrimStartAndEnd().IsEmpty())
	{
		Style = TEXT("\nThe game's style guide for this language:\n\n") + InBatch.Style.TrimStartAndEnd() + TEXT("\n");
	}
	// The terms of this batch's texts, each once.
	TArray<FString> TermLines;
	TSet<FString> Seen;
	for (const FDreamLocTranslateUnit& Unit : InBatch.Units)
	{
		for (const TPair<FString, FString>& Term : Unit.Terms)
		{
			if (!Seen.Contains(Term.Key))
			{
				Seen.Add(Term.Key);
				TermLines.Add(FString::Printf(TEXT("- %s -> %s"), *Term.Key, *Term.Value));
			}
		}
	}
	FString Terms;
	if (TermLines.Num() > 0)
	{
		Terms = TEXT("\nTerms in these texts, each to be translated so:\n\n") + FString::Join(TermLines, TEXT("\n")) + TEXT("\n");
	}

	FString Out = Template
		.Replace(TEXT("{{source_language}}"), *LanguageName(InBatch.SourceCulture))
		.Replace(TEXT("{{source_culture}}"), *InBatch.SourceCulture)
		.Replace(TEXT("{{target_language}}"), *LanguageName(InBatch.Culture))
		.Replace(TEXT("{{target_culture}}"), *InBatch.Culture)
		.Replace(TEXT("{{style}}"), *Style)
		.Replace(TEXT("{{terms}}"), *Terms);
	if (!Project.IsEmpty())
	{
		Out += TEXT("\n") + Project + TEXT("\n");
	}
	return Out;
}

FString DreamLocTranslatePrompt::User(const FDreamLocTranslateBatch& InBatch)
{
	using namespace DreamLocTranslatePromptLocal;
	TArray<TSharedPtr<FJsonValue>> Entries;
	for (const FDreamLocTranslateUnit& Unit : InBatch.Units)
	{
		const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetNumberField(TEXT("id"), Unit.Id);
		Entry->SetStringField(TEXT("ns"), Unit.Namespace);
		Entry->SetStringField(TEXT("key"), Unit.Key);
		Entry->SetStringField(TEXT("src"), Unit.Source);
		if (!Unit.Note.IsEmpty()) { Entry->SetStringField(TEXT("note"), Unit.Note); }
		if (Unit.Max >= 0) { Entry->SetNumberField(TEXT("max"), Unit.Max); }
		if (!Unit.Kind.IsEmpty()) { Entry->SetStringField(TEXT("kind"), Unit.Kind); }
		if (Unit.Context.Num() > 0)
		{
			const TSharedRef<FJsonObject> Context = MakeShared<FJsonObject>();
			for (const FDreamLocExtractContext& Item : Unit.Context)
			{
				if (Item.bList)
				{
					Context->SetArrayField(Item.Name, Strings(Item.Values));
				}
				else
				{
					Context->SetStringField(Item.Name, Item.Values.Num() > 0 ? Item.Values[0] : FString());
				}
			}
			Entry->SetObjectField(TEXT("context"), Context);
		}
		if (Unit.Nearby.Num() > 0) { Entry->SetArrayField(TEXT("nearby"), Strings(Unit.Nearby)); }
		if (Unit.Terms.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> Terms;
			for (const TPair<FString, FString>& Term : Unit.Terms)
			{
				const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
				Object->SetStringField(TEXT("term"), Term.Key);
				Object->SetStringField(TEXT("text"), Term.Value);
				Terms.Add(MakeShared<FJsonValueObject>(Object));
			}
			Entry->SetArrayField(TEXT("terms"), Terms);
		}
		if (Unit.Keeps.Num() > 0) { Entry->SetArrayField(TEXT("keep"), Strings(Unit.Keeps)); }
		if (!Unit.Prev.IsEmpty())
		{
			Entry->SetStringField(TEXT("prev"), Unit.Prev);
			Entry->SetStringField(TEXT("previousText"), Unit.PreviousText);
		}
		if (!Unit.Reference.IsEmpty()) { Entry->SetStringField(TEXT("reference"), Unit.Reference); }
		if (!Unit.LastText.IsEmpty() || !Unit.Problems.IsEmpty())
		{
			Entry->SetStringField(TEXT("lastText"), Unit.LastText);
			Entry->SetStringField(TEXT("problems"), Unit.Problems);
		}
		Entries.Add(MakeShared<FJsonValueObject>(Entry));
	}
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("sourceCulture"), InBatch.SourceCulture);
	Root->SetStringField(TEXT("culture"), InBatch.Culture);
	Root->SetArrayField(TEXT("entries"), Entries);
	return Serialize(Root);
}

bool DreamLocTranslatePrompt::ParseAnswer(const FString& InContent, const FDreamLocTranslateBatch& InBatch, FDreamLocTranslateReply& InOutReply)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(DreamLocTranslatePromptLocal::JsonPart(InContent));
	const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid() || !Root->TryGetArrayField(TEXT("entries"), Entries) || Entries == nullptr)
	{
		return false;
	}
	TMap<FString, int32> ByKey;
	TSet<int32> Ids;
	for (const FDreamLocTranslateUnit& Unit : InBatch.Units)
	{
		ByKey.Add(Unit.Namespace + TEXT("\x01") + Unit.Key, Unit.Id);
		Ids.Add(Unit.Id);
	}
	for (const TSharedPtr<FJsonValue>& Value : *Entries)
	{
		const TSharedPtr<FJsonObject>* Entry = nullptr;
		FString Text;
		if (!Value.IsValid() || !Value->TryGetObject(Entry) || Entry == nullptr || !(*Entry)->TryGetStringField(TEXT("text"), Text))
		{
			InOutReply.Warnings.Add(TEXT("an answer entry without a text was left out"));
			continue;
		}
		int32 Id = INDEX_NONE;
		double Number = 0.0;
		if ((*Entry)->TryGetNumberField(TEXT("id"), Number) && Ids.Contains(int32(Number)))
		{
			Id = int32(Number);
		}
		else
		{
			FString Namespace;
			FString Key;
			(*Entry)->TryGetStringField(TEXT("ns"), Namespace);
			(*Entry)->TryGetStringField(TEXT("key"), Key);
			Id = ByKey.FindRef(Namespace + TEXT("\x01") + Key, INDEX_NONE);
		}
		if (Id == INDEX_NONE)
		{
			InOutReply.Warnings.Add(TEXT("an answer entry for a text that was not asked for was left out"));
			continue;
		}
		InOutReply.Texts.Add(Id, Text);
	}
	return true;
}

FString DreamLocTranslatePrompt::OneLine(const FString& InText, int32 InMax)
{
	FString Out = InText.Replace(TEXT("\r"), TEXT("\\r")).Replace(TEXT("\n"), TEXT("\\n")).Replace(TEXT("\t"), TEXT("\\t"));
	if (Out.Len() > InMax)
	{
		Out = Out.Left(FMath::Max(0, InMax - 3)) + TEXT("...");
	}
	return TEXT("\"") + Out + TEXT("\"");
}
