$release = 'C:\Orbita\releases\tu_app-tu-final-direct-20260921'
$status = 'C:\Orbita\tu_final_direct.status'
Set-Content -LiteralPath $status -Value ('BEGIN|' + (Get-Date -Format o))
Start-Process -FilePath "$release\tu_app.exe" -WorkingDirectory $release
Start-Sleep -Seconds 5
$process = Get-Process tu_app -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -like '*tu_app-tu-final-direct-20260921*' }
if ($process) {
    Add-Content -LiteralPath $status -Value (
        'RUNNING|' + $process.Id + '|SESSION=' + $process.SessionId + '|PATH=' + $process.Path)
} else {
    Add-Content -LiteralPath $status -Value 'NOT_FOUND'
}
