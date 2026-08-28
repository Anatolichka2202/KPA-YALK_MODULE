param(
    [string]$AdapterAddress = '192.168.0.115',
    [string]$LocalAddress = '192.168.0.50',
    [int]$Port = 1113,
    [ValidateSet('reset','configure_yalk','configure_ytp','select_yalk')]
    [string]$Operation = 'configure_yalk',
    [int]$TimeoutMilliseconds = 2500
)

$commandIds = @{
    reset = 0x16
    configure_yalk = 0x14
    configure_ytp = 0x15
    select_yalk = 0x0A
}
$packet = New-Object byte[] 128
$packet[0] = 0x52 # R
$packet[1] = 0x4F # O
$packet[2] = 0x4B # K
$packet[3] = 0x54 # T
$packet[4] = $commandIds[$Operation]
switch ($Operation) {
    configure_yalk { $packet[5] = 1; $packet[6] = 43; $packet[7] = 1; $packet[8] = 0 }
    configure_ytp  { $packet[5] = 1; $packet[6] = 1;  $packet[7] = 1 }
    select_yalk    { $packet[5] = 0; $packet[6] = 0;  $packet[7] = 1 }
}

$listener = New-Object System.Net.Sockets.UdpClient
$listener.Client.SetSocketOption([Net.Sockets.SocketOptionLevel]::Socket,
    [Net.Sockets.SocketOptionName]::ReuseAddress, $true)
$listener.Client.Bind((New-Object Net.IPEndPoint([Net.IPAddress]::Parse($LocalAddress), $Port)))
$listener.Client.ReceiveTimeout = 100

try {
    [void]$listener.Send($packet, $packet.Length,
        (New-Object Net.IPEndPoint([Net.IPAddress]::Parse($AdapterAddress), $Port)))
    "SENT_OPERATION=$Operation"
    "SENT_HEX=$([BitConverter]::ToString($packet).Replace('-', ''))"

    $watch = [Diagnostics.Stopwatch]::StartNew()
    $seen = @{}
    while ($watch.ElapsedMilliseconds -lt $TimeoutMilliseconds) {
        try {
            $remote = New-Object Net.IPEndPoint([Net.IPAddress]::Any, 0)
            $received = $listener.Receive([ref]$remote)
            if ($remote.Address.ToString() -ne $AdapterAddress) { continue }
            $prefixLength = [Math]::Min(16, $received.Length)
            $prefix = [BitConverter]::ToString($received, 0, $prefixLength).Replace('-', '')
            $key = "$($received.Length):$prefix"
            if (-not $seen.ContainsKey($key)) {
                $seen[$key] = $true
                "RECEIVED_FROM=$remote LENGTH=$($received.Length) PREFIX=$prefix"
            }
        } catch [Net.Sockets.SocketException] {
            if ($_.Exception.SocketErrorCode -ne [Net.Sockets.SocketError]::TimedOut) { throw }
        }
    }
    "UNIQUE_RESPONSES=$($seen.Count)"
} finally {
    $listener.Dispose()
}
