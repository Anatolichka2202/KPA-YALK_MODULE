#include "brp_tu_window.h"

#include "history_plot.h"
#include "test_page.h"
#include "tu_flow_widget.h"
#include "frame_checks.h"
#include "encoding_utils.h"
#include "hardware/akip1160.h"
#include "hardware/isd_router.h"
#include "hardware/v7_meter.h"
#include "metadata_service.h"
#include "miltech/orbita_sample_bridge.h"
#include "miltech/orbita_sample_simulator.h"
#include "orbita_stand/sample_source.h"

#include <QComboBox>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QTimer>
#include <QUuid>
#include <QXmlStreamReader>
#include <QtConcurrentRun>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <stdexcept>

namespace {
using namespace miltech::brp;
using namespace orbita::stand;
using tu::RunVerdict;

constexpr int groupsPerSecond = 128;
constexpr int counterTimeoutMs = 5000;
constexpr int captureTimeoutMs = 3000;

YAML::Node loadStand(const QDir& root)
{
    QFile file(root.filePath(QStringLiteral("data/stand_brp.yaml")));
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("Профиль стенда БРП не открыт");
    const auto profile = YAML::Load(file.readAll().toStdString());
    if (profile["schema"].as<int>() != 1) throw std::runtime_error("Неверная схема профиля стенда БРП");
    return profile;
}

struct MeterReading {
    double volts = 0;
    QString error;
};

tu::hardware::V7Config loadV7(const QDir& root)
{
    const auto v7 = loadStand(root)["v7"];
    const tu::hardware::V7Config config{
        {v7["resources"].as<std::vector<std::string>>(), v7["timeout_ms"].as<unsigned>()},
        v7["voltage_command"].as<std::string>()};
    if (config.visa.resources.empty() || !config.visa.timeoutMilliseconds || config.dcVoltageCommand.empty())
        throw std::runtime_error("Не настроены адрес и команда В7");
    return config;
}

MeterReading readMeter(const QDir& root)
{
    try {
        return {tu::hardware::readV7Voltage(loadV7(root)), {}};
    } catch (const std::exception& error) {
        return {0, QString::fromUtf8(error.what())};
    }
}

struct IsdPreparation {
    unsigned acknowledged = 0;
    QString error;
};

IsdPreparation prepareIsd(const QDir& root, const QString& host)
{
    try {
        const auto baseline = loadStand(root)["isd_baseline"];
        tu::hardware::IsdRouter isd({host.toStdString()});
        return {isd.prepareYagr(baseline["type2_last"].as<unsigned>(),
                    baseline["type3_last"].as<unsigned>(), baseline["dac_last"].as<unsigned>(),
                    baseline["command_gap_ms"].as<unsigned>()), {}};
    } catch (const std::exception& error) {
        return {0, QString::fromUtf8(error.what())};
    }
}

std::vector<uint16_t> readYasmFixture(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("Файл теста ЯСМ не открыт");
    QXmlStreamReader xml(&file);
    if (!xml.readNextStartElement() || xml.name() != QStringLiteral("FRAME")
        || xml.attributes().value(QStringLiteral("TYPE")) != QStringLiteral("BC"))
        throw std::runtime_error("Ожидается файл .bcfrm с кадром контроллера МКО");

    std::vector<uint16_t> words;
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement() || xml.name() != QStringLiteral("data")) continue;
        while (xml.readNextStartElement()) {
            if (xml.name() != QStringLiteral("sequence")) {
                xml.skipCurrentElement();
                continue;
            }
            for (const auto& token : xml.readElementText().split(
                     QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts)) {
                bool ok = false;
                const auto word = token.toUInt(&ok, 16);
                if (!ok || word > 0xFFFF || words.size() == groupWords / 2)
                    throw std::runtime_error("Некорректные слова данных в файле .bcfrm");
                words.push_back(static_cast<uint16_t>(word));
            }
        }
    }
    if (xml.hasError() || words.empty()) throw std::runtime_error("В файле .bcfrm отсутствует корректная последовательность данных");
    return words;
}

QString readEquipment(QString host, QString akipPort)
{
    QStringList results;
    if (!host.isEmpty()) {
        try {
            tu::hardware::IsdRouter isd({host.toStdString()});
            isd.probe();
            results << QStringLiteral("ИСД ответил по HTTP. Состояние реле не проверялось.");
        } catch (const std::exception& error) {
            results << QStringLiteral("ИСД: %1").arg(QString::fromUtf8(error.what()));
        }
    }
    if (!akipPort.isEmpty()) {
        try {
            tu::hardware::Akip1160 akip({akipPort.toStdString()});
            const auto identity = QString::fromStdString(akip.probe());
            const auto state = akip.readState();
            results << QStringLiteral("АКИП: %1, выход %2, измерено %3 В / %4 А")
                .arg(identity, state.outputEnabled ? QStringLiteral("включён")
                                                   : QStringLiteral("выключен"))
                .arg(state.measuredVoltageV, 0, 'f', 3)
                .arg(state.measuredCurrentA, 0, 'f', 3);
        } catch (const std::exception& error) {
            results << QStringLiteral("АКИП: %1").arg(QString::fromUtf8(error.what()));
        }
    }
    return results.join(QLatin1Char('\n'));
}
} // namespace

struct BrpTuWindow::Impl {
    Impl(BrpTuWindow& window, bool simulation)
        : window(window), simulation(simulation), root(QCoreApplication::applicationDirPath())
    {
        window.setWindowTitle(QStringLiteral("БРП · проверка ТУ"));
        auto* rootLayout = new QVBoxLayout(&window);
        rootLayout->setContentsMargins(0, 0, 0, 0);
        page = new TestPage(&window);
        rootLayout->addWidget(page);
        auto* flow = page->flow();
        QObject::connect(flow, &TuFlowWidget::readinessRequested, &window,
            [this](const QString& serial) { checkStand(serial); });
        QObject::connect(flow, &TuFlowWidget::startRequested, &window,
            [this](const QString& serial) { startScenario(serial); });
        QObject::connect(page, &TestPage::pointReady, &window, [this] { measureStage(); });
        QObject::connect(page, &TestPage::skipCurrentStepRequested, &window, [this] {
            if (pointCanStart())
                completeStage(tu::RunVerdict::Incomplete, QStringLiteral("Пропущено оператором."), {}, true);
        });
        QObject::connect(page, &TestPage::stopRequested, &window, [this] {
            if (currentStage < 0) return;
            const auto message = preparingIsd
                ? QStringLiteral("Испытание остановлено. ИСД завершает начатую последовательность OFF.")
                : QStringLiteral("Испытание остановлено оператором.");
            endSession(message, tu::RunVerdict::Aborted);
        });

        auto* manual = page->manualPage();
        manual->setObjectName(QStringLiteral("brpManualPage"));
        manual->setStyleSheet(QStringLiteral(R"QSS(
#brpManualPage QGroupBox { background:#102333; border:1px solid #264257; border-radius:8px;
    margin-top:14px; padding-top:4px; font-weight:600; }
#brpManualPage QGroupBox::title { subcontrol-origin:margin; left:13px; padding:0 5px; color:#c5d8e7; }
#brpManualPage QScrollArea { border:0; background:#08131d; }
#brpManualBody { background:#08131d; }
#brpManualPage QComboBox QAbstractItemView { background:#102333; color:#eaf4fb;
    selection-background-color:#245379; selection-color:#fff; border:1px solid #264257; }
#brpManualPage QPlainTextEdit { background:#0a1722; border:1px solid #264257;
    border-radius:6px; padding:6px; font-family:Consolas; }
#brpManualPage QLineEdit:focus, #brpManualPage QComboBox:focus { border-color:#58a5ff; }
#brpManualPage QPushButton[accent="true"]:enabled { background:#1b5086; border-color:#427cb5; }
#brpManualPage QPushButton[accent="true"]:hover:enabled { background:#23619b; }
#brpManualPage QPushButton[stop="true"]:enabled { background:#3a1d24; border-color:#7e3340; }
)QSS"));
        auto* outer = new QVBoxLayout(manual);
        outer->setContentsMargins(18, 14, 18, 18);
        outer->setSpacing(10);
        auto* header = new QHBoxLayout;
        auto* back = new QPushButton(QStringLiteral("← К проверке ТУ"), manual);
        back->setObjectName(QStringLiteral("brpManualBack"));
        header->addWidget(back);
        auto* manualTitle = new QLabel(QStringLiteral("Ручное управление БРП"), manual);
        QFont manualFont = manualTitle->font();
        manualFont.setPointSize(17);
        manualFont.setBold(true);
        manualTitle->setFont(manualFont);
        header->addWidget(manualTitle);
        header->addStretch();
        if (simulation) {
            auto* modeLabel = new QLabel(QStringLiteral("Имитация"), manual);
            modeLabel->setProperty("muted", true);
            header->addWidget(modeLabel);
        }
        outer->addLayout(header);

        auto* scroll = new QScrollArea(manual);
        scroll->setWidgetResizable(true);
        auto* body = new QWidget(scroll);
        body->setObjectName(QStringLiteral("brpManualBody"));
        body->setAttribute(Qt::WA_StyledBackground);
        scroll->setWidget(body);
        outer->addWidget(scroll, 1);
        auto* columns = new QHBoxLayout(body);
        columns->setContentsMargins(0, 0, 0, 0);
        columns->setSpacing(14);
        auto* side = new QVBoxLayout;
        side->setSpacing(10);
        QObject::connect(back, &QPushButton::clicked, &window, [this] {
            if (bridge) endSession(QStringLiteral("Сбор остановлен при выходе из ручного режима."));
            page->showFlow();
        });

        auto* interfaceBox = new QGroupBox(QStringLiteral("Интерфейс и монитор Орбита"), manual);
        auto* interfaceLayout = new QVBoxLayout(interfaceBox);
        auto* interfaceControls = new QHBoxLayout;
        interfaceMode = new QComboBox(interfaceBox);
        interfaceMode->setObjectName(QStringLiteral("brpInterfaceMode"));
        interfaceMode->addItems({QStringLiteral("RS-485 · счётчик"), QStringLiteral("ЯСМ · внешний тест"),
                                 QStringLiteral("M8 · приём кадра")});
        port = new QComboBox(interfaceBox);
        port->setEditable(true);
        port->setMinimumContentsLength(14);
        port->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        port->lineEdit()->setPlaceholderText(QStringLiteral("COM-порт RS-485"));
        for (const auto& entry : QSerialPortInfo::availablePorts()) port->addItem(entry.portName());
        port->setCurrentIndex(-1);
        port->setVisible(!simulation);
        start = new QPushButton(QStringLiteral("Проверить интерфейс"), interfaceBox);
        start->setObjectName(QStringLiteral("brpInterfaceStart"));
        start->setProperty("accent", true);
        stop = new QPushButton(QStringLiteral("Остановить сбор"), interfaceBox);
        stop->setObjectName(QStringLiteral("brpInterfaceStop"));
        stop->setProperty("stop", true);
        stop->setEnabled(false);
        interfaceControls->addWidget(interfaceMode, 1);
        interfaceControls->addWidget(port, 1);
        interfaceLayout->addLayout(interfaceControls);
        yasmCodes = new QLineEdit(interfaceBox);
        yasmCodes->setObjectName(QStringLiteral("brpYasmCodes"));
        yasmCodes->setPlaceholderText(QStringLiteral("Коды M16 из внешнего теста ЯСМ, HEX через пробел"));
        yasmCodes->setVisible(false);
        yasmFile = new QPushButton(QStringLiteral("Открыть тест .bcfrm"), interfaceBox);
        yasmFile->setVisible(false);
        if (simulation) {
            QStringList example;
            for (auto code : simulatedYasmPattern) example.append(QString::number(code, 16));
            yasmCodes->setText(example.join(QLatin1Char(' ')));
        }
        auto* yasmControls = new QHBoxLayout;
        yasmControls->addWidget(yasmCodes, 1);
        yasmControls->addWidget(yasmFile);
        interfaceLayout->addLayout(yasmControls);
        m8Addresses = new QLineEdit(interfaceBox);
        m8Addresses->setObjectName(QStringLiteral("brpM8Addresses"));
        m8Addresses->setPlaceholderText(QStringLiteral("Адреса M08… через пробел"));
        if (simulation) m8Addresses->setText(QStringLiteral("M08A10T01 M08A20T01"));
        m8Invert = new QCheckBox(QStringLiteral("Инверсия сигнала"), interfaceBox);
        m8Addresses->hide();
        m8Invert->hide();
        auto* m8Controls = new QHBoxLayout;
        m8Controls->addWidget(m8Addresses, 1);
        m8Controls->addWidget(m8Invert);
        interfaceLayout->addLayout(m8Controls);
        auto* interfaceActions = new QHBoxLayout;
        interfaceActions->addWidget(start);
        interfaceActions->addWidget(stop);
        interfaceActions->addStretch();
        interfaceLayout->addLayout(interfaceActions);
        QObject::connect(yasmCodes, &QLineEdit::textEdited, &window, [this] { yasmFixture.clear(); });
        QObject::connect(yasmFile, &QPushButton::clicked, &window, [this] {
            const auto path = QFileDialog::getOpenFileName(&this->window, QStringLiteral("Тест ЯСМ"),
                {}, QStringLiteral("Кадр контроллера МКО (*.bcfrm)"));
            if (path.isEmpty()) return;
            try {
                auto words = readYasmFixture(path);
                (void)YasmCheck::fromBusWords(words);
                yasmFixture = std::move(words);
                yasmCodes->setText(QFileInfo(path).fileName());
                log->appendPlainText(QStringLiteral("ЯСМ: загружено %1 слов эталонных данных.").arg(yasmFixture.size()));
            } catch (const std::exception& error) {
                log->appendPlainText(QStringLiteral("Файл ЯСМ: %1").arg(QString::fromUtf8(error.what())));
            }
        });

        status = new QLabel(simulation
            ? QStringLiteral("Синтетические кадры M16. COM-порт и стендовые приборы отключены.")
            : QStringLiteral("Подготовьте питание и подключите RS-485 и вход декодера Орбита."), interfaceBox);
        status->setWordWrap(true);
        status->setTextFormat(Qt::PlainText);
        status->setTextInteractionFlags(Qt::TextSelectableByMouse);
        status->setProperty("muted", true);
        status->setObjectName(QStringLiteral("brpInterfaceStatus"));
        interfaceLayout->addWidget(status);
        graph = new brp::ui::HistoryPlot(interfaceBox);
        graph->configure(QStringLiteral("Счётчик RS-485"), QStringLiteral("код"), QStringLiteral("Орбита M16"));
        interfaceLayout->addWidget(graph, 1);
        interfaceLayout->addStretch();
        QObject::connect(interfaceMode, &QComboBox::currentIndexChanged, &window, [this, columns, interfaceBox](int index) {
            port->setVisible(!this->simulation && index == 0);
            yasmCodes->setVisible(index == 1);
            yasmFile->setVisible(index == 1);
            graph->setVisible(index == 0);
            columns->setAlignment(interfaceBox, index == 0 ? Qt::Alignment{} : Qt::AlignTop);
            m8Addresses->setVisible(index == 2);
            m8Invert->setVisible(index == 2);
            checksPanel->setVisible(index != 2);
            start->setText(index == 2 ? QStringLiteral("Начать приём M8")
                                     : QStringLiteral("Проверить интерфейс"));
            if (index == 2)
                status->setText(QStringLiteral("Приём M8 через вход АЦП. Укажите адреса M08."));
            else
                status->setText(QStringLiteral("Готово к запуску проверки M16."));
            updateScenario();
        });
        columns->addWidget(interfaceBox, 5);
        columns->addLayout(side, 4);

        auto* checksBox = new QGroupBox(QStringLiteral("Кадровый контроль · раздел 3.2"), manual);
        checksPanel = checksBox;
        auto* checksLayout = new QFormLayout(checksBox);
        checksLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        checkList = new QComboBox(checksBox);
        checkList->setObjectName(QStringLiteral("brpCheckList"));
        channelList = new QComboBox(checksBox);
        for (auto* combo : {checkList, channelList}) {
            combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            combo->setMinimumContentsLength(16);
            QObject::connect(combo, &QComboBox::currentTextChanged, combo, &QWidget::setToolTip);
        }
        details = new QLabel(checksBox);
        details->setWordWrap(true);
        details->setProperty("muted", true);
        expectedState = new QComboBox(checksBox);
        expectedState->addItems({QStringLiteral("Выбрать состояние"), QStringLiteral("0"), QStringLiteral("1")});
        capture = new QPushButton(QStringLiteral("Сравнить с В7"), checksBox);
        capture->setObjectName(QStringLiteral("brpCapture"));
        capture->setProperty("accent", true);
        capture->setEnabled(false);
        checksLayout->addRow(QStringLiteral("Строка таблицы 1"), checkList);
        checksLayout->addRow(QStringLiteral("Адрес Орбита"), channelList);
        checksLayout->addRow(details);
        checksLayout->addRow(QStringLiteral("Ожидаемый бит"), expectedState);
        checksLayout->setRowVisible(expectedState, false);
        checkForm = checksLayout;
        checksLayout->addRow(capture);
        side->addWidget(checksBox);

        if (!simulation) {
            auto* instruments = new QGroupBox(QStringLiteral("Оборудование стенда"), manual);
            auto* instrumentsLayout = new QFormLayout(instruments);
            instrumentsLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
            isdHost = new QLineEdit(instruments);
            auto* akipPort = new QLineEdit(instruments);
            isdHost->setPlaceholderText(QStringLiteral("Адрес ИСД"));
            akipPort->setPlaceholderText(QStringLiteral("COM-порт АКИП"));
            auto* probe = new QPushButton(QStringLiteral("Проверить ИСД и АКИП"), instruments);
            prepare = new QPushButton(QStringLiteral("Подготовить ИСД · ЦАП OFF"), instruments);
            instrumentsLayout->addRow(QStringLiteral("ИСД"), isdHost);
            instrumentsLayout->addRow(QStringLiteral("АКИП"), akipPort);
            instrumentsLayout->addRow(probe);
            instrumentsLayout->addRow(prepare);
            side->addWidget(instruments);
            QObject::connect(isdHost, &QLineEdit::textChanged, &window, [this] {
                isdReady = false;
                updateCaptureControls();
            });
            QObject::connect(prepare, &QPushButton::clicked, &window, [this] { startIsdPreparation(); });
            QObject::connect(probe, &QPushButton::clicked, &window, [this, probe, akipPort] {
                if (preparingIsd || pointActive) return;
                const auto host = isdHost->text().trimmed();
                const auto supplyPort = akipPort->text().trimmed();
                if (host.isEmpty() && supplyPort.isEmpty()) return;
                if (!supplyPort.isEmpty() && supplyPort.compare(port->currentText().trimmed(), Qt::CaseInsensitive) == 0) {
                    log->appendPlainText(QStringLiteral("АКИП и RS-485 требуют разных COM-портов."));
                    return;
                }
                probing = true;
                probe->setEnabled(false);
                updateCaptureControls();
                auto* watcher = new QFutureWatcher<QString>(&this->window);
                QObject::connect(watcher, &QFutureWatcherBase::finished, &this->window, [this, watcher, probe] {
                    log->appendPlainText(watcher->result());
                    watcher->deleteLater();
                    probe->setEnabled(true);
                    probing = false;
                    updateCaptureControls();
                });
                watcher->setFuture(QtConcurrent::run(readEquipment, host, supplyPort));
            });
        }

        log = new QPlainTextEdit(&window);
        log->setObjectName(QStringLiteral("brpLog"));
        log->setReadOnly(true);
        log->setMaximumBlockCount(300);
        log->setMinimumHeight(110);
        log->setPlaceholderText(QStringLiteral("Здесь появятся результаты ручных проверок."));
        auto* journal = new QGroupBox(QStringLiteral("Журнал стенда"), manual);
        auto* journalLayout = new QVBoxLayout(journal);
        journalLayout->addWidget(log);
        side->addWidget(journal, 1);

        QObject::connect(start, &QPushButton::clicked, &window, [this] { startInterface(); });
        QObject::connect(stop, &QPushButton::clicked, &window, [this] {
            endSession(QStringLiteral("Сбор остановлен оператором"));
        });
        QObject::connect(capture, &QPushButton::clicked, &window, [this] { startCapture(); });
        QObject::connect(checkList, &QComboBox::currentIndexChanged, &window,
            [this](int index) { displayCheck(index); });
        QObject::connect(expectedState, &QComboBox::currentIndexChanged, &window,
            [this] { updateCaptureControls(); });
        QObject::connect(&refresh, &QTimer::timeout, &window, [this] { update(); });
        QObject::connect(&send, &QTimer::timeout, &window, [this] { writeCounter(); });

        try {
            QFile config(root.filePath(QStringLiteral("data/frame_checks.yaml")));
            if (!config.open(QIODevice::ReadOnly)) throw std::runtime_error("Файл кадровых проверок БРП не открыт");
            checks = loadFrameChecks(config.readAll().toStdString());

            const auto dbPath = root.filePath(QStringLiteral("data/parameters.db"));
            if (!QFileInfo::exists(dbPath) || !metadata.open(dbPath))
                log->appendPlainText(QStringLiteral("База параметров Орбита недоступна. Показаны адреса из списка БРП."));
            for (const auto& check : checks) {
                checkList->addItem(QStringLiteral("%1. %2 · %3")
                    .arg(check.number).arg(QString::fromStdString(check.title),
                                           QString::fromStdString(check.cell)));
            }
        } catch (const std::exception& error) {
            start->setEnabled(false);
            page->flow()->setScenarioAvailable(false, QString::fromUtf8(error.what()));
            log->appendPlainText(QString::fromUtf8(error.what()));
        }
        updateScenario();
    }

    ~Impl() { shutdown(); }

    void checkStand(const QString& serialNumber)
    {
        auto* flow = page->flow();
        const auto connection = QStringLiteral("Подключение");
        const auto isd = QStringLiteral("ИСД");
        const auto meter = QStringLiteral("В7");
        flow->beginStandCheck(serialNumber, {connection, isd, meter});
        QString error;
        const int mode = interfaceMode->currentIndex();
        if (mode == 2)
            error = QStringLiteral("Выберите RS-485 или ЯСМ в ручном управлении.");
        else if (!simulation && mode == 0 && port->currentText().trimmed().isEmpty())
            error = QStringLiteral("Укажите COM-порт RS-485 в ручном управлении.");
        else if (mode == 1 && yasmFixture.empty() && yasmCodes->text().trimmed().isEmpty())
            error = QStringLiteral("Загрузите тест ЯСМ или укажите ожидаемые коды.");
        else if (!simulation && isdHost->text().trimmed().isEmpty())
            error = QStringLiteral("Укажите адрес ИСД в ручном управлении.");
        else if (bridge || preparingIsd || probing || meterBusy)
            error = QStringLiteral("Дождитесь завершения обращения к оборудованию.");
        if (!error.isEmpty()) {
            flow->setEquipmentStatus(connection, false, error);
            return;
        }
        flow->setEquipmentStatus(connection, true);
        if (simulation) {
            flow->setEquipmentStatus(isd, true);
            flow->setEquipmentStatus(meter, true);
            return;
        }

        // Проверка доступности только читает приборы. Адресные команды OFF
        // посылаются после проверки интерфейса в самом испытании.
        const auto host = isdHost->text().trimmed();
        using Readiness = std::pair<QString, QString>;
        probing = true;
        updateCaptureControls();
        auto* watcher = new QFutureWatcher<Readiness>(&window);
        QObject::connect(watcher, &QFutureWatcherBase::finished, &window, [this, watcher, isd, meter] {
            const auto result = watcher->result();
            watcher->deleteLater();
            probing = false;
            page->flow()->setEquipmentStatus(isd, result.first.isEmpty(), result.first);
            page->flow()->setEquipmentStatus(meter, result.second.isEmpty(), result.second);
            updateCaptureControls();
        });
        watcher->setFuture(QtConcurrent::run([host, root = root] {
            Readiness result;
            try {
                tu::hardware::IsdRouter router({host.toStdString()});
                router.probe();
            } catch (const std::exception& exception) {
                result.first = QString::fromUtf8(exception.what());
            }
            try {
                tu::hardware::VisaInstrument meter(loadV7(root).visa);
                const auto identity = meter.query("*IDN?");
                if (identity.find_first_not_of(" \t\r\n") == std::string::npos)
                    throw std::runtime_error("Пустой ответ на *IDN?");
            } catch (const std::exception& exception) {
                result.second = QString::fromUtf8(exception.what());
            }
            return result;
        }));
    }

    void startScenario(const QString& serialNumber)
    {
        if (currentStage >= 0 || bridge || preparingIsd || probing || meterBusy) return;
        const bool yasmMode = interfaceMode->currentIndex() == 1;
        const int interfaceRow = yasmMode ? 13 : 12;
        const auto input = std::find_if(checks.begin(), checks.end(), [interfaceRow](const auto& check) {
            return check.number == interfaceRow;
        });
        if (input == checks.end()) {
            page->flow()->setEquipmentStatus(QStringLiteral("Подключение"), false,
                QStringLiteral("В настройках отсутствует строка %1.").arg(interfaceRow));
            return;
        }

        run = {};
        run.objectSerial = serialNumber.toStdString();
        run.scenarioId = simulation ? "brp.m16.sim" : "brp.m16";
        run.scenarioTitle = simulation ? "Имитация · кадровые проверки" : "Кадровые проверки";
        run.startedAt = std::chrono::system_clock::now();
        run.runId = (QStringLiteral("BRP_") + (simulation ? QStringLiteral("SIM_") : QString())
            + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz_"))
            + QUuid::createUuid().toString(QUuid::Id128).left(8)).toStdString();
        run.steps.push_back({yasmMode ? "yasm" : "rs485",
            yasmMode ? "ЯСМ · сверка внешнего теста" : "RS-485 · счётчик 0–1023", input->requirement});
        run.steps.push_back({"isd_off", "Сброс ИСД · ЦАП OFF", {}});
        voltagePoints.clear();
        for (std::size_t index = 0; index < checks.size(); ++index) {
            const auto& check = checks[index];
            if (check.voltagePointRequirement.empty()) continue;
            for (std::size_t channel = 0; channel < check.addresses.size(); ++channel) {
                const auto& name = check.channelNames.empty() ? check.title : check.channelNames[channel];
                run.steps.push_back({
                    QStringLiteral("row_%1_%2").arg(check.number).arg(channel + 1).toStdString(),
                    QStringLiteral("Строка %1 · %2 · одна точка с В7").arg(check.number)
                        .arg(QString::fromStdString(name)).toStdString(),
                    check.voltagePointRequirement});
                voltagePoints.emplace_back(index, channel);
            }
        }

        currentStage = 0;
        waitingForPoint = false;
        page->beginRun(run);
        page->setStage(0, QString::fromStdString(run.steps.front().title),
            yasmMode ? QStringLiteral("Последовательность ЯСМ в M16P1A22")
                     : QStringLiteral("Счётчик RS-485 · два периода 0–1023"),
            QStringLiteral("код"), QStringLiteral("Орбита M16"));
        setProgress(QStringLiteral("Ожидание тестовой последовательности в кадре M16."));
        startInterface();
    }

    bool pointCanStart() const
    {
        return waitingForPoint && gatePassed && (simulation || isdReady)
            && !pointActive && !meterBusy && !preparingIsd && !probing;
    }

    void setProgress(const QString& detail)
    {
        stageProgress = detail;
        updateScenario();
    }

    void updateScenario()
    {
        if (currentStage >= 0) page->setProgress(stageProgress, pointCanStart());
    }

    void recordStage(RunVerdict result, const QString& detail,
                     std::vector<tu::MeasurementResult> measurements = {}, bool skipped = false)
    {
        auto& step = run.steps[currentStage];
        step.verdict = result;
        step.message = detail.toStdString();
        step.measurements = std::move(measurements);
        step.operatorSkipped = skipped;
        page->setStageResult(currentStage, step);
        page->appendRunEvent({std::chrono::system_clock::now(), step.nodeId, "FINISH", step.message, result});
    }

    void completeStage(RunVerdict result, const QString& detail,
                       std::vector<tu::MeasurementResult> measurements = {}, bool skipped = false)
    {
        if (currentStage < 2 && result != RunVerdict::Ok) {
            endSession(detail, result);
            return;
        }
        recordStage(result, detail, std::move(measurements), skipped);
        ++currentStage;
        if (static_cast<std::size_t>(currentStage) == run.steps.size()) {
            currentStage = -1;
            endSession(QStringLiteral("Сбор кадров завершён."));
            finishScenario();
        } else if (currentStage == 1) {
            page->setStage(currentStage, QString::fromStdString(run.steps[currentStage].title), {});
            setProgress(QStringLiteral("Ожидание подтверждений OFF реле и ЦАП."));
            if (simulation)
                completeStage(RunVerdict::Ok, QStringLiteral("Имитация подготовки ИСД."));
            else
                startIsdPreparation();
        } else {
            const auto [check, channel] = voltagePoints[currentStage - 2];
            const auto address = QString::fromStdString(checks[check].addresses[channel]);
            page->setStage(currentStage, QString::fromStdString(run.steps[currentStage].title),
                address, QStringLiteral("В"), QStringLiteral("ЯГР мин., В"), QStringLiteral("ЯГР макс., В"));
            waitingForPoint = true;
            setProgress(QStringLiteral("Подайте стабильное напряжение и подключите В7 к той же точке."));
        }
    }

    void measureStage()
    {
        if (!pointCanStart()) return;
        const auto [check, channel] = voltagePoints[currentStage - 2];
        waitingForPoint = false;
        setProgress(QStringLiteral("Считываются В7 и кадр M16."));
        beginPoint(check, channel, 0);
    }

    void finishScenario()
    {
        currentStage = -1;
        waitingForPoint = false;
        run.finishedAt = std::chrono::system_clock::now();
        run.verdict = RunVerdict::Ok;
        for (const auto& step : run.steps)
            run.verdict = tu::combineVerdicts(run.verdict,
                step.verdict == RunVerdict::NotRun ? RunVerdict::Incomplete : step.verdict);
        page->finishRun(run);
    }

    void updateCaptureControls()
    {
        const int index = checkList->currentIndex();
        bool ready = false;
        if (index >= 0 && channelList->currentIndex() >= 0) {
            const auto& check = checks.at(index);
            ready = check.bits != 1 || expectedState->currentIndex() > 0;
        }
        capture->setEnabled(ready && gatePassed && (simulation || isdReady)
            && !pointActive && !meterBusy && !preparingIsd && !probing);
        capture->setToolTip(!simulation && gatePassed && !isdReady
            ? QStringLiteral("Перед измерениями подтвердите адресные OFF ИСД и ЦАП.")
            : QString());
        if (prepare) {
            prepare->setEnabled(!preparingIsd && !pointActive && !probing
                && !isdHost->text().trimmed().isEmpty());
            prepare->setText(isdReady ? QStringLiteral("ИСД подготовлен · повторить")
                                      : QStringLiteral("Подготовить ИСД · ЦАП OFF"));
            isdHost->setEnabled(!preparingIsd && !pointActive && !probing);
        }
        updateScenario();
    }

    void startIsdPreparation()
    {
        const auto host = isdHost->text().trimmed();
        if (preparingIsd || pointActive || probing || host.isEmpty()) return;
        isdReady = false;
        preparingIsd = true;
        updateCaptureControls();
        log->appendPlainText(QStringLiteral("ИСД: выполняются адресные OFF для реле и ЦАП."));
        const auto session = interfaceSessionId;
        auto* watcher = new QFutureWatcher<IsdPreparation>(&window);
        QObject::connect(watcher, &QFutureWatcherBase::finished, &window, [this, watcher, session] {
            const auto result = watcher->result();
            watcher->deleteLater();
            preparingIsd = false;
            const bool confirmed = result.error.isEmpty() && result.acknowledged > 0;
            isdReady = session == interfaceSessionId && confirmed;
            const auto message = confirmed
                ? QStringLiteral("ИСД: подтверждены %1 адресных OFF, включая ЦАП.").arg(result.acknowledged)
                : QStringLiteral("ИСД не подготовлен. %1").arg(result.error);
            log->appendPlainText(message);
            if (session == interfaceSessionId && currentStage == 1)
                completeStage(confirmed ? RunVerdict::Ok : RunVerdict::Error, message);
            updateCaptureControls();
        });
        watcher->setFuture(QtConcurrent::run(prepareIsd, root, host));
    }

    void displayCheck(int index)
    {
        channelList->clear();
        if (index < 0) return;
        const auto& check = checks.at(index);
        for (std::size_t i = 0; i < check.addresses.size(); ++i) {
            const auto address = QString::fromStdString(check.addresses[i]);
            const auto name = metadata.name(address);
            const auto rate = groupWords / check.layouts[i].stride * groupsPerSecond;
            auto label = QStringLiteral("%1 · %2 слотов/с").arg(address).arg(rate);
            if (!check.channelNames.empty())
                label.prepend(QString::fromStdString(check.channelNames[i]) + QStringLiteral(" · "));
            else if (!name.isEmpty()) label += QStringLiteral(" · БД: %1").arg(name);
            channelList->addItem(label);
        }
        details->setText(QStringLiteral("%1. %2").arg(QString::fromStdString(check.requirement),
                                                         QString::fromStdString(check.note)));
        const bool discrete = check.bits == 1;
        checkForm->setRowVisible(expectedState, discrete);
        expectedState->setCurrentIndex(simulation && discrete ? 1 : 0);
        capture->setText(discrete ? QStringLiteral("Проверить состояние в кадре")
            : check.scale ? QStringLiteral("Сравнить с В7") : QStringLiteral("Считать коды из кадра"));
        updateCaptureControls();
    }

    void startInterface()
    {
        try {
            const bool yasmMode = interfaceMode->currentIndex() == 1;
            const bool m8Mode = interfaceMode->currentIndex() == 2;
            double sampleRate = 10'000'000;
            if (yasmMode && !yasmFixture.empty()) {
                yasm.emplace(YasmCheck::fromBusWords(yasmFixture));
            } else if (yasmMode) {
                std::vector<unsigned> expected;
                for (const auto& item : yasmCodes->text().split(QRegularExpression(QStringLiteral("\\s+")),
                                                              Qt::SkipEmptyParts)) {
                    bool ok = false;
                    const auto value = item.toUInt(&ok, 16);
                    if (!ok) throw std::runtime_error("Коды внешнего теста ЯСМ задаются в HEX");
                    expected.push_back(value);
                }
                yasm.emplace(std::move(expected));
            } else if (!simulation && !m8Mode) {
                const auto portName = port->currentText().trimmed();
                if (portName.isEmpty()) throw std::runtime_error("Выберите COM-порт RS-485");
                serial.setPortName(portName);
                if (!serial.setBaudRate(counterBaud) || !serial.setDataBits(QSerialPort::Data8)
                    || !serial.setParity(QSerialPort::NoParity) || !serial.setStopBits(QSerialPort::OneStop)
                    || !serial.setFlowControl(QSerialPort::NoFlowControl)
                    || !serial.open(QIODevice::WriteOnly))
                    throw std::runtime_error(serial.errorString().toStdString());
            }
            if (simulation) {
                if (m8Mode) {
                    source = miltech::integration::createOrbitaM8SampleSimulator([](std::size_t word) {
                        return static_cast<uint16_t>(((word / 1024 + word % 1024) & 1023) << 1);
                    });
                } else {
                    source = miltech::integration::createOrbitaSampleSimulator(simulatedWord);
                }
                if (!source->open()) throw std::runtime_error("Имитатор Орбита не открылся");
            } else {
                const auto config = loadStand(root);
                if (!config["e2010"].IsMap())
                    throw std::runtime_error("Неверная схема профиля E20-10");
                if (m8Mode) sampleRate = config["e2010"]["sample_rate_khz"].as<double>() * 1000;
                source = createE2010SampleSource(config["e2010"].as<std::map<std::string, std::string>>());
            }

            // Состояние обнуляется до подключения callback. Перед любым следующим
            // запуском shutdown ждёт завершения потока, который изменяет эти поля.
            counter = {};
            observeCounter = simulation && !yasmMode && !m8Mode;
            nextCounter = 0;
            gatePassed = false;
            if (m8Mode) {
                decoder = std::make_unique<orbita::Orbita>(orbita::Informativity::M8, sampleRate);
                std::vector<orbita::ChannelSpec> channels;
                for (const auto& item : m8Addresses->text().split(
                        QRegularExpression(QStringLiteral("[\\s,]+")), Qt::SkipEmptyParts)) {
                    const auto address = encoding::normalizeAddress(item.toStdString());
                    if (address.rfind("M08", 0) != 0 || !orbita::isValidChannelAddress(address))
                        throw std::invalid_argument("Неверный адрес M08: " + address);
                    channels.push_back({address, {}, {}});
                }
                if (channels.empty()) throw std::invalid_argument("Задайте хотя бы один адрес M08");
                decoder->setChannels(channels);
                if (decoder->getChannels().size() != channels.size())
                    throw std::invalid_argument("В списке есть адреса за пределами цикла M8");
                decoder->setInvertSignal(m8Invert->isChecked());
            } else {
                decoder = std::make_unique<orbita::Orbita>();
                decoder->setGroupCallback([this](const auto& words, int group, int cycle) {
                    consume(words, group, cycle);
                });
            }
            bridge = std::make_unique<miltech::integration::OrbitaSampleBridge>(*source, *decoder);
            if (!bridge->start()) throw std::runtime_error(bridge->lastError());
            start->setEnabled(false);
            stop->setEnabled(true);
            port->setEnabled(false);
            interfaceMode->setEnabled(false);
            yasmCodes->setEnabled(false);
            yasmFile->setEnabled(false);
            m8Addresses->setEnabled(false);
            m8Invert->setEnabled(false);
            graph->clear();
            status->setText(m8Mode ? QStringLiteral("M8: поиск маркеров фразы и цикла") : yasmMode
                ? QStringLiteral("Ожидание кодов внешнего теста ЯСМ в M16P1A22")
                : QStringLiteral("Ожидание двух непрерывных периодов счётчика 0–1023"));
            gateClock.start();
            refresh.start(60);
            if (!simulation && !yasmMode && !m8Mode) send.start(10);
            updateScenario();
        } catch (const std::exception& error) {
            endSession(QStringLiteral("Запуск не выполнен: %1").arg(QString::fromUtf8(error.what())));
        }
    }

    void writeCounter()
    {
        if (serial.error() != QSerialPort::NoError) {
            endSession(QStringLiteral("Ошибка COM-порта: %1").arg(serial.errorString()));
            return;
        }
        if (serial.bytesToWrite()) return;
        // Подаём порциями, чтобы системная очередь не накопила данные после
        // остановки. 256 байт за 10 мс укладываются в 500 000 бод при 8N1.
        const auto bytes = counterBytes(nextCounter, 128);
        const auto n = static_cast<qint64>(bytes.size());
        if (serial.write(reinterpret_cast<const char*>(bytes.data()), n) != n) {
            endSession(QStringLiteral("Не удалось записать всю порцию в COM-порт: %1").arg(serial.errorString()));
            return;
        }
        std::lock_guard<std::mutex> lock(dataMutex);
        observeCounter = true;
    }

    void consume(const std::vector<uint16_t>& words, int group, int cycle)
    {
        std::lock_guard<std::mutex> lock(dataMutex);
        if (yasm) yasm->consume(words, group, cycle);
        if (observeCounter) counter.consume(words, group, cycle);
        if (!reading || groupCount == groupsPerSecond || groupGap) return;
        if (group < 1 || group > 32 || cycle < 1 || cycle > 4 || words.size() != groupWords) {
            groupGap = true;
            return;
        }
        const int ordinal = (cycle - 1) * 32 + group - 1;
        groupGap = groupCount && ordinal != (lastOrdinal + 1) % groupsPerSecond;
        if (groupGap) return;
        lastOrdinal = ordinal;
        ++groupCount;
        const auto& check = checks.at(activeCheck);
        CodeRange groupCodes;
        groupCodes.consume(words, check, activeChannel);
        codes.merge(groupCodes);
        // Один элемент на группу. Кривые показывают реальные крайние
        // отсчёты этого адреса, а В7 остаётся двумя отдельными показаниями.
        if (check.scale && groupCodes.count) {
            minimumTrace.append(check.scale->volts(groupCodes.minimum));
            maximumTrace.append(check.scale->volts(groupCodes.maximum));
        }
    }

    void update()
    {
        const auto sourceError = bridge->lastError();
        if (!sourceError.empty() || !bridge->isRunning()) {
            endSession(QStringLiteral("Ошибка потока Орбита: %1").arg(QString::fromStdString(sourceError)));
            return;
        }

        if (interfaceMode->currentIndex() == 2) {
            const auto snapshot = decoder->getSnapshot();
            const auto now = std::chrono::steady_clock::now();
            QStringList values;
            for (const auto& channel : snapshot.values) {
                // Поддерживаемые адреса обновляются хотя бы раз за цикл.
                // После паузы длиннее кадра старый код скрываем.
                const bool fresh = channel.valid && now - channel.updatedAt < std::chrono::seconds(1);
                values.append(QStringLiteral("%1 = %2").arg(QString::fromStdString(channel.address),
                    fresh ? QString::number(channel.value) : QStringLiteral("нет отсчёта")));
            }
            status->setText(QStringLiteral("M8 · групп %1 · ошибки фраз %2 % · ошибки групп %3 %\n%4")
                .arg(snapshot.stats.frames_processed).arg(snapshot.stats.phrase_error_percent)
                .arg(snapshot.stats.group_error_percent).arg(values.join(QLatin1Char('\n'))));
            return;
        }

        if (!gatePassed) {
            const bool yasmMode = yasm.has_value();
            std::vector<unsigned> trace;
            {
                std::lock_guard<std::mutex> lock(dataMutex);
                gatePassed = yasmMode ? yasm->passed() : counter.passed();
                trace = yasmMode ? yasm->trace() : counter.trace();
            }
            const QVector<double> series(trace.begin(), trace.end());
            if (currentStage == 0) page->setSeries(series);
            if (!yasmMode) graph->setSeries(series);
            if (gatePassed) {
                send.stop();
                if (serial.isOpen()) serial.close();
                const auto result = yasmMode
                    ? QStringLiteral("ЯСМ: ожидаемая последовательность получена в кадре.")
                    : QStringLiteral("RS-485: получены два непрерывных периода счётчика.");
                status->setText(simulation ? QStringLiteral("Имитатор. ") + result : result);
                log->appendPlainText(status->text());
                if (currentStage == 0) completeStage(RunVerdict::Ok, status->text());
                updateCaptureControls();
            } else if (gateClock.elapsed() >= counterTimeoutMs) {
                endSession(yasmMode ? QStringLiteral("Последовательность ЯСМ за 5 с не найдена.")
                                    : QStringLiteral("RS-485: за 5 с не получены два непрерывных периода."),
                    RunVerdict::Fail);
            }
            return;
        }

        bool complete;
        bool finished;
        int groups;
        QVector<double> minimum, maximum;
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            if (!reading) return;
            groups = groupCount;
            finished = groupCount >= groupsPerSecond || groupGap || captureClock.elapsed() >= captureTimeoutMs;
            if (finished) reading = false;
            complete = !groupGap && groupCount == groupsPerSecond && codes.count;
            if (currentStage >= 2) {
                minimum = minimumTrace;
                maximum = maximumTrace;
            }
        }
        if (currentStage >= 2) {
            page->setSeries(minimum, maximum);
            setProgress(finished ? QStringLiteral("Считывается второе показание В7.")
                                 : QStringLiteral("Группы M16: %1 из %2").arg(groups).arg(groupsPerSecond));
        }
        if (!finished) return;
        const auto& check = checks.at(activeCheck);
        if (!complete) {
            finishPoint(pointName() + QStringLiteral(" Полный набор последовательных групп не получен."));
            return;
        }
        if (codes.fillCount == codes.count || (check.bits == 1 && codes.fillCount)) {
            finishPoint(pointName() + QStringLiteral(" В адресе обнаружено заполнение ЯФК 0x2AA. Точка не оценена."));
            return;
        }
        if (check.scale) {
            requestMeter(true);
        } else if (check.bits == 1) {
            finishPoint(pointName() + QStringLiteral(" %1 отсч., состояния %2–%3, ожидалось %4. %5.")
                .arg(codes.count).arg(codes.minimum).arg(codes.maximum).arg(state)
                .arg(codes.matches(state, state) ? QStringLiteral("Совпало") : QStringLiteral("Расхождение")));
        } else {
            finishPoint(pointName() + QStringLiteral(" %1 отсч., коды %2–%3. Шкала пересчёта в вольты не задана.")
                .arg(codes.count).arg(codes.minimum).arg(codes.maximum));
        }
    }

    void startCapture()
    {
        if (!capture->isEnabled()) return;
        const auto& check = checks.at(checkList->currentIndex());
        const auto expected = check.bits == 1 ? static_cast<unsigned>(expectedState->currentIndex() - 1) : 0u;
        beginPoint(checkList->currentIndex(), channelList->currentIndex(), expected);
    }

    void beginPoint(std::size_t check, std::size_t channel, unsigned expected)
    {
        activeCheck = check;
        activeChannel = channel;
        state = expected;
        pointActive = true;
        ++pointId;
        checkList->setEnabled(false);
        channelList->setEnabled(false);
        expectedState->setEnabled(false);
        updateCaptureControls();
        if (checks.at(activeCheck).scale) requestMeter(false);
        else beginFrameCapture();
    }

    void beginFrameCapture()
    {
        std::lock_guard<std::mutex> lock(dataMutex);
        codes = {};
        minimumTrace.clear();
        maximumTrace.clear();
        groupCount = 0;
        groupGap = false;
        captureClock.start();
        reading = true;
    }

    QString pointName() const
    {
        const auto& check = checks.at(activeCheck);
        const auto& requirement = currentStage >= 2 ? check.voltagePointRequirement : check.requirement;
        return QStringLiteral("Строка %1 · %2 · %3.")
            .arg(check.number).arg(QString::fromStdString(requirement),
                                  QString::fromStdString(check.addresses.at(activeChannel)));
    }

    void finishPoint(const QString& message, RunVerdict result = RunVerdict::Incomplete,
                     std::vector<tu::MeasurementResult> measurements = {})
    {
        log->appendPlainText(message);
        pointActive = false;
        checkList->setEnabled(true);
        channelList->setEnabled(true);
        expectedState->setEnabled(true);
        if (currentStage >= 2) completeStage(result, message, std::move(measurements));
        updateCaptureControls();
    }

    void requestMeter(bool after)
    {
        if (simulation) {
            meterResult({checks.at(activeCheck).scale->volts(512), {}}, after);
            return;
        }
        // В7 опрашивается вне GUI. Номер точки не даёт запоздалому ответу
        // после остановки потока попасть в следующий запуск.
        meterBusy = true;
        const auto id = pointId;
        auto* watcher = new QFutureWatcher<MeterReading>(&window);
        QObject::connect(watcher, &QFutureWatcherBase::finished, &window, [this, watcher, id, after] {
            const auto result = watcher->result();
            watcher->deleteLater();
            meterBusy = false;
            if (pointActive && id == pointId) meterResult(result, after);
            else updateCaptureControls();
        });
        watcher->setFuture(QtConcurrent::run(readMeter, root));
    }

    void meterResult(const MeterReading& result, bool after)
    {
        if (!result.error.isEmpty()) {
            finishPoint(pointName() + QStringLiteral(" Ошибка В7. %1").arg(result.error));
            return;
        }
        if (!after) {
            referenceBefore = result.volts;
            if (currentStage >= 2) page->setVoltage(QStringLiteral("%1 В").arg(result.volts, 0, 'f', 3), {}, {});
            beginFrameCapture();
            return;
        }
        const auto& scale = *checks.at(activeCheck).scale;
        const auto compared = compareVoltage(scale, codes, referenceBefore, result.volts);
        const auto format = [](double volts) { return QString::number(volts, 'f', 3); };
        QString detail = QStringLiteral(" В7 до/после %1 / %2 В. Код %3–%4 (%5 отсч.), ЯГР %6–%7 В. Макс. |Δ| %8 В.")
            .arg(format(referenceBefore), format(result.volts)).arg(codes.minimum).arg(codes.maximum).arg(codes.count)
            .arg(format(compared.minimumVolts), format(compared.maximumVolts), format(compared.maximumErrorVolts));
        auto verdict = RunVerdict::Incomplete;
        if (compared.toleranceVolts) {
            const auto limit = *compared.toleranceVolts;
            if (currentStage >= 2 && (std::min(referenceBefore, result.volts) < scale.minVolts
                    || std::max(referenceBefore, result.volts) > scale.maxVolts)) {
                detail += QStringLiteral(" Показания В7 за пределами шкалы. Точка не оценена.");
            } else if (std::abs(referenceBefore - result.volts) > limit) {
                detail += QStringLiteral(" Уровень В7 изменился сильнее допуска %1 В. Точка не оценена.").arg(format(limit));
            } else {
                const bool passed = compared.maximumErrorVolts <= limit;
                verdict = passed ? RunVerdict::Ok : RunVerdict::Fail;
                detail += QStringLiteral(" Допуск точки ±%1 В. %2.").arg(format(limit),
                    passed ? QStringLiteral("Точка в допуске") : QStringLiteral("Точка вне допуска"));
            }
        } else {
            detail += QStringLiteral(" Диагностическое сравнение.");
        }
        std::vector<tu::MeasurementResult> measurements;
        if (currentStage >= 2) {
            const auto voltage = [](const QString& value) { return value + QStringLiteral(" В"); };
            page->setVoltage(voltage(format(referenceBefore)), voltage(format(result.volts)),
                             voltage(format(compared.maximumErrorVolts)));
            const auto& address = checks.at(activeCheck).addresses.at(activeChannel);
            tu::MeasurementResult value;
            value.parameterKey = address;
            value.title = "Максимальное расхождение ЯГР и В7";
            value.measured = compared.maximumErrorVolts;
            value.upperLimit = *compared.toleranceVolts;
            value.unit = "В";
            value.verdict = verdict;
            value.attributes = {
                {"v7_before_v", format(referenceBefore).toStdString()},
                {"v7_after_v", format(result.volts).toStdString()},
                {"code_min", std::to_string(codes.minimum)}, {"code_max", std::to_string(codes.maximum)},
                {"samples", std::to_string(codes.count)},
                {"yagr_min_v", format(compared.minimumVolts).toStdString()},
                {"yagr_max_v", format(compared.maximumVolts).toStdString()}
            };
            measurements.push_back(std::move(value));
        }
        finishPoint(pointName() + detail, verdict, std::move(measurements));
    }

    void shutdown()
    {
        refresh.stop();
        send.stop();
        if (serial.isOpen()) {
            serial.clear(QSerialPort::Output);
            serial.close();
        }
        bridge.reset();
        decoder.reset();
        source.reset();
        yasm.reset();
        pointActive = false;
        ++pointId;
        ++interfaceSessionId;
        reading = gatePassed = false;
    }

    void endSession(const QString& message, RunVerdict result = RunVerdict::Error)
    {
        shutdown();
        status->setText(message);
        log->appendPlainText(message);
        start->setEnabled(true);
        stop->setEnabled(false);
        port->setEnabled(true);
        interfaceMode->setEnabled(true);
        yasmCodes->setEnabled(true);
        yasmFile->setEnabled(true);
        m8Addresses->setEnabled(true);
        m8Invert->setEnabled(true);
        checkList->setEnabled(true);
        channelList->setEnabled(true);
        expectedState->setEnabled(true);
        isdReady = false;
        if (currentStage >= 0) {
            recordStage(result, message);
            finishScenario();
        }
        updateCaptureControls();
    }

    BrpTuWindow& window;
    const bool simulation;
    const QDir root;
    TestPage* page = nullptr;
    tu::ScenarioRunResult run;
    std::vector<std::pair<std::size_t, std::size_t>> voltagePoints;
    QVector<double> minimumTrace, maximumTrace;
    int currentStage = -1;
    bool waitingForPoint = false;
    QString stageProgress;
    MetadataService metadata;
    std::vector<FrameCheck> checks;
    QComboBox *interfaceMode = nullptr, *port = nullptr, *checkList = nullptr,
              *channelList = nullptr, *expectedState = nullptr;
    QLineEdit *isdHost = nullptr, *yasmCodes = nullptr, *m8Addresses = nullptr;
    QCheckBox* m8Invert = nullptr;
    QGroupBox* checksPanel = nullptr;
    QPushButton *start = nullptr, *stop = nullptr, *capture = nullptr, *prepare = nullptr, *yasmFile = nullptr;
    QLabel *status = nullptr, *details = nullptr;
    QFormLayout* checkForm = nullptr;
    QPlainTextEdit* log = nullptr;
    brp::ui::HistoryPlot* graph = nullptr;
    QSerialPort serial;
    QTimer send, refresh;
    QElapsedTimer gateClock, captureClock;
    std::unique_ptr<ISampleSource> source;
    std::unique_ptr<orbita::Orbita> decoder;
    std::unique_ptr<miltech::integration::OrbitaSampleBridge> bridge;
    std::mutex dataMutex;
    CounterCheck counter;
    std::optional<YasmCheck> yasm;
    std::vector<uint16_t> yasmFixture;
    bool observeCounter = false;
    unsigned nextCounter = 0;
    bool gatePassed = false;
    bool reading = false, groupGap = false;
    int groupCount = 0, lastOrdinal = 0;
    std::size_t activeCheck = 0, activeChannel = 0;
    unsigned state = 0;
    unsigned pointId = 0;
    unsigned interfaceSessionId = 0;
    bool isdReady = false, preparingIsd = false;
    bool probing = false;
    bool pointActive = false, meterBusy = false;
    double referenceBefore = 0;
    CodeRange codes;
};

BrpTuWindow::BrpTuWindow(bool simulation, QWidget* parent)
    : QWidget(parent), impl_(std::make_unique<Impl>(*this, simulation))
{}

BrpTuWindow::~BrpTuWindow() = default;

void BrpTuWindow::closeEvent(QCloseEvent* event)
{
    if (impl_->currentStage >= 0)
        impl_->endSession(QStringLiteral("Окно закрыто во время испытания."), RunVerdict::Aborted);
    QWidget::closeEvent(event);
}
