param(
    [Parameter(Mandatory = $true)][string]$Gxx,
    [Parameter(Mandatory = $true)][string]$Prefix,
    [Parameter(Mandatory = $true)][string]$Output
)

$ErrorActionPreference = "Stop"
$standards = @(
    @{ Name = "Cxx11"; Flag = "c++11" },
    @{ Name = "Cxx14"; Flag = "c++14" },
    @{ Name = "Cxx17"; Flag = "c++17" },
    @{ Name = "Cxx20"; Flag = "c++20" }
)
$content = "// Generated from $Gxx. Do not edit.`n"
foreach ($standard in $standards) {
    $lines = '' | & $Gxx "-std=$($standard.Flag)" -dM -E -x c++ - 2>$null
    if ($LASTEXITCODE -ne 0) {
        throw "g++ -std=$($standard.Flag) -dM failed with exit code $LASTEXITCODE"
    }
    $filtered = $lines | Where-Object { $_ -notmatch '^#define __cplusplus ' }
    if (-not $filtered) { throw "no predefined macros for $($standard.Flag)" }
    $content += "static const char kGccProfile$($Prefix)$($standard.Name)Text[] = R`"GCCPROFILE(`n" +
        ($filtered -join "`n") + "`n)GCCPROFILE`";`n"
    Write-Output "$($standard.Flag): $($filtered.Count) macros"
}
$directory = Split-Path -Parent $Output
if ($directory) { New-Item -ItemType Directory -Force -Path $directory | Out-Null }
Set-Content -Path $Output -Value $content -Encoding ascii
Write-Output "wrote $Output"
