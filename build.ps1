# SPDX-License-Identifier: GPL-3.0-or-later

<#
.SYNOPSIS
  Builds the GRIN-Markdown Notepad++ plugin.

.EXAMPLE
  .\build.ps1                      # build Win32 and x64
  .\build.ps1 -Platform Win32 -Install
  .\build.ps1 -Package             # also write dist\GRIN-Markdown-<version>-<platform>.zip
#>
[CmdletBinding()]
param(
    [ValidateSet('Win32', 'x64', 'ARM64', 'all')]
    [string]$Platform = 'all',
    [ValidateSet('Release', 'Debug')]
    [string]$Config = 'Release',
    [switch]$Package,
    [switch]$Install,
    [switch]$RebuildWeb
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

function Find-VisualStudio {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { throw 'Visual Studio (with the C++ workload) was not found.' }
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json
    if (-not $vs) { throw 'No Visual Studio installation with the C++ build tools was found.' }
    return $vs[0]
}

function Get-Generator($vs) {
    switch ([int]($vs.installationVersion.Split('.')[0])) {
        18 { return 'Visual Studio 18 2026' }
        17 { return 'Visual Studio 17 2022' }
        16 { return 'Visual Studio 16 2019' }
        default { throw "Unsupported Visual Studio version $($vs.installationVersion)" }
    }
}

function Get-PeMachine($path) {
    $bytes = [System.IO.File]::ReadAllBytes($path)
    $pe = [BitConverter]::ToInt32($bytes, 0x3c)
    switch ([BitConverter]::ToUInt16($bytes, $pe + 4)) {
        0x014c { 'Win32' }
        0x8664 { 'x64' }
        0xAA64 { 'ARM64' }
        default { 'unknown' }
    }
}

function Find-NotepadPlusPlus {
    foreach ($key in 'HKLM:\SOFTWARE\Notepad++', 'HKLM:\SOFTWARE\WOW6432Node\Notepad++') {
        $dir = (Get-ItemProperty -Path $key -ErrorAction SilentlyContinue).'(default)'
        if ($dir -and (Test-Path (Join-Path $dir 'notepad++.exe'))) { return $dir }
    }
    foreach ($dir in "$env:ProgramFiles\Notepad++", "${env:ProgramFiles(x86)}\Notepad++") {
        if (Test-Path (Join-Path $dir 'notepad++.exe')) { return $dir }
    }
    return $null
}

# --- WebView2 SDK (downloaded, not committed: the static loader libs are ~33 MB) ---
$wv2Version = '1.0.4258.31'
$wv2Sha256 = '56F7F4B8BF9AEE4B8EFEFBBDD4F67D5F74EBD1B100ED0806DA71BF76AF481AA9'
$wv2Dir = Join-Path $root 'third_party\webview2'
$wv2Stamp = Join-Path $wv2Dir "version-$wv2Version.txt"
if (-not (Test-Path $wv2Stamp)) {
    Write-Host "==> Downloading WebView2 SDK $wv2Version from NuGet" -ForegroundColor Cyan
    $pkg = Join-Path ([System.IO.Path]::GetTempPath()) "microsoft.web.webview2.$wv2Version.nupkg"
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -UseBasicParsing -OutFile $pkg `
        "https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/$wv2Version/microsoft.web.webview2.$wv2Version.nupkg"
    if ((Get-FileHash $pkg -Algorithm SHA256).Hash -ne $wv2Sha256) {
        Remove-Item $pkg
        throw 'WebView2 SDK download failed its SHA-256 check.'
    }
    if (Test-Path $wv2Dir) { Remove-Item -Recurse -Force $wv2Dir }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($pkg)
    try {
        foreach ($entry in $zip.Entries) {
            $name = $entry.FullName
            if (-not $entry.Name) { continue }   # folder entry
            if ($name -like 'build/native/include/*' -or $name -like 'build/native/*/WebView2LoaderStatic.lib' -or $name -eq 'LICENSE.txt') {
                $dest = Join-Path $wv2Dir ($name -replace '/', '\')
                New-Item -ItemType Directory -Force -Path (Split-Path $dest) | Out-Null
                [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $dest, $true)
            }
        }
    } finally { $zip.Dispose() }
    Remove-Item $pkg
    Set-Content -Path $wv2Stamp -Value $wv2Version
}

# --- Web assets --------------------------------------------------------------
$web = Join-Path $root 'web'
if ($RebuildWeb -or -not (Test-Path (Join-Path $web 'dist\index.html'))) {
    Push-Location $web
    try {
        if (-not (Get-Command npm -ErrorAction SilentlyContinue)) { throw 'Node.js (npm) is needed to build the web assets.' }
        if (Test-Path 'package-lock.json') { npm ci } else { npm install }
        if ($LASTEXITCODE) { throw 'npm install failed' }
        npm run build
        if ($LASTEXITCODE) { throw 'web build failed' }
    } finally { Pop-Location }
}

# --- Native build ------------------------------------------------------------
$vs = Find-VisualStudio
$cmake = Join-Path $vs.installationPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path $cmake)) { $cmake = 'cmake' }
$generator = Get-Generator $vs

$platforms = if ($Platform -eq 'all') { @('Win32', 'x64') } else { @($Platform) }
$version = (Select-String -Path (Join-Path $root 'src\resource.h') -Pattern 'GRIN_VERSION_STR\s+"([^"]+)"').Matches[0].Groups[1].Value

foreach ($p in $platforms) {
    $buildDir = Join-Path $root "build\$p"
    Write-Host "==> Building $p ($Config) with $generator" -ForegroundColor Cyan
    & $cmake -S $root -B $buildDir -G $generator -A $p
    if ($LASTEXITCODE) { throw "CMake configure failed for $p" }
    & $cmake --build $buildDir --config $Config
    if ($LASTEXITCODE) { throw "Build failed for $p" }

    $out = Join-Path $buildDir "$Config\GRIN-Markdown"
    Write-Host "    Output: $out"

    if ($Package) {
        $dist = Join-Path $root 'dist'
        New-Item -ItemType Directory -Force -Path $dist | Out-Null
        $zip = Join-Path $dist "GRIN-Markdown-$version-$p.zip"
        if (Test-Path $zip) { Remove-Item $zip }
        # Not Compress-Archive: Windows PowerShell 5.1 writes '\' separators, which other unzip tools
        # (and Plugins Admin) turn into flat files named 'web\index.html'.
        Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
        $archive = [System.IO.Compression.ZipFile]::Open($zip, 'Create')
        try {
            foreach ($file in Get-ChildItem -Path $out -Recurse -File) {
                $entry = $file.FullName.Substring($out.Length + 1).Replace('\', '/')
                [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $file.FullName, $entry, 'Optimal') | Out-Null
            }
        } finally { $archive.Dispose() }
        $hash = (Get-FileHash $zip -Algorithm SHA256).Hash
        Write-Host ("    Package: {0} ({1:N0} KB, SHA-256 {2})" -f $zip, ((Get-Item $zip).Length / 1KB), $hash)
    }
}

# --- Install -----------------------------------------------------------------
if ($Install) {
    $npp = Find-NotepadPlusPlus
    if (-not $npp) { throw 'Notepad++ installation not found.' }
    $arch = Get-PeMachine (Join-Path $npp 'notepad++.exe')
    if ($platforms -notcontains $arch) { throw "Notepad++ at $npp is $arch; build that platform first (-Platform $arch)." }
    if (Get-Process notepad++ -ErrorAction SilentlyContinue) { throw 'Close Notepad++ before installing.' }

    $source = Join-Path $root "build\$arch\$Config\GRIN-Markdown"
    $target = Join-Path $npp 'plugins\GRIN-Markdown'
    Write-Host "==> Installing $arch build to $target" -ForegroundColor Cyan
    $copy = "if (Test-Path '$target') { Remove-Item -Recurse -Force '$target' }; Copy-Item -Recurse -Force '$source' '$target'"
    try {
        Invoke-Expression $copy
    } catch [System.UnauthorizedAccessException] {
        Write-Host '    Needs administrator rights; asking for elevation...'
        $proc = Start-Process powershell -Verb RunAs -Wait -PassThru -ArgumentList '-NoProfile', '-Command', $copy
        if ($proc.ExitCode) { throw 'Elevated copy failed.' }
    }
    Write-Host '    Done. Start Notepad++ and use Plugins > GRIN-Markdown > Markdown Preview.'
}
