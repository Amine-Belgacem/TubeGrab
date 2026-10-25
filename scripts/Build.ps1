param([ValidateSet('Debug','Release')][string]$Configuration = 'Release', [switch]$Test, [string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (!(Test-Path $vswhere)) { throw 'Install Visual Studio 2022 with Desktop development with C++.' }
$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (!$msbuild) { throw 'MSBuild was not found. Install the Visual Studio C++ desktop workload and Windows SDK.' }
$outputOption = @()
if ($OutputDirectory) {
    $outputPath = if ([IO.Path]::IsPathRooted($OutputDirectory)) { [IO.Path]::GetFullPath($OutputDirectory) } else { [IO.Path]::GetFullPath((Join-Path $root $OutputDirectory)) }
    $outputOption = @('/p:OutDir=' + $outputPath.TrimEnd('\') + '\')
}
& $msbuild (Join-Path $root 'TubeGrab.sln') /m /nologo /v:minimal "/p:Configuration=$Configuration" /p:Platform=x64 @outputOption
if ($LASTEXITCODE) { throw "Build failed ($LASTEXITCODE)." }
if ($Test) {
    $testExe = if ($OutputDirectory) { Join-Path $outputPath 'TubeGrab.Tests.exe' } else { Join-Path $root "bin\x64\$Configuration\TubeGrab.Tests.exe" }
    & $testExe
    if ($LASTEXITCODE) { throw "Tests failed ($LASTEXITCODE)." }
}
