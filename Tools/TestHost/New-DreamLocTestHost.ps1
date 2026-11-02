# Copyright DreamMoon. All Rights Reserved.
#
# Creates (or refreshes) the DreamLoc test host: a minimal code project that builds DreamLoc on its own and runs its
# automation tests away from any working project.
#
#   <HostDir>/                    copy of Tools/TestHost/Template
#   <HostDir>/Plugins/DreamLoc    this repository: left alone when it is already there (the host is where the
#                                 repository usually lives), else a git worktree of it on -Branch
#   <HostDir>/Plugins/DreamGUI    a git worktree of the DreamGUI repository: on -DreamGUIBranch when given (made
#                                 from -DreamGUIBase when it does not exist yet), else detached at -DreamGUIBase.
#                                 DreamGUI is there for the end-to-end check that a .dui's text follows the .dloc
#                                 files; DreamLoc itself does not depend on it.
#
# Running the script again re-copies the template and leaves both plugins alone.

[CmdletBinding()]
param(
    [string]$HostDir = 'I:\UnrealProject_Moon\DEV_58\DreamLocTestHost',
    [string]$Branch = 'main',
    [string]$EngineAssociation = '5.8',
    [string]$DreamGUIRepo = 'I:\UnrealProject_Moon\DEV_58\DevTest\Plugins\DreamGUI',
    [string]$DreamGUIBranch,
    [string]$DreamGUIBase = 'origin/main'
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$template = Join-Path $PSScriptRoot 'Template'

function Invoke-Git([string[]]$GitArgs) {
    & git @GitArgs
    if ($LASTEXITCODE -ne 0) { throw "git $($GitArgs -join ' ') failed (exit $LASTEXITCODE)" }
}

Write-Host "==> Template -> $HostDir"
New-Item -ItemType Directory -Force -Path $HostDir | Out-Null
Copy-Item -Path (Join-Path $template '*') -Destination $HostDir -Recurse -Force
$projectPath = Join-Path $HostDir 'DreamLocTestHost.uproject'
$project = Get-Content -LiteralPath $projectPath -Raw -Encoding UTF8 | ConvertFrom-Json
$project.EngineAssociation = $EngineAssociation
$project | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $projectPath -Encoding UTF8

$pluginDir = Join-Path $HostDir 'Plugins\DreamLoc'
if (Test-Path -LiteralPath $pluginDir) {
    Write-Host "    DreamLoc already there: $pluginDir"
} else {
    Write-Host "==> Worktree $Branch -> $pluginDir"
    Invoke-Git @('-C', $repo, 'worktree', 'add', $pluginDir, $Branch)
}

$guiDir = Join-Path $HostDir 'Plugins\DreamGUI'
if (Test-Path -LiteralPath $guiDir) {
    Write-Host "    DreamGUI already there: $guiDir"
} else {
    if (-not (Test-Path -LiteralPath $DreamGUIRepo)) { throw "No DreamGUI repository at $DreamGUIRepo. Pass -DreamGUIRepo." }
    Invoke-Git @('-C', $DreamGUIRepo, 'fetch', 'origin', '--quiet')
    if ($DreamGUIBranch) {
        & git -C $DreamGUIRepo rev-parse --verify --quiet "refs/heads/$DreamGUIBranch" | Out-Null
        if ($LASTEXITCODE -eq 0) {
            Write-Host "==> DreamGUI worktree $DreamGUIBranch -> $guiDir"
            Invoke-Git @('-C', $DreamGUIRepo, 'worktree', 'add', $guiDir, $DreamGUIBranch)
        } else {
            Write-Host "==> DreamGUI worktree $DreamGUIBranch (new, from $DreamGUIBase) -> $guiDir"
            Invoke-Git @('-C', $DreamGUIRepo, 'worktree', 'add', '-b', $DreamGUIBranch, $guiDir, $DreamGUIBase)
        }
    } else {
        Write-Host "==> DreamGUI worktree at $DreamGUIBase (detached) -> $guiDir"
        Invoke-Git @('-C', $DreamGUIRepo, 'worktree', 'add', '--detach', $guiDir, $DreamGUIBase)
    }
}

$info = [ordered]@{
    schema            = 1
    repoPath          = $repo
    pluginPath        = $pluginDir
    dreamGuiRepo      = $DreamGUIRepo
    dreamGuiPath      = $guiDir
    engineAssociation = $EngineAssociation
}
$info | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $HostDir '.dreamloc-testhost.json') -Encoding utf8

Write-Host "==> Done. Build and test with: pwsh -File `"$pluginDir\Tools\dloc.ps1`" test"
