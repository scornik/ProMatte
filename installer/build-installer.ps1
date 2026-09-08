<#
.SYNOPSIS
  Builds ProMatte-Setup-<version>.exe from the staged plugin tree.
.PARAMETER StageDir  Staged plugin tree (default: ..\build\stage)
.PARAMETER Version   Version string embedded in the installer (default: from CMakeLists.txt)
#>
param(
    [string]$StageDir = (Join-Path $PSScriptRoot "..\build\stage"),
    [string]$Version = ""
)
$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
if (-not $Version) {
    $cm = Get-Content (Join-Path $root "CMakeLists.txt") -Raw
    if ($cm -match 'project\(promatte VERSION ([0-9.]+)') { $Version = $Matches[1] } else { $Version = "1.0.0" }
}
$StageDir = (Resolve-Path $StageDir).Path
foreach ($required in @("obs-plugins\64bit\promatte.dll", "obs-plugins\64bit\onnxruntime.dll",
                        "data\obs-plugins\promatte\models\manifest.json", "data\obs-plugins\promatte\effects\promatte_composite.effect")) {
    if (-not (Test-Path (Join-Path $StageDir $required))) { throw "staged file missing: $required (build the 'promatte' target first)" }
}
$candidates = @(
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
    "$env:ProgramFiles(x86)\Inno Setup 6\ISCC.exe",
    "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
)
$iscc = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { throw "Inno Setup 6 not found (winget install JRSoftware.InnoSetup)" }
$out = Join-Path $PSScriptRoot "output"
New-Item -ItemType Directory -Force $out | Out-Null
Write-Host "Building installer version $Version from $StageDir"
& $iscc "/DVersion=$Version" "/DStageDir=$StageDir" "/O$out" (Join-Path $PSScriptRoot "ProMatte.iss")
if ($LASTEXITCODE -ne 0) { throw "ISCC failed with exit code $LASTEXITCODE" }
Get-ChildItem $out -Filter "ProMatte-Setup-*.exe" | ForEach-Object {
    $hash = (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower()
    "$hash  $($_.Name)" | Set-Content "$($_.FullName).sha256"
    Write-Host ("{0}  {1:N1} MB  sha256 {2}" -f $_.Name, ($_.Length / 1MB), $hash)
}
