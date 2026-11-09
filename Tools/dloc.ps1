# Copyright DreamMoon. All Rights Reserved.
#
# DreamLoc command line.
#
#   dloc.ps1 make                     build the test host's editor target (the C++)
#   dloc.ps1 test [-Filter F] [-NoBuild]
#                                     make, then run the automation tests headless in the test host (every
#                                     DreamLoc.* test and the host's DreamLocHost.* end-to-end check; -Filter
#                                     replaces that, several prefixes joined with `+`)
#
#   dloc.ps1 check [-File F] [-Root R] [-WithSync]
#                                     every .dloc: what does not read (1xxx), each translation against its source
#                                     (2xxx), and across files (3xxx); -WithSync adds what a dry-run sync says
#   dloc.ps1 fmt [-File F] [-Root R] [-Write | -Verify] [-Markers]
#                                     print the canonical text; -Write rewrites the files that differ; -Verify
#                                     writes nothing and fails on any file not canonical. A file with errors is
#                                     never rewritten
#   dloc.ps1 show -Culture C [-Root R]
#                                     the translations the engine would get for that culture, and from which file
#   dloc.ps1 gather [-Plugin P]       run the engine's text gather (C++ LOCTEXT, asset FTexts, with [Gather]
#                                     MetaData=true the reflected types' display names, tooltips and categories;
#                                     dloc.ini [Gather]) into Engine extraction manifests
#   dloc.ps1 sync [-Gather] [-Plugin P] [-Culture C] [-DryRun]
#                                     bring every target culture's .dloc files up to date with the extraction
#                                     manifests: new keys, changed sources, renames, removed texts; -Gather first
#                                     gathers; -Culture limits it to some cultures (comma-separated)
#   dloc.ps1 migrate -From K -To K [-Namespace N] [-ToNamespace N] [-Force]
#                                     move a translation sync would not move alone (it said so with DLOC3002)
#   dloc.ps1 import-engine [-Target Game] [-NativeCulture C] [-Culture C] [-Force] [-DryRun]
#                                     take the translations of an engine localization target into the .dloc files
#   dloc.ps1 bake [-DryRun]           the project's and plugins' translations into Content/Localization/DreamLoc for a
#                                     packaged game; warns when the packaging settings would leave them out
#   dloc.ps1 prune [-DryRun]          delete the removed blocks, and the files left empty
#   dloc.ps1 freeze                   record the keys as shipped (DLoc/dloc.frozen): a sync that loses one fails
#   dloc.ps1 export -Culture C [-Only untranslated,stale] [-Format json|xliff|po|csv] [-Out F] [-Files P] [-Reference C]
#                                     the entries to translate, for an AI (json carries its own instructions), a
#                                     translator's tool or a spreadsheet; -Files limits it to some .dloc paths
#   dloc.ps1 import -In F [-Culture C] [-State machine] [-Force] [-DryRun]
#                                     translations back in: a reviewed one (state done) is kept unless -Force
#   -Plugin P                         gather and sync a plugin's own Source/Content into its own DLoc/ folder
#
# Every command but make and test runs `UnrealEditor-Cmd <project> -run=DreamLoc` on -Project, else the project above -File,
# else the project above the current directory, else the test host, and keep the lines that matter: diagnostics
# (`<file>(<line>,<col>): error DLOCnnnn: ...`), the summary, errors and crashes (-Raw shows everything).
# -File is a .dloc (absolute, relative to the current directory, or relative to the project directory). -Root
# replaces the project's and the enabled plugins' DLoc/ folders (comma-separated). fmt prints the text on the
# output stream and its status lines on the host; several files' texts come between `===== begin <name> =====`
# and `===== end <name> =====` lines, and -Markers puts them around a single text too.
# The engine log of each run is kept in <project>/Saved/DreamLoc/.
# The engine is the project's EngineAssociation, looked up among registered source builds and Launcher
# installations; -EngineRoot <dir> names one instead.
#
# Everything runs below normal priority (-Priority changes it), and the editor gets the user's UE-ZenDataPath and
# UE-LocalDataCachePath even when this shell was started without them.
#
# Exit codes
#   make: 0 built, 2 failed.
#   test: 0 green, 1 red (a test failed or an ensure fired), 2 not judged (the build failed, the editor crashed or
#         timed out, or no test ran). Results land in <host>/Saved/DreamLocTests/<time>/: the engine's index.json,
#         the run log and summary.md.
#   check, fmt, show, gather, sync, migrate, import-engine: 0 no errors, 1 errors (fmt -Verify: a file not
#         canonical), 2 the commandlet did not finish.

[CmdletBinding()]
param(
    [Parameter(Position = 0)][ValidateSet('make', 'test', 'check', 'fmt', 'show', 'gather', 'sync', 'migrate', 'import-engine', 'bake', 'prune', 'freeze', 'export', 'import')][string]$Command = 'test',
    [string]$Filter,
    [switch]$NoBuild,
    [string]$Project,
    [string]$EngineRoot,
    [int]$TimeoutMinutes = 0,
    [string]$File,
    [string[]]$Root,
    [string]$Culture,
    [switch]$Write,
    [switch]$Verify,
    [switch]$Markers,
    [switch]$Gather,
    [switch]$DryRun,
    [switch]$WithSync,
    [switch]$Force,
    [string]$From,
    [string]$To,
    [string]$Namespace,
    [string]$ToNamespace,
    [string]$Target,
    [string]$NativeCulture,
    [string]$Plugin,
    [string]$Only,
    [string]$Format,
    [string]$Out,
    [string]$Files,
    [string]$Reference,
    [string]$In,
    [string]$State,
    [switch]$Raw,
    [ValidateSet('Idle', 'BelowNormal', 'Normal')][string]$Priority = 'BelowNormal'
)

$ErrorActionPreference = 'Stop'

# A redirected stdout is otherwise written in the console's code page, which mangles Chinese text.
if ([Console]::IsOutputRedirected) { [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false) }

# Children inherit a below-normal priority class: the build, the compiler and the editor all run under this one.
try { (Get-Process -Id $PID).PriorityClass = $Priority } catch { }

# Derived data and Zen caches where the user keeps them, never the default on C:.
foreach ($name in @('UE-ZenDataPath', 'UE-LocalDataCachePath')) {
    if (-not [Environment]::GetEnvironmentVariable($name, 'Process')) {
        $value = [Environment]::GetEnvironmentVariable($name, 'User')
        if ($value) { [Environment]::SetEnvironmentVariable($name, $value, 'Process') }
    }
}

$DefaultFilter = 'DreamLoc.+DreamLocHost.'
$MinTests = 10

function Write-Step([string]$Text) { Write-Host "==> $Text" -ForegroundColor Cyan }
function Write-Note([string]$Text) { Write-Host "    $Text" }
function Write-Problem([string]$Text) { Write-Host "!!  $Text" -ForegroundColor Yellow }

function Find-HostProject {
    if ($Project) { return (Resolve-Path $Project).Path }
    # The plugin normally lives in the host: <host>/Plugins/DreamLoc/Tools.
    $candidate = Get-ChildItem -Path (Join-Path $PSScriptRoot '..\..\..') -Filter 'DreamLocTestHost.uproject' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($candidate) { return $candidate.FullName }
    $default = 'I:\UnrealProject_Moon\DEV_58\DreamLocTestHost\DreamLocTestHost.uproject'
    if (Test-Path $default) { return $default }
    throw 'No test host found. Create it with Tools/TestHost/New-DreamLocTestHost.ps1, or pass -Project.'
}

function Find-ProjectAbove([string]$Path) {
    $dir = if (Test-Path -LiteralPath $Path -PathType Container) { $Path } else { Split-Path -Parent $Path }
    while ($dir) {
        $found = Get-ChildItem -LiteralPath $dir -Filter '*.uproject' -File -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($found) { return $found.FullName }
        $parent = Split-Path -Parent $dir
        if ($parent -eq $dir) { break }
        $dir = $parent
    }
    return $null
}

function Resolve-EngineRoot([string]$UProject) {
    $assoc = (Get-Content -LiteralPath $UProject -Raw -Encoding UTF8 | ConvertFrom-Json).EngineAssociation
    $root = $EngineRoot
    if (-not $root) {
        $builds = Get-ItemProperty 'HKCU:\Software\Epic Games\Unreal Engine\Builds' -ErrorAction SilentlyContinue
        $root = if ($builds) { $builds.$assoc } else { $null }
        if (-not $root) {
            $launcher = Get-ItemProperty "HKLM:\SOFTWARE\EpicGames\Unreal Engine\$assoc" -ErrorAction SilentlyContinue
            if ($launcher) { $root = $launcher.InstalledDirectory }
        }
        if (-not $root) {
            $manifest = Join-Path $env:ProgramData 'Epic\UnrealEngineLauncher\LauncherInstalled.dat'
            if (Test-Path -LiteralPath $manifest) {
                $installs = (Get-Content -LiteralPath $manifest -Raw -Encoding UTF8 | ConvertFrom-Json).InstallationList
                $install = $installs | Where-Object { $_.AppName -eq "UE_$assoc" } | Select-Object -First 1
                if ($install) { $root = $install.InstallLocation }
            }
        }
    }
    if (-not $root) { throw "Engine association $assoc is not registered. Pass -EngineRoot." }
    foreach ($required in @('Engine\Build\Build.version', 'Engine\Build\BatchFiles\Build.bat')) {
        if (-not (Test-Path -LiteralPath (Join-Path $root $required) -PathType Leaf)) {
            throw "Engine $root is missing $required. Pass -EngineRoot to select a complete installation."
        }
    }
    return $root.Replace('/', '\')
}

$sourceFile = $File
if ($File -and (Test-Path -LiteralPath $File -PathType Leaf)) { $sourceFile = (Resolve-Path -LiteralPath $File).Path }

$uproject = if ($Command -in 'make', 'test') { Find-HostProject }
elseif ($Project) { (Resolve-Path $Project).Path }
elseif ($sourceFile -and [IO.Path]::IsPathRooted($sourceFile) -and (Find-ProjectAbove $sourceFile)) { Find-ProjectAbove $sourceFile }
elseif (Find-ProjectAbove (Get-Location).Path) { Find-ProjectAbove (Get-Location).Path }
else { Find-HostProject }
$projectDir = Split-Path $uproject
$hostName = [IO.Path]::GetFileNameWithoutExtension($uproject)
$engineRoot = Resolve-EngineRoot $uproject
$editorCmd = Join-Path $engineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'

function Invoke-Build {
    Write-Step "Building ${hostName}Editor"
    $log = Join-Path $projectDir 'Saved\DreamLocTests\build.log'
    New-Item -ItemType Directory -Force -Path (Split-Path $log) | Out-Null
    $buildBat = Join-Path $engineRoot 'Engine\Build\BatchFiles\Build.bat'
    & $buildBat "${hostName}Editor" Win64 Development "-Project=$uproject" -WaitMutex -NoHotReloadFromIDE *> $log
    $code = $LASTEXITCODE
    $errors = @(Select-String -LiteralPath $log -Pattern 'error C\d+|error LNK\d+|error :|\): error' | Select-Object -First 20 | ForEach-Object { $_.Line.Trim() })
    if ($code -ne 0) {
        Write-Problem "Build failed (exit $code); log: $log"
        foreach ($e in $errors) { Write-Note $e }
        return $false
    }
    Write-Note (Select-String -LiteralPath $log -Pattern 'Result: |Total execution time' | ForEach-Object { $_.Line.Trim() })
    return $true
}

function Get-Digest([string]$RunDir, [int]$Floor, [bool]$TimedOut, $ExitCode, [double]$Seconds) {
    $runLog = Join-Path $RunDir 'run.log'
    $logLines = if (Test-Path -LiteralPath $runLog) { @(Get-Content -LiteralPath $runLog -Encoding utf8) } else { @() }
    $infra = @()
    $crash = @($logLines | Where-Object { $_ -match '=== Critical error: ===|Fatal error!|Unhandled Exception|Assertion failed:' } | Select-Object -First 5)
    if ($crash.Count -gt 0) { $infra += 'The editor crashed:'; $infra += $crash }
    if ($TimedOut) { $infra += 'The run timed out.' }
    $ensures = @($logLines | Where-Object { $_ -match 'Ensure condition failed' } | Select-Object -Unique)
    $loadFailures = @($logLines | Where-Object { $_ -match "Plugin 'DreamLoc' failed to load|Unable to load module.*DreamLoc" } | Select-Object -First 3)
    if ($loadFailures.Count -gt 0) { $infra += 'The plugin did not load:'; $infra += $loadFailures }

    $tests = @()
    $indexPath = Join-Path $RunDir 'index.json'
    if (Test-Path -LiteralPath $indexPath) {
        $index = Get-Content -LiteralPath $indexPath -Raw -Encoding utf8 | ConvertFrom-Json
        foreach ($t in @($index.tests)) {
            $errors = @(); $infos = @()
            foreach ($entry in @($t.entries)) {
                if ($entry.event.type -eq 'Error') { $errors += [string]$entry.event.message }
                elseif ($entry.event.type -eq 'Info') { $infos += [string]$entry.event.message }
            }
            $tests += [pscustomobject]@{ Path = [string]$t.fullTestPath; State = [string]$t.state; Duration = [double]$t.duration; Errors = $errors; Infos = $infos }
        }
    } else {
        $infra += "No index.json in $RunDir; the tests did not report."
    }

    $passed = @($tests | Where-Object { $_.State -eq 'Success' })
    $failed = @($tests | Where-Object { $_.State -eq 'Fail' })
    $incomplete = @($tests | Where-Object { $_.State -notin 'Success', 'Fail', 'Skipped' })
    # The engine exits with -1 when a test failed or logged an error: a red run, not a broken one.
    $redExit = $ExitCode -eq -1 -and ($failed.Count -gt 0 -or $ensures.Count -gt 0)
    if (-not $TimedOut -and ($null -eq $ExitCode -or ($ExitCode -ne 0 -and -not $redExit))) { $infra += "The editor did not exit successfully (exit $ExitCode)." }
    if ($incomplete.Count -gt 0) { $infra += "$($incomplete.Count) test(s) did not finish: $($incomplete.Path -join ', ')." }
    $ran = $passed.Count + $failed.Count
    if ($ran -lt $Floor) { $infra += "Only $ran test(s) ran; expected at least $Floor. Did DreamLocTests load?" }

    $code = if ($infra.Count -gt 0) { 2 } elseif ($failed.Count -gt 0 -or $ensures.Count -gt 0) { 1 } else { 0 }
    $word = switch ($code) { 0 { 'green' } 1 { 'red' } default { 'not judged' } }

    $summary = @("# DreamLoc tests -- $word", '')
    $summary += ('Ran {0}: {1} passed, {2} failed; {3:N0} s in the editor.' -f $ran, $passed.Count, $failed.Count, $Seconds)
    if ($infra.Count -gt 0) { $summary += ''; $summary += '## Not judged'; $summary += @($infra | ForEach-Object { "- $_" }) }
    if ($failed.Count -gt 0) {
        $summary += ''; $summary += '## Failed'
        foreach ($t in $failed) { foreach ($e in ($t.Errors | Select-Object -First 8)) { $summary += "- $($t.Path): $e" } }
    }
    if ($ensures.Count -gt 0) { $summary += ''; $summary += '## Ensures'; $summary += @($ensures | ForEach-Object { "- $($_.Trim())" }) }
    $summary += ''; $summary += '## Tests'
    foreach ($t in $tests) {
        $summary += ('- {0} {1} ({2:N0} ms)' -f $t.State, $t.Path, ($t.Duration * 1000.0))
        foreach ($i in ($t.Infos | Select-Object -First 40)) { $summary += "    - $i" }
    }
    Set-Content -LiteralPath (Join-Path $RunDir 'summary.md') -Value ($summary -join "`n") -Encoding utf8
    Write-Step "Tests: $word"
    foreach ($line in $summary | Select-Object -Skip 2) { if ($line) { Write-Note $line } }
    return $code
}

function Invoke-Tests {
    $runFilter = if ($Filter) { $Filter.Trim() } else { $DefaultFilter }
    if ($runFilter -match "[,;'`"]") { throw "The filter may not hold commas, semicolons or quotes: $runFilter" }
    $timeout = if ($TimeoutMinutes -gt 0) { $TimeoutMinutes } else { 30 }
    $runDir = Join-Path $projectDir ('Saved\DreamLocTests\{0}' -f (Get-Date -Format 'yyyyMMdd-HHmmss'))
    New-Item -ItemType Directory -Force -Path $runDir | Out-Null
    $runLog = Join-Path $runDir 'run.log'
    # The translation tests set their own translators.ini; this one catches anything else, so the user's is never read.
    [Environment]::SetEnvironmentVariable('DREAMLOC_TRANSLATORS_INI', (Join-Path $runDir 'translators.ini'), 'Process')
    # No value may end in a backslash: the C runtime reads \" as a quote.
    $parts = @(
        "`"$uproject`"",
        "-ExecCmds=`"Automation RunTests $runFilter`"",
        '-TestExit="Automation Test Queue Empty"',
        "-ReportExportPath=`"$($runDir.TrimEnd('\'))`"",
        "-abslog=`"$runLog`"",
        '-unattended', '-nopause', '-nosplash', '-NoSound', '-nullrhi', '-NoLiveCoding',
        # The editor's home screen checks the internet every few seconds; the tests stay off the network.
        '-ini:Engine:[ConsoleVariables]:HomeScreen.EnableHomeScreen=0'
    )
    $argLine = $parts -join ' '
    Set-Content -LiteralPath (Join-Path $runDir 'command.txt') -Value "`"$editorCmd`" $argLine" -Encoding utf8
    Write-Step "Running $runFilter"
    Write-Note "log: $runLog"
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $timedOut = $false
    $proc = Start-Process -FilePath $editorCmd -ArgumentList $argLine -WorkingDirectory $projectDir -NoNewWindow -PassThru `
        -RedirectStandardOutput (Join-Path $runDir 'stdout.txt') -RedirectStandardError (Join-Path $runDir 'stderr.txt')
    $null = $proc.Handle
    Set-Content -LiteralPath (Join-Path $runDir 'pid.txt') -Value $proc.Id -Encoding utf8
    if (-not $proc.WaitForExit($timeout * 60 * 1000)) {
        $timedOut = $true
        Write-Problem "Still running after $timeout minutes: killing the editor."
        try { $proc.Kill($true) } catch { }
        $null = $proc.WaitForExit(60000)
    }
    $watch.Stop()
    $exitCode = if ($timedOut) { $null } else { $proc.ExitCode }
    $floor = if ($Filter) { 1 } else { $MinTests }
    return (Get-Digest -RunDir $runDir -Floor $floor -TimedOut $timedOut -ExitCode $exitCode -Seconds $watch.Elapsed.TotalSeconds)
}

function Format-Switch([string]$Name, [string]$Value) {
    # One argument, quoted. A value may not end in a backslash (the C runtime reads \" as a quote) nor hold a quote.
    if ($Value.Contains('"')) { throw "-$Name may not hold a quote: $Value" }
    return "-$Name=`"$($Value.TrimEnd('\'))`""
}

$script:CommandletExit = 2

function Invoke-Commandlet {
    $arguments = @()
    switch ($Command) {
        'fmt' { $arguments += '-Format' }
        'show' { $arguments += '-Show' }
        'gather' { $arguments += '-Gather' }
        'sync' { $arguments += '-Sync'; if ($Gather) { $arguments += '-Gather' } }
        'migrate' { $arguments += '-Migrate' }
        'import-engine' { $arguments += '-ImportEngine' }
        'bake' { $arguments += '-Bake' }
        'prune' { $arguments += '-Prune' }
        'freeze' { $arguments += '-Freeze' }
        'export' { $arguments += '-Export' }
        'import' { $arguments += '-Import' }
    }
    if ($DryRun) { $arguments += '-DryRun' }
    if ($WithSync) { $arguments += '-WithSync' }
    if ($Force) { $arguments += '-Force' }
    if ($From) { $arguments += Format-Switch 'From' $From }
    if ($To) { $arguments += Format-Switch 'To' $To }
    if ($Namespace) { $arguments += Format-Switch 'Namespace' $Namespace }
    if ($ToNamespace) { $arguments += Format-Switch 'ToNamespace' $ToNamespace }
    if ($Target) { $arguments += Format-Switch 'Target' $Target }
    if ($NativeCulture) { $arguments += Format-Switch 'NativeCulture' $NativeCulture }
    if ($Plugin) { $arguments += Format-Switch 'Plugin' $Plugin }
    foreach ($pair in @(@('Only', $Only), @('Format', $Format), @('Out', $Out), @('Files', $Files), @('Reference', $Reference), @('In', $In), @('State', $State))) {
        if ($pair[1]) { $arguments += Format-Switch $pair[0] $pair[1] }
    }
    if ($sourceFile) { $arguments += Format-Switch 'File' $sourceFile }
    if ($Root) { $arguments += Format-Switch 'Root' (($Root | ForEach-Object { $_.TrimEnd('\') }) -join ',') }
    if ($Culture) { $arguments += Format-Switch 'Culture' $Culture }
    if ($Write) { $arguments += '-Write' }
    if ($Verify) { $arguments += '-Verify' }

    $runDir = Join-Path $projectDir 'Saved\DreamLoc'
    New-Item -ItemType Directory -Force -Path $runDir | Out-Null
    $stamp = '{0}-{1}' -f $Command, (Get-Date -Format 'yyyyMMdd-HHmmss')
    $log = Join-Path $runDir "$stamp.log"
    $stdoutFile = Join-Path $runDir "$stamp.stdout.txt"
    $stderrFile = Join-Path $runDir "$stamp.stderr.txt"
    $parts = @("`"$uproject`"", '-run=DreamLoc') + $arguments + @(
        "-abslog=`"$log`"", '-unattended', '-nopause', '-nosplash', '-NoSound', '-nullrhi', '-NoLiveCoding')
    $argLine = $parts -join ' '
    Write-Verbose "`"$editorCmd`" $argLine"

    $proc = Start-Process -FilePath $editorCmd -ArgumentList $argLine -WorkingDirectory $projectDir -NoNewWindow -PassThru `
        -RedirectStandardOutput $stdoutFile -RedirectStandardError $stderrFile
    $null = $proc.Handle
    $timedOut = $false
    $limit = if ($TimeoutMinutes -gt 0) { $TimeoutMinutes * 60 * 1000 } else { -1 }
    if (-not $proc.WaitForExit($limit)) {
        $timedOut = $true
        try { $proc.Kill($true) } catch { }
        $null = $proc.WaitForExit(60000)
    }
    $exitCode = if ($timedOut) { $null } else { $proc.ExitCode }

    $stdoutLines = if (Test-Path -LiteralPath $stdoutFile) { @(Get-Content -LiteralPath $stdoutFile -Encoding utf8) } else { @() }
    $logLines = if (Test-Path -LiteralPath $log) { @(Get-Content -LiteralPath $log -Encoding utf8) } else { @() }

    # fmt prints text: that goes on the output stream, everything else on the host.
    $printsText = $Command -eq 'fmt' -and -not $Write -and -not $Verify
    $status = if ($printsText) { { param($Line) Write-Host $Line } } else { { param($Line) Write-Output $Line } }

    if ($Raw) {
        $stdoutLines | ForEach-Object { Write-Output $_ }
        $logLines | ForEach-Object { & $status $_ }
    } else {
        $blocks = [Collections.Generic.List[object]]::new()
        $current = $null
        foreach ($line in $stdoutLines) {
            if ($line -match '^===== begin (.*) =====$') { $current = [pscustomobject]@{ Name = $Matches[1]; Lines = [Collections.Generic.List[string]]::new() }; continue }
            if ($line -match '^===== end (.*) =====$') { if ($current) { $blocks.Add($current); $current = $null }; continue }
            if ($current) { $current.Lines.Add($line) }
        }
        $marked = $Markers -or $blocks.Count -gt 1
        foreach ($block in $blocks) {
            if ($marked) { Write-Output "===== begin $($block.Name) =====" }
            $block.Lines | ForEach-Object { Write-Output $_ }
            if ($marked) { Write-Output "===== end $($block.Name) =====" }
        }
        foreach ($line in $logLines) {
            $clean = ($line -replace '^\[[^\]]*\]\[\s*\d+\]', '').TrimEnd()
            if ($clean -match '^LogDreamLocCommandlet: (?:(?:Display|Warning|Error|Log): )?(.*)$') { & $status $Matches[1]; continue }
            if ($clean -match '=== Critical error|Fatal error|Unhandled Exception|Assertion failed|: Error: |DLOC\d{4}') { & $status $clean }
        }
    }

    $crashed = @($logLines + $stdoutLines | Where-Object { $_ -match '=== Critical error: ===|Fatal error!|Unhandled Exception|Assertion failed:' }).Count -gt 0
    $finished = @($logLines | Where-Object { $_ -match 'LogDreamLocCommandlet: (?:Display: )?DreamLoc \w+: ' }).Count -gt 0
    $script:CommandletExit = 0
    if ($timedOut) { Write-Problem "Still running after $TimeoutMinutes minutes: the editor was killed. Log: $log"; $script:CommandletExit = 2 }
    elseif ($crashed) { Write-Problem "The editor crashed. Log: $log"; $script:CommandletExit = 2 }
    elseif (-not $finished) { Write-Problem "The commandlet did not finish (exit $exitCode). Log: $log"; $script:CommandletExit = 2 }
    elseif ($exitCode -ne 0) { $script:CommandletExit = 1 }
}

switch ($Command) {
    'make' { if (Invoke-Build) { exit 0 } else { exit 2 } }
    'test' {
        if (-not $NoBuild) { if (-not (Invoke-Build)) { exit 2 } }
        exit (Invoke-Tests)
    }
    default {
        Invoke-Commandlet
        exit $script:CommandletExit
    }
}
