param([string]$Configuration = 'Release', [string]$Output = 'dist\TubeGrab-2.3.0-win64.zip', [string]$BinaryDirectory)
$ErrorActionPreference = 'Stop'
$sourceRoot = [IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$exe = if ($BinaryDirectory) { Join-Path ([IO.Path]::GetFullPath($BinaryDirectory)) 'TubeGrab.exe' } else { Join-Path $sourceRoot "bin\x64\$Configuration\TubeGrab.exe" }
if (!(Test-Path -LiteralPath $exe)) { throw 'Build TubeGrab in Release/x64 first.' }
$archivePath = if ([IO.Path]::IsPathRooted($Output)) { [IO.Path]::GetFullPath($Output) } else { Join-Path $sourceRoot $Output }
New-Item -ItemType Directory -Force -Path (Split-Path $archivePath -Parent) | Out-Null
$temporaryArchive = $archivePath + '.' + [guid]::NewGuid().ToString('N') + '.tmp'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::Open($temporaryArchive, [IO.Compression.ZipArchiveMode]::Create)
try {
    $files = [ordered]@{
        'TubeGrab.exe' = $exe
        'README.md' = (Join-Path $sourceRoot 'README.md')
        'VALIDATION.md' = (Join-Path $sourceRoot 'VALIDATION.md')
        'THIRD_PARTY_NOTICES.md' = (Join-Path $sourceRoot 'THIRD_PARTY_NOTICES.md')
        'scripts/Setup-Tools.ps1' = (Join-Path $sourceRoot 'scripts\Setup-Tools.ps1')
        'tools/README.md' = (Join-Path $sourceRoot 'tools\README.md')
        'licenses/json.LICENSE.MIT' = (Join-Path $sourceRoot 'third_party\json.LICENSE.MIT')
    }
    $manifest = @()
    foreach ($entry in $files.GetEnumerator()) {
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $entry.Value, $entry.Key, [IO.Compression.CompressionLevel]::Optimal) | Out-Null
        $manifest += (Get-FileHash -LiteralPath $entry.Value -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + $entry.Key
    }
    $hashEntry = $archive.CreateEntry('SHA256SUMS.txt')
    $writer = New-Object IO.StreamWriter($hashEntry.Open(), (New-Object Text.UTF8Encoding($false)))
    try { $writer.Write(($manifest -join "`r`n") + "`r`n") } finally { $writer.Dispose() }
} finally { $archive.Dispose() }
Move-Item -LiteralPath $temporaryArchive -Destination $archivePath -Force
$check = [IO.Compression.ZipFile]::OpenRead($archivePath)
try {
    foreach ($entry in @('TubeGrab.exe', 'scripts/Setup-Tools.ps1', 'SHA256SUMS.txt')) {
        if (!$check.GetEntry($entry)) { throw "Release archive is missing $entry." }
    }
    foreach ($entry in $check.Entries) {
        if ($entry.FullName -eq 'SHA256SUMS.txt') { continue }
        $stream = $entry.Open(); $algorithm = [Security.Cryptography.SHA256]::Create()
        try { $hash = [BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-','').ToLowerInvariant() }
        finally { $stream.Dispose(); $algorithm.Dispose() }
        if ($manifest -notcontains ($hash + '  ' + $entry.FullName)) { throw "Archive hash mismatch: $($entry.FullName)" }
    }
    Write-Host "Release archive verified: $archivePath ($($check.Entries.Count) files)."
} finally { $check.Dispose() }
(Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + [IO.Path]::GetFileName($archivePath) |
    Set-Content -LiteralPath ($archivePath + '.sha256') -Encoding ASCII
