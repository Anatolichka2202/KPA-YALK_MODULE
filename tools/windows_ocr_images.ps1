[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$InputDirectory,
    [Parameter(Mandatory)]
    [string]$OutputFile,
    [string]$LanguageTag = 'ru'
)

$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Runtime.WindowsRuntime
[Windows.Globalization.Language, Windows.Globalization, ContentType = WindowsRuntime] | Out-Null
[Windows.Media.Ocr.OcrEngine, Windows.Foundation, ContentType = WindowsRuntime] | Out-Null
[Windows.Storage.StorageFile, Windows.Storage, ContentType = WindowsRuntime] | Out-Null
[Windows.Storage.FileAccessMode, Windows.Storage, ContentType = WindowsRuntime] | Out-Null
[Windows.Storage.Streams.IRandomAccessStream, Windows.Storage.Streams, ContentType = WindowsRuntime] | Out-Null
[Windows.Graphics.Imaging.BitmapDecoder, Windows.Graphics.Imaging, ContentType = WindowsRuntime] | Out-Null
[Windows.Graphics.Imaging.SoftwareBitmap, Windows.Graphics.Imaging, ContentType = WindowsRuntime] | Out-Null

function Await-WinRt {
    param(
        [Parameter(Mandatory)]
        $Operation,
        [Parameter(Mandatory)]
        [Type]$ResultType
    )

    $method = [System.WindowsRuntimeSystemExtensions].GetMethods() |
        Where-Object {
            $_.Name -eq 'AsTask' -and
            $_.IsGenericMethod -and
            $_.GetParameters().Count -eq 1
        } |
        Select-Object -First 1
    $task = $method.MakeGenericMethod($ResultType).Invoke($null, @($Operation))
    $task.Wait()
    return $task.Result
}

$language = New-Object Windows.Globalization.Language($LanguageTag)
$engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromLanguage($language)
if (-not $engine) {
    throw "Windows OCR language is unavailable: $LanguageTag"
}

$images = Get-ChildItem -LiteralPath $InputDirectory -File |
    Where-Object { $_.Extension -match '^\.(png|jpg|jpeg|tif|tiff|bmp)$' } |
    Sort-Object Name
if (-not $images) {
    throw "No images found in $InputDirectory"
}

$builder = New-Object System.Text.StringBuilder
foreach ($image in $images) {
    Write-Host "OCR $($image.Name)"
    $file = Await-WinRt `
        ([Windows.Storage.StorageFile]::GetFileFromPathAsync($image.FullName)) `
        ([Windows.Storage.StorageFile])
    $stream = Await-WinRt `
        ($file.OpenAsync([Windows.Storage.FileAccessMode]::Read)) `
        ([Windows.Storage.Streams.IRandomAccessStream])
    try {
        $decoder = Await-WinRt `
            ([Windows.Graphics.Imaging.BitmapDecoder]::CreateAsync($stream)) `
            ([Windows.Graphics.Imaging.BitmapDecoder])
        $bitmap = Await-WinRt `
            ($decoder.GetSoftwareBitmapAsync()) `
            ([Windows.Graphics.Imaging.SoftwareBitmap])
        try {
            $result = Await-WinRt `
                ($engine.RecognizeAsync($bitmap)) `
                ([Windows.Media.Ocr.OcrResult])
            [void]$builder.AppendLine("===== $($image.BaseName) =====")
            [void]$builder.AppendLine($result.Text)
            [void]$builder.AppendLine()
        } finally {
            if ($bitmap) { $bitmap.Dispose() }
        }
    } finally {
        if ($stream) { $stream.Dispose() }
    }
}

$parent = Split-Path -Parent $OutputFile
if ($parent) {
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
}
[System.IO.File]::WriteAllText(
    $OutputFile,
    $builder.ToString(),
    (New-Object System.Text.UTF8Encoding($true)))
Write-Host "Wrote $OutputFile"
