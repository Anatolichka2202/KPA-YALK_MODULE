#include "brp_tu_window.h"
#include "hardware/isd_router.h"

#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QSpinBox>
#include <QWheelEvent>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QDialog>
#include <QDir>
#include <QListWidget>
#include <QJsonArray>
#include <QProcess>
#include "app/tu_report_writer.h"
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QInputDialog>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QtConcurrentRun>

#include <array>
#include <iostream>
#include <utility>
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
    auto fullContactRange = QtConcurrent::run([port = server.serverPort()] {
        tu::hardware::IsdRouter isd({"127.0.0.1", port, 500});
        return isd.prepareYagr(64, 1, 1, 20);
    });
    require(waitUntil([&] { return fullContactRange.isFinished(); }), "full ISD baseline timed out");
    require(fullContactRange.result() == 66
        && paths.at(41) == "/type=2num=41val=0"
        && paths.at(46) == "/type=2num=46val=0"
        && paths.at(64) == "/type=2num=64val=0"
        && paths.back() == "/type=1num=1val=0work=0", "ISD omitted the YAGR-4 relays");

    paths.clear();
    reject = true;
    auto failure = QtConcurrent::run(run);
    require(waitUntil([&] { return failure.isFinished(); }), "rejected preparation timed out");
    require(failure.result() == 0 && paths.size() == 2, "ISD continued after a rejected OFF");
}

template <typename Action>
void useFileDialog(QPushButton& button, Action action)
{
    // Таймер живёт столько же, сколько локальные параметры теста.
    bool handled = false;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &timer, [&] {
        auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
        if (!dialog) return;
        handled = true;
        action(*dialog);
        if (dialog->isVisible()) dialog->reject();
        timer.stop();
    });
    timer.start(10);
    button.click();
    require(handled, "test did not reach the file dialog");
}

void chooseYasmFile(BrpTuWindow& window, const QString& path)
{
    auto* button = window.findChild<QPushButton*>("brpOpenYasmFixture");
    require(button && button->isVisible(), "YASM import is not available");
    useFileDialog(*button, [&](QFileDialog& dialog) {
        if (auto* fileName = dialog.findChild<QLineEdit*>("fileNameEdit")) fileName->setText(path);
        QMetaObject::invokeMethod(&dialog, "accept", Qt::DirectConnection);
    });
}

QString writeYasmFixture(const QTemporaryDir& directory, const QString& name, const QByteArray& data)
{
    require(directory.isValid(), "YASM fixture directory not created");
    QFile file(directory.filePath(name));
    require(file.open(QIODevice::WriteOnly) && file.write(data) == data.size(), "YASM fixture not saved");
    return file.fileName();
}

void testMissingPurumk()
{
    BrpTuWindow window(false);
    window.show();
    window.findChild<QPushButton*>("brpManualOpen")->click();
    auto* mode = window.findChild<QComboBox*>("brpInterfaceMode");
    auto* start = window.findChild<QPushButton*>("brpInterfaceStart");
    auto* button = window.findChild<QPushButton*>("brpLaunchPurumk");
    auto* log = window.findChild<QPlainTextEdit*>("brpLog");
    require(mode && start && button && log, "PURUMK controls missing");
    if (QFileInfo::exists(QDir(QCoreApplication::applicationDirPath()).filePath("PURUMK.exe"))) {
        std::cerr << "Skipping missing-PURUMK scenario: a real executable is deployed next to CTest\n";
        return;
    }
    mode->setCurrentIndex(1);
    useFileDialog(*button, [](QFileDialog& dialog) { dialog.reject(); });
    require(log->toPlainText().contains(QStringLiteral("PURUMK не запущена")),
            "missing external executable was not reported");
    mode->setCurrentIndex(0);
    require(start->isVisible() && start->isEnabled(), "missing PURUMK blocked the RS485 controls");
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
    auto* bbPr = window.findChild<QPushButton*>("brpIssueBbPr");
    auto* purumk = window.findChild<QPushButton*>("brpLaunchPurumk");
    require(bbPr && purumk, "BB PR signal action or PURUMK launcher missing");
    bbPr->click();
    require(log->toPlainText().contains(QStringLiteral("ИМИТАЦИЯ запроса выдачи")),
            "BB PR simulator incorrectly claimed physical output");
    require(!capture->isEnabled(), "measurement was available before interface validation");
    // Live counter is a monitor, not a 5-second acceptance gate.
    mode->setCurrentIndex(0);
    auto* counterPlot = window.findChild<QWidget*>("brpCounterPlot");
    require(counterPlot && counterPlot->isVisible(), "dedicated RS485 waveform is hidden");
    start->click();
    require(waitUntil([&] {
        return status->text().contains(QStringLiteral("ДВА ПЕРИОДА ПОДТВЕРЖДЕНЫ"));
    }, 12000), "live counter did not show two received cycles");
    require(stop->isEnabled() && !start->isEnabled(),
            "live counter incorrectly stopped as soon as two cycles passed");
    stop->click();
    require(waitUntil([&] { return start->isEnabled(); }, 20000),
            "live RS485 counter did not release stream on operator stop");
    mode->setCurrentIndex(1);
    const QTemporaryDir yasmFiles;
    chooseYasmFile(window, writeYasmFixture(yasmFiles, "matched.bcfrm",
        "<FRAME TYPE='BC'><data><sequence>B212 675C</sequence></data></FRAME>"));
    require(log->toPlainText().contains(QStringLiteral("ЯСМ: загружено 2 слов")),
            "valid XML fixture was not imported");
    chooseYasmFile(window, writeYasmFixture(yasmFiles, "malformed.bcfrm",
        "<FRAME TYPE='BC'><data><sequence>ZZZZ</sequence>"));
    require(log->toPlainText().contains(QStringLiteral("Файл ЯСМ:"))
        && log->toPlainText().contains(QStringLiteral("Действующий эталон ЯСМ: matched.bcfrm")),
            "malformed fixture replaced the active reference or did not report an error");
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
    require(!start->isEnabled(), "start was enabled while Orbita decoder was still stopping");
    require(waitUntil([&] { return start->isEnabled(); }, 20000), "async stop did not release stream");
    mode->setCurrentIndex(2);
    start->click();
    require(waitUntil([&] {
        const auto text = status->text();
        return text.contains(QStringLiteral("M08A10T01 = "))
            && !text.contains(QStringLiteral("нет отсчёта"));
    }), "M8 monitor did not display decoded channels");
    require(!capture->isEnabled(), "M16 acceptance was enabled from the M8 monitor");
    stop->click();
    require(waitUntil([&] { return start->isEnabled(); }, 20000), "M8 stop did not release stream");
    mode->setCurrentIndex(3);
    auto* graphToggle = window.findChild<QCheckBox*>("brpMonitorGraphToggle");
    auto* comToggle = window.findChild<QCheckBox*>("brpMonitorCounterToCom");
    require(graphToggle && graphToggle->isChecked(), "M16 bar graph must be on by default");
    require(comToggle && !comToggle->isChecked(), "COM stimulus cannot be enabled silently");
    // Simulation verifies the M16 receive path without opening any physical COM.
    comToggle->setChecked(true);
    start->click();
    require(waitUntil([&] {
        return status->text().contains(QStringLiteral("M16P1A10B60C11T01 = 512"));
    }), "passive M16 monitor did not show the decoded BRP address");
    require(waitUntil([&] {
        return status->text().contains(QStringLiteral("два периода 0–1023 ПОДТВЕРЖДЕНЫ"));
    }, 12000), "M16 monitor did not validate returned RS485 counter");
    require(status->text().contains(QStringLiteral("Счётчик RS-485 · M16P2:"))
        && status->text().contains(QStringLiteral("Ептм ·")),
        "enabled counter replaced the Orbita profile instead of extending it");
    auto* manualTime = window.findChild<QLabel*>("brpManualBoardTime");
    require(manualTime && waitUntil([&] { return manualTime->text() == QStringLiteral("Бортовое время · 0 с"); }),
            "received zero MTV was not displayed as a valid mark");
    stop->click();
    require(waitUntil([&] { return start->isEnabled(); }, 20000), "M16 stop did not release stream");
    comToggle->setChecked(false);
    auto* view = window.findChild<QComboBox*>("brpMonitorPlotView");
    auto* trendChannel = window.findChild<QComboBox*>("brpMonitorTrendChannel");
    auto* barsPlot = window.findChild<QWidget*>("brpMonitorPlot");
    auto* trendPlot = window.findChild<QWidget*>("brpMonitorTrend");
    require(view && trendChannel && trendPlot && barsPlot && view->count() == 2,
            "monitor must provide both live bars and a separate time trend");
    view->setCurrentIndex(0);
    start->click();
    require(waitUntil([&] { return trendChannel->count() >= 3; }),
            "trend channels did not include Eptm");
    const int eptm = trendChannel->findText(QStringLiteral("Ептм"), Qt::MatchContains);
    require(eptm >= 0, "Eptm missing from monitor trend selector");
    require(waitUntil([&] { return status->text().contains(QStringLiteral("Ептм ·"))
        && !status->text().contains(QStringLiteral("нет отсчёта")); }), "M16 columns did not receive data");
    const QPointF slot(52 + (eptm + 0.5) * (barsPlot->width() - 66) / trendChannel->count(), 64);
    QMouseEvent click(QEvent::MouseButtonPress, slot, barsPlot->mapToGlobal(slot.toPoint()),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(barsPlot, &click);
    require(barsPlot->isVisible() && trendPlot->isVisible() && trendChannel->currentIndex() == eptm,
            "clicking Eptm did not reveal its trend below the live bars");
    require(waitUntil([&] { return trendPlot->property("brpTrendRawPoints").toInt() >= 150; }, 12000),
            "Eptm trend did not preserve intra-group samples");
    auto* trendWindow = window.findChild<QComboBox*>("brpMonitorTrendWindow");
    require(trendWindow && trendWindow->currentData().toDouble() == 1.0, "unexpected initial trend window");
    const auto wheelAt = trendPlot->rect().center();
    QWheelEvent zoom(wheelAt, trendPlot->mapToGlobal(wheelAt), {}, {0, 120}, Qt::NoButton,
                     Qt::ControlModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(trendPlot, &zoom);
    require(zoom.isAccepted() && trendWindow->currentData().toDouble() == 0.25,
            "Ctrl+wheel did not shorten the trend window");
    trendWindow->setCurrentIndex(1);
    require(waitUntil([&] {
        const auto text = status->text();
        return text.contains(QStringLiteral(" · сред. ")) && text.contains(QStringLiteral(" · полуразмах "))
            && text.contains(QStringLiteral(" · мин. ")) && text.contains(QStringLiteral(" · макс. "));
    }), "Eptm monitor statistics did not reach the selected channel");
    auto* opacity = window.findChild<QSpinBox*>("brpMonitorOverlayOpacity");
    require(opacity && opacity->value() == 45, "missing monitor overlay opacity");
    opacity->setValue(0);
    require(barsPlot->isVisible() && trendPlot->isVisible(), "opacity hid the live values or trend");
    opacity->setValue(45);
    view->setCurrentIndex(0);
    require(barsPlot->isVisible() && !trendPlot->isVisible(), "closing the trend hid the live bars");
    stop->click();
    require(waitUntil([&] { return start->isEnabled(); }, 20000), "trend monitor stop did not release stream");
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
    stop->click();
    require(waitUntil([&] { return start->isEnabled(); }, 20000), "address monitor stop did not finish");

    QTimer::singleShot(0, &window, [] {
        auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
        if (dialog) {
            dialog->setTextValue(QStringLiteral("Второй профиль"));
            dialog->accept();
        }
    });
    window.findChild<QAction*>("brpAddMonitorProfile")->trigger();
    auto* profiles = window.findChild<QComboBox*>("brpMonitorProfiles");
    require(profiles && profiles->count() == 2, "second monitor profile was not created");
    window.findChild<QLineEdit*>("brpM8Addresses")->setText(QStringLiteral("M16P1A10B60C21T01"));
    window.findChild<QCheckBox*>("brpMonitorGraphToggle")->setChecked(true);
    start->click();
    require(waitUntil([&] {
        const auto text = status->text();
        return text.contains(QStringLiteral("ББ СУ · M16P1A10B60C11T01 = 512"))
            && text.contains(QStringLiteral("M16P1A10B60C21T01 = 512"));
    }), "combined monitor did not show channels from both profiles");
    require(window.findChild<QWidget*>("brpMonitorPlot")->isVisible(), "combined monitor graph is hidden");
    stop->click();
    require(waitUntil([&] { return start->isEnabled(); }, 20000), "combined monitor stop did not finish");
    auto* e2010 = window.findChild<QAction*>("brpE2010Profile");
    require(e2010, "E20-10 profile action is missing");
    e2010->trigger();
    const auto addresses = window.findChild<QLineEdit*>("brpM8Addresses")->text().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    require(addresses.size() == 20 && addresses.at(6) == QStringLiteral("M16P1A10B60C31T01"),
            "E20-10 profile lost its channel order");
    require(addresses.endsWith(QStringLiteral("M16P1A30B40C21T05")),
            "E20-10 profile did not include the complete contact word");
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
    selectInterface(1);
    check->click();
    require(start->isVisible() && readiness->text() == QStringLiteral("СТЕНД ГОТОВ"), "stand readiness screen is wrong");
    start->click();
    require(stop->isVisible() && graph->isVisible() && !manual->isVisible() && !measure->isVisible(),
        "runtime did not show the interface graph and lock unattended measurements");
    require(waitUntil([&] { return measure->isVisible(); }), "scenario did not reach operator confirmation");
    auto* bbPrRuntime = window.findChild<QPushButton*>("brpIssueBbPrRuntime");
    require(bbPrRuntime && bbPrRuntime->isVisible() && bbPrRuntime->isEnabled(),
            "BB PR output action missing from main TU runner");
    bbPrRuntime->click();
    require(window.findChild<QPlainTextEdit*>("brpLog")->toPlainText().contains(
                QStringLiteral("ИМИТАЦИЯ запроса выдачи")),
            "BB PR runtime action failed to record simulated request");
    auto* interfaceStage = window.findChild<QLabel*>("tuRequirementStatus_yasm");
    auto* isdStage = window.findChild<QLabel*>("tuRequirementStatus_isd_off");
    auto* coverage = window.findChild<QLabel*>("tuRequirementStatus_m16_coverage");
    require(interfaceStage && isdStage && coverage && interfaceStage->text() == QStringLiteral("НОРМА")
        && isdStage->text() == QStringLiteral("НОРМА")
        && coverage->text() == QStringLiteral("НОРМА"),
        "interface, ISD preparation or M16 coverage stage is wrong");
    require(window.findChild<QLabel*>("brpStageHeading")->text() == QStringLiteral("M16P1A10B20T01"),
        "active POS stage is missing its address");

    measure->click();
    require(!measure->isVisible() && skip->isEnabled(),
            "measurement must not be restarted while Ctrl+Shift+Q remains available");
    require(waitUntil([&] { return measure->isVisible(); }), "scenario did not advance after first V7 comparison");
    revealSkip->click();
    require(skip->isVisible(), "skip action did not reveal");
    skip->click();
    measure->click();
    // Four POS channels x two nonnegative points, plus BB SU/PR/Eptm: 11 total.
    // Two measurements were triggered above; complete the remaining 9.
    for (int point = 2; point < 11; ++point) {
        require(waitUntil([&] { return measure->isVisible(); }, 12000),
                "scenario did not reach the next multi-point measurement");
        measure->click();
    }
    // Discrete inputs are checked in states 0 and 1, with manual switching
    // when no verified ISD route has been configured for the physical input.
    auto* continueStage = window.findChild<QPushButton*>("brpContinueStage");
    auto* retryStage = window.findChild<QPushButton*>("brpRetryStage");
    require(continueStage && retryStage, "operator retry/continue controls missing");
    bool retriedFailure = false;
    for (int signalPoint = 0; signalPoint < 16; ++signalPoint) {
        require(waitUntil([&] { return measure->isVisible(); }, 12000),
                "scenario did not offer a discrete input state confirmation");
        measure->click();
        require(waitUntil([&] {
            return continueStage->isVisible() || measure->isVisible() || name->isVisible();
        }, 12000), "discrete input check did not settle");
        if (continueStage->isVisible() && !retriedFailure) {
            retryStage->click();
            require(waitUntil([&] { return measure->isVisible(); }, 12000),
                    "retry did not restart only the failed discrete point");
            measure->click();
            require(waitUntil([&] {
                return continueStage->isVisible() || measure->isVisible() || name->isVisible();
            }, 12000), "retried point did not reach a verdict");
            retriedFailure = true;
        }
        if (continueStage->isVisible()) continueStage->click();
    }
    require(retriedFailure, "no failed point was offered for retry");
    require(waitUntil([&] { return name->isVisible(); }, 12000),
            "scenario did not finish after discrete checks");

    const auto completeHtml = exportReport();
    const auto base = completeHtml.chopped(5);
    require(rows->rowCount() == 30 && stageResult(2) == QStringLiteral("НОРМА")
        && stageResult(3) == QStringLiteral("НЕПОЛНАЯ")
        && stageResult(4) == QStringLiteral("НОРМА · ПРИНУД.")
        && stageResult(5) == QStringLiteral("НЕПОЛНАЯ"),
        "report summary did not preserve the forced verdict");
    const auto runId = QFileInfo(completeHtml).completeBaseName().mid(3); // TU_<run-id>.html
    const auto checkpoint = readText(reports.path() + QStringLiteral("/runs/") + runId + QStringLiteral(".json"));
    require(checkpoint.contains(QStringLiteral("\"forcedOk\": true"))
        && checkpoint.contains(QStringLiteral("\"attempt_1\"")) == false
        && checkpoint.contains(QStringLiteral(".attempt_1"))
        && checkpoint.contains(QStringLiteral(".attempt_2"))
        && checkpoint.contains(QStringLiteral("OPERATOR_RETRY"))
        && checkpoint.contains(QStringLiteral("OPERATOR_CONTINUE"))
        && checkpoint.contains(QStringLiteral("\"finished_ms\":")),
        "durable checkpoint lost a forced verdict or attempt history");
    const auto powerOn = checkpoint.indexOf(QStringLiteral("POWER_ON"));
    const auto powerOff = checkpoint.indexOf(QStringLiteral("POWER_OFF"));
    require(powerOn >= 0 && powerOff > powerOn, "completed run lost its power lifecycle");
    const auto html = readText(completeHtml);
    const auto txt = readText(base + QStringLiteral(".txt"));
    const auto csv = readText(base + QStringLiteral(".csv"));
    require(html.contains(QStringLiteral("@page{size:A4;margin:16mm}"))
        && html.contains(QStringLiteral("A &lt; B; &quot;C&quot;"))
        && txt.contains(QStringLiteral("РЕЗУЛЬТАТЫ КАДРОВЫХ ПРОВЕРОК БРП №BRP-01"))
        && txt.contains(QStringLiteral("Имитация")) && txt.contains(QStringLiteral("ПРИНУДИТЕЛЬНЫЙ ОК"))
        && html.contains(QStringLiteral("M16P1A10B60C31T01"))
        && txt.contains(QStringLiteral("M16P1A10B60C31T01"))
        && csv.contains(QStringLiteral("v7_before_v=20.020"))
        && csv.contains(QStringLiteral("ПРИНУДИТЕЛЬНЫЙ ОК"))
        && csv.contains(QStringLiteral(".attempt_2"))
        && csv.contains(QStringLiteral("Итог прогона"))
        && csv.contains(QStringLiteral("M16P1A10B60C31T01")), "reference report lost the BRP results or print layout");

    newCheck();
    selectInterface(2); // Оставляем диагностику M8 выбранной и запускаем ТУ M16.
    check->click();
    require(start->isVisible() && readiness->text() == QStringLiteral("СТЕНД ГОТОВ"),
            "manual M8 selection blocked formal M16 acceptance");
    start->click();
    auto* runTime = window.findChild<QLabel*>("brpRunBoardTime");
    // Останавливаем именно интерфейсный этап. Ожидание первого МТВ
    // могло позволить имитатору уже подтвердить два периода счётчика.
    require(runTime && runTime->text().contains(QStringLiteral("ожидание маркера M16")),
            "formal route with M8 selected did not start a M16 session");
    stop->click();
    require(waitUntil([&] { return name->isVisible(); }, 20000),
            "aborted run did not finish background Orbita teardown");
    const auto cancelledHtml = exportReport();
    const auto abortedId = QFileInfo(cancelledHtml).completeBaseName().mid(3);
    const auto abortedCheckpoint = readText(reports.path() + QStringLiteral("/runs/") + abortedId + QStringLiteral(".json"));
    require(abortedCheckpoint.contains(QStringLiteral("POWER_OFF")), "operator stop lost its power cleanup");
    require(cancelledHtml != completeHtml
        && stageResult(0) == QStringLiteral("ОСТАНОВЛЕНО") && stageResult(2) == QStringLiteral("НЕ ВЫПОЛНЕНО"),
        "cancelled run left a stale result");

    newCheck();
    manual->click();
    mode->setCurrentIndex(1);
    const QTemporaryDir yasmFiles;
    chooseYasmFile(window, writeYasmFixture(yasmFiles, "different.bcfrm",
        "<FRAME TYPE='BC'><data><sequence>2211 4433</sequence></data></FRAME>"));
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
    require(waitUntil([&] {
        return simulation.findChild<QLineEdit*>("tuCompletionOperator")->isVisible();
    }, 20000), "report entry is missing after asynchronous stop");
}

void startHistoryRun(BrpTuWindow& window, const QString& serial)
{
    window.show();
    window.findChild<QLineEdit*>("tuManualSerial")->setText(serial);
    action(window, "tuStandAction")->click();
    action(window, "tuStartAction")->click();
}

void testPosVoltageSweep()
{
    using Point = BrpTuWindow::SimulatedVoltagePoint;
    const QTemporaryDir reports;
    require(reports.isValid(), "POS fixture directory was not created");
    const auto previous = qgetenv("TU_REPORT_DIR");
    qputenv("TU_REPORT_DIR", reports.path().toLocal8Bit());

    bool failCentre = true;
    BrpTuWindow::SimulatedVoltageSource sweep = [&](const std::string&, double target) -> std::optional<Point> {
        // Фиксированные коды из шкалы −6…+6 В при кодах 20…1003.
        if (target ==  5.0) return Point{921,  5.0,  5.01};
        if (std::exchange(failCentre, false)) return Point{540, 0.0, 0.01};
        return Point{512, 0.0, 0.01};
    };
    BrpTuWindow window(std::move(sweep));
    startHistoryRun(window, QStringLiteral("BRP-POS-SWEEP"));
    auto* measure = action(window, "brpPointAction");
    auto* retry = window.findChild<QPushButton*>("brpRetryStage");
    auto* stop = action(window, "tuStopAction");
    const auto saved = [&] {
        const auto files = listTuCheckpoints();
        require(files.size() == 1, "POS run did not produce exactly one checkpoint");
        const auto run = readTuCheckpoint(files.first().absoluteFilePath());
        require(run.value("serial").toString() == "BRP-POS-SWEEP", "POS checkpoint belongs to a different run");
        return run.value("steps").toArray();
    };
    const auto waitForPoint = [&] {
        require(waitUntil([&] { return measure->isVisible(); }, 20000), "POS point did not become available");
    };
    waitForPoint();
    measure->click(); // ПОС1, нулевая точка с заведомо неверным кодом.
    require(retry && waitUntil([&] { return retry->isVisible(); }), "out-of-tolerance POS point was not rejected");
    require(saved().at(3).toObject().value("verdict").toString() == "FAIL", "failed POS point was not persisted");
    retry->click();
    for (int i = 1; i < 8; ++i) {
        waitForPoint();
        measure->click();
    }
    waitForPoint(); // Следующий этап после 8 точек ПОС.
    const auto steps = saved();
    const std::array<QString, 4> addresses{
        "M16P1A10B20T01", "M16P1A10B30T01", "M16P1A10B40T01", "M16P1A10B50T01"};
    const std::array<QString, 2> codes{"512", "921"};
    for (int i = 0; i < 8; ++i) {
        const auto step = steps.at(3 + i).toObject();
        require(step.value("verdict").toString() == "OK", "confirmed POS point did not pass");
        const auto measurements = step.value("measurements").toArray();
        require(measurements.size() == 1, "POS point did not persist a single measurement");
        const auto measurement = measurements.first().toObject();
        require(measurement.value("key").toString() == addresses[i / 2], "POS result used the wrong channel address");
        const auto attributes = measurement.value("attributes").toObject();
        require(attributes.value("code_min").toString() == codes[i % 2]
            && attributes.value("code_max").toString() == codes[i % 3]
            && attributes.value("v7_before_v").toString() != attributes.value("v7_after_v").toString(),
            "POS result lost its independent V7 readings or captured code");
    }
    const auto attempts = steps.at(3).toObject().value("attempts").toArray();
    require(attempts.size() == 2 && attempts.first().toObject().value("verdict").toString() == "FAIL"
        && attempts.last().toObject().value("verdict").toString() == "OK", "successful POS retry lost the failed attempt");
    stop->click();
    require(waitUntil([&] { return window.findChild<QLineEdit*>("tuCompletionOperator")->isVisible(); }, 20000),
            "completed sweep did not stop");

    BrpTuWindow::SimulatedVoltageSource fault = [](const std::string&, double) -> std::optional<Point> {
        return Point{512, 0.0, std::nullopt};
    };
    BrpTuWindow failure(std::move(fault));
    startHistoryRun(failure, QStringLiteral("BRP-V7-FAIL"));
    auto* faultMeasure = action(failure, "brpPointAction");
    require(waitUntil([&] { return faultMeasure->isVisible(); }, 20000), "V7 fault fixture never reached capture");
    faultMeasure->click();
    require(waitUntil([&] { return failure.findChild<QLineEdit*>("tuCompletionOperator")->isVisible(); }, 20000),
            "V7 fault did not stop the scenario");
    QJsonObject faultRun;
    for (const auto& file : listTuCheckpoints()) {
        const auto run = readTuCheckpoint(file.filePath());
        if (run.value("serial").toString() == "BRP-V7-FAIL") faultRun = run;
    }
    const auto faultSteps = faultRun.value("steps").toArray();
    require(faultRun.value("verdict").toString() == "ERROR" && faultSteps.at(3).toObject().value("verdict").toString() == "ERROR"
        && faultSteps.at(3).toObject().value("message").toString().contains(QStringLiteral("Ошибка В7"))
        && faultSteps.at(4).toObject().value("verdict").toString() == "NOT_RUN",
        "V7 fault was reported as DUT failure or advanced the scenario");
    if (previous.isNull()) qunsetenv("TU_REPORT_DIR");
    else qputenv("TU_REPORT_DIR", previous);
}

void testHistoryAfterCrash()
{
    const QTemporaryDir reports;
    require(reports.isValid(), "history fixture directory was not created");
    const auto previous = qgetenv("TU_REPORT_DIR");
    qputenv("TU_REPORT_DIR", reports.path().toLocal8Bit());
    QProcess child;
    child.start(QCoreApplication::applicationFilePath(),
                {QStringLiteral("-platform"), QStringLiteral("offscreen"), QStringLiteral("--checkpoint-child")});
    require(child.waitForStarted(), "simulator child did not start");
    require(waitUntil([&] {
        const auto files = listTuCheckpoints();
        if (files.size() != 1) return false;
        const auto saved = readTuCheckpoint(files.first().filePath());
        const auto steps = saved.value(QStringLiteral("steps")).toArray();
        return !steps.isEmpty() && steps.first().toObject().value(QStringLiteral("verdict")).toString() == "OK";
    }, 20000), "child did not save a completed attempt before export");
    child.kill();
    require(child.waitForFinished() && child.exitStatus() == QProcess::CrashExit,
            "checkpoint fixture did not terminate abruptly");
    const auto savedFile = listTuCheckpoints().first().filePath();
    const auto savedBytes = readText(savedFile);
    require(readTuCheckpoint(savedFile).value(QStringLiteral("finished_ms")).isNull(),
            "killed process produced a terminal verdict");
    require(QDir(reports.path()).entryList({QStringLiteral("TU_*.html")}, QDir::Files).isEmpty(),
            "child unexpectedly exported a report");

    BrpTuWindow restarted(true);
    restarted.show();
    auto* open = restarted.findChild<QPushButton*>("brpHistoryOpen");
    require(open && open->isVisible(), "history is inaccessible after restart");
    open->click();
    auto* history = restarted.findChild<QListWidget*>("brpHistoryRuns");
    auto* detail = restarted.findChild<QPlainTextEdit*>("brpHistoryDetail");
    require(history && detail && history->count() == 1 && detail->toPlainText().contains("BRP-CRASH")
        && detail->toPlainText().contains(QStringLiteral("Прогон не завершён"))
        && detail->toPlainText().contains(QStringLiteral("Попытка 1")),
        "history did not display the interrupted run and its saved attempt");
    history->window()->close();

    startHistoryRun(restarted, QStringLiteral("BRP-NEW"));
    const auto files = listTuCheckpoints();
    require(files.size() == 2 && readText(savedFile) == savedBytes,
            "a new run overwrote the interrupted run");
    action(restarted, "tuStopAction")->click();
    require(waitUntil([&] {
        return restarted.findChild<QLineEdit*>("tuCompletionOperator")->isVisible();
    }, 20000), "restarted run did not exit");
    if (previous.isNull()) qunsetenv("TU_REPORT_DIR");
    else qputenv("TU_REPORT_DIR", previous);
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication application(argc, argv);
    try {
        if (application.arguments().contains(QStringLiteral("--checkpoint-child"))) {
            BrpTuWindow simulation(true);
            startHistoryRun(simulation, QStringLiteral("BRP-CRASH"));
            return application.exec();
        }
        testHistoryAfterCrash();
        testPosVoltageSweep();
        testIsdPreparation();
#if BRP_ENABLE_MANUAL_TESTS
        testMissingPurumk();
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
