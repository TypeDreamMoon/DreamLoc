// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "DreamLocCommandlet.generated.h"

/**
 * DreamLoc without the editor UI.
 *
 *   UnrealEditor-Cmd.exe <Project>.uproject -run=DreamLoc [mode] [options]
 *
 * Modes
 *   (none)      check: read every .dloc and report what does not read
 *   -Format     print each file's canonical text to stdout between `===== begin <file> =====` and
 *               `===== end <file> =====`; with -Write rewrite the files that differ (UTF-8 without BOM, LF);
 *               with -Verify write nothing and count every file that is not canonical as an error.
 *               A file with errors is never rewritten.
 *   -Show       the translations the engine would get for -Culture=<culture>, one per line, as
 *               `<namespace> <key> = "<text>"`, with the file each comes from
 *
 * Options
 *   -File=<path>      one file (absolute, or relative to the project directory) instead of every file
 *   -Root=<dirs>      DLoc folders instead of the project's and the enabled plugins' (comma-separated)
 *   -Culture=<name>   -Show: the culture; the engine's fallbacks and dloc.ini's follow it
 *
 * Every diagnostic is logged as `<file>(<line>,<col>): error DLOCnnnn: message`, then a one-line summary
 * (`DreamLoc check: ...`). The exit code is 0 when nothing went wrong and 1 when anything did.
 */
UCLASS()
class UDreamLocCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UDreamLocCommandlet();

	virtual int32 Main(const FString& Params) override;
};
