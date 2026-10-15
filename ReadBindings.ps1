$ErrorActionPreference = 'Stop'
try {
    $bindingDir = Join-Path $env:LOCALAPPDATA 'Frontier Developments\Elite Dangerous\Options\Bindings'
    $preset = @(Get-Content -LiteralPath (Join-Path $bindingDir 'StartPreset.4.start'))[0].Trim()
    $escaped = [regex]::Escape($preset)
    $file = Get-ChildItem -LiteralPath $bindingDir -Filter '*.binds' |
        Where-Object { $_.Name -match "^$escaped\.(4\.\d+)\.binds$" } |
        Sort-Object { [version]([regex]::Match($_.Name, '(4\.\d+)\.binds$').Groups[1].Value) } -Descending |
        Select-Object -First 1
    if (-not $file) { throw "No local version-4 bindings found for the General Controls preset '$preset'." }
    [xml]$bindings = Get-Content -LiteralPath $file.FullName -Raw
    function Get-VirtualKey([string]$key) {
        if ($key -match '^Key_([A-Z0-9])$') { return [int][char]$Matches[1] }
        $names = @{ Key_Enter=13; Key_Space=32; Key_UpArrow=38; Key_DownArrow=40; Key_LeftArrow=37; Key_RightArrow=39; Key_Backspace=8; Key_Escape=27; Key_Tab=9 }
        if ($names.ContainsKey($key)) { return $names[$key] }
        throw "Unsupported navigation key: $key"
    }
    $lines = @()
    foreach ($action in @('UI_Up','UI_Down','UI_Left','UI_Right','UI_Select')) {
        $node = $bindings.Root.SelectSingleNode($action)
        $chosen = @($node.Primary, $node.Secondary) | Where-Object { $_ -and $_.Device -eq 'Keyboard' -and $_.Key -ne '' } | Select-Object -First 1
        if (-not $chosen) { throw "$action needs a Keyboard binding in Elite." }
        $vk = Get-VirtualKey $chosen.Key
        $mods = 0
        foreach ($modifier in @($chosen.Modifier)) {
            if (-not $modifier) { continue }
            if ($modifier.Device -ne 'Keyboard') { throw "$action uses a non-keyboard modifier." }
            switch -Regex ($modifier.Key) {
                '^Key_(Left|Right)?Shift$' { $mods = $mods -bor 1; break }
                '^Key_(Left|Right)?Control$' { $mods = $mods -bor 2; break }
                '^Key_(Left|Right)?Alt$' { $mods = $mods -bor 4; break }
                default { throw "Unsupported modifier: $($modifier.Key)" }
            }
        }
        $lines += "$vk $mods"
    }
    Set-Content -LiteralPath (Join-Path $PSScriptRoot 'navigation.txt') -Value $lines -Encoding ascii
    Set-Content -LiteralPath (Join-Path $PSScriptRoot 'navigation-source.txt') -Value $file.FullName -Encoding utf8
    Write-Output "Navigation bindings loaded from $($file.Name)."
} catch {
    Write-Error $_
    exit 1
}
