# DreamLoc

Localization for Unreal Engine 5.8, authored as text. Translations live in `.dloc` files, one folder per language,
and DreamLoc hands them to the engine's own FText translation table. Everything that shows an FText -- DreamGUI,
UMG, Slate, C++ `LOCTEXT`, DataTables -- is translated from the same place, and nothing displays text any other way.

```
<Project>/
  DLoc/
    dloc.ini                     source language (default zh-Hans), target languages, extra fallbacks
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

## Command line

```
pwsh -File Tools/dloc.ps1 check                 # read every .dloc and report what does not read (DLOCnnnn)
pwsh -File Tools/dloc.ps1 fmt -Write            # rewrite the files into the canonical layout
pwsh -File Tools/dloc.ps1 show -Culture en      # the translations the engine would get, and from which file
pwsh -File Tools/dloc.ps1 test                  # build the test host and run the automation tests
```

The test host is made by `Tools/TestHost/New-DreamLocTestHost.ps1`. It has DreamGUI in it too, for the end-to-end
check that a `.dui`'s text follows the `.dloc` files; DreamLoc itself depends on neither DreamGUI nor anything else
of the Dream plugins. DreamGUI writes what it would have translated to `Intermediate/DLoc/extract/DreamGUI/`, which
DreamLoc's sync will read.

## Status

The first stage: the file format, the text source, `dloc.ini`, hot reload in the editor, and the command line's
check, fmt and show. Sync from the extraction manifests, key migration, the engine's own gathered text, baking to
locres for shipping, import and export, and the content checks are still to come.
