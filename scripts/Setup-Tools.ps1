param([string]$Destination = (Join-Path (Split-Path $PSScriptRoot -Parent) 'tools'))
$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$Destination = [IO.Path]::GetFullPath($Destination)
New-Item -ItemType Directory -Force -Path $Destination | Out-Null
$stage = Join-Path $Destination ('.staging-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage | Out-Null
$headers = @{ 'User-Agent' = 'TubeGrab-Setup/1.0'; 'Accept' = 'application/vnd.github+json' }

function Get-Release([string]$Repo) {
    Invoke-RestMethod -Uri "https://api.github.com/repos/$Repo/releases/latest" -Headers $headers
}
function Get-Asset($Release, [string]$Name) {
    $asset = @($Release.assets | Where-Object { $_.name -eq $Name })
    if ($asset.Count -ne 1) { throw "Release $($Release.tag_name) does not contain $Name. See README.md for manual installation." }
    return $asset[0]
}
function Download-Verified($Asset, [string]$Path) {
    if ($Asset.browser_download_url -notmatch '^https://github\.com/(yt-dlp/yt-dlp|yt-dlp/FFmpeg-Builds|denoland/deno)/releases/download/') { throw 'Unexpected dependency download host or repository.' }
    Write-Host "Downloading $($Asset.name)…"
    Invoke-WebRequest -UseBasicParsing -Uri $Asset.browser_download_url -OutFile $Path -Headers $headers
    if ($Asset.digest -notmatch '^sha256:([a-fA-F0-9]{64})$') { throw "GitHub did not supply a SHA-256 digest for $($Asset.name). Nothing has been installed. Use the manual installation instructions." }
    $expected = $Matches[1]
    $actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    if ($actual -ne $expected) { throw "SHA-256 mismatch for $($Asset.name). Nothing has been installed." }
}
try {
    $yt = Get-Release 'yt-dlp/yt-dlp'
    $ff = Get-Release 'yt-dlp/FFmpeg-Builds'
    $deno = Get-Release 'denoland/deno'
    Download-Verified (Get-Asset $yt 'yt-dlp.exe') (Join-Path $stage 'yt-dlp.exe')
    $ffAsset = @($ff.assets | Where-Object { $_.name -match '^ffmpeg-.*-win64-gpl\.zip$' } | Sort-Object name | Select-Object -First 1)
    if ($ffAsset.Count -ne 1) { throw 'No static win64 GPL FFmpeg ZIP was found. See manual installation instructions.' }
    Download-Verified $ffAsset[0] (Join-Path $stage 'ffmpeg.zip')
    Download-Verified (Get-Asset $deno 'deno-x86_64-pc-windows-msvc.zip') (Join-Path $stage 'deno.zip')
    Expand-Archive -LiteralPath (Join-Path $stage 'ffmpeg.zip') -DestinationPath (Join-Path $stage 'ffmpeg')
    Expand-Archive -LiteralPath (Join-Path $stage 'deno.zip') -DestinationPath (Join-Path $stage 'deno')
    foreach ($name in @('ffmpeg.exe','ffprobe.exe','deno.exe')) {
        $source = @(Get-ChildItem -LiteralPath $stage -Recurse -File -Filter $name)
        if ($source.Count -ne 1) { throw "Expected exactly one $name in the downloaded archives." }
        Copy-Item -LiteralPath $source[0].FullName -Destination (Join-Path $stage $name)
    }
    # Validate binaries while staged; never execute a download before its digest is verified.
    foreach ($name in @('yt-dlp','ffmpeg','ffprobe','deno')) {
        $versionFlag = if ($name -in @('ffmpeg','ffprobe')) { '-version' } else { '--version' }
        $output = & (Join-Path $stage "$name.exe") $versionFlag 2>&1
        if ($LASTEXITCODE -ne 0) { throw "$name did not run successfully on this machine." }
        Write-Host ($output | Select-Object -First 1)
    }
    # Preflight exclusive write access before replacing any tool; close TubeGrab first.
    foreach ($name in @('yt-dlp.exe','ffmpeg.exe','ffprobe.exe','deno.exe')) {
        $target = Join-Path $Destination $name
        if (Test-Path -LiteralPath $target) {
            $probe = [IO.File]::Open($target, 'Open', 'ReadWrite', 'None'); $probe.Dispose()
        }
    }
    foreach ($name in @('yt-dlp.exe','ffmpeg.exe','ffprobe.exe','deno.exe')) {
        Copy-Item -LiteralPath (Join-Path $stage $name) -Destination (Join-Path $Destination $name) -Force
    }
    @{ installedUtc = [DateTime]::UtcNow.ToString('o'); ytDlp = $yt.tag_name; ffmpeg = $ff.tag_name; deno = $deno.tag_name } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Destination 'tool-versions.json') -Encoding UTF8
    # Keep the FFmpeg distribution's license files next to the installed tools.
    $licenses = Join-Path $Destination 'licenses'
    New-Item -ItemType Directory -Force -Path $licenses | Out-Null
    Get-ChildItem -LiteralPath (Join-Path $stage 'ffmpeg') -Recurse -File | Where-Object { $_.Name -match '^(LICENSE|COPYING)' } |
        ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $licenses $_.Name) -Force }
    Write-Host "Tools installed at $Destination. Build the app, or select this directory using Tools folder in TubeGrab."
} finally {
    # Only delete the unique staging directory created by this invocation, inside Destination.
    $resolvedStage = [IO.Path]::GetFullPath($stage)
    $expectedParent = [IO.Path]::GetFullPath($Destination).TrimEnd('\')
    if ((Split-Path $resolvedStage -Parent) -eq $expectedParent -and (Split-Path $resolvedStage -Leaf) -match '^\.staging-[a-f0-9]{32}$') {
        Remove-Item -LiteralPath $resolvedStage -Recurse -Force -ErrorAction SilentlyContinue
    }
}
