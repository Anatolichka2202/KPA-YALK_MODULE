[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PackageDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$packageRoot = (Resolve-Path -LiteralPath $PackageDirectory).Path
$application = Join-Path $packageRoot 'MilTechStation.exe'
if (-not (Test-Path -LiteralPath $application -PathType Leaf)) {
    throw "MilTechStation.exe not found in installed package: $packageRoot"
}

Push-Location -LiteralPath ([IO.Path]::GetTempPath())
try {
    $captureId = [Guid]::NewGuid().ToString('N')
    $stdoutPath = Join-Path ([IO.Path]::GetTempPath()) "miltech-package-smoke-$captureId.out"
    $stderrPath = Join-Path ([IO.Path]::GetTempPath()) "miltech-package-smoke-$captureId.err"
    $process = Start-Process -FilePath $application -ArgumentList @('--package-smoke') `
        -WorkingDirectory (Get-Location).Path -WindowStyle Hidden -Wait -PassThru `
        -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
    $output = @()
    if (Test-Path -LiteralPath $stdoutPath) { $output += Get-Content -LiteralPath $stdoutPath }
    if (Test-Path -LiteralPath $stderrPath) { $output += Get-Content -LiteralPath $stderrPath }
    if ($process.ExitCode -ne 0) {
        $output | ForEach-Object { Write-Output $_ }
        throw "Installed package smoke failed with exit code $($process.ExitCode)"
    }
    $output | ForEach-Object { Write-Output $_ }
    Write-Output 'INSTALLED_PACKAGE_SMOKE_OK (exit code 0)'
    Remove-Item -LiteralPath $stdoutPath, $stderrPath -ErrorAction SilentlyContinue
} finally {
    Pop-Location
}
