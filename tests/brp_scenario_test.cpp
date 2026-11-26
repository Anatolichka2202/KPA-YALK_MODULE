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
    auto* manual = window.findChild<QPushButton*>("brpManualOpen");
    require(mode && list && start && stop && capture && log && status && manual, "BRP controls are missing");
    manual->click();
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
    stop->click();
    mode->setCurrentIndex(3);
    start->click();
    require(waitUntil([&] {
        return status->text().contains(QStringLiteral("M16P1A10B60C11T01 = 512"));
    }), "passive M16 monitor did not show the decoded BRP address");
    stop->click();
    window.findChild<QPushButton*>("brpBrowseDb")->click();
    auto* addressTable = window.findChild<QTableWidget*>("brpAddressTable");
    require(addressTable && addressTable->rowCount() > 0
        && addressTable->editTriggers() == QAbstractItemView::NoEditTriggers,
        "read-only address catalog did not load");
    int row = 0;
    while (row < addressTable->rowCount()
        && addressTable->item(row, 1)->text() != QStringLiteral("M16P1A10B60C11T01")) ++row;
    require(row < addressTable->rowCount(), "named BRP parameter is missing");
    addressTable->selectRow(row);
    window.findChild<QPushButton*>("brpApplyMonitorParameters")->click();
    require(window.findChild<QLineEdit*>("brpM8Addresses")->text() == QStringLiteral("M16P1A10B60C11T01"),
        "selected parameter did not replace the monitor address list");
    start->click();
    require(waitUntil([&] {
        return status->text().contains(QStringLiteral("ББ СУ · M16P1A10B60C11T01 = 512"));
    }), "monitor did not display the selected parameter by name");
}

QString readText(const QString& path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "protocol file could not be opened");
    return QString::fromUtf8(file.readAll());
}

QPushButton* action(BrpTuWindow& window, const char* property)
{
    for (auto* button : window.findChildren<QPushButton*>()) {
        if (button->property(property).toBool()) return button;
    }
    throw std::runtime_error(property);
}

void testScenarioAndReport()
{
    const QTemporaryDir reports;
    require(reports.isValid(), "report directory could not be created");
    qputenv("TU_REPORT_DIR", reports.path().toLocal8Bit());
    BrpTuWindow window(true);
    window.resize(1440, 900);
    window.show();
    auto* mode = window.findChild<QComboBox*>("brpInterfaceMode");
    auto* serial = window.findChild<QLineEdit*>("tuManualSerial");
    auto* name = window.findChild<QLineEdit*>("tuCompletionOperator");
    auto* yasmCodes = window.findChild<QLineEdit*>("brpYasmCodes");
    auto* manual = window.findChild<QPushButton*>("brpManualOpen");
    auto* back = window.findChild<QPushButton*>("brpManualBack");
    auto* readiness = window.findChild<QLabel*>("tuReadyState");
    auto* stop = action(window, "tuStopAction");
    auto* check = action(window, "tuStandAction");
    auto* start = action(window, "tuStartAction");
    auto* buildReport = action(window, "tuReportAction");
    auto* measure = action(window, "brpPointAction");
    auto* skip = window.findChild<QPushButton*>("skipStage");
    auto* revealSkip = window.findChild<QPushButton*>("tuSkipRevealHotspot");
    auto* rows = window.findChild<QTableWidget*>("tuReportTable");
    auto* graph = window.findChild<QWidget*>("tuStageHistory");
    require(mode && serial && name && yasmCodes && manual && back && readiness
        && skip && revealSkip && rows && graph, "TU screens are incomplete");

    const auto selectInterface = [&](int index) {
        require(manual->isVisible(), "manual controls are missing on the selection screen");
        manual->click();
        require(mode->isVisible() && back->isVisible(), "manual page did not open");
        mode->setCurrentIndex(index);
        back->click();
    };
    const auto newCheck = [&] {
        for (auto* button : window.findChildren<QPushButton*>()) {
            if (button->isVisible() && button->text() == QStringLiteral("Новая проверка")) {
                button->click();
                return;
            }
        }
        throw std::runtime_error("report has no new check action");
    };
    const auto stageResult = [&](int index) { return rows->item(index, 1)->text(); };
    const auto exportReport = [&]() {
        require(name->isVisible() && !buildReport->isEnabled(), "operator entry was skipped");
        name->setText(QStringLiteral("A < B; \"C\""));
        buildReport->click();
        require(rows->isVisible(), "report page did not open");
        const auto path = window.findChild<QLabel*>("tuReportPath")->text();
        const auto prefix = QStringLiteral("Протокол ТУ: ");
        require(path.startsWith(prefix + reports.path()),
                "report file was not written to the configured directory");
        return path.mid(prefix.size());
    };

    require(!check->isEnabled(), "stand check started without a serial number");
    serial->setText(QStringLiteral("BRP-01"));
    selectInterface(2);
    check->click();
    require(readiness->isVisible() && readiness->text() == QStringLiteral("СТЕНД НЕ ГОТОВ")
        && !start->isVisible(), "M8 diagnostics allowed a M16 scenario");
    for (auto* button : window.findChildren<QPushButton*>()) {
        if (button->isVisible() && button->text() == QStringLiteral("Выбрать другой SN")) button->click();
    }

    selectInterface(1);
    check->click();
    require(start->isVisible() && readiness->text() == QStringLiteral("СТЕНД ГОТОВ"), "stand readiness screen is wrong");
    start->click();
    require(stop->isVisible() && graph->isVisible() && !manual->isVisible() && !measure->isVisible(),
        "runtime did not show the interface graph and lock unattended measurements");
    require(waitUntil([&] { return measure->isVisible(); }), "scenario did not reach operator confirmation");
    auto* interfaceStage = window.findChild<QLabel*>("tuRequirementStatus_yasm");
    auto* isdStage = window.findChild<QLabel*>("tuRequirementStatus_isd_off");
    require(interfaceStage && isdStage && interfaceStage->text() == QStringLiteral("НОРМА")
        && isdStage->text() == QStringLiteral("НОРМА"), "interface or preparation stage is wrong");
    require(window.findChild<QLabel*>("brpStageHeading")->text() == QStringLiteral("M16P1A10B60C11T01"),
        "active stage is missing its address");

    measure->click();
    require(!measure->isVisible() && !skip->isEnabled(), "a running measurement could be restarted or skipped");
    require(waitUntil([&] { return measure->isVisible(); }), "scenario did not advance after first V7 comparison");
    revealSkip->click();
    require(skip->isVisible(), "skip action did not reveal");
    skip->click();
    measure->click();
    require(waitUntil([&] { return name->isVisible(); }), "scenario did not finish after last measurement");

    const auto completeHtml = exportReport();
    const auto base = completeHtml.chopped(5);
    require(rows->rowCount() == 5 && stageResult(2) == QStringLiteral("НОРМА")
        && stageResult(3) == QStringLiteral("ПРОПУЩЕНО") && stageResult(4) == QStringLiteral("НОРМА"),
        "report summary did not preserve the stage verdicts");
    const auto html = readText(completeHtml);
    const auto txt = readText(base + QStringLiteral(".txt"));
    const auto csv = readText(base + QStringLiteral(".csv"));
    require(html.contains(QStringLiteral("@page{size:A4;margin:16mm}"))
        && html.contains(QStringLiteral("A &lt; B; &quot;C&quot;"))
        && txt.contains(QStringLiteral("РЕЗУЛЬТАТЫ КАДРОВЫХ ПРОВЕРОК БРП №BRP-01"))
        && txt.contains(QStringLiteral("Имитация")) && txt.contains(QStringLiteral("ПРОПУЩЕНО"))
        && csv.contains(QStringLiteral("v7_before_v=20.020"))
        && csv.contains(QStringLiteral("M16P1A10B60C31T01")), "reference report lost the BRP results or print layout");

    newCheck();
    selectInterface(0);
    check->click();
    start->click();
    stop->click();
    const auto cancelledHtml = exportReport();
    require(cancelledHtml != completeHtml
        && stageResult(0) == QStringLiteral("ОСТАНОВЛЕНО") && stageResult(2) == QStringLiteral("НЕ ВЫПОЛНЕНО"),
        "cancelled run left a stale result");

    newCheck();
    manual->click();
    mode->setCurrentIndex(1);
    yasmCodes->setText(QStringLiteral("1 2"));
    back->click();
    check->click();
    start->click();
    require(waitUntil([&] { return name->isVisible(); }), "YASM timeout did not end the scenario");
    const auto failureHtml = exportReport();
    require(stageResult(0) == QStringLiteral("НЕ НОРМА")
        && stageResult(1) == QStringLiteral("НЕ ВЫПОЛНЕНО")
        && stageResult(2) == QStringLiteral("НЕ ВЫПОЛНЕНО"), "failed interface let the scenario progress");
    require(readText(failureHtml).contains(QStringLiteral("за 5 с не найдена")), "interface failure is missing from report");
}

void testOperatorBuild()
{
    BrpTuWindow stand(false);
    stand.show();
    require(stand.findChild<QComboBox*>("brpRs485Port")->isVisible(), "RS-485 port is missing");
    require(!stand.findChild<QPushButton*>("brpManualOpen"), "diagnostics leaked into production");
    auto* serial = stand.findChild<QLineEdit*>("tuManualSerial");
    serial->setText(QStringLiteral("BRP-01"));
    action(stand, "tuStandAction")->click();
    require(stand.findChild<QLabel*>("tuReadyDetail")->text().contains(QStringLiteral("COM-порт RS-485")),
            "missing port did not block stand readiness");

    BrpTuWindow simulation(true);
    simulation.show();
    simulation.findChild<QLineEdit*>("tuManualSerial")->setText(QStringLiteral("BRP-02"));
    action(simulation, "tuStandAction")->click();
    action(simulation, "tuStartAction")->click();
    auto* measure = action(simulation, "brpPointAction");
    require(waitUntil([&] { return measure->isVisible(); }), "RS-485 scenario did not reach the first point");
    measure->click();
    require(waitUntil([&] { return measure->isVisible(); }), "RS-485 scenario did not advance after V7");
    action(simulation, "tuStopAction")->click();
    require(simulation.findChild<QLineEdit*>("tuCompletionOperator")->isVisible(), "report entry is missing");
}

} // namespace

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    try {
        testIsdPreparation();
#if BRP_ENABLE_MANUAL_TESTS
        testWindow();
        testScenarioAndReport();
#else
        testOperatorBuild();
#endif
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
