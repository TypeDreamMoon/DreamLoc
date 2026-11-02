# DreamLoc

Localization for Unreal Engine 5.8, authored as text. Translations live in `.dloc` files, one folder per language,
and DreamLoc hands them to the engine's own FText translation table. Everything that shows an FText -- DreamGUI,
UMG, Slate, C++ `LOCTEXT`, DataTables -- is translated from the same place, and nothing displays text any other way.

```
<Project>/
  DLoc/
    dloc.ini                     source language (default zh-Hans), target languages, extra fallbacks, what to gather
    en/
      DUI/UI/Settings.dloc       mirrors the source file it translates: DUI/UI/Settings.dui
    ja/ ...
```

```
// DLoc/en/DUI/UI/Settings.dloc
dloc 1
culture en
namespace /Game/UI/Settings/WBP_SettingsScreen

Title.Text {
    at    DUI/UI/Settings.dui#Title
    src   "设置"
    text  "Settings"
    state done
}

Hint.Text {
    at    DUI/UI/Settings.dui#Footer/Hint
    prev  "按 {Key} 返回"
    src   "按 {Key} 返回上一页"
    text  "Press {Key} to go back"
    state done
}
```

- `src` is the current source text and `text` the translation. An entry without `text` is untranslated and shows
  the source.
- `prev` is the source the translation was made from, when the source has changed since. Such an entry is stale:
  the engine sees that its source does not match and shows the current source until someone reviews the
  translation and deletes the `prev` line.
- `at`, `note` and `max` describe where the text comes from and how long it may be; `state` is `machine`, `draft`
  or `done`. Comments (`// ...`) are kept wherever they stand.
- Strings take the escapes `\" \\ \n \t \r`. Files are UTF-8 without a byte order mark.

## How it reaches the screen

`FDreamLocTextSource` is registered with `FTextLocalizationManager` before the engine first loads game text. For
each language the engine asks for (most specific first, then the `[Fallback]` languages of `dloc.ini`) it reads
`DLoc/<language>/**/*.dloc` from every layer -- enabled plugins' `DLoc/` (layer 0), the project's (layer 1), and
mounted ones (2 and up) -- and registers each translation at priority
`-1000 + language index * 64 + (63 - layer)`: above the engine's own locres files, a more specific language
before a higher layer, a mod over the project over a plugin. Only game text is touched; the editor's own text never
is.

In the editor, the game localization preview shows the translations: `DreamLoc.Preview en` in the console (or the
engine's `EnableGameLocalizationPreview`). Saving a `.dloc` while the preview is on reloads the translations without
a restart.

## Where the entries come from

Tools that read sources write extraction manifests: one JSON file per source under
`Intermediate/DLoc/extract/<tool>/`. DreamGUI writes one for each `.dui` it compiles. `dloc gather` runs the
engine's own text gather -- C++ `LOCTEXT` / `NSLOCTEXT`, FText properties of assets -- as far as its manifest and
turns it into one Engine manifest per file (`dloc.ini` `[Gather]` says which folders). `dloc sync` then brings each
target language's `.dloc` files up to date:

- a new key gets an entry with its `src`; a changed source keeps the translation and records the old source as
  `prev`; `at`, `note`, `max` and `src` follow the source; `text`, `state` and comments are never touched;
- a key that disappeared goes to its file's `removed { }` block, kept until pruned, and comes back with its
  translation if the key returns; a deleted source's file keeps all its translations there;
- renames keep translations: a `renamedFrom` in the manifest (a `.dui` node written `Text Heading (was: Title)`)
  moves the translation; a namespace that disappeared moves whole to a new one that has at least 80% of its
  (key, text) pairs (a `.dui`'s `class` line changed, or a file without one moved); a vanished key whose text matches
  exactly one new key of the namespace moves to it. Several candidates or only a similar text: a warning, and
  `dloc migrate` moves it;
- a source moved with its keys: its translations move to the new file and the old file goes;
- a translation written by hand somewhere else moves to its file once a manifest has its key; translations of a
  source that exists but was not compiled are left alone;
- a key the source tool numbers by position (`"stable": false` in the manifest: a DreamNarrative line without an
  `#id`) whose translation was made from another source gets a warning (DLOC3030): a line put in before it moves every
  key after it onto its neighbour's text, which only looks like edits. Give the texts ids (`dnc freeze`).

`dloc import-engine -Target Game` takes the translations an engine localization target already has
(`Content/Localization/Game/<language>/Game.archive`) into the `.dloc` files once, so the localization dashboard is
not needed any more.

## Command line

```
pwsh -File Tools/dloc.ps1 check [-WithSync]     # every check (DLOCnnnn); -WithSync adds what a dry-run sync says
pwsh -File Tools/dloc.ps1 fmt -Write            # rewrite the files into the canonical layout
pwsh -File Tools/dloc.ps1 show -Culture en      # the translations the engine would get, and from which file
pwsh -File Tools/dloc.ps1 gather                # the engine's text gather into Engine extraction manifests
pwsh -File Tools/dloc.ps1 sync [-Gather]        # bring the .dloc files up to date with the manifests
pwsh -File Tools/dloc.ps1 migrate -From Old.Text -To New.Text
pwsh -File Tools/dloc.ps1 import-engine -Target Game
pwsh -File Tools/dloc.ps1 bake                  # the translations into Content/Localization/DreamLoc for packaging
pwsh -File Tools/dloc.ps1 export -Culture en    # what has no translation yet, as JSON for an AI (or -Format xliff/po/csv)
pwsh -File Tools/dloc.ps1 import -In Saved/DLoc/en-batch.json -State machine
pwsh -File Tools/dloc.ps1 prune                 # delete the removed blocks
pwsh -File Tools/dloc.ps1 freeze                # record the keys as shipped: a sync that loses one fails
pwsh -File Tools/dloc.ps1 test                  # build the test host and run the automation tests
```

The test host is made by `Tools/TestHost/New-DreamLocTestHost.ps1`. It has DreamGUI in it too, for the end-to-end
checks that a `.dui`'s text follows the `.dloc` files and keeps its translations through renames and moves; DreamLoc
itself depends on neither DreamGUI nor anything else of the Dream plugins.

## Shipping

`dloc bake` writes what a packaged game reads into `Content/Localization/DreamLoc/`: a `DreamLoc.locres` per target
culture (the project's translations over the plugins', the `dloc.ini` fallbacks filled in, a stale translation
under the hash of its old source so it still shows the source), the source culture's own file mapping every text to
its source, and `DreamLoc.locmeta`. The engine's staging packages that folder like any localization target, for the
cultures in *Project Settings > Packaging > Localizations to Package* (`CulturesToStage`); bake warns about a culture
it bakes that is not staged.

`dloc.ini` `[Publish] Mode` says who reads them in the packaged game:

- `Source` (default): DreamLoc, when there is no `DLoc/` folder. Mods go over them: a plugin of the project's `Mods/`
  folder, or one mounted while the game runs (DLC, user mods: `IPluginManager::MountNewlyCreatedPlugin`), with a
  `DLoc/` folder is a layer of its own, and its cultures -- a language the game itself does not have -- join the
  game's language list.
- `Native`: the engine, with `+LocalizationPaths=%GAMEDIR%Content/Localization/DreamLoc` in `[Internationalization]`
  of `DefaultGame.ini` (bake warns when it is missing). DreamLoc leaves the baked files alone. Mods are for `Source`.

Once a version is out, `dloc freeze` records its keys in `DLoc/dloc.frozen`: a later sync that finds one of them gone
from the sources, and not renamed, writes nothing and says so (DLOC3022). `dloc prune` deletes the removed blocks
whose translations nobody needs any more. A plugin keeps its own texts with `dloc gather -Plugin <name>` and
`dloc sync -Plugin <name>`, into its own `DLoc/` folder (with its own `dloc.ini`, or the project's cultures).

With DreamNarrative in the test host too (a worktree in `Plugins/DreamNarrative`, the plugin enabled in the
`.uproject`, the `.dna` under `DNarrative/`), `DreamLocHost.Acceptance.DreamNarrative.*` takes a chapter through DLoc:
first `dnc.ps1 build` and `dnc.ps1 loc-export` with `-Project <host> -Root <host>/DNarrative` (an absolute root), then
the test syncs the chapter's manifest, imports the old CSV with its translations, checks, and reads the compiled
chapter's texts under the English preview. Without DreamNarrative the test has nothing to check and says so.

`Tools/TestHost/Test-Package.ps1` packages the test host, Development and Shipping, finds the baked files in the pak
and runs each package with `-DreamLocProbe`: it reads its texts in Chinese and English, mounts a mod that brings
Polish, reads them in Polish, and writes what it saw for the script to compare.

## Translating outside the files

`dloc export -Culture en` writes what needs a translator -- by default what has no translation and what is stale;
`-Only` picks others (`machine`, `draft`, `done`, `all`), `-Files` one screen's file, `-Reference ja` adds Japanese
beside each -- as JSON (for an AI), XLIFF 1.2, PO or CSV. Each entry has its source text, note, `max`, where it comes
from, what the source tool says about it (its manifest's `kind` and `context`: who speaks a line, the line before it,
what each `{0}` is), the texts around it in its file, the culture's terms the source uses and the words it keeps, and
for a stale entry the old source and its translation. `dloc import -In <file>` brings the translations back by
namespace and key: `-State machine` marks them, a reviewed translation (`state done`) stays unless `-Force`, and a
translation made for a source text that has changed since comes in stale. A CSV's columns are found by name, so
DreamNarrative's old `dnc loc-export` file imports too (it names no culture: give `-Culture`).

For an AI the JSON is the whole job: it carries its own instructions (keep the arguments and tags, the plural forms
the language needs, the length, the terms), and the model only fills in each entry's `text`:

```
pwsh -File Tools/dloc.ps1 export -Culture en -Files DUI/UI/Settings
(the model translates Saved/DLoc/en-batch.json)
pwsh -File Tools/dloc.ps1 import -In Saved/DLoc/en-batch.json -State machine
pwsh -File Tools/dloc.ps1 check
(a person reviews, fixes what check reports, and sets state done)
```

## Checks

`dloc check` reports, as `<file>(<line>,<col>): error DLOCnnnn: ...`:

- **1xxx**, what does not read: strings, escapes, fields and their values, duplicate keys, blocks, the header; a
  `culture` line against its folder, a folder that is no culture, a translation without a source text; `dloc.ini`.
- **2xxx**, each translation against its `src`: its format arguments (none missing, none added); the plural forms
  its culture needs (the engine's own pattern check: Russian needs one / few / many / other, Japanese has one and a
  `plural` there is only a warning); a pattern the engine cannot format; rich-text tags in pairs and the same as the
  source's; blanks at either end; line breaks; `max` (a full-width character counts 2, arguments nothing, a plural
  its widest form); the culture's terms (`_terms.dloc`).
- **3xxx**, the files against each other and the sources: a stale translation, a removed block, a key in two files
  of a culture, a mod translating a key differently from the project, how much of each culture is untranslated;
  with `-WithSync`, rename candidates, conflicting sources, sources never compiled and files sync would change.

`Tests/Check/` holds a deliberately broken sample for each, saying in `// expect:` comments what the check reports
and where; `DreamLoc.Check.SamplesReportWhatTheySay` holds the check to it. The VS Code extension
[dreamloc-language-support](https://github.com/TypeDreamMoon/dreamloc-language-support) runs the same checks as you
type (all but the plural forms and the project-wide ones) and `dloc check` on request.

## Status

Done: the file format, the text source, `dloc.ini`, hot reload in the editor, sync with key migration, the engine's
gather and the import of its localization targets, the checks, bake with Source and Native shipping, mod layers,
prune and freeze, and export / import for AI, XLIFF, PO and CSV. Still to come: DreamGUI's own checks (overflow and
missing glyphs measured in the real widget), and DreamNarrative.
