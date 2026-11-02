# Check samples

Each `.dloc` here is wrong on purpose, in the ways its first comment lines say:

    // expect: DLOC1006@13:5 DLOC1007@15:5

`DLOCnnnn@line:column` per finding `dloc check` must report for the file, a line of 0 for one about the whole
file; a file without an `expect` line must report nothing. The folder is a DLoc folder: `<culture>/` holds the files
of a culture, and `Drafts/` is a folder that is no culture. `DreamLoc.Check.SamplesReportWhatTheySay` runs the check
on this folder and compares; the VS Code extension's tests run its own checks on a copy.
