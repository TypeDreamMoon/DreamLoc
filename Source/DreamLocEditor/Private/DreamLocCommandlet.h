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
 *   -Gather     run the engine's text gather (dloc.ini [Gather]) and write the Engine extraction manifests
 *   -Sync       bring every target culture's .dloc files up to date with the extraction manifests; with -Gather,
 *               gather first. -DryRun reports without writing
 *   -Migrate    move a translation from -From=<key> to -To=<key> in every culture (-Namespace=, -ToNamespace=
 *               when the key alone does not say; -Force replaces a translation the new key has)
 *   -ImportEngine
 *               take the translations of the localization target -Target=<name> (default Game) into the .dloc
 *               files; -NativeCulture= when the target does not say, -Force replaces reviewed ones, -DryRun
 *
 * Options
 *   -File=<path>      one file (absolute, or relative to the project directory) instead of every file
 *   -Root=<dirs>      DLoc folders instead of the project's and the enabled plugins' (comma-separated)
 *   -Culture=<name>   -Show: the culture; the engine's fallbacks and dloc.ini's follow it. -Sync, -ImportEngine:
 *                     only these cultures (comma-separated) instead of the dloc.ini TargetCultures
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
