#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QNetworkDatagram>
#include <QProcess>
#include <QTcpSocket>
#include <QThread>
#include <QUdpSocket>

#include <cstdlib>
#include <iostream>

namespace {

[[noreturn]] void fail(const char* message)
{
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
}

void require(bool condition, const char* message)
{
    if (!condition) fail(message);
}

bool waitTcp(quint16 port, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, port);
        if (socket.waitForConnected(150)) return true;
        QThread::msleep(30);
    }
    return false;
}

QByteArray fragmentedExchange(quint16 port,
                              const QByteArray& first,
                              const QByteArray& second,
                              int timeoutMs = 1500)
{
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, port);
    require(socket.waitForConnected(timeoutMs), "TCP test connection failed");
    socket.write(first);
    require(socket.waitForBytesWritten(timeoutMs), "first TCP fragment was not written");
    QThread::msleep(30);
    require(socket.bytesAvailable() == 0,
            "simulator answered before the complete protocol request arrived");
    socket.write(second);
    require(socket.waitForBytesWritten(timeoutMs), "second TCP fragment was not written");
    require(socket.waitForReadyRead(timeoutMs), "simulator did not answer complete request");
    QByteArray response = socket.readAll();
    while (socket.waitForReadyRead(25)) response += socket.readAll();
    return response;
}

QByteArray roktCommand(quint8 mode, quint8 channel = 0)
{
    QByteArray command(128, 0);
    command[0] = 'R';
    command[1] = 'O';
    command[2] = 'K';
    command[3] = 'T';
    command[4] = char(0x0A);
    command[5] = char(mode);
    command[6] = char(channel);
    return command;
}

QByteArray requestFrame(QUdpSocket& udp, const QByteArray& command, int expectedSize)
{
    while (udp.hasPendingDatagrams()) udp.receiveDatagram();
    require(udp.writeDatagram(command, QHostAddress(QStringLiteral("127.0.0.2")), 1113)
                == command.size(),
            "cannot send adapter command to simulator");

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 1500) {
        if (!udp.hasPendingDatagrams()) {
            udp.waitForReadyRead(100);
            QCoreApplication::processEvents();
            continue;
        }
        const QByteArray frame = udp.receiveDatagram().data();
        if (frame.size() == expectedSize) return frame;
    }
    fail("simulator did not emit the expected adapter frame size");
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    require(argc >= 2, "simulator executable path is required");

    QUdpSocket udp;
    require(udp.bind(QHostAddress(QStringLiteral("127.0.0.1")), 1113,
                     QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint),
            "cannot bind test adapter receive socket 127.0.0.1:1113");

    QProcess simulator;
    simulator.setProgram(QString::fromLocal8Bit(argv[1]));
    simulator.setArguments({QStringLiteral("-platform"), QStringLiteral("offscreen")});
    simulator.start();
    require(simulator.waitForStarted(3000), "MilTechStationSimulator did not start");

    const auto stopSimulator = [&] {
        simulator.terminate();
        if (!simulator.waitForFinished(1200)) {
            simulator.kill();
            simulator.waitForFinished(1200);
        }
    };

    if (!waitTcp(15025, 3000) || !waitTcp(18080, 3000)) {
        stopSimulator();
        fail("simulator TCP endpoints did not become ready");
    }

    const QByteArray scpi = fragmentedExchange(15025, "*ID", "N?\n");
    require(scpi.contains("MILTECH,PROTOCOL-SIMULATOR,LOCAL,1.0"),
            "fragmented SCPI request returned an unexpected response");

    const QByteArray http = fragmentedExchange(
        18080,
        "GET /?type=4 HTTP/1.1\r\nHost: localhost\r\n",
        "\r\n");
    require(http.startsWith("HTTP/1.1 200 OK"),
            "fragmented ISD HTTP request returned an unexpected response");

    const QByteArray yalk = requestFrame(udp, roktCommand(0x00), 204);
    require(yalk.size() == 204, "YALK frame must contain 204 bytes");

    const QByteArray ytp = requestFrame(udp, roktCommand(0x02), 68);
    require(ytp.size() == 68 && quint8(ytp[0]) == 0x01 && quint8(ytp[2]) == 0x34,
            "YTP ROKT frame header/size is invalid");

    const QByteArray yvp = requestFrame(udp, roktCommand(0x01), 136);
    require(yvp.size() == 136 && quint8(yvp[0]) == 0x00 && quint8(yvp[1]) == 0x00
                && quint8(yvp[2]) == 0x2B && quint8(yvp[3]) == 0x08,
            "YVP ROKT 136-byte frame header/size is invalid");

    const QByteArray yvpChannel = requestFrame(udp, roktCommand(0x03, 2), 132);
    require(yvpChannel.size() == 132 && quint8(yvpChannel[0]) == 0x03
                && quint8(yvpChannel[1]) == 0x00 && quint8(yvpChannel[2]) == 0x2D
                && quint8(yvpChannel[3]) == 2,
            "YVP channel 132-byte frame header/channel is invalid");

    require(simulator.state() == QProcess::Running,
            "simulator exited unexpectedly during protocol test");
    stopSimulator();

    std::cout << "Simulator protocol/framing test passed\n";
    return EXIT_SUCCESS;
}
