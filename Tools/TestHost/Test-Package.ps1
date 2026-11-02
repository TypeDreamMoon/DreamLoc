# Copyright DreamMoon. All Rights Reserved.
#
# The packaged check: bakes the test host's translations, packages it (Development and Shipping by default), looks
# for the baked files in the pak, then runs each package without a window and with the null renderer, started with
# -DreamLocProbe: the game reads its texts in Chinese and English, mounts TestMods/DreamLocTestMod (a mod that brings
# Polish), reads them in Polish, writes what it saw as JSON and quits. The script compares that with what it should be.
#
#   pwsh -File Test-Package.ps1 [-Configuration Development,Shipping] [-NoPackage] [-NoBake]
#
# Run `dloc.ps1 test` once before: an acceptance test saves the UMG asset the probe reads.
# -NoPackage reuses the packages already in <host>/Saved/Packages/<configuration> and only runs the probe.
# Everything runs below normal priority; packaging takes a while (the game target is built and the content cooked).
# Exit code: 0 every check passed, 1 a check failed, 2 packaging or the game did not finish.

[CmdletBinding()]
param(
    [ValidateSet('Development', 'Shipping')][string[]]$Configuration = @('Development', 'Shipping'),
    [string]$HostDir,
    [string]$EngineRoot,
    [switch]$NoPackage,
    [switch]$NoBake,
    [int]$TimeoutMinutes = 120,
    [ValidateSet('Idle', 'BelowNormal', 'Normal')][string]$Priority = 'BelowNormal'
)

$ErrorActionPreference = 'Stop'
if ([Console]::IsOutputRedirected) { [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false) }
try { (Get-Process -Id $PID).PriorityClass = $Priority } catch { }
foreach ($name in @('UE-ZenDataPath', 'UE-LocalDataCachePath')) {
    if (-not [Environment]::GetEnvironmentVariable($name, 'Process')) {
        $value = [Environment]::GetEnvironmentVariable($name, 'User')
        if ($value) { [Environment]::SetEnvironmentVariable($name, $value, 'Process') }
    }
}

function Write-Step([string]$Text) { Write-Host "==> $Text" -ForegroundColor Cyan }
function Write-Note([string]$Text) { Write-Host "    $Text" }
function Write-Problem([string]$Text) { Write-Host "!!  $Text" -ForegroundColor Yellow }

if (-not $HostDir) { $HostDir = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path }
$uproject = Get-ChildItem -LiteralPath $HostDir -Filter '*.uproject' | Select-Object -First 1
if (-not $uproject) { throw "No .uproject in $HostDir" }
$hostName = $uproject.BaseName
$assoc = (Get-Content -LiteralPath $uproject.FullName -Raw -Encoding UTF8 | ConvertFrom-Json).EngineAssociation
if (-not $EngineRoot) {
    $builds = Get-ItemProperty 'HKCU:\Software\Epic Games\Unreal Engine\Builds' -ErrorAction SilentlyContinue
    $EngineRoot = if ($builds) { $builds.$assoc } else { $null }
    if (-not $EngineRoot) {
        $manifest = Join-Path $env:ProgramData 'Epic\UnrealEngineLauncher\LauncherInstalled.dat'
        $install = (Get-Content -LiteralPath $manifest -Raw -Encoding UTF8 | ConvertFrom-Json).InstallationList | Where-Object { $_.AppName -eq "UE_$assoc" } | Select-Object -First 1
        if ($install) { $EngineRoot = $install.InstallLocation }
    }
}
if (-not $EngineRoot) { throw "Engine $assoc is not registered; pass -EngineRoot." }
$uat = Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat'
$unrealPak = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealPak.exe'
$packages = Join-Path $HostDir 'Saved\Packages'
New-Item -ItemType Directory -Force -Path $packages | Out-Null
$mod = Join-Path $HostDir 'TestMods\DreamLocTestMod\DreamLocTestMod.uplugin'
if (-not (Test-Path -LiteralPath $mod)) { throw "No test mod at $mod; run New-DreamLocTestHost.ps1 again." }
# The UMG text the probe reads is in an asset the acceptance tests save (DreamLocHost.Acceptance.CppAndAsset...).
$umgAsset = Join-Path $HostDir 'Content\Acceptance\Gather\WBP_GatherUmg.uasset'
if (-not (Test-Path -LiteralPath $umgAsset)) { throw "No $umgAsset; run 'dloc.ps1 test' once first, it saves the asset." }

if (-not $NoBake) {
    Write-Step 'Baking the translations'
    & pwsh -NoProfile -File (Join-Path $HostDir 'Plugins\DreamLoc\Tools\dloc.ps1') bake -Project $uproject.FullName -Priority $Priority | ForEach-Object { Write-Note $_ }
    if ($LASTEXITCODE -ne 0) { Write-Problem "dloc bake failed (exit $LASTEXITCODE)"; exit 2 }
}

# What each step of the probe must have read: the code's LOCTEXT and the UMG text block's text.
$expected = [ordered]@{
    'start'       = @{ code = '继续游戏'; umg = '开始游戏' }
    'en'          = @{ code = 'Continue'; umg = 'Start Game' }
    'zh-Hans'     = @{ code = '继续游戏'; umg = '开始游戏' }
    'mod mounted' = @{ code = '继续游戏'; umg = '开始游戏' }
    'pl'          = @{ code = 'Kontynuuj'; umg = 'Rozpocznij grę' }
}

$results = [Collections.Generic.List[string]]::new()
$failed = 0
$broken = 0
foreach ($config in $Configuration) {
    $archive = Join-Path $packages $config
    if (-not $NoPackage) {
        Write-Step "Packaging $config (log: $packages\$config.log)"
        $watch = [Diagnostics.Stopwatch]::StartNew()
        & $uat BuildCookRun "-project=$($uproject.FullName)" -noP4 -platform=Win64 "-clientconfig=$config" -build -cook -stage -pak -archive `
            "-archivedirectory=$archive" -nocompileeditor -skipbuildeditor -unattended -utf8output *> (Join-Path $packages "$config.log")
        $code = $LASTEXITCODE
        Write-Note ('{0} in {1:N0} min, exit {2}' -f $config, $watch.Elapsed.TotalMinutes, $code)
        if ($code -ne 0) {
            Write-Problem "Packaging $config failed"
            Select-String -LiteralPath (Join-Path $packages "$config.log") -Pattern 'error|ERROR' | Select-Object -Last 10 | ForEach-Object { Write-Note $_.Line.Trim() }
            $results.Add("- $config packaging failed (exit $code)")
            $broken++
            continue
        }
    }

    $exeName = if ($config -eq 'Shipping') { "$hostName-Win64-Shipping.exe" } else { "$hostName.exe" }
    $exe = Get-ChildItem -LiteralPath $archive -Recurse -Filter $exeName -ErrorAction SilentlyContinue | Where-Object { $_.DirectoryName -match 'Binaries\\Win64$' } | Select-Object -First 1
    if (-not $exe) { Write-Problem "No $exeName under $archive"; $results.Add("- $config has no $exeName"); $broken++; continue }

    # The baked files must be in the pak: the engine's staging takes Content/Localization/* for CulturesToStage.
    $pak = Get-ChildItem -LiteralPath $archive -Recurse -Filter '*.pak' | Select-Object -First 1
    if ($pak) {
        $listing = & $unrealPak $pak.FullName -List 2>&1 | Out-String
        foreach ($file in @('Localization/DreamLoc/DreamLoc.locmeta', 'Localization/DreamLoc/en/DreamLoc.locres', 'Localization/DreamLoc/zh-Hans/DreamLoc.locres')) {
            $found = $listing -match [regex]::Escape($file)
            $results.Add("- $config pak has $file`: $(if ($found) { 'yes' } else { 'NO' })")
            Write-Note $results[-1]
            if (-not $found) { $failed++ }
        }
    } else {
        $results.Add("- $config has no .pak"); $failed++
    }

    Write-Step "Running the $config probe"
    $probe = Join-Path $packages "$config-probe.json"
    Remove-Item -LiteralPath $probe -ErrorAction SilentlyContinue
    $arguments = "-DreamLocProbe=`"$probe`" -DreamLocMod=`"$mod`" -culture=zh-Hans -nullrhi -NoSound -nosplash -unattended -abslog=`"$packages\$config-game.log`""
    $process = Start-Process -FilePath $exe.FullName -ArgumentList $arguments -PassThru -WindowStyle Hidden
    if (-not $process.WaitForExit(5 * 60 * 1000)) {
        try { $process.Kill($true) } catch { }
        Write-Problem "The $config game did not quit within 5 minutes"
        $results.Add("- $config game timed out"); $broken++; continue
    }
    if (-not (Test-Path -LiteralPath $probe)) {
        Write-Problem "The $config game wrote no probe file (exit $($process.ExitCode))"
        $results.Add("- $config game wrote nothing (exit $($process.ExitCode))"); $broken++; continue
    }
    $data = Get-Content -LiteralPath $probe -Raw -Encoding UTF8 | ConvertFrom-Json
    foreach ($note in $data.notes) { Write-Note "note: $note" }
    foreach ($step in $data.steps) {
        $want = $expected[$step.step]
        $ok = $want -and $step.code -eq $want.code -and $step.umg -eq $want.umg
        if ($step.step -eq 'mod mounted' -or $step.step -eq 'pl') {
            # The language is there, and the mod is a layer over the project (2 and up), not read as a plain plugin.
            $modLayer = @($step.layers | Where-Object { $_ -match '^(\d+) Mod\.DreamLocTestMod$' -and [int]$Matches[1] -ge 2 })
            $ok = $ok -and ($step.cultures -contains 'pl') -and $modLayer.Count -eq 1 -and -not ($step.layers -match 'Plugin\.DreamLocTestMod')
        }
        $line = '- {0} {1}: culture {2}, code "{3}", umg "{4}", cultures [{5}], layers [{6}]: {7}' -f $config, $step.step, $step.culture, $step.code, $step.umg,
            ($step.cultures -join ', '), ($step.layers -join '; '), $(if ($ok) { 'ok' } else { 'WRONG' })
        $results.Add($line)
        Write-Note $line
        if (-not $ok) { $failed++ }
    }
    if (@($data.steps).Count -ne $expected.Count) { $results.Add("- $config probe has $(@($data.steps).Count) steps, expected $($expected.Count)"); $failed++ }
}

$word = if ($broken -gt 0) { 'not judged' } elseif ($failed -gt 0) { 'red' } else { 'green' }
$summary = @("# DreamLoc package check -- $word", '', "Configurations: $($Configuration -join ', ')", '') + $results
Set-Content -LiteralPath (Join-Path $packages 'summary.md') -Value ($summary -join "`n") -Encoding utf8
Write-Step "Package check: $word ($packages\summary.md)"
exit $(if ($broken -gt 0) { 2 } elseif ($failed -gt 0) { 1 } else { 0 })
