$content = Get-Content cmake\options_win.cmake
$insertLine = $content | Where-Object { $_ -match "if \(DESKTOP_APP_SPECIAL_TARGET\)" } | Select-Object -First 1
if ($insertLine) {
    $insertIndex = [array]::IndexOf($content, $insertLine)
    $beforeContent = $content[0..($insertIndex-1)]
    $afterContent = $content[$insertIndex..($content.Length-1)]
    $patchContent = Get-Content opt_patch.txt
    $newContent = $beforeContent + $patchContent + $afterContent
    $newContent | Set-Content cmake\options_win.cmake
}
