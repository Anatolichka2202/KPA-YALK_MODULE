param(
    [Parameter(Mandatory = $true)][string]$Path,
    [string]$Destination = '192.168.0.115.1113'
)

$seen = @{}
$collect = $false
$packet = New-Object System.Collections.Generic.List[byte]

function Publish-Packet {
    if ($packet.Count -le 42) { return }
    $payload = $packet.GetRange(42, $packet.Count - 42).ToArray()
    $hex = [BitConverter]::ToString($payload).Replace('-', '')
    if (-not $seen.ContainsKey($hex)) {
        $seen[$hex] = $true
        "PAYLOAD_CAPTURED_BYTES=$($payload.Length) HEX=$hex"
    }
}

foreach ($line in [IO.File]::ReadLines($Path)) {
    if ($line -match '^\[') {
        if ($collect) { Publish-Packet }
        $collect = $false
        $packet.Clear()
        continue
    }
    if ($line.Contains("> $Destination") -and $line.Contains('UDP')) {
        $collect = $true
        $packet.Clear()
        continue
    }
    if (-not $collect) { continue }
    if ($line -match '^\s*0x[0-9A-Fa-f]+:\s+(.+)$') {
        foreach ($word in ($Matches[1] -split '\s+')) {
            if ($word -notmatch '^[0-9A-Fa-f]{4}$') { continue }
            $packet.Add([Convert]::ToByte($word.Substring(0, 2), 16))
            $packet.Add([Convert]::ToByte($word.Substring(2, 2), 16))
        }
    }
}
if ($collect) { Publish-Packet }
