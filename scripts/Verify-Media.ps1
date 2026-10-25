param([string]$Tools = (Join-Path (Split-Path $PSScriptRoot -Parent) 'tools'), [string]$Verifier)
$ErrorActionPreference = 'Stop'
$Tools = [IO.Path]::GetFullPath($Tools)
$root = Split-Path $PSScriptRoot -Parent
$work = Join-Path $root 'build\media-validation'
New-Item -ItemType Directory -Force -Path $work | Out-Null
$source = Join-Path $work 'source.mp4'
$ffmpeg = Join-Path $Tools 'ffmpeg.exe'
$ffprobe = Join-Path $Tools 'ffprobe.exe'
$ytdlp = Join-Path $Tools 'yt-dlp.exe'
& $ffmpeg -hide_banner -loglevel error -y -f lavfi -i 'color=c=blue:s=320x240:r=25:d=2' -f lavfi -i 'sine=frequency=440:duration=2' -c:v libx264 -pix_fmt yuv420p -c:a aac -shortest $source
if ($LASTEXITCODE) { throw 'FFmpeg could not create the test media.' }
$infoPath = Join-Path $work 'fixture.info.json'
$fixture = @{
    id = 'abcdefghijk'; title = 'TubeGrab local media validation'; extractor = 'generic'; extractor_key = 'Generic';
    webpage_url = 'https://example.invalid/clearfetch-validation'; duration = 2;
    formats = @(@{format_id='local';url=([Uri]$source).AbsoluteUri;ext='mp4';vcodec='avc1.64000d';acodec='mp4a.40.2';height=240;width=320;protocol='file'})
} | ConvertTo-Json -Depth 5
[IO.File]::WriteAllText($infoPath, $fixture, (New-Object Text.UTF8Encoding($false)))
$base = @('--ignore-config','--no-plugin-dirs','--no-colors','--encoding','utf-8','--no-js-runtimes','--js-runtimes',"deno:$(Join-Path $Tools 'deno.exe')",
    '--continue','--part','--no-overwrites','--windows-filenames','--retries','5','--fragment-retries','5','--retry-sleep','http:exp=1:20',
    '--abort-on-unavailable-fragments','--newline','--progress','--progress-delta','0.3','--no-simulate',
    '--progress-template','download:CFPROGRESS:{"downloaded_bytes":%(progress.downloaded_bytes|0)j,"total_bytes":%(progress.total_bytes|0)j,"total_bytes_estimate":%(progress.total_bytes_estimate|0)j,"speed":%(progress.speed|0)j,"eta":%(progress.eta|-1)j}','--progress-template','postprocess:CFPOST:%(progress.status)s',
    '--print','after_move:CFFILE:%(filepath)j','--ffmpeg-location',$Tools,'--enable-file-urls','--load-info-json',$infoPath)
foreach ($format in @('mp4','mp3')) {
    # Each run has its own output folder, so old artifacts cannot make a failing test appear successful.
    $output = Join-Path $work ($format + '-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $output | Out-Null
    $options = if ($format -eq 'mp4') {
        @('-f','bv[ext=mp4][vcodec^=avc1][height<=480]+ba[ext=m4a][acodec^=mp4a]/b[ext=mp4][vcodec^=avc1][acodec^=mp4a][height<=480]',
          '--merge-output-format','mp4','--remux-video','mp4','--postprocessor-args','Merger+ffmpeg_o:-movflags +faststart')
    } else { @('-f','bestaudio/best','--extract-audio','--audio-format','mp3','--audio-quality','0') }
    $lines = & $ytdlp @base @options --paths $output --output 'validation [%(id)s].%(ext)s' 2>&1
    if ($LASTEXITCODE) { $lines | Write-Host; throw "Real yt-dlp $format validation failed." }
    $progressLines = @($lines | Where-Object { "$_".StartsWith('CFPROGRESS:') })
    if (!$progressLines.Count) { throw "No progress was emitted for $format." }
    foreach ($line in $progressLines) {
        $progress = ("$line".Substring(11) | ConvertFrom-Json)
        if ($null -eq $progress.downloaded_bytes -or $null -eq $progress.eta) { throw 'Missing numeric progress fields.' }
    }
    $marker = @($lines | Where-Object { "$_".StartsWith('CFFILE:') })
    if ($marker.Count -ne 1) { $lines | Write-Host; throw "Missing final-file marker for $format." }
    $path = ("$($marker[0])".Substring(7) | ConvertFrom-Json)
    if (!(Test-Path -LiteralPath $path)) { throw 'Reported file does not exist.' }
    $probe = & $ffprobe -v error -show_streams -of json $path
    if ($LASTEXITCODE) { throw 'ffprobe rejected the generated output.' }
    $streams = ($probe | ConvertFrom-Json).streams
    if ($format -eq 'mp4') {
        if (!($streams | Where-Object codec_name -eq 'h264') -or !($streams | Where-Object codec_name -eq 'aac')) { throw 'MP4 does not contain H.264 and AAC.' }
    } elseif (!($streams | Where-Object codec_name -eq 'mp3') -or ($streams | Where-Object codec_type -eq 'video')) { throw 'MP3 stream validation failed.' }
    $lines | Set-Content -LiteralPath (Join-Path $output 'engine.log') -Encoding UTF8
    if ($Verifier) {
        & $Verifier --verify-media --tools $Tools --file $path --format $format
        if ($LASTEXITCODE) { throw "The production output validator rejected real $format media." }
        $corrupt = Join-Path $output ('corrupt.' + $format)
        [IO.File]::WriteAllText($corrupt, 'This nonempty file is not media.')
        $before = (Get-FileHash -LiteralPath $corrupt -Algorithm SHA256).Hash
        & $Verifier --verify-media --tools $Tools --file $corrupt --format $format
        if ($LASTEXITCODE -ne 1 -or !(Test-Path -LiteralPath $corrupt) -or (Get-FileHash -LiteralPath $corrupt -Algorithm SHA256).Hash -ne $before) {
            throw 'The production output validator must reject and preserve corrupt media.'
        }
    }
    Write-Host "PASS: $format container and codecs accepted by ffprobe at $path"
}
Write-Host 'Offline real-engine media validation passed. This does not exercise YouTube network extraction.'
