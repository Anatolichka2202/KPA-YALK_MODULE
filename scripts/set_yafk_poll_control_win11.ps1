[CmdletBinding()]
param(
    [ValidateSet('EnableYafkPolling', 'AdapterTechMode')]
    [string]$Mode = 'EnableYafkPolling',
    [string]$AdapterAddress = '192.168.0.115',
    [string]$LocalAddress = '192.168.0.50',
    [string]$ReceiveAddress = '0.0.0.0',
    [int]$CommandPort = 1001,
    [int]$AcknowledgementPort = 1101,
    [int]$TimeoutMilliseconds = 1500,
    [int]$Attempts = 3
)

$ErrorActionPreference = 'Stop'

if ($TimeoutMilliseconds -lt 100) {
    throw 'TimeoutMilliseconds must be at least 100'
}
if ($Attempts -lt 1) {
    throw 'Attempts must be at least 1'
}

$operation = if ($Mode -eq 'EnableYafkPolling') { 28 } else { 27 }
[byte[]]$command = 0x44, 0x01, [byte]$operation
$adapterIp = [System.Net.IPAddress]::Parse($AdapterAddress)
$localIp = [System.Net.IPAddress]::Parse($LocalAddress)
$receiveIp = [System.Net.IPAddress]::Parse($ReceiveAddress)
$adapterEndpoint = [System.Net.IPEndPoint]::new($adapterIp, $CommandPort)

$receiver = [System.Net.Sockets.UdpClient]::new(
    [System.Net.IPEndPoint]::new($receiveIp, $AcknowledgementPort))
$receiver.Client.ReceiveTimeout = 50
$sender = [System.Net.Sockets.UdpClient]::new(
    [System.Net.IPEndPoint]::new($localIp, 0))

try {
    Write-Output ('REQUEST mode={0} adapter={1}:{2} local={3} ack_bind={4}:{5} hex={6}' -f
        $Mode, $AdapterAddress, $CommandPort, $LocalAddress, $ReceiveAddress,
        $AcknowledgementPort, ([BitConverter]::ToString($command)))

    for ($attempt = 1; $attempt -le $Attempts; $attempt++) {
        [void]$sender.Send($command, $command.Length, $adapterEndpoint)
        $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMilliseconds)

        while ([DateTime]::UtcNow -lt $deadline) {
            if (-not $receiver.Client.Poll(50000, [System.Net.Sockets.SelectMode]::SelectRead)) {
                continue
            }

            $remote = [System.Net.IPEndPoint]::new([System.Net.IPAddress]::Any, 0)
            $reply = $receiver.Receive([ref]$remote)
            $hex = [BitConverter]::ToString($reply)
            Write-Output ('RX attempt={0} from={1}:{2} bytes={3} hex={4}' -f
                $attempt, $remote.Address, $remote.Port, $reply.Length, $hex)

            if ($remote.Address.Equals($adapterIp) -and
                $remote.Port -eq $AcknowledgementPort -and
                $reply.Length -eq $command.Length -and
                [BitConverter]::ToString($reply) -eq [BitConverter]::ToString($command)) {
                Write-Output ('RESULT ack=true mode={0}' -f $Mode)
                exit 0
            }
        }

        Write-Output ('TIMEOUT attempt={0}/{1}' -f $attempt, $Attempts)
    }

    [Console]::Error.WriteLine(
        ('No matching ACK from {0}:{1}; YAFK state is not confirmed' -f
            $AdapterAddress, $AcknowledgementPort))
    exit 3
} finally {
    $sender.Dispose()
    $receiver.Dispose()
}
