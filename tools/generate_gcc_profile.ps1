param(
    [string]$Gxx = "g++",
    [string]$Output = "src/GccProfileData.inc"
)

$ErrorActionPreference = "Stop"

$lines = '' | & $Gxx -std=c++20 -dM -E -x c++ - 2>$null
if ($LASTEXITCODE -ne 0) {
    throw "g++ -dM failed with exit code $LASTEXITCODE"
}

$filtered = $lines | Where-Object { $_ -notmatch '^#define __cplusplus ' }
if (-not $filtered) {
    throw "g++ produced no predefined macros"
}

$content = "static const char kGccProfileText[] = R`"GCCPROFILE(`n" +
    ($filtered -join "`n") + "`n)GCCPROFILE`";`n"

$directory = Split-Path -Parent $Output
if ($directory) {
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
}
Set-Content -Path $Output -Value $content -Encoding ascii
Write-Output "wrote $Output ($($filtered.Count) macros)"
