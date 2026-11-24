#include "brp_tu_window.h"
#include "hardware/isd_router.h"

#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QtConcurrentRun>

#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

template <typename Predicate>
bool waitUntil(Predicate condition, int timeout = 8000)
{
    QElapsedTimer clock;
    clock.start();
    while (!condition() && clock.elapsed() < timeout) {
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }
    return condition();
}

void testIsdPreparation()
{
    QTcpServer server;
    require(server.listen(QHostAddress::LocalHost, 0), "loopback ISD could not start");
    QStringList paths;
    bool reject = false;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
        auto* socket = server.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
            if (!socket->peek(socket->bytesAvailable()).contains("\r\n\r\n")) return;
            const auto request = socket->readAll();
            const auto fields = request.left(request.indexOf("\r\n")).split(' ');
            if (fields.size() < 2) return;
            paths.append(QString::fromLatin1(fields[1]));
            const QByteArray body = reject && paths.size() == 2 ? "FAIL" : "OK";
            socket->write("HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: "
                          + QByteArray::number(body.size()) + "\r\n\r\n" + body);
            socket->disconnectFromHost();
        });
    });

    const auto run = [port = server.serverPort()] {
        try {
            tu::hardware::IsdRouter isd({"127.0.0.1", port, 500});
            return isd.prepareYagr(1, 2, 2, 30);
        } catch (const std::exception&) {
            return 0u;
        }
    };
    auto success = QtConcurrent::run(run);
    require(waitUntil([&] { return success.isFinished(); }), "ISD preparation timed out");
    require(success.result() == 5, "ISD did not acknowledge every OFF");
    const QStringList expected{"/type=3num=1val=0", "/type=3num=2val=0",
        "/type=2num=1val=0", "/type=1num=1val=0work=0", "/type=1num=2val=0work=0"};
    require(paths == expected, "ISD used the wrong baseline commands");

    paths.clear();
    reject = true;
    auto failure = QtConcurrent::run(run);
    require(waitUntil([&] { return failure.isFinished(); }), "rejected preparation timed out");
    require(failure.result() == 0 && paths.size() == 2, "ISD continued after a rejected OFF");
}

void testWindow()
{
    BrpTuWindow window(true);
    window.show();
    auto* mode = window.findChild<QComboBox*>("brpInterfaceMode");
    auto* list = window.findChild<QComboBox*>("brpCheckList");
    auto* start = window.findChild<QPushButton*>("brpInterfaceStart");
    auto* capture = window.findChild<QPushButton*>("brpCapture");
    auto* log = window.findChild<QPlainTextEdit*>("brpLog");
    require(mode && list && start && capture && log, "BRP controls are missing");
    require(!capture->isEnabled(), "measurement was available before interface validation");
    mode->setCurrentIndex(1);
    start->click();
    require(waitUntil([&] { return log->toPlainText().contains(QStringLiteral("ЯСМ: ожидаемая последовательность")); }),
            "YASM mode did not validate the external pattern");
    list->setCurrentIndex(2); // Строка 3, ББ СУ.
    require(capture->isEnabled(), "analog measurement did not become available");
    capture->click();
    require(waitUntil([&] { return log->toPlainText().contains(QStringLiteral("Точка в допуске")); }),
            "simulated voltage did not pass the comparison");
    list->setCurrentIndex(6); // Строка 10, сухие контакты.
    require(capture->isEnabled(), "discrete measurement did not become available");
    capture->click();
    require(waitUntil([&] { return log->toPlainText().contains(QStringLiteral("Совпало")); }),
            "simulated contact state did not match");
}

} // namespace

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    try {
        testIsdPreparation();
        testWindow();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
