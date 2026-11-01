Add-Type -AssemblyName System.Drawing
$dir = "C:\Users\User\.gemini\antigravity\scratch\win-notch-monitor\verify_out"
$docsDir = "C:\Users\User\.gemini\antigravity\scratch\win-notch-monitor\docs\images"
$artDir = "C:\Users\User\.gemini\antigravity\brain\863dcc5a-1aa6-41a6-ae7f-9a5c9506ed42"

Get-ChildItem -Path $dir -Filter "*.bmp" | ForEach-Object {
    $bmp = [System.Drawing.Bitmap]::FromFile($_.FullName)
    $pngName = $_.BaseName + ".png"
    $target1 = Join-Path $docsDir $pngName
    $target2 = Join-Path $artDir $pngName
    $bmp.Save($target1, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Save($target2, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Host "Converted $pngName"
}
