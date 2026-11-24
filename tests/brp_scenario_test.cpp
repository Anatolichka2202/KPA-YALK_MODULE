#include "brp_tu_window.h"
#include "hardware/isd_router.h"

#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTabWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
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
    auto* stop = window.findChild<QPushButton*>("brpInterfaceStop");
    auto* capture = window.findChild<QPushButton*>("brpCapture");
    auto* log = window.findChild<QPlainTextEdit*>("brpLog");
    auto* status = window.findChild<QLabel*>("brpInterfaceStatus");
    auto* tabs = window.findChild<QTabWidget*>();
    require(mode && list && start && stop && capture && log && status && tabs, "BRP controls are missing");
    tabs->setCurrentIndex(1);
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

    stop->click();
    mode->setCurrentIndex(2);
    start->click();
    require(waitUntil([&] {
        const auto text = status->text();
        return text.contains(QStringLiteral("M08A10T01 = "))
            && !text.contains(QStringLiteral("нет отсчёта"));
    }), "M8 monitor did not display decoded channels");
    require(!capture->isEnabled(), "M16 acceptance was enabled from the M8 monitor");
}

QString readText(const QString& path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "protocol file could not be opened");
    return QString::fromUtf8(file.readAll());
}

void testScenarioAndReport()
{
    const QTemporaryDir reports;
    require(reports.isValid(), "report directory could not be created");
    qputenv("TU_REPORT_DIR", reports.path().toLocal8Bit());
    BrpTuWindow window(true);
    window.resize(1000, 720);
    window.show();
    auto* mode = window.findChild<QComboBox*>("brpInterfaceMode");
    auto* serial = window.findChild<QLineEdit*>("brpSerialNumber");
    auto* name = window.findChild<QLineEdit*>("brpOperator");
    auto* yasmCodes = window.findChild<QLineEdit*>("brpYasmCodes");
    auto* start = window.findChild<QPushButton*>("brpScenarioStart");
    auto* stop = window.findChild<QPushButton*>("brpScenarioStop");
    auto* skip = window.findChild<QPushButton*>("brpScenarioSkip");
    auto* measure = window.findChild<QPushButton*>("brpScenarioMeasure");
    auto* rows = window.findChild<QTableWidget*>("brpStageTable");
    auto* path = window.findChild<QLabel*>("brpReportLocation");
    auto* manualStart = window.findChild<QPushButton*>("brpInterfaceStart");
    require(mode && serial && name && yasmCodes && start && stop && skip && measure && rows && path && manualStart,
        "scenario controls are missing");
    require(!start->isEnabled(), "scenario started without run metadata");
    serial->setText(QStringLiteral("BRP-01"));
    name->setText(QStringLiteral("A < B; \"C\""));
    mode->setCurrentIndex(2);
    require(!start->isEnabled(), "M8 diagnostics allowed a M16 scenario");
    mode->setCurrentIndex(1);
    require(start->isEnabled() && rows->rowCount() == 5, "M16 scenario plan is incomplete");
    start->click();
    require(stop->isEnabled() && !manualStart->isEnabled() && !measure->isEnabled(),
        "active scenario did not lock manual control and unprepared measurements");
    require(waitUntil([&] { return measure->isEnabled(); }), "scenario did not reach operator confirmation");
    require(rows->item(0, 0)->text() == QStringLiteral("13")
        && rows->item(0, 3)->text() == QStringLiteral("Пройдено")
        && rows->item(1, 3)->text() == QStringLiteral("Пройдено")
        && rows->item(2, 1)->text() == QStringLiteral("3.2.1.7.3"), "interface or preparation result is wrong");

    measure->click();
    require(!measure->isEnabled() && !skip->isEnabled(), "a running measurement could be restarted or skipped");
    require(waitUntil([&] { return measure->isEnabled(); }), "scenario did not advance after first V7 comparison");
    require(rows->item(2, 3)->text() == QStringLiteral("Пройдено"), "V7 result is wrong");
    skip->click();
    require(rows->item(3, 3)->text() == QStringLiteral("Пропущено"), "operator skip was not recorded");
    measure->click();
    require(waitUntil([&] { return start->isEnabled(); }), "scenario did not finish after last measurement");
    const auto completeHtml = path->text();
    if (!completeHtml.endsWith(QStringLiteral(".html")))
        std::cerr << completeHtml.toStdString() << '\n';
    require(completeHtml.endsWith(QStringLiteral(".html")) && manualStart->isEnabled(),
        "completed scenario did not save report or release controls");
    const auto base = completeHtml.chopped(5);
    const auto html = readText(completeHtml);
    const auto txt = readText(base + QStringLiteral(".txt"));
    const auto csv = readText(base + QStringLiteral(".csv"));
    require(html.contains(QStringLiteral("A &lt; B; &quot;C&quot;"))
        && txt.contains(QStringLiteral("ИМИТАЦИЯ")) && txt.contains(QStringLiteral("Пропущено"))
        && csv.contains(QStringLiteral("\"В7 до\";\"20.020\";\"В\""))
        && csv.contains(QStringLiteral("M16P1A10B60C31T01")), "protocol did not preserve results and measurements");

    mode->setCurrentIndex(0);
    start->click();
    stop->click();
    require(start->isEnabled() && !stop->isEnabled() && !measure->isEnabled()
        && rows->item(0, 0)->text() == QStringLiteral("12")
        && rows->item(0, 3)->text() == QStringLiteral("Остановлено")
        && rows->item(2, 3)->text() == QStringLiteral("Не выполнено"), "cancelled run left a stale result or control");
    require(path->text() != completeHtml && readText(path->text()).contains(QStringLiteral("Остановлено")),
        "cancelled run did not produce its own report");

    mode->setCurrentIndex(1);
    yasmCodes->setText(QStringLiteral("1 2"));
    start->click();
    require(waitUntil([&] { return start->isEnabled(); }), "YASM timeout did not end the scenario");
    require(!measure->isEnabled() && rows->item(0, 3)->text() == QStringLiteral("Не пройдено")
        && rows->item(1, 3)->text() == QStringLiteral("Не выполнено")
        && rows->item(2, 3)->text() == QStringLiteral("Не выполнено"), "failed interface let the scenario progress");
    require(readText(path->text()).contains(QStringLiteral("за 5 с не найдена")), "interface failure is missing from report");
}

} // namespace

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    try {
        testIsdPreparation();
        testWindow();
        testScenarioAndReport();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
