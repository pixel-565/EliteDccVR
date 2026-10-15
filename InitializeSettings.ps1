$ErrorActionPreference = 'Stop'
$defaults = Join-Path $PSScriptRoot 'defaults'
Get-ChildItem -LiteralPath $defaults -File | ForEach-Object {
    $target = Join-Path $PSScriptRoot $_.Name
    if (-not (Test-Path -LiteralPath $target)) {
        Copy-Item -LiteralPath $_.FullName -Destination $target
    }
}
