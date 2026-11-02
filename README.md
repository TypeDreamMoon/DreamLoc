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
  source that exists but was not compiled are left alone.

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
pwsh -File Tools/dloc.ps1 test                  # build the test host and run the automation tests
```

The test host is made by `Tools/TestHost/New-DreamLocTestHost.ps1`. It has DreamGUI in it too, for the end-to-end
checks that a `.dui`'s text follows the `.dloc` files and keeps its translations through renames and moves; DreamLoc
itself depends on neither DreamGUI nor anything else of the Dream plugins.

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

Done: the file format, the text source, `dloc.ini`, hot reload in the editor, check / fmt / show, sync with key
migration, the engine's gather, the import of engine localization targets, and the checks. Still to come: baking to
locres for shipping, mod layers, export and import of PO / XLIFF / CSV, `dloc prune`, and DreamGUI's own checks
(overflow and missing glyphs in the real widget).
