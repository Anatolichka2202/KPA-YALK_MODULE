param(
    [string]$AdapterAddress = '192.168.0.115',
    [string]$LocalAddress = '192.168.0.50',
    [int]$Port = 1113,
    [ValidateRange(0, 30)]
    [int]$Mode = 6,
    [switch]$Single,
    [int]$TimeoutMs = 2500
)

$ErrorActionPreference = 'Stop'
$flags = if ($Single) { 3 } else { 1 }
[byte[]]$command = 0x44, $flags, $Mode

$socket = [Net.Sockets.Socket]::new(
    [Net.Sockets.AddressFamily]::InterNetwork,
    [Net.Sockets.SocketType]::Dgram,
    [Net.Sockets.ProtocolType]::Udp)
$socket.SetSocketOption([Net.Sockets.SocketOptionLevel]::Socket,
    [Net.Sockets.SocketOptionName]::ReuseAddress, $true)
$socket.ReceiveTimeout = 200
$socket.Bind([Net.IPEndPoint]::new([Net.IPAddress]::Parse($LocalAddress), $Port))

try {
    $target = [Net.IPEndPoint]::new([Net.IPAddress]::Parse($AdapterAddress), $Port)
    [void]$socket.SendTo($command, $target)
    $commandHex = ([BitConverter]::ToString($command)).Replace('-', '')
    Write-Output ('SENT={0}' -f $commandHex)

    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    $dataPackets = 0
    while ([DateTime]::UtcNow -lt $deadline) {
        [byte[]]$buffer = [byte[]]::new(65535)
        $remote = [Net.EndPoint]([Net.IPEndPoint]::new([Net.IPAddress]::Any, 0))
        try {
            $count = $socket.ReceiveFrom($buffer, [ref]$remote)
        } catch [Net.Sockets.SocketException] {
            if ($_.Exception.SocketErrorCode -eq [Net.Sockets.SocketError]::TimedOut) { continue }
            throw
        }
        if ($remote.Address.ToString() -ne $AdapterAddress) { continue }
        $isAck = $count -eq 3
        $isAck = $isAck -and $buffer[0] -eq $command[0]
        $isAck = $isAck -and $buffer[1] -eq $command[1]
        $isAck = $isAck -and $buffer[2] -eq $command[2]
        if ($isAck) {
            Write-Output ('ACK={0}' -f $commandHex)
            Write-Output ('DATA_PACKETS_IGNORED={0}' -f $dataPackets)
            exit 0
        }
        if ($count -eq 200 -or $count -eq 204) { $dataPackets++ }
    }
    throw "Адаптер не подтвердил режим $Mode за $TimeoutMs мс; кадров данных принято: $dataPackets"
} finally {
    $socket.Dispose()
}
