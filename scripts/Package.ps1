param([string]$Output = 'dist\TubeGrab-source.zip')
$ErrorActionPreference = 'Stop'
$sourceRoot = [IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$archivePath = if ([IO.Path]::IsPathRooted($Output)) { [IO.Path]::GetFullPath($Output) } else { Join-Path $sourceRoot $Output }
New-Item -ItemType Directory -Force -Path (Split-Path $archivePath -Parent) | Out-Null
$temporaryArchive = $archivePath + '.' + [guid]::NewGuid().ToString('N') + '.tmp'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::Open($temporaryArchive, [IO.Compression.ZipArchiveMode]::Create)
try {
    $files = @('TubeGrab.sln','TubeGrab.vcxproj','Common.Build.props','README.md','THIRD_PARTY_NOTICES.md','VALIDATION.md','.gitignore','tools\README.md') |
        ForEach-Object { Get-Item -LiteralPath (Join-Path $sourceRoot $_) }
    foreach ($folder in @('src','tests','scripts','resources','third_party','design')) {
        $directory = Join-Path $sourceRoot $folder
        if (Test-Path -LiteralPath $directory) { $files += Get-ChildItem -LiteralPath $directory -File -Recurse }
    }
    foreach ($file in $files) {
        $entry = $file.FullName.Substring($sourceRoot.Length + 1).Replace('\','/')
        if ($entry.StartsWith('tests/results/')) { continue }
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $file.FullName, $entry, [IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
} finally { $archive.Dispose() }
Move-Item -LiteralPath $temporaryArchive -Destination $archivePath -Force
$check = [IO.Compression.ZipFile]::OpenRead($archivePath)
try {
    foreach ($entry in @('TubeGrab.sln','src/AppUI.cpp','tests/NativeUiTests.cpp','third_party/json.hpp')) {
        if (!$check.GetEntry($entry)) { throw "Source archive is missing $entry." }
    }
    Write-Host "Source archive verified: $archivePath ($($check.Entries.Count) files)."
} finally { $check.Dispose() }
