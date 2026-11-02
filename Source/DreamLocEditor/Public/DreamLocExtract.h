// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/DateTime.h"

/**
 * An extraction manifest: the texts one source file has to translate, as the tool that reads the source wrote them.
 *
 *   {
 *     "version": 1,
 *     "provider": "DreamGUI",
 *     "source": "DUI/UI/Settings.dui",
 *     "namespace": "/Game/UI/Settings/WBP_SettingsScreen",
 *     "entries": [
 *       { "key": "Title.Text", "src": "设置", "at": "Title", "kind": "DreamText" },
 *       { "key": "Back.Text", "src": "返回", "at": "Footer/Back", "renamedFrom": "BackBtn.Text" }
 *     ]
 *   }
 *
 * One file per source, at <owner>/Intermediate/DLoc/extract/<provider>/<source>.json, where the owner is the project
 * or the plugin the source belongs to. A source with texts in several namespaces writes
 * `"namespaces": [{ "namespace": ..., "entries": [...] }]` instead of the two keys. DreamLoc never reads a source file
 * itself: what to translate is what the manifests say.
 */
/** One `context` member of a manifest entry: a string, or a list of strings (`"args": ["金币数", "玩家名"]`). */
struct DREAMLOCEDITOR_API FDreamLocExtractContext
{
	FString Name;
	TArray<FString> Values;
	bool bList = false;

	/** As a translator reads it: the string, or the list as `{0} 金币数, {1} 玩家名` for args and `a, b` otherwise. */
	FString Describe() const;
};

struct DREAMLOCEDITOR_API FDreamLocExtractEntry
{
	FString Key;
	/** The source text. */
	FString Source;
	/** Where in the source file: a node path, an object path. Empty for a whole-file location. */
	FString At;
	FString Note;
	/** The longest the translation may be, in display width; -1 when there is no limit. */
	int32 Max = -1;
	/** What the text is on: a widget type, a line of dialogue. For translators. */
	FString Kind;
	/** The key this text had before, when the tool knows it was renamed. Same namespace. */
	FString RenamedFrom;
	/**
	 * False for a key the tool numbers by position (`dialogue.3`): a text inserted before it moves every key after it
	 * onto its neighbour's text, which sync can only see as sources that changed.
	 */
	bool bStable = true;
	/** For translators: what the tool knows about the text -- the speaker, the dialogue, what each argument is. */
	TArray<FDreamLocExtractContext> Context;
};

struct DREAMLOCEDITOR_API FDreamLocExtractNamespace
{
	FString Namespace;
	TArray<FDreamLocExtractEntry> Entries;
};

struct DREAMLOCEDITOR_API FDreamLocExtractManifest
{
	int32 Version = 1;
	FString Provider;
	/** "Plugin.<name>" for a plugin's source; empty for the project's. */
	FString Root;
	/** The source file, relative to its owner: `DUI/UI/Settings.dui`, `Source/Game/Private/HUD.cpp`. */
	FString Source;
	TArray<FDreamLocExtractNamespace> Namespaces;

	/** Where the manifest was read from; empty for one made in memory. */
	FString Path;
	/** When the file was last written; unset for one made in memory. */
	FDateTime TimeStamp;

	int32 NumEntries() const;
	/** Finds or adds the namespace, keeping the order of first appearance. */
	FDreamLocExtractNamespace& FindOrAddNamespace(const FString& InNamespace);
};

namespace DreamLocExtract
{
	/** The provider name of manifests made from the engine's own text gather. */
	inline constexpr const TCHAR* EngineProvider = TEXT("Engine");

	/** The file `dloc gather` leaves next to the Engine manifests to say they cover every source the gather searched. */
	inline constexpr const TCHAR* GatherStampName = TEXT("_gather.json");

	/** False when the text is not a manifest; OutError says why. */
	DREAMLOCEDITOR_API bool Parse(const FString& InText, FDreamLocExtractManifest& OutManifest, FString& OutError);

	/** The text of a manifest: one entry per line, keys in a fixed order, so the same texts give the same bytes. */
	DREAMLOCEDITOR_API FString Write(const FDreamLocExtractManifest& InManifest);

	DREAMLOCEDITOR_API bool LoadFile(const FString& InPath, FDreamLocExtractManifest& OutManifest, FString& OutError);

	/** Writes the manifest unless the file already holds exactly that text. False when it cannot be written. */
	DREAMLOCEDITOR_API bool SaveFileIfChanged(const FString& InPath, const FDreamLocExtractManifest& InManifest);

	/** <owner>/Intermediate/DLoc/extract, absolute. */
	DREAMLOCEDITOR_API FString GetExtractDirectory(const FString& InOwnerDirectory);

	/** Every manifest under an extract directory, sorted; the gather stamp is not one. */
	DREAMLOCEDITOR_API TArray<FString> FindManifestFiles(const FString& InExtractDirectory);

	/**
	 * The .dloc a manifest's translations go in, relative to DLoc/<culture>/: the source path without its extension
	 * (`DUI/UI/Settings.dloc`). The engine's sources go under Engine/ (`Engine/Source/Game/Private/HUD.dloc`): their
	 * own top folders, Source and Content, would say nothing about where the texts came from.
	 */
	DREAMLOCEDITOR_API FString GetDlocPath(const FDreamLocExtractManifest& InManifest);

	/** The `at` of a .dloc entry: `<source>#<where>`, or the source alone. */
	DREAMLOCEDITOR_API FString MakeAt(const FString& InSource, const FString& InWhere);

	/** The source file of an `at` value: what comes before the `#`. */
	DREAMLOCEDITOR_API FString GetAtSource(const FString& InAt);
}
