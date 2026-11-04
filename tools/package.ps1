# Packs the staged plugin into dist\blackcamera-obs-<version>-windows-x64.zip.
# The archive holds one folder, blackcamera-obs\, to be extracted into
# %ProgramData%\obs-studio\plugins. The version comes from CMakeLists.txt.
#
#   powershell -ExecutionPolicy Bypass -File tools\package.ps1
#   powershell -ExecutionPolicy Bypass -File tools\package.ps1 -Tag v0.1.0   # CI: the tag must match the version

param(
    [string]$Config = 'Release',
    [string]$BuildDir,
    [string]$OutDir,
    [string]$Tag
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not $BuildDir) { $BuildDir = Join-Path $root 'build' }
if (-not $OutDir) { $OutDir = Join-Path $root 'dist' }

$cmakeLists = Get-Content (Join-Path $root 'CMakeLists.txt') -Raw
if ($cmakeLists -notmatch 'project\(\s*\S+\s+VERSION\s+([0-9.]+)') { throw 'No project VERSION in CMakeLists.txt' }
$version = $Matches[1]
if ($Tag -and $Tag -ne "v$version") { throw "Tag $Tag does not match the project version $version (expected v$version)" }

$stage = Join-Path $BuildDir "stage\$Config\blackcamera-obs"
if (-not (Test-Path (Join-Path $stage 'bin\64bit\blackcamera-obs.dll'))) { throw "The plugin is not built: $stage" }

New-Item -ItemType Directory -Force $OutDir | Out-Null
$tmp = Join-Path $OutDir 'pkg'
if (Test-Path $tmp) { Remove-Item $tmp -Recurse -Force }
New-Item -ItemType Directory -Force $tmp | Out-Null
$pkg = Join-Path $tmp 'blackcamera-obs'
Copy-Item $stage $pkg -Recurse
Copy-Item (Join-Path $root 'LICENSE'), (Join-Path $root 'THIRD_PARTY_NOTICES.md') $pkg

$zip = Join-Path $OutDir "blackcamera-obs-$version-windows-x64.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
# Entries are added one by one with '/' separators: Compress-Archive of Windows PowerShell 5.1
# writes '\', which some unzip tools turn into file names instead of folders
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::Open($zip, [IO.Compression.ZipArchiveMode]::Create)
try {
    Get-ChildItem $pkg -Recurse -File | ForEach-Object {
        $entry = 'blackcamera-obs/' + $_.FullName.Substring($pkg.Length + 1).Replace('\', '/')
        [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $_.FullName, $entry,
                                                                     [IO.Compression.CompressionLevel]::Optimal)
    }
} finally { $archive.Dispose() }
Remove-Item $tmp -Recurse -Force

Write-Host "Packed $zip ($([math]::Round((Get-Item $zip).Length / 1MB, 1)) MB)"
