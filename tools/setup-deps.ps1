<#
.SYNOPSIS
  Downloads and prepares all build dependencies into .deps/:
    - obs-studio 31.1.1 source + obs-deps 2025-07-11 prebuilt bundle, then builds and
      installs libobs (Development component) into .deps/obs-sdk
    - ONNX Runtime 1.24.4 (DirectML build, NuGet)
    - DirectML 1.15.4 (NuGet, x64 binaries + headers)
  Re-runnable; skips steps whose outputs already exist.
#>
param(
    [string]$ObsVersion = "31.1.1",
    [string]$ObsDepsVersion = "2025-07-11",
    [string]$OrtVersion = "1.24.4",
    [string]$DmlVersion = "1.15.4",
    [string]$Generator = "Visual Studio 17 2022",
    [switch]$SkipObsBuild
)
$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$deps = Join-Path $root ".deps"
$dl = Join-Path $deps "downloads"
New-Item -ItemType Directory -Force $dl | Out-Null

function Fetch($url, $file) {
    if (Test-Path $file) { Write-Host "  exists: $(Split-Path $file -Leaf)"; return }
    Write-Host "  downloading $url"
    Invoke-WebRequest -Uri $url -OutFile $file -UseBasicParsing
}
function Unzip($file, $dest) {
    if (Test-Path $dest) { return }
    New-Item -ItemType Directory -Force $dest | Out-Null
    Expand-Archive -Path $file -DestinationPath $dest -Force
}

Write-Host "== obs-studio $ObsVersion + obs-deps $ObsDepsVersion"
$obsZip = Join-Path $dl "obs-studio-$ObsVersion.zip"
Fetch "https://github.com/obsproject/obs-studio/archive/refs/tags/$ObsVersion.zip" $obsZip
if (-not (Test-Path (Join-Path $deps "obs-studio-$ObsVersion"))) { Expand-Archive $obsZip -DestinationPath $deps -Force }
$depsZip = Join-Path $dl "windows-deps-$ObsDepsVersion-x64.zip"
Fetch "https://github.com/obsproject/obs-deps/releases/download/$ObsDepsVersion/windows-deps-$ObsDepsVersion-x64.zip" $depsZip
Unzip $depsZip (Join-Path $deps "obs-deps")

Write-Host "== ONNX Runtime $OrtVersion (DirectML)"
$ortPkg = Join-Path $dl "Microsoft.ML.OnnxRuntime.DirectML.$OrtVersion.nupkg"
Fetch "https://www.nuget.org/api/v2/package/Microsoft.ML.OnnxRuntime.DirectML/$OrtVersion" $ortPkg
if (-not (Test-Path (Join-Path $deps "onnxruntime\runtimes\win-x64\native\onnxruntime.dll"))) {
    Copy-Item $ortPkg "$ortPkg.zip" -Force
    Expand-Archive "$ortPkg.zip" -DestinationPath (Join-Path $deps "onnxruntime") -Force
    Remove-Item "$ortPkg.zip"
}

Write-Host "== DirectML $DmlVersion"
$dmlPkg = Join-Path $dl "Microsoft.AI.DirectML.$DmlVersion.nupkg"
Fetch "https://www.nuget.org/api/v2/package/Microsoft.AI.DirectML/$DmlVersion" $dmlPkg
if (-not (Test-Path (Join-Path $deps "directml\bin\x64-win\DirectML.dll"))) {
    Copy-Item $dmlPkg "$dmlPkg.zip" -Force
    $tmp = Join-Path $deps "directml-tmp"
    Expand-Archive "$dmlPkg.zip" -DestinationPath $tmp -Force
    $dst = Join-Path $deps "directml"
    New-Item -ItemType Directory -Force "$dst\bin\x64-win", "$dst\include" | Out-Null
    Copy-Item "$tmp\bin\x64-win\*" "$dst\bin\x64-win\" -Force
    Copy-Item "$tmp\include\*" "$dst\include\" -Force
    Copy-Item "$tmp\LICENSE*.txt" "$dst\" -Force
    Remove-Item $tmp -Recurse -Force
    Remove-Item "$dmlPkg.zip"
}

if (-not $SkipObsBuild) {
    Write-Host "== building libobs (this takes a few minutes)"
    $src = Join-Path $deps "obs-studio-$ObsVersion"
    $sdk = Join-Path $deps "obs-sdk"
    if (-not (Test-Path (Join-Path $sdk "lib\obs.lib"))) {
        $bld = Join-Path $src "build_x64"
        cmake -S $src -B $bld -G $Generator -A x64 `
            -DOBS_CMAKE_VERSION:STRING=3.0.0 -DENABLE_PLUGINS:BOOL=OFF -DENABLE_FRONTEND:BOOL=OFF -DENABLE_UI:BOOL=OFF `
            -DOBS_VERSION_OVERRIDE:STRING=$ObsVersion -DCMAKE_PREFIX_PATH="$deps/obs-deps"
        if ($LASTEXITCODE -ne 0) { throw "libobs configure failed" }
        cmake --build $bld --config Release --target obs-frontend-api --parallel
        if ($LASTEXITCODE -ne 0) { throw "libobs build failed" }
        cmake --build $bld --config Release --target libobs-d3d11 --parallel
        cmake --install $bld --config Release --component Development --prefix $sdk
        Copy-Item (Join-Path $bld "libobs-d3d11\Release\libobs-d3d11.dll") (Join-Path $sdk "bin\64bit\") -Force
    } else { Write-Host "  exists: obs-sdk" }
}
Write-Host "done. Configure with: cmake -S . -B build -G `"$Generator`" -A x64"
