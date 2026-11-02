// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocExtract.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace DreamLocExtractLocal
{
	FString Quote(const FString& InText)
	{
		FString Out;
		Out.Reserve(InText.Len() + 2);
		Out.AppendChar(TEXT('"'));
		for (const TCHAR Char : InText)
		{
			switch (Char)
			{
			case TEXT('"'):  Out += TEXT("\\\""); break;
			case TEXT('\\'): Out += TEXT("\\\\"); break;
			case TEXT('\n'): Out += TEXT("\\n"); break;
			case TEXT('\r'): Out += TEXT("\\r"); break;
			case TEXT('\t'): Out += TEXT("\\t"); break;
			default:
				if (Char < 0x20)
				{
					Out += FString::Printf(TEXT("\\u%04x"), (uint32)Char);
				}
				else
				{
					Out.AppendChar(Char);
				}
				break;
			}
		}
		Out.AppendChar(TEXT('"'));
		return Out;
	}

	FString WriteEntry(const FDreamLocExtractEntry& InEntry)
	{
		TArray<FString> Members;
		Members.Add(FString::Printf(TEXT("\"key\": %s"), *Quote(InEntry.Key)));
		Members.Add(FString::Printf(TEXT("\"src\": %s"), *Quote(InEntry.Source)));
		if (!InEntry.At.IsEmpty())
		{
			Members.Add(FString::Printf(TEXT("\"at\": %s"), *Quote(InEntry.At)));
		}
		if (!InEntry.Note.IsEmpty())
		{
			Members.Add(FString::Printf(TEXT("\"note\": %s"), *Quote(InEntry.Note)));
		}
		if (InEntry.Max >= 0)
		{
			Members.Add(FString::Printf(TEXT("\"max\": %d"), InEntry.Max));
		}
		if (!InEntry.Kind.IsEmpty())
		{
			Members.Add(FString::Printf(TEXT("\"kind\": %s"), *Quote(InEntry.Kind)));
		}
		if (!InEntry.RenamedFrom.IsEmpty())
		{
			Members.Add(FString::Printf(TEXT("\"renamedFrom\": %s"), *Quote(InEntry.RenamedFrom)));
		}
		if (!InEntry.bStable)
		{
			Members.Add(TEXT("\"stable\": false"));
		}
		if (InEntry.Context.Num() > 0)
		{
			TArray<FString> Items;
			for (const FDreamLocExtractContext& Context : InEntry.Context)
			{
				FString Value;
				if (Context.bList)
				{
					TArray<FString> Quoted;
					for (const FString& Element : Context.Values)
					{
						Quoted.Add(Quote(Element));
					}
					Value = TEXT("[") + FString::Join(Quoted, TEXT(", ")) + TEXT("]");
				}
				else
				{
					Value = Quote(Context.Values.Num() > 0 ? Context.Values[0] : FString());
				}
				Items.Add(FString::Printf(TEXT("%s: %s"), *Quote(Context.Name), *Value));
			}
			Members.Add(TEXT("\"context\": { ") + FString::Join(Items, TEXT(", ")) + TEXT(" }"));
		}
		return TEXT("{ ") + FString::Join(Members, TEXT(", ")) + TEXT(" }");
	}

	void WriteEntries(FString& Out, const TArray<FDreamLocExtractEntry>& InEntries, const TCHAR* InIndent)
	{
		if (InEntries.Num() == 0)
		{
			Out += TEXT("[]");
			return;
		}
		Out += TEXT("[\n");
		for (int32 Index = 0; Index < InEntries.Num(); ++Index)
		{
			Out += InIndent;
			Out += TEXT("  ");
			Out += WriteEntry(InEntries[Index]);
			Out += Index + 1 < InEntries.Num() ? TEXT(",\n") : TEXT("\n");
		}
		Out += InIndent;
		Out += TEXT("]");
	}

	bool ReadEntries(const TArray<TSharedPtr<FJsonValue>>& InValues, FDreamLocExtractNamespace& OutNamespace, FString& OutError)
	{
		for (int32 Index = 0; Index < InValues.Num(); ++Index)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!InValues[Index].IsValid() || !InValues[Index]->TryGetObject(Object) || Object == nullptr)
			{
				OutError = FString::Printf(TEXT("entry %d of namespace '%s' is not an object"), Index + 1, *OutNamespace.Namespace);
				return false;
			}
			FDreamLocExtractEntry& Entry = OutNamespace.Entries.AddDefaulted_GetRef();
			if (!(*Object)->TryGetStringField(TEXT("key"), Entry.Key) || Entry.Key.IsEmpty())
			{
				OutError = FString::Printf(TEXT("entry %d of namespace '%s' has no key"), Index + 1, *OutNamespace.Namespace);
				return false;
			}
			if (!(*Object)->TryGetStringField(TEXT("src"), Entry.Source))
			{
				OutError = FString::Printf(TEXT("'%s' in namespace '%s' has no src"), *Entry.Key, *OutNamespace.Namespace);
				return false;
			}
			(*Object)->TryGetStringField(TEXT("at"), Entry.At);
			(*Object)->TryGetStringField(TEXT("note"), Entry.Note);
			(*Object)->TryGetStringField(TEXT("kind"), Entry.Kind);
			(*Object)->TryGetStringField(TEXT("renamedFrom"), Entry.RenamedFrom);
			int32 Max = -1;
			if ((*Object)->TryGetNumberField(TEXT("max"), Max))
			{
				Entry.Max = Max;
			}
			bool bStable = true;
			if ((*Object)->TryGetBoolField(TEXT("stable"), bStable))
			{
				Entry.bStable = bStable;
			}
			const TSharedPtr<FJsonObject>* Context = nullptr;
			if ((*Object)->TryGetObjectField(TEXT("context"), Context) && Context != nullptr && Context->IsValid())
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Member : (*Context)->Values)
				{
					if (!Member.Value.IsValid() || Member.Value->IsNull())
					{
						continue;
					}
					FDreamLocExtractContext& Item = Entry.Context.AddDefaulted_GetRef();
					Item.Name = Member.Key;
					const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
					if (Member.Value->TryGetArray(List) && List != nullptr)
					{
						Item.bList = true;
						for (const TSharedPtr<FJsonValue>& Element : *List)
						{
							FString Text;
							if (Element.IsValid() && Element->TryGetString(Text))
							{
								Item.Values.Add(Text);
							}
						}
					}
					else
					{
						FString Text;
						Member.Value->TryGetString(Text);
						Item.Values.Add(Text);
					}
				}
			}
		}
		return true;
	}
}

FString FDreamLocExtractContext::Describe() const
{
	if (!bList)
	{
		return Values.Num() > 0 ? Values[0] : FString();
	}
	TArray<FString> Items;
	for (int32 Index = 0; Index < Values.Num(); ++Index)
	{
		Items.Add(Name == TEXT("args") ? FString::Printf(TEXT("{%d} %s"), Index, *Values[Index]) : Values[Index]);
	}
	return FString::Join(Items, TEXT(", "));
}

int32 FDreamLocExtractManifest::NumEntries() const
{
	int32 Count = 0;
	for (const FDreamLocExtractNamespace& Namespace : Namespaces)
	{
		Count += Namespace.Entries.Num();
	}
	return Count;
}

FDreamLocExtractNamespace& FDreamLocExtractManifest::FindOrAddNamespace(const FString& InNamespace)
{
	for (FDreamLocExtractNamespace& Namespace : Namespaces)
	{
		if (Namespace.Namespace.Equals(InNamespace, ESearchCase::CaseSensitive))
		{
			return Namespace;
		}
	}
	FDreamLocExtractNamespace& Added = Namespaces.AddDefaulted_GetRef();
	Added.Namespace = InNamespace;
	return Added;
}

bool DreamLocExtract::Parse(const FString& InText, FDreamLocExtractManifest& OutManifest, FString& OutError)
{
	using namespace DreamLocExtractLocal;
	OutManifest = FDreamLocExtractManifest();
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = FString::Printf(TEXT("not JSON: %s"), *Reader->GetErrorMessage());
		return false;
	}
	int32 Version = 0;
	if (!Root->TryGetNumberField(TEXT("version"), Version) || Version < 1)
	{
		OutError = TEXT("no \"version\"");
		return false;
	}
	if (Version > 1)
	{
		OutError = FString::Printf(TEXT("version %d is newer than this DreamLoc reads (1)"), Version);
		return false;
	}
	OutManifest.Version = Version;
	if (!Root->TryGetStringField(TEXT("provider"), OutManifest.Provider) || OutManifest.Provider.IsEmpty())
	{
		OutError = TEXT("no \"provider\"");
		return false;
	}
	if (!Root->TryGetStringField(TEXT("source"), OutManifest.Source) || OutManifest.Source.IsEmpty())
	{
		OutError = TEXT("no \"source\"");
		return false;
	}
	FPaths::NormalizeFilename(OutManifest.Source);
	Root->TryGetStringField(TEXT("root"), OutManifest.Root);

	const TArray<TSharedPtr<FJsonValue>>* Namespaces = nullptr;
	if (Root->TryGetArrayField(TEXT("namespaces"), Namespaces) && Namespaces != nullptr)
	{
		for (int32 Index = 0; Index < Namespaces->Num(); ++Index)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!(*Namespaces)[Index].IsValid() || !(*Namespaces)[Index]->TryGetObject(Object) || Object == nullptr)
			{
				OutError = FString::Printf(TEXT("namespaces[%d] is not an object"), Index);
				return false;
			}
			FDreamLocExtractNamespace& Namespace = OutManifest.Namespaces.AddDefaulted_GetRef();
			(*Object)->TryGetStringField(TEXT("namespace"), Namespace.Namespace);
			const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
			if ((*Object)->TryGetArrayField(TEXT("entries"), Entries) && Entries != nullptr && !ReadEntries(*Entries, Namespace, OutError))
			{
				return false;
			}
		}
		return true;
	}

	FDreamLocExtractNamespace& Namespace = OutManifest.Namespaces.AddDefaulted_GetRef();
	Root->TryGetStringField(TEXT("namespace"), Namespace.Namespace);
	const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
	if (Root->TryGetArrayField(TEXT("entries"), Entries) && Entries != nullptr && !ReadEntries(*Entries, Namespace, OutError))
	{
		return false;
	}
	return true;
}

FString DreamLocExtract::Write(const FDreamLocExtractManifest& InManifest)
{
	using namespace DreamLocExtractLocal;
	FString Out = TEXT("{\n");
	Out += FString::Printf(TEXT("  \"version\": %d,\n"), InManifest.Version);
	Out += FString::Printf(TEXT("  \"provider\": %s,\n"), *Quote(InManifest.Provider));
	if (!InManifest.Root.IsEmpty())
	{
		Out += FString::Printf(TEXT("  \"root\": %s,\n"), *Quote(InManifest.Root));
	}
	Out += FString::Printf(TEXT("  \"source\": %s,\n"), *Quote(InManifest.Source));
	if (InManifest.Namespaces.Num() == 1)
	{
		Out += FString::Printf(TEXT("  \"namespace\": %s,\n"), *Quote(InManifest.Namespaces[0].Namespace));
		Out += TEXT("  \"entries\": ");
		WriteEntries(Out, InManifest.Namespaces[0].Entries, TEXT("  "));
		Out += TEXT("\n}\n");
		return Out;
	}
	Out += TEXT("  \"namespaces\": [");
	for (int32 Index = 0; Index < InManifest.Namespaces.Num(); ++Index)
	{
		Out += Index == 0 ? TEXT("\n") : TEXT(",\n");
		Out += FString::Printf(TEXT("    { \"namespace\": %s, \"entries\": "), *Quote(InManifest.Namespaces[Index].Namespace));
		WriteEntries(Out, InManifest.Namespaces[Index].Entries, TEXT("    "));
		Out += TEXT(" }");
	}
	Out += InManifest.Namespaces.Num() > 0 ? TEXT("\n  ]\n}\n") : TEXT("]\n}\n");
	return Out;
}

bool DreamLocExtract::LoadFile(const FString& InPath, FDreamLocExtractManifest& OutManifest, FString& OutError)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *InPath))
	{
		OutManifest = FDreamLocExtractManifest();
		OutError = TEXT("the file cannot be read");
		return false;
	}
	const bool bParsed = Parse(Text, OutManifest, OutError);
	OutManifest.Path = InPath;
	OutManifest.TimeStamp = IFileManager::Get().GetTimeStamp(*InPath);
	return bParsed;
}

bool DreamLocExtract::SaveFileIfChanged(const FString& InPath, const FDreamLocExtractManifest& InManifest)
{
	const FString Text = Write(InManifest);
	FString Existing;
	if (IFileManager::Get().FileExists(*InPath) && FFileHelper::LoadFileToString(Existing, *InPath) && Existing == Text)
	{
		return true;
	}
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(InPath), /*Tree*/ true);
	return FFileHelper::SaveStringToFile(Text, *InPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

FString DreamLocExtract::GetExtractDirectory(const FString& InOwnerDirectory)
{
	FString Directory = FPaths::ConvertRelativePathToFull(InOwnerDirectory / TEXT("Intermediate/DLoc/extract"));
	FPaths::NormalizeDirectoryName(Directory);
	return Directory;
}

TArray<FString> DreamLocExtract::FindManifestFiles(const FString& InExtractDirectory)
{
	TArray<FString> Paths;
	if (IFileManager::Get().DirectoryExists(*InExtractDirectory))
	{
		IFileManager::Get().FindFilesRecursive(Paths, *InExtractDirectory, TEXT("*.json"), /*Files*/ true, /*Directories*/ false);
	}
	Paths.RemoveAll([](const FString& InPath) { return FPaths::GetCleanFilename(InPath).Equals(GatherStampName, ESearchCase::IgnoreCase); });
	for (FString& Path : Paths)
	{
		FPaths::NormalizeFilename(Path);
	}
	Paths.Sort();
	return Paths;
}

FString DreamLocExtract::GetDlocPath(const FDreamLocExtractManifest& InManifest)
{
	const FString Path = FPaths::GetBaseFilename(InManifest.Source, /*bRemovePath*/ false) + TEXT(".dloc");
	return InManifest.Provider == EngineProvider ? FString(TEXT("Engine/")) + Path : Path;
}

FString DreamLocExtract::MakeAt(const FString& InSource, const FString& InWhere)
{
	return InWhere.IsEmpty() ? InSource : InSource + TEXT("#") + InWhere;
}

FString DreamLocExtract::GetAtSource(const FString& InAt)
{
	FString Source;
	return InAt.Split(TEXT("#"), &Source, nullptr) ? Source : InAt;
}
