param(
    [Parameter(Mandatory = $true)]
    [string]$Archive,
    [Parameter(Mandatory = $true)]
    [string]$Destination
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Archive -PathType Leaf)) {
    throw "Архив не найден: $Archive"
}

if (-not (Test-Path -LiteralPath $Destination)) {
    New-Item -ItemType Directory -Path $Destination | Out-Null
}

Expand-Archive -LiteralPath $Archive -DestinationPath $Destination -Force
$application = Join-Path $Destination 'OrbitaDesktop.exe'
if (-not (Test-Path -LiteralPath $application -PathType Leaf)) {
    throw "После распаковки не найден OrbitaDesktop.exe: $application"
}

Write-Output "READY=$application"
