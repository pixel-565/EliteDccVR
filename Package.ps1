param([string]$OutputDirectory = (Join-Path $PSScriptRoot 'dist'))
$ErrorActionPreference = 'Stop'
$version = (Get-Content -LiteralPath (Join-Path $PSScriptRoot 'VERSION') -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw 'VERSION must contain a semantic version.' }
$files = @('bin\EliteDccVR.exe','bin\VRKeyboard.UiHost.exe','bin\VRKeyboard.UiBridge.dll','bin\openvr_api.dll',
    'Launch.cmd','InitializeSettings.ps1','ReadBindings.ps1','actions.json','app.vrmanifest',
    'exobiology-catalog.json','VERSION','README.md','LICENSE','THIRD_PARTY_NOTICES.md')
foreach ($file in $files) {
    if (-not (Test-Path -LiteralPath (Join-Path $PSScriptRoot $file) -PathType Leaf)) {
        throw "Required release file missing: $file. Run build.cmd first."
    }
}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$stage = Join-Path ([System.IO.Path]::GetTempPath()) ('EliteDccVR-package-' + [guid]::NewGuid().ToString('N'))
$bundle = Join-Path $stage 'EliteDccVR'
New-Item -ItemType Directory -Path $bundle | Out-Null
# Allowlist release contents. Never copy a working browser profile or local runtime settings.
foreach ($file in $files) {
    $destination = Join-Path $bundle $file
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination $destination
}
foreach ($directory in @('defaults','bindings','home')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $directory) -Destination $bundle -Recurse
}
foreach ($directory in @('openvr','minhook','json','webview2','eddi')) {
    $source = Join-Path $PSScriptRoot ('vendor\'+$directory)
    $destination = Join-Path $bundle ('vendor\'+$directory)
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    Get-ChildItem -LiteralPath $source -File | Where-Object { $_.Name -match '(?i)license|notice|copying|revision' } | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $destination
    }
}
$archive = Join-Path $OutputDirectory "EliteDccVR-v$version-windows-x64.zip"
Compress-Archive -LiteralPath $bundle -DestinationPath $archive -Force
$hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
[System.IO.File]::WriteAllText(($archive+'.sha256'), ($hash+'  '+[System.IO.Path]::GetFileName($archive)+[Environment]::NewLine), [System.Text.Encoding]::ASCII)
Write-Output "Release archive: $archive"
Write-Output "SHA256: $hash"
# Keep the uniquely named temporary directory available for inspection; no recursive cleanup is performed.
