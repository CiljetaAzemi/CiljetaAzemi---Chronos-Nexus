param(
    [Parameter(Mandatory)][string]$HighsRoot,
    [Parameter(Mandatory)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$package = (Resolve-Path -LiteralPath $HighsRoot).Path
$out = (New-Item -ItemType Directory -Path $OutputDirectory -Force).FullName
$exe = Join-Path $out 'highs-cargo-probe.exe'
$arguments = @('/nologo','/std:c++20','/EHsc','/O2',"/I$package/include/highs", "/Fo$out/cargo.obj", "/Fe$exe",
    (Join-Path $PSScriptRoot 'Highs-Cargo-Probe.cpp'), (Join-Path $package 'lib/highs.lib'))
& cl.exe @arguments
if ($LASTEXITCODE) { throw 'HiGHS probe compilation failed.' }
$oldPath = $env:PATH
try {
    $env:PATH = (Join-Path $package 'bin') + [IO.Path]::PathSeparator + $oldPath
    $result = & $exe
    if ($LASTEXITCODE) { throw 'HiGHS probe failed its exact-reference assertion.' }
    $result | Set-Content -LiteralPath (Join-Path $out 'highs-cargo-probe.csv')
    $result
} finally {
    $env:PATH = $oldPath
}
