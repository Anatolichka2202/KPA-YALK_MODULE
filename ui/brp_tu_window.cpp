#include "brp_tu_window.h"
#include "app/tu_report_writer.h"

#include "history_plot.h"
#include "address_set_model.h"
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
#include <QDialog>
#include <QInputDialog>
#include <QMenu>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QHeaderView>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTableWidget>
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
#include <QProcess>
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
#include <array>
#include <cmath>
#include <mutex>
#include <limits>
#include <stdexcept>
#include <thread>

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

struct BbPrCommandResult {
    bool acknowledged = false;
    QString message;
};

BbPrCommandResult issueBbPrPulse(const QDir& root)
{
    std::unique_ptr<tu::hardware::IsdRouter> router;
    try {
        const auto profile = loadStand(root);
        const auto route = profile["bb_pr_signal"];
        if (!route || !route.IsMap())
            return {false, QStringLiteral("ББ ПР: маршрут выдачи не задан в stand_brp.yaml; команда не отправлена.")};
        const unsigned type = route["type"].as<unsigned>();
        const unsigned channel = route["channel"].as<unsigned>();
        const unsigned milliseconds = route["pulse_ms"].as<unsigned>();
        if ((type != 2 && type != 3) || channel == 0
            || channel > (type == 2 ? 40u : 96u)
            || milliseconds < 20 || milliseconds > 5000)
            throw std::invalid_argument("Неверные параметры выхода ББ ПР в профиле стенда");
        router = std::make_unique<tu::hardware::IsdRouter>(tu::hardware::IsdConfig{
            profile["isd"]["host"].as<std::string>()});
        router->setSwitch(type, channel, true);
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
        router->setSwitch(type, channel, false);
        return {true, QStringLiteral("ББ ПР: ИСД подтвердил ON и OFF контакта %1/%2; физическую реакцию сверяйте по M16.")
            .arg(type).arg(channel)};
    } catch (const std::exception& error) {
        if (router) router->safeStop();
        return {false, QStringLiteral("ББ ПР: отказ выдачи или снятия воздействия: %1. Проверьте безопасное состояние стенда.")
            .arg(QString::fromUtf8(error.what()))};
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

using Readiness = std::array<QString, 4>;

Readiness probeStand(const QDir& root, const YAML::Node& profile, const QString& host, const QString& rs485)
{
    Readiness result;
    try {
        tu::hardware::IsdRouter router({host.toStdString()});
        router.probe();
    } catch (const std::exception& exception) {
        result[0] = QString::fromUtf8(exception.what());
    }
    try {
        tu::hardware::VisaInstrument meter(loadV7(root).visa);
        const auto identity = meter.query("*IDN?");
        if (identity.find_first_not_of(" \t\r\n") == std::string::npos)
            throw std::runtime_error("Пустой ответ на *IDN?");
    } catch (const std::exception& exception) {
        result[1] = QString::fromUtf8(exception.what());
    }
    try {
        const auto supply = profile["akip"];
        tu::hardware::AkipConfig config;
        config.portName = supply["port"].as<std::string>();
        config.expectedIdentity = supply["expected_idn"].as<std::string>();
        if (rs485.compare(QString::fromStdString(config.portName), Qt::CaseInsensitive) == 0)
            throw std::runtime_error("АКИП и RS-485 требуют разных COM-портов.");
        tu::hardware::Akip1160 instrument(config);
        instrument.probe();
    } catch (const std::exception& exception) {
        result[2] = QString::fromUtf8(exception.what());
    }
    try {
        auto adc = createE2010SampleSource(profile["e2010"].as<std::map<std::string, std::string>>());
        if (!adc->open()) throw std::runtime_error("АЦП не открылся по профилю стенда.");
        adc->close();
    } catch (const std::exception& exception) {
        result[3] = QString::fromUtf8(exception.what());
    }
    return result;
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
        QObject::connect(page, &TestPage::bbPrSignalRequested, &window,
            [this] { triggerBbPr(); });
        QObject::connect(page, &TestPage::skipCurrentStepRequested, &window, [this] {
            if (currentStage < 3 || pendingDecision || forcedOkActive) return;
            forcedOkActive = true;
            const tu::RunEvent event{std::chrono::system_clock::now(),
                run.steps[currentStage].nodeId, "OPERATOR_OVERRIDE",
                "Запрошен принудительный ОК; измерение продолжается", RunVerdict::NotRun};
            run.events.push_back(event);
            page->appendRunEvent(event);
            persistRun();
            setProgress(QStringLiteral("Принудительный ОК отмечен. Измерение продолжается в штатном режиме."));
        });
        QObject::connect(page, &TestPage::retryStageRequested, &window, [this] {
            if (!pendingDecision || currentStage < 2) return;
            pendingDecision = false;
            forcedOkActive = false;
            page->setRetryAvailable(false);
            if (currentStage == 2) {
                page->setStage(2, QString::fromStdString(run.steps[2].title),
                    QStringLiteral("Контроль адресов M16"), QStringLiteral("код"));
                beginCoverage();
                return;
            }
            const auto [check, channel] = voltagePoints[currentStage - 3];
            const auto address = QString::fromStdString(checks[check].addresses[channel]);
            const int expected = digitalExpectations[currentStage - 3];
            page->setStage(currentStage, QString::fromStdString(run.steps[currentStage].title),
                address, expected < 0 ? QStringLiteral("В") : QStringLiteral("бит"));
            waitingForPoint = true;
            setProgress(expected < 0
                ? QStringLiteral("Повторная попытка: подготовьте напряжение и измерьте точку.")
                : QStringLiteral("Повторная попытка: установите состояние входа %1 и измерьте.").arg(expected));
        });
        QObject::connect(page, &TestPage::continueStageRequested, &window, [this] {
            if (!pendingDecision || currentStage < 2) return;
            advanceStage();
        });
        QObject::connect(page, &TestPage::stopRequested, &window, [this] {
            if (currentStage < 0) return;
            const auto message = preparingIsd
                ? QStringLiteral("Испытание остановлено. ИСД завершает начатую последовательность OFF.")
                : QStringLiteral("Испытание остановлено оператором.");
            endSession(message, tu::RunVerdict::Aborted);
        });

        flow->setRs485Selection(!simulation);
#if BRP_ENABLE_MANUAL_TESTS
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
        auto* database = new QPushButton(QStringLiteral("Параметры Орбиты…"), manual);
        database->setObjectName(QStringLiteral("brpBrowseDb"));
        header->addWidget(database);
        QObject::connect(database, &QPushButton::clicked, &window, [this] { browseDatabase(); });
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

        auto* interfaceBox = new QGroupBox(QStringLiteral("Орбита · приём через Е20-10"), manual);
        auto* interfaceLayout = new QVBoxLayout(interfaceBox);
        auto* interfaceControls = new QHBoxLayout;
        interfaceMode = new QComboBox(interfaceBox);
        interfaceMode->setObjectName(QStringLiteral("brpInterfaceMode"));
        interfaceMode->addItems({QStringLiteral("RS-485 → ячейка · счётчик"), QStringLiteral("ЯСМ · внешний тест"),
                                 QStringLiteral("Орбита M8 · приём"), QStringLiteral("Орбита M16 · приём")});
        port = new QComboBox(interfaceBox);
        port->setEditable(true);
        port->setMinimumContentsLength(14);
        port->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        port->lineEdit()->setPlaceholderText(QStringLiteral("COM-порт RS-485"));
        for (const auto& entry : QSerialPortInfo::availablePorts()) port->addItem(entry.portName());
        port->setCurrentIndex(-1);
        port->setVisible(!simulation);
        counterToCom = new QCheckBox(QStringLiteral("Счётчик RS-485 → COM"), interfaceBox);
        counterToCom->setObjectName(QStringLiteral("brpMonitorCounterToCom"));
        counterToCom->setToolTip(QStringLiteral("Отправлять тест 0–1023 на выбранный COM во время приёма M16 через Е20-10. Отдельная физическая линия RS-485 обязательна."));
        counterToCom->setChecked(false);
        counterToCom->hide();
        start = new QPushButton(QStringLiteral("Проверить интерфейс"), interfaceBox);
        start->setObjectName(QStringLiteral("brpInterfaceStart"));
        start->setProperty("accent", true);
        stop = new QPushButton(QStringLiteral("Остановить сбор"), interfaceBox);
        stop->setObjectName(QStringLiteral("brpInterfaceStop"));
        stop->setProperty("stop", true);
        stop->setEnabled(false);
        interfaceControls->addWidget(interfaceMode, 1);
        interfaceControls->addWidget(port, 1);
        interfaceControls->addWidget(counterToCom);
        interfaceLayout->addLayout(interfaceControls);
        QObject::connect(counterToCom, &QCheckBox::toggled, &window, [this](bool checked) {
            if (port && interfaceMode) port->setVisible(!simulation
                && (selectedMode() == 0 || (selectedMode() == 3 && checked)));
        });
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
        purumk = new QPushButton(QStringLiteral("Запустить PURUMK…"), interfaceBox);
        purumk->setObjectName(QStringLiteral("brpLaunchPurumk"));
        purumk->setToolTip(QStringLiteral("Внешняя программа формирует сигнал; БРП отдельно проверяет приём в M16."));
        purumk->setVisible(false);
        purumk->setEnabled(!simulation);
        yasmControls->addWidget(purumk);
        interfaceLayout->addLayout(yasmControls);
        QObject::connect(purumk, &QPushButton::clicked, &window, [this] {
            QString program = purumkProgram;
            if (program.isEmpty()) {
                const auto bundled = root.filePath(QStringLiteral("PURUMK.exe"));
                if (QFileInfo::exists(bundled)) program = bundled;
                else program = QFileDialog::getOpenFileName(&this->window,
                    QStringLiteral("Выберите исполняемый файл PURUMK"), root.path(),
                    QStringLiteral("Приложение PURUMK (*.exe);;Все файлы (*)"));
            }
            if (program.isEmpty()) return;
            if (!QFileInfo::exists(program)) {
                appendLog(QStringLiteral("PURUMK: исполняемый файл не найден: %1").arg(program));
                return;
            }
            if (QProcess::startDetached(program, {})) {
                purumkProgram = program;
                appendLog(QStringLiteral("PURUMK запущена. Сигнал и его наличие ещё НЕ подтверждены."));
            } else {
                appendLog(QStringLiteral("Не удалось запустить PURUMK: %1").arg(program));
            }
        });
        m8Addresses = new QLineEdit(interfaceBox);
        m8Addresses->setObjectName(QStringLiteral("brpM8Addresses"));
        m8Addresses->setPlaceholderText(QStringLiteral("Адреса M08… через пробел"));

        m8Invert = new QCheckBox(QStringLiteral("Инверсия сигнала"), interfaceBox);
        m8Addresses->hide();
        m8Invert->hide();
        auto* m8Controls = new QHBoxLayout;
        m8Controls->addWidget(m8Addresses, 1);
        m8Controls->addWidget(m8Invert);
        interfaceLayout->addLayout(m8Controls);
        profileBar = new QWidget(interfaceBox);
        auto* profileLayout = new QHBoxLayout(profileBar);
        profileLayout->setContentsMargins(0, 0, 0, 0);
        profileCombo = new QComboBox(profileBar);
        profileCombo->setObjectName(QStringLiteral("brpMonitorProfiles"));
        auto* profileActions = new QPushButton(QStringLiteral("Профили"), profileBar);
        auto* menu = new QMenu(profileActions);
        profileActions->setMenu(menu);
        auto* addProfile = menu->addAction(QStringLiteral("Создать профиль…"));
        addProfile->setObjectName(QStringLiteral("brpAddMonitorProfile"));
        auto* renameProfile = menu->addAction(QStringLiteral("Переименовать…"));
        auto* deleteProfile = menu->addAction(QStringLiteral("Удалить текущий профиль"));
        menu->addSeparator();
        auto* loadProfiles = menu->addAction(QStringLiteral("Загрузить наборы…"));
        auto* saveProfiles = menu->addAction(QStringLiteral("Сохранить наборы…"));
        combineProfiles = new QCheckBox(QStringLiteral("Объединить"), profileBar);
        combineProfiles->setChecked(true);
        combineProfiles->setToolTip(QStringLiteral("Показать каналы выбранной информативности из всех профилей"));
        profileLayout->addWidget(profileCombo, 1);
        profileLayout->addWidget(profileActions);
        profileLayout->addWidget(combineProfiles);
        interfaceLayout->addWidget(profileBar);
        profileBar->hide();
        profiles.addToActive({{"M16P1A10B60C11T01", "ББ СУ", "ЯГР1"},
                              {"M16P1A10B60C21T01", "ББ ПР", "ЯГР1"}});
        if (simulation)
            profiles.addToActive({{"M08A10T01", {}, {}}, {"M08A20T01", {}, {}}});
        refreshProfiles();
        QObject::connect(profileCombo, &QComboBox::currentIndexChanged, &window, [this](int index) {
            if (index < 0) return;
            profiles.setActiveIndex(index);
            showProfileAddresses();
        });
        QObject::connect(m8Addresses, &QLineEdit::editingFinished, &window, [this] {
            if (selectedMode() < 2 || bridge || !m8Addresses->isModified()) return;
            try { storeProfileAddresses(); }
            catch (const std::exception& error) { status->setText(QString::fromUtf8(error.what())); }
        });
        QObject::connect(addProfile, &QAction::triggered, &window, [this] {
            const auto name = QInputDialog::getText(&this->window, QStringLiteral("Новый профиль"),
                QStringLiteral("Название"));
            if (name.trimmed().isEmpty()) return;
            profiles.addSet(name);
            refreshProfiles();
            showProfileAddresses();
        });
        QObject::connect(renameProfile, &QAction::triggered, &window, [this] {
            const auto name = QInputDialog::getText(&this->window, QStringLiteral("Имя профиля"),
                QStringLiteral("Название"), QLineEdit::Normal, profiles.at(profiles.activeIndex()).title);
            profiles.renameSet(profiles.activeIndex(), name);
            refreshProfiles();
        });
        QObject::connect(deleteProfile, &QAction::triggered, &window, [this] {
            if (profiles.removeSet(profiles.activeIndex())) {
                refreshProfiles();
                showProfileAddresses();
            }
        });
        QObject::connect(loadProfiles, &QAction::triggered, &window, [this] { profileFile(false); });
        QObject::connect(saveProfiles, &QAction::triggered, &window, [this] { profileFile(true); });
        plotEnabled = new QCheckBox(QStringLiteral("График"), interfaceBox);
        plotEnabled->setObjectName(QStringLiteral("brpMonitorGraphToggle"));
        plotEnabled->setChecked(true); // Show live columns by default.
        plotEnabled->hide();
        auto* interfaceActions = new QHBoxLayout;
        auto* monitorButton = new QPushButton(QStringLiteral("Монитор Орбиты"), interfaceBox);
        QObject::connect(monitorButton, &QPushButton::clicked, interfaceMode, [this] {
            if (!bridge) interfaceMode->setCurrentIndex(3);
        });
        interfaceActions->addWidget(monitorButton);
        bbPrButton = new QPushButton(QStringLiteral("ББ ПР · выдать сигнал"), interfaceBox);
        bbPrButton->setObjectName(QStringLiteral("brpIssueBbPr"));
        bbPrButton->setToolTip(QStringLiteral("Выдача через заданный в профиле контакт ИСД; без карты контакта команда не отправляется."));
        QObject::connect(bbPrButton, &QPushButton::clicked, &window,
            [this] { triggerBbPr(); });
        interfaceActions->addWidget(bbPrButton);
        interfaceActions->addWidget(start);
        interfaceActions->addWidget(stop);
        interfaceActions->addWidget(plotEnabled);
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
                appendLog(QStringLiteral("ЯСМ: загружено %1 слов эталонных данных.").arg(yasmFixture.size()));
            } catch (const std::exception& error) {
                appendLog(QStringLiteral("Файл ЯСМ: %1").arg(QString::fromUtf8(error.what())));
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
        monitorPlot = new brp::ui::HistoryPlot(interfaceBox);
        monitorPlot->setObjectName(QStringLiteral("brpMonitorPlot"));
        monitorPlot->configure(QStringLiteral("Коды M16 / M8 · текущие значения по адресам"), QStringLiteral("код"), {});
        monitorPlot->setMinimumHeight(240);
        monitorPlot->hide();
        interfaceLayout->addWidget(monitorPlot, 1);
        QObject::connect(plotEnabled, &QCheckBox::toggled, monitorPlot, &QWidget::setVisible);
        interfaceLayout->addStretch();
        QObject::connect(interfaceMode, &QComboBox::currentIndexChanged, &window, [this, columns, interfaceBox](int index) {
            port->setVisible(!this->simulation && (index == 0
                || (index == 3 && counterToCom->isChecked())));
            counterToCom->setVisible(index == 3 && !this->simulation);
            if (purumk) purumk->setVisible(index == 1 && !this->simulation);
            yasmCodes->setVisible(index == 1);
            yasmFile->setVisible(index == 1);
            graph->setVisible(index == 0);
            columns->setAlignment(interfaceBox, index == 0 ? Qt::Alignment{} : Qt::AlignTop);
            m8Addresses->setVisible(index >= 2);
            if (index >= 2) {
                m8Addresses->setPlaceholderText(index == 2 ? QStringLiteral("Адреса M08… через пробел")
                                                         : QStringLiteral("Адреса M16… через пробел"));
                showProfileAddresses();
            }
            m8Invert->setVisible(index == 2);
            profileBar->setVisible(index >= 2);
            plotEnabled->setVisible(index >= 2);
            monitorPlot->setVisible(index >= 2 && plotEnabled->isChecked());
            if (index >= 2 && plotEnabled->isChecked()) columns->setAlignment(interfaceBox, Qt::Alignment{});

            checksPanel->setVisible(index < 2);
            start->setText(index >= 2 ? QStringLiteral("Начать приём Орбиты")
                                     : QStringLiteral("Проверить интерфейс"));
            if (index >= 2)
                status->setText(QStringLiteral("Пассивный приём через Е20-10. COM не задействован; для отправки счётчика на ячейку включите «Счётчик RS-485 → COM»."));
            else
                status->setText(QStringLiteral("Приём кадра идёт через Е20-10. RS-485 используется только для подачи счётчика на ячейку."));
            updateScenario();
        });
        columns->addWidget(interfaceBox, 5);
        columns->addLayout(side, 4);

        auto* checksBox = new QGroupBox(QStringLiteral("Проверка сигналов в кадре M16"), manual);
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
            isdHost = new QLineEdit(instruments);
            isdHost->setReadOnly(true);
            auto* probe = new QPushButton(QStringLiteral("Обновить состояние приборов"), instruments);
            prepare = new QPushButton(QStringLiteral("Подготовить ИСД · ЦАП OFF"), instruments);
            instrumentsLayout->addRow(QStringLiteral("ИСД"), isdHost);
            instrumentsLayout->addRow(probe);
            instrumentsLayout->addRow(prepare);
            side->addWidget(instruments);
            QObject::connect(prepare, &QPushButton::clicked, &window, [this] { startIsdPreparation(); });
            QObject::connect(probe, &QPushButton::clicked, &window, [this] { checkManualEquipment(); });
            QObject::connect(flow, &TuFlowWidget::manualRequested, &window, [this] {
            appendLog(QStringLiteral("Ручной режим: питание БРП внешнее. ИСД, АКИП и В7 не опрашиваются автоматически; контроль RS-485/Орбиты запускается отдельно."));
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
#endif
        QObject::connect(&refresh, &QTimer::timeout, &window, [this] { update(); });
        QObject::connect(&send, &QTimer::timeout, &window, [this] { writeCounter(); });

        try {
            QFile config(root.filePath(QStringLiteral("data/frame_checks.yaml")));
            if (!config.open(QIODevice::ReadOnly)) throw std::runtime_error("Файл кадровых проверок БРП не открыт");
            checks = loadFrameChecks(config.readAll().toStdString());

#if BRP_ENABLE_MANUAL_TESTS
            const auto dbPath = root.filePath(QStringLiteral("data/parameters.db"));
            if (!QFileInfo::exists(dbPath) || !metadata.open(dbPath))
                appendLog(QStringLiteral("База параметров Орбита недоступна. Показаны адреса из списка БРП."));
            for (const auto& check : checks) {
                checkList->addItem(QStringLiteral("%1. %2 · %3")
                    .arg(check.number).arg(QString::fromStdString(check.title),
                                           QString::fromStdString(check.cell)));
            }
#endif
        } catch (const std::exception& error) {
            if (start) start->setEnabled(false);
            page->flow()->setScenarioAvailable(false, QString::fromUtf8(error.what()));
            appendLog(QString::fromUtf8(error.what()));
        }
        updateScenario();
    }

    ~Impl()
    {
        // The window normally postpones closing until the asynchronous decoder
        // teardown finishes. During application destruction, still protect the
        // callback's Impl lifetime until the worker has exited.
        refresh.stop();
        send.stop();
        if (stopWatcher) stopWatcher->future().waitForFinished();
        if (bridge) bridge.reset();
        decoder.reset();
        source.reset();
    }

    void refreshProfiles()
    {
        const QSignalBlocker blocker(profileCombo);
        profileCombo->clear();
        for (int i = 0; i < profiles.count(); ++i)
            profileCombo->addItem(profiles.at(i).title);
        profileCombo->setCurrentIndex(profiles.activeIndex());
    }

    QString monitorPrefix() const
    {
        return selectedMode() == 2 ? QStringLiteral("M08") : QStringLiteral("M16");
    }

    void showProfileAddresses()
    {
        const auto prefix = monitorPrefix().toStdString();
        QStringList addresses;
        for (const auto& spec : profiles.at(profiles.activeIndex()).specs)
            if (spec.address.rfind(prefix, 0) == 0) addresses.append(QString::fromStdString(spec.address));
        m8Addresses->setText(addresses.join(QLatin1Char(' ')));
    }

    void storeProfileAddresses()
    {
        const auto prefix = monitorPrefix().toStdString();
        const auto previous = profiles.at(profiles.activeIndex()).specs;
        auto channels = previous;
        channels.erase(std::remove_if(channels.begin(), channels.end(), [&prefix](const auto& spec) {
            return spec.address.rfind(prefix, 0) == 0;
        }), channels.end());
        for (const auto& token : m8Addresses->text().split(QRegularExpression(QStringLiteral("[\\s,]+")), Qt::SkipEmptyParts)) {
            const auto address = encoding::normalizeAddress(token.toStdString());
            if (address.rfind(prefix, 0) != 0 || !orbita::isValidChannelAddress(address))
                throw std::invalid_argument("Некорректный адрес для формата " + prefix + ": " + address);
            if (std::any_of(channels.begin(), channels.end(), [&address](const auto& spec) {
                return spec.address == address;
            })) continue;
            const auto found = std::find_if(previous.begin(), previous.end(), [&address](const auto& spec) {
                return spec.address == address;
            });
            const auto name = monitorNames.value(QString::fromStdString(address),
                metadata.name(QString::fromStdString(address)));
            if (found == previous.end()) channels.push_back({address, name.toStdString(), {}});
            else {
                auto spec = *found;
                if (monitorNames.contains(QString::fromStdString(address))) spec.name = name.toStdString();
                channels.push_back(std::move(spec));
            }
        }
        profiles.replaceActive(channels);
    }

    void profileFile(bool save)
    {
        if (bridge) return;
        const auto pattern = QStringLiteral("Наборы параметров (*.json)");
        const auto path = save
            ? QFileDialog::getSaveFileName(&window, QStringLiteral("Сохранить профили"), {}, pattern)
            : QFileDialog::getOpenFileName(&window, QStringLiteral("Загрузить профили"), {}, pattern);
        if (path.isEmpty()) return;
        if (save) {
            QSaveFile file(path);
            const auto bytes = profiles.serialize();
            if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
                status->setText(file.errorString());
            else status->setText(QStringLiteral("Профили сохранены."));
            return;
        }
        QFile file(path);
        QString error;
        if (!file.open(QIODevice::ReadOnly)) error = file.errorString();
        else if (profiles.restore(file.readAll(), &error)) {
            refreshProfiles();
            showProfileAddresses();
            status->setText(QStringLiteral("Профили загружены."));
            return;
        }
        status->setText(error);
    }

    void browseDatabase(const QString& databasePath = {})
    {
        auto* dialog = new QDialog(&window);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowTitle(QStringLiteral("Параметры Орбиты"));
        dialog->resize(940, 540);
        auto* layout = new QVBoxLayout(dialog);
        const auto path = databasePath.isEmpty() ? root.filePath(QStringLiteral("data/parameters.db")) : databasePath;
        auto* summary = new QLabel(path, dialog);
        summary->setTextFormat(Qt::PlainText);
        summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(summary);
        auto* filter = new QLineEdit(dialog);
        filter->setPlaceholderText(QStringLiteral("Поиск по имени, адресу, категории или типу"));
        layout->addWidget(filter);
        auto* table = new QTableWidget(0, 6, dialog);
        table->setObjectName(QStringLiteral("brpAddressTable"));
        table->setHorizontalHeaderLabels({QStringLiteral("Параметр"), QStringLiteral("Адрес / компоненты"), QStringLiteral("Категория"),
            QStringLiteral("Тип"), QStringLiteral("Ед."), QStringLiteral("Источник")});
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionMode(QAbstractItemView::ExtendedSelection);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setAlternatingRowColors(true);
        table->verticalHeader()->hide();
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        layout->addWidget(table);

        const auto addRow = [table](const QStringList& fields, const QString& fullAddress) {
            const int row = table->rowCount();
            table->insertRow(row);
            for (int column = 0; column < fields.size(); ++column)
                table->setItem(row, column, new QTableWidgetItem(fields[column]));
            table->item(row, 0)->setData(Qt::UserRole, fullAddress);
        };
        for (const auto& check : checks) {
            for (std::size_t channel = 0; channel < check.addresses.size(); ++channel) {
                const auto address = QString::fromStdString(check.addresses[channel]);
                const auto name = QString::fromStdString(
                    check.channelNames.empty() ? check.title : check.channelNames[channel]);
                addRow({name, address, QString::fromStdString(check.cell),
                    check.bits == 1 ? QStringLiteral("Бит") : QStringLiteral("Код"),
                    {}, QStringLiteral("БРП · таблица 1")}, address);
            }
        }

        const auto connection = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QString error;
        {
            auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
            db.setDatabaseName(path);
            db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
            if (!db.open()) {
                error = db.lastError().text();
            } else {
                QSqlQuery query(db);
                if (!query.exec(QStringLiteral(
                        "SELECT a.full_address, p.name, p.signal_type, p.units, a.is_zu, "
                        "a.stream, a.a, a.b, a.c, a.d, a.e, a.x, a.t, a.p, p.category "
                        "FROM addresses a JOIN parameters p ON p.id=a.param_id "
                        "ORDER BY p.category, p.name, a.id"))) error = query.lastError().text();
                while (query.next()) {
                    auto address = query.value(0).toString();
                    if (address.isEmpty()) {
                        address = query.value(5).toString();
                        constexpr char components[] = {'A', 'B', 'C', 'D', 'E', 'X', 'T', 'P'};
                        for (int i = 0; i < 8; ++i) {
                            const auto value = query.value(i + 6).toString();
                            if (!value.isEmpty()) address += QLatin1Char(components[i]) + value.rightJustified(2, QLatin1Char('0'));
                        }
                    }
                    const bool storage = query.value(4).toBool();
                    const auto full = QString::fromStdString(encoding::normalizeAddress(query.value(0).toString().toStdString()));
                    const auto value = full.toStdString();
                    const bool usable = !storage && (value.rfind("M08", 0) == 0 || value.rfind("M16", 0) == 0)
                        && orbita::isValidChannelAddress(value);
                    addRow({query.value(1).toString(), address, query.value(14).toString(),
                        query.value(2).toString(), query.value(3).toString(),
                        storage ? QStringLiteral("ЗУ") : QStringLiteral("База Орбиты")}, usable ? full : QString());
                }
            }
        }
        QSqlDatabase::removeDatabase(connection);
        summary->setText(error.isEmpty() ? QStringLiteral("%1 · %2 строк · БРП и каталог Орбиты").arg(path).arg(table->rowCount())
                                         : QStringLiteral("%1 · %2").arg(path, error));
        QObject::connect(filter, &QLineEdit::textChanged, table, [table](const QString& text) {
            const auto needle = text.trimmed();
            for (int row = 0; row < table->rowCount(); ++row) {
                bool matches = needle.isEmpty();
                for (int column = 0; column < table->columnCount() && !matches; ++column)
                    matches = table->item(row, column)->text().contains(needle, Qt::CaseInsensitive);
                table->setRowHidden(row, !matches);
            }
        });
        auto* actions = new QHBoxLayout;
        auto* changeDatabase = new QPushButton(QStringLiteral("Открыть другую БД…"), dialog);
        auto* apply = new QPushButton(QStringLiteral("Показать выбранные параметры"), dialog);
        apply->setObjectName(QStringLiteral("brpApplyMonitorParameters"));
        actions->addWidget(changeDatabase);
        actions->addStretch();
        actions->addWidget(apply);
        layout->addLayout(actions);
        auto* selectionStatus = new QLabel(dialog);
        selectionStatus->setWordWrap(true);
        layout->addWidget(selectionStatus);

        QObject::connect(changeDatabase, &QPushButton::clicked, dialog, [this, dialog, path] {
            const auto file = QFileDialog::getOpenFileName(dialog, QStringLiteral("Открыть SQLite"),
                QFileInfo(path).absolutePath(), QStringLiteral("SQLite (*.db *.sqlite *.sqlite3);;Все файлы (*)"));
            if (!file.isEmpty()) {
                dialog->close();
                browseDatabase(file);
            }
        });
        QObject::connect(apply, &QPushButton::clicked, dialog, [this, dialog, table, selectionStatus] {
            if (bridge) {
                selectionStatus->setText(QStringLiteral("Остановите текущий приём, чтобы сменить параметры."));
                return;
            }
            QStringList addresses;
            QHash<QString, QString> names;
            QString prefix;
            for (const auto& index : table->selectionModel()->selectedRows()) {
                if (table->isRowHidden(index.row())) continue;
                const auto* item = table->item(index.row(), 0);
                const auto address = item->data(Qt::UserRole).toString();
                if (address.isEmpty() || (!prefix.isEmpty() && !address.startsWith(prefix))) {
                    selectionStatus->setText(QStringLiteral("Выберите параметры одного формата с полными адресами кадра."));
                    return;
                }
                prefix = address.left(3);
                if (!names.contains(address)) {
                    addresses.append(address);
                    names.insert(address, item->text());
                }
            }
            if (addresses.isEmpty()) {
                selectionStatus->setText(QStringLiteral("Выберите хотя бы один параметр."));
                return;
            }
            interfaceMode->setCurrentIndex(prefix == QStringLiteral("M08") ? 2 : 3);
            m8Addresses->setText(addresses.join(QLatin1Char(' ')));
            for (auto it = names.cbegin(); it != names.cend(); ++it)
                monitorNames.insert(it.key(), it.value());
            storeProfileAddresses();
            status->setText(QStringLiteral("Выбрано параметров: %1. Нажмите «Начать приём Орбиты».").arg(addresses.size()));
            dialog->accept();
        });
        dialog->open();
    }

    void checkManualEquipment()
    {
        if (bridge || stoppingStream || preparingIsd || probing || meterBusy) return;
        try {
            const auto profile = loadStand(root);
            const auto host = QString::fromStdString(profile["isd"]["host"].as<std::string>()).trimmed();
            isdHost->setText(host);
            probing = true;
            updateCaptureControls();
            appendLog(QStringLiteral("Проверка приборов по профилю стенда…"));
            auto* watcher = new QFutureWatcher<Readiness>(&window);
            QObject::connect(watcher, &QFutureWatcherBase::finished, &window, [this, watcher] {
                const auto result = watcher->result();
                watcher->deleteLater();
                probing = false;
                const std::array names{QStringLiteral("ИСД"), QStringLiteral("В7"),
                                       QStringLiteral("АКИП"), QStringLiteral("Е20-10")};
                for (std::size_t i = 0; i < names.size(); ++i)
                    appendLog(QStringLiteral("%1 · %2").arg(names[i], result[i].isEmpty()
                        ? QStringLiteral("доступен") : result[i]));
                updateCaptureControls();
            });
            const auto rs485 = port->currentText().trimmed();
            watcher->setFuture(QtConcurrent::run(probeStand, root, profile, host, rs485));
        } catch (const std::exception& error) {
            appendLog(QString::fromUtf8(error.what()));
        }
    }

    void appendLog(const QString& message)
    {
        if (log) log->appendPlainText(message);
    }

    void triggerBbPr()
    {
        if (bbPrBusy || preparingIsd || probing || pointActive || meterBusy || coverageActive
            || reading || (currentStage >= 0 && currentStage < 3)
            || (currentStage < 0 && bridge)) return;
        if (simulation) {
            appendLog(QStringLiteral("ББ ПР: ИМИТАЦИЯ запроса выдачи. Аппаратная команда не отправлялась."));
            return;
        }
        if (!isdReady) {
            appendLog(QStringLiteral("ББ ПР: сначала выполните адресную подготовку OFF ИСД."));
            return;
        }
        bbPrBusy = true;
        if (bbPrButton) bbPrButton->setEnabled(false);
        updateScenario();
        auto* watcher = new QFutureWatcher<BbPrCommandResult>(&window);
        QObject::connect(watcher, &QFutureWatcherBase::finished, &window, [this, watcher] {
            const auto reply = watcher->result();
            watcher->deleteLater();
            bbPrBusy = false;
            if (bbPrButton) bbPrButton->setEnabled(true);
            appendLog(reply.message);
            updateScenario();
        });
        watcher->setFuture(QtConcurrent::run(issueBbPrPulse, root));
    }

    int selectedMode() const
    {
        return interfaceMode ? interfaceMode->currentIndex() : 0;
    }

    QString selectedPort() const
    {
        return currentStage >= 0 || !port ? page->flow()->rs485Port() : port->currentText().trimmed();
    }

    void checkStand(const QString& serialNumber)
    {
        auto* flow = page->flow();
        const auto connection = QStringLiteral("Подключение");
        const auto isd = QStringLiteral("ИСД");
        const auto meter = QStringLiteral("В7");
        const auto supply = QStringLiteral("АКИП");
        const auto adc = QStringLiteral("Е20-10");
        flow->beginStandCheck(serialNumber, {connection, isd, meter, supply, adc});
        QString error;
        const int mode = selectedMode();
        if (mode >= 2)
            error = QStringLiteral("Выберите RS-485 или ЯСМ в ручном управлении.");
        else if (!simulation && mode == 0 && page->flow()->rs485Port().isEmpty())
            error = QStringLiteral("Выберите COM-порт RS-485 на стартовом экране.");
        else if (mode == 1 && yasmFixture.empty() && yasmCodes->text().trimmed().isEmpty())
            error = QStringLiteral("Загрузите тест ЯСМ или укажите ожидаемые коды.");
        else if (bridge || stoppingStream || preparingIsd || probing || meterBusy)
            error = QStringLiteral("Дождитесь завершения обращения к оборудованию.");
        if (!error.isEmpty()) {
            flow->setEquipmentStatus(connection, false, error);
            return;
        }
        flow->setEquipmentStatus(connection, true);
        if (simulation) {
            flow->setEquipmentStatus(isd, true);
            flow->setEquipmentStatus(meter, true);
            flow->setEquipmentStatus(supply, true);
            flow->setEquipmentStatus(adc, true);
            return;
        }

        // Проверка доступности только читает приборы. Адресные команды OFF
        // посылаются после проверки интерфейса в самом испытании.
        YAML::Node profile;
        try {
            profile = loadStand(root);
            scenarioIsdHost = QString::fromStdString(profile["isd"]["host"].as<std::string>()).trimmed();
            if (scenarioIsdHost.isEmpty()) throw std::runtime_error("В профиле отсутствует адрес ИСД.");
        } catch (const std::exception& exception) {
            flow->setEquipmentStatus(isd, false, QString::fromUtf8(exception.what()));
            return;
        }
        const auto host = scenarioIsdHost;
        probing = true;
        updateCaptureControls();
        auto* watcher = new QFutureWatcher<Readiness>(&window);
        QObject::connect(watcher, &QFutureWatcherBase::finished, &window, [this, watcher, isd, meter, supply, adc] {
            const auto result = watcher->result();
            watcher->deleteLater();
            probing = false;
            const std::array codes{isd, meter, supply, adc};
            for (std::size_t i = 0; i < codes.size(); ++i)
                page->flow()->setEquipmentStatus(codes[i], result[i].isEmpty(), result[i]);
            updateCaptureControls();
        });
        watcher->setFuture(QtConcurrent::run([host, root = root, profile, rs485 = flow->rs485Port(), mode] {
            return probeStand(root, profile, host, mode == 0 ? rs485 : QString());
        }));
    }

    void startScenario(const QString& serialNumber)
    {
        if (currentStage >= 0 || bridge || stoppingStream || preparingIsd || probing || meterBusy) return;
        const bool yasmMode = selectedMode() == 1;
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
        run.steps.push_back({"m16_coverage", "M16 · наличие адресов БРП за два цикла",
            "3.2.1.6–3.2.1.13"});
        voltagePoints.clear();
        pointTargets.clear();
        digitalExpectations.clear();
        for (std::size_t index = 0; index < checks.size(); ++index) {
            const auto& check = checks[index];
            if (check.voltagePointRequirement.empty()) continue;
            for (std::size_t channel = 0; channel < check.addresses.size(); ++channel) {
                const auto& name = check.channelNames.empty() ? check.title : check.channelNames[channel];
                const std::size_t passes = check.pointTargets.empty() ? 1 : check.pointTargets.size();
                for (std::size_t point = 0; point < passes; ++point) {
                    const bool targeted = !check.pointTargets.empty();
                    const auto title = targeted
                        ? QStringLiteral("Строка %1 · %2 · точка %3 В, В7")
                            .arg(check.number).arg(QString::fromStdString(name))
                            .arg(check.pointTargets[point], 0, 'f', 1)
                        : QStringLiteral("Строка %1 · %2 · сравнение с В7")
                            .arg(check.number).arg(QString::fromStdString(name));
                    run.steps.push_back({
                        QStringLiteral("row_%1_%2_point_%3").arg(check.number).arg(channel + 1)
                            .arg(point + 1).toStdString(), title.toStdString(), check.voltagePointRequirement});
                    voltagePoints.emplace_back(index, channel);
                    pointTargets.push_back(targeted ? std::optional<double>(check.pointTargets[point])
                                                    : std::nullopt);
                    digitalExpectations.push_back(-1);
                }
            }
        }

        // A discrete input has two observable states. Missing ISD wiring is
        // an explicit operator action, followed by automated fresh M16 capture.
        for (std::size_t index = 0; index < checks.size(); ++index) {
            const auto& check = checks[index];
            if (check.bits != 1) continue;
            for (std::size_t channel = 0; channel < check.addresses.size(); ++channel) {
                const auto& name = check.channelNames.empty() ? check.title : check.channelNames[channel];
                for (int state = 0; state <= 1; ++state) {
                    run.steps.push_back({
                        QStringLiteral("bit_%1_%2_%3").arg(check.number).arg(channel + 1).arg(state).toStdString(),
                        QStringLiteral("Строка %1 · %2 · состояние %3")
                            .arg(check.number).arg(QString::fromStdString(name)).arg(state).toStdString(),
                        check.requirement});
                    voltagePoints.emplace_back(index, channel);
                    pointTargets.push_back(std::nullopt);
                    digitalExpectations.push_back(state);
                }
            }
        }

        // Save the initial run before any equipment is activated. No silent
        // fallback to an in-memory-only run if the result directory is unavailable.
        if (!persistRun()) {
            page->flow()->setEquipmentStatus(QStringLiteral("Подключение"), false,
                QStringLiteral("Не удалось открыть историю результатов БРП."));
            return;
        }
        currentStage = 0;
        waitingForPoint = false;
        pendingDecision = false;
        forcedOkActive = false;
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
        const bool canPulse = currentStage >= 3 && (simulation || isdReady)
            && !bbPrBusy && !pointActive && !meterBusy && !coverageActive && !reading
            && !preparingIsd && !probing;
        page->setBbPrSignalAvailable(canPulse);
        if (currentStage >= 0)
            page->setProgress(stageProgress, pointCanStart(),
                currentStage >= 3 && !pendingDecision && !forcedOkActive);
    }

    bool persistRun()
    {
        try {
            writeTuCheckpoint(run);
            return true;
        } catch (const std::exception& error) {
            appendLog(QStringLiteral("ОШИБКА СОХРАНЕНИЯ РЕЗУЛЬТАТОВ: %1")
                .arg(QString::fromUtf8(error.what())));
            return false;
        }
    }

    void recordStage(RunVerdict result, const QString& detail,
                     std::vector<tu::MeasurementResult> measurements = {}, bool skipped = false)
    {
        auto& step = run.steps[currentStage];
        tu::StepRunResult attempt;
        attempt.nodeId = step.nodeId + ".attempt_" + std::to_string(step.children.size() + 1);
        attempt.title = "Попытка " + std::to_string(step.children.size() + 1);
        attempt.tuRequirement = step.tuRequirement;
        attempt.verdict = result; // Always preserve the actual pre-override verdict.
        attempt.message = detail.toStdString();
        attempt.measurements = measurements;
        step.children.push_back(std::move(attempt));
        step.forcedOk = forcedOkActive && (result == RunVerdict::Fail || result == RunVerdict::Ok);
        step.verdict = step.forcedOk ? RunVerdict::Ok : result;
        step.message = detail.toStdString();
        if (step.forcedOk)
            step.message += " [ПРИНУДИТЕЛЬНЫЙ ОК; фактические измерения сохранены]";
        step.measurements = std::move(measurements);
        step.operatorSkipped = skipped;
        const tu::RunEvent event{std::chrono::system_clock::now(), step.nodeId,
            "FINISH", step.message, step.verdict};
        run.events.push_back(event);
        page->setStageResult(currentStage, step);
        page->appendRunEvent(event);
        persistRun();
    }

    void advanceStage()
    {
        ++currentStage;
        forcedOkActive = false;
        pendingDecision = false;
        page->setRetryAvailable(false);
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
        } else if (currentStage == 2) {
            page->setStage(2, QString::fromStdString(run.steps[2].title),
                QStringLiteral("Контроль адресов M16"), QStringLiteral("код"));
            beginCoverage();
        } else {
            const auto [check, channel] = voltagePoints[currentStage - 3];
            const auto address = QString::fromStdString(checks[check].addresses[channel]);
            const int expectedBit = digitalExpectations[currentStage - 3];
            page->setStage(currentStage, QString::fromStdString(run.steps[currentStage].title),
                address, expectedBit < 0 ? QStringLiteral("В") : QStringLiteral("бит"),
                expectedBit < 0 ? QStringLiteral("ЯГР мин., В") : QStringLiteral("M16"),
                expectedBit < 0 ? QStringLiteral("ЯГР макс., В") : QStringLiteral("Ожидаемое состояние"));
            waitingForPoint = true;
            setProgress(expectedBit >= 0
                ? QStringLiteral("Установите физическое состояние входа %1, затем нажмите «Измерить».")
                    .arg(expectedBit)
                : QStringLiteral("Подайте стабильное напряжение и подключите В7 к той же точке."));
        }
    }

    void completeStage(RunVerdict result, const QString& detail,
                       std::vector<tu::MeasurementResult> measurements = {}, bool skipped = false)
    {
        if (currentStage < 2 && result != RunVerdict::Ok) {
            endSession(detail, result);
            return;
        }
        recordStage(result, detail, std::move(measurements), skipped);
        if (currentStage >= 2 && result == RunVerdict::Fail
            && !run.steps[currentStage].forcedOk) {
            pendingDecision = true;
            waitingForPoint = false;
            page->setRetryAvailable(true);
            setProgress(QStringLiteral("НЕ НОРМА: повторите пункт или продолжите с отказом."));
            return;
        }
        // Equipment faults are not DUT failures and cannot be overridden.
        if (currentStage >= 2 && result == RunVerdict::Error) {
            endSession(QStringLiteral("Ошибка оборудования: испытание остановлено."), RunVerdict::Error, true);
            return;
        }
        advanceStage();
    }

    void measureStage()
    {
        if (!pointCanStart()) return;
        const auto [check, channel] = voltagePoints[currentStage - 3];
        waitingForPoint = false;
        const int expectedBit = digitalExpectations[currentStage - 3];
        setProgress(expectedBit < 0 ? QStringLiteral("Считываются В7 и кадр M16.")
                                   : QStringLiteral("Проверяется состояние дискретного входа M16."));
        beginPoint(check, channel, expectedBit < 0 ? 0u : static_cast<unsigned>(expectedBit));
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
        persistRun();
        page->finishRun(run);
    }

    void updateCaptureControls()
    {
        if (!checkList) { updateScenario(); return; }
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
        const auto host = currentStage >= 0 ? scenarioIsdHost : isdHost->text().trimmed();
        if (preparingIsd || pointActive || probing || host.isEmpty()) return;
        isdReady = false;
        preparingIsd = true;
        updateCaptureControls();
        appendLog(QStringLiteral("ИСД: выполняются адресные OFF для реле и ЦАП."));
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
            appendLog(message);
            if (session == interfaceSessionId && currentStage == 1)
                completeStage(confirmed ? RunVerdict::Ok : RunVerdict::Error, message);
            updateCaptureControls();
        });
        watcher->setFuture(QtConcurrent::run(prepareIsd, root, host));
    }

    void displayCheck(int index)
    {
        if (channelList) channelList->clear();
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
            const bool yasmMode = selectedMode() == 1;
            const bool m8Mode = selectedMode() == 2;
            const bool monitor = selectedMode() >= 2;
            const bool sendCounter = !simulation && (selectedMode() == 0
                || (selectedMode() == 3 && counterToCom && counterToCom->isChecked()));
            if (stoppingStream) throw std::runtime_error("Орбита завершает предыдущий сеанс. Повторный запуск пока заблокирован.");
            if (probing) throw std::runtime_error("Дождитесь проверки приборов.");
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
            }
            if (sendCounter) {
                const auto portName = selectedPort();
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
                if (monitor) sampleRate = config["e2010"]["sample_rate_khz"].as<double>() * 1000;
                source = createE2010SampleSource(config["e2010"].as<std::map<std::string, std::string>>());
            }

            // Состояние обнуляется до подключения callback. Перед любым следующим
            // запуском shutdown ждёт завершения потока, который изменяет эти поля.
            counter = {};
            observeCounter = simulation && !yasmMode && !monitor;
            nextCounter = 0;
            gatePassed = false;
            if (monitor) {
                decoder = std::make_unique<orbita::Orbita>(
                    m8Mode ? orbita::Informativity::M8 : orbita::Informativity::M16, sampleRate);
                storeProfileAddresses();
                const auto prefix = monitorPrefix().toStdString();
                auto channels = combineProfiles->isChecked() ? profiles.combinedSpecs()
                                                            : profiles.at(profiles.activeIndex()).specs;
                channels.erase(std::remove_if(channels.begin(), channels.end(), [&prefix](const auto& spec) {
                    return spec.address.rfind(prefix, 0) != 0;
                }), channels.end());
                if (channels.empty()) throw std::invalid_argument("В выбранных профилях нет адресов " + prefix);
                decoder->setChannels(channels);
                if (decoder->getChannels().size() != channels.size())
                    throw std::invalid_argument("В профилях есть адреса за пределами кадра");
                activeMonitorNames.clear();
                monitorHistory.clear();
                monitorPlot->clear();
                for (const auto& spec : channels)
                    activeMonitorNames.insert(QString::fromStdString(spec.address), QString::fromStdString(spec.name));
                if (m8Mode) decoder->setInvertSignal(m8Invert->isChecked());
            } else {
                decoder = std::make_unique<orbita::Orbita>();
                decoder->setGroupCallback([this](const auto& words, int group, int cycle) {
                    consume(words, group, cycle);
                });
            }
            bridge = std::make_unique<miltech::integration::OrbitaSampleBridge>(*source, *decoder);
            if (!bridge->start()) throw std::runtime_error(bridge->lastError());
            if (start) start->setEnabled(false);
            if (stop) stop->setEnabled(true);
            if (port) port->setEnabled(false);
            if (interfaceMode) interfaceMode->setEnabled(false);
            if (yasmCodes) yasmCodes->setEnabled(false);
            if (yasmFile) yasmFile->setEnabled(false);
            if (m8Addresses) m8Addresses->setEnabled(false);
            if (profileBar) profileBar->setEnabled(false);
            if (m8Invert) m8Invert->setEnabled(false);
            if (graph) graph->clear();
            if (status) status->setText(monitor ? QStringLiteral("Ожидание синхронизации Орбиты") : yasmMode
                ? QStringLiteral("Ожидание кодов внешнего теста ЯСМ в M16P1A22")
                : QStringLiteral("Ожидание двух непрерывных периодов счётчика 0–1023"));
            gateClock.start();
            refresh.start(60);
            if (sendCounter) {
                appendLog(QStringLiteral("RS-485: счётчик 0–1023 передаётся в %1; обратная телеметрия принимается только через Е20-10.")
                    .arg(selectedPort()));
                send.start(10);
            }
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
        if (coverageActive && group >= 1 && group <= 32 && cycle >= 1 && cycle <= 4
            && words.size() == groupWords) {
            const int ordinal = (cycle - 1) * 32 + group - 1;
            if (coverageLastOrdinal >= 0 && ordinal != (coverageLastOrdinal + 1) % groupsPerSecond)
                ++coverageGapGroups;
            coverageLastOrdinal = ordinal;
            ++coverageGroups;
            for (auto& point : coverage)
                point.samples.consume(words, checks[point.check], point.channel);
        }
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

        if (selectedMode() >= 2) {
            const auto snapshot = decoder->getSnapshot();
            const auto now = std::chrono::steady_clock::now();
            QStringList values;
            QVector<brp::ui::HistoryPlot::BarValue> bars;
            for (const auto& channel : snapshot.values) {
                // Поддерживаемые адреса обновляются хотя бы раз за цикл.
                // После паузы длиннее кадра старый код скрываем.
                const bool fresh = channel.valid && now - channel.updatedAt < std::chrono::seconds(1);
                const auto address = QString::fromStdString(channel.address);
                const auto name = activeMonitorNames.value(address);
                const auto label = name.isEmpty() ? address : QStringLiteral("%1 · %2").arg(name, address);
                values.append(QStringLiteral("%1 = %2").arg(label,
                    fresh ? QString::number(channel.value) : QStringLiteral("нет отсчёта")));
                if (plotEnabled->isChecked())
                    bars.append({name.isEmpty() ? address : name, channel.value, fresh});
            }
            if (plotEnabled->isChecked()) monitorPlot->setBars(std::move(bars));
            status->setText(QStringLiteral("%1 · групп %2 · ошибки фраз %3 % · ошибки групп %4 %\n%5")
                .arg(selectedMode() == 2 ? QStringLiteral("M8") : QStringLiteral("M16"))
                .arg(snapshot.stats.frames_processed).arg(snapshot.stats.phrase_error_percent)
                .arg(snapshot.stats.group_error_percent).arg(values.join(QLatin1Char('\n'))));
            return;
        }

        if (currentStage == 2 && coverageActive) {
            bool finished = false;
            bool sufficient = false;
            int gaps = 0;
            std::vector<CoveragePoint> captured;
            {
                std::lock_guard<std::mutex> lock(dataMutex);
                const int count = coverageGroups;
                finished = count >= 2 * groupsPerSecond || coverageClock.elapsed() >= 5500;
                sufficient = count >= 2 * groupsPerSecond;
                gaps = coverageGapGroups;
                if (finished) {
                    coverageActive = false;
                    captured = coverage;
                }
            }
            if (finished) {
                std::vector<tu::MeasurementResult> measurements;
                bool allPresent = sufficient && gaps <= 4;
                for (const auto& item : captured) {
                    const auto& check = checks[item.check];
                    tu::MeasurementResult m;
                    m.parameterKey = check.addresses[item.channel];
                    m.title = check.channelNames.empty() ? check.title : check.channelNames[item.channel];
                    m.measured = item.samples.count;
                    m.unit = "отсч.";
                    m.verdict = sufficient && item.samples.count && item.samples.fillCount < item.samples.count
                        ? RunVerdict::Ok : RunVerdict::Fail;
                    m.message = m.verdict == RunVerdict::Ok ? "Свежие кадры приняты" : "Адрес отсутствует/заполнение";
                    allPresent = allPresent && m.verdict == RunVerdict::Ok;
                    measurements.push_back(std::move(m));
                }
                completeStage(allPresent ? RunVerdict::Ok : RunVerdict::Fail,
                    QStringLiteral("M16: %1 адресных проверок; разрывов %2; %3.")
                        .arg(measurements.size()).arg(gaps)
                        .arg(allPresent ? QStringLiteral("все присутствуют")
                                        : QStringLiteral("недостаточно свежих кадров")),
                    std::move(measurements));
            } else {
                setProgress(QStringLiteral("Наблюдение M16: %1 из %2 групп")
                    .arg(coverageGroups).arg(2 * groupsPerSecond));
            }
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
            if (!yasmMode && graph) graph->setSeries(series);
            if (gatePassed) {
                send.stop();
                if (serial.isOpen()) serial.close();
                const auto result = yasmMode
                    ? QStringLiteral("ЯСМ: ожидаемая последовательность получена в кадре.")
                    : QStringLiteral("RS-485: получены два непрерывных периода счётчика.");
                const auto message = simulation ? QStringLiteral("Имитатор. ") + result : result;
                if (status) status->setText(message);
                appendLog(message);
                if (currentStage == 0) completeStage(RunVerdict::Ok, message);
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
            const bool matched = codes.matches(state, state);
            tu::MeasurementResult value;
            value.parameterKey = check.addresses.at(activeChannel);
            value.title = "Состояние дискретного входа";
            value.reference = state;
            value.measured = codes.minimum;
            value.lowerLimit = state;
            value.upperLimit = state;
            value.unit = "бит";
            value.verdict = matched ? RunVerdict::Ok : RunVerdict::Fail;
            value.message = matched ? "Состояние соответствует воздействию" : "Состояние не соответствует";
            value.attributes = {{ "min", std::to_string(codes.minimum) },
                                { "max", std::to_string(codes.maximum) },
                                { "samples", std::to_string(codes.count) }};
            finishPoint(pointName() + QStringLiteral(" %1 отсч., состояния %2–%3, ожидалось %4. %5.")
                .arg(codes.count).arg(codes.minimum).arg(codes.maximum).arg(state)
                .arg(matched ? QStringLiteral("Совпало") : QStringLiteral("Расхождение")),
                value.verdict, {std::move(value)});
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
        if (checkList) checkList->setEnabled(false);
        if (channelList) channelList->setEnabled(false);
        if (expectedState) expectedState->setEnabled(false);
        updateCaptureControls();
        if (checks.at(activeCheck).scale) requestMeter(false);
        else beginFrameCapture();
    }

    void beginCoverage()
    {
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            coverage.clear();
            for (std::size_t check = 0; check < checks.size(); ++check)
                for (std::size_t channel = 0; channel < checks[check].addresses.size(); ++channel)
                    coverage.push_back({check, channel, {}});
            coverageGroups = 0;
            coverageGapGroups = 0;
            coverageLastOrdinal = -1;
            coverageActive = true;
            coverageClock.start();
        }
        setProgress(QStringLiteral("Собираются свежие адресные отсчёты M16 за несколько циклов."));
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
        appendLog(message);
        pointActive = false;
        if (checkList) checkList->setEnabled(true);
        if (channelList) channelList->setEnabled(true);
        if (expectedState) expectedState->setEnabled(true);
        if (currentStage >= 3) completeStage(result, message, std::move(measurements));
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
        // A nominal setpoint label is not proof of applying that stimulus.
        // Reject a repeated centre-point reading as evidence of ±6 V coverage.
        if (currentStage >= 3 && static_cast<std::size_t>(currentStage - 3) < pointTargets.size()) {
            const auto& target = pointTargets[currentStage - 3];
            if (target) {
                const double window = checks.at(activeCheck).targetWindowVolts;
                if (std::abs(referenceBefore - *target) > window
                    || std::abs(result.volts - *target) > window) {
                    verdict = RunVerdict::Incomplete;
                    detail += QStringLiteral(" В7 не подтвердил воздействие %1 В (окно ±%2 В); точка не оценена.")
                        .arg(format(*target), format(window));
                }
            }
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
        // GUI must never join the 10 MHz acquisition/decoder pipeline.
        // Keep all three dependencies alive on the same background worker;
        // the bridge stops the producer before the decoder and only then releases
        // the borrowed references. No subsequent session may start until finished.
        if (!stoppingStream && (bridge || decoder || source)) {
            stoppingStream = true;
            auto oldBridge = std::move(bridge);
            auto oldDecoder = std::move(decoder);
            auto oldSource = std::move(source);
            auto* watcher = new QFutureWatcher<void>(&window);
            stopWatcher = watcher;
            QObject::connect(watcher, &QFutureWatcherBase::finished, &window,
                [this, watcher] {
                    if (stopWatcher == watcher) stopWatcher = nullptr;
                    stoppingStream = false;
                    watcher->deleteLater();
                    if (start) start->setEnabled(true);
                    if (stop) stop->setEnabled(false);
                    if (status) status->setText(QStringLiteral("Поток Орбиты остановлен; оборудование освобождено."));
                    updateCaptureControls();
                    if (closeWhenStopped) {
                        closeWhenStopped = false;
                        QMetaObject::invokeMethod(&window, [this] { window.close(); }, Qt::QueuedConnection);
                    }
                });
            watcher->setFuture(QtConcurrent::run(
                [oldBridge = std::move(oldBridge), oldDecoder = std::move(oldDecoder),
                 oldSource = std::move(oldSource)]() mutable {
                    oldBridge.reset();
                    oldDecoder.reset();
                    oldSource.reset();
                }));
        }
        yasm.reset();
        pointActive = false;
        ++pointId;
        ++interfaceSessionId;
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            reading = gatePassed = false;
            observeCounter = false;
            coverageActive = false;
        }
    }

    void endSession(const QString& message, RunVerdict result = RunVerdict::Error,
                    bool stageAlreadyRecorded = false)
    {
        shutdown();
        if (status) status->setText(message);
        appendLog(message);
        if (start) start->setEnabled(!stoppingStream);
        if (stop) stop->setEnabled(false);
        if (port) port->setEnabled(true);
        if (interfaceMode) interfaceMode->setEnabled(true);
        if (yasmCodes) yasmCodes->setEnabled(true);
        if (yasmFile) yasmFile->setEnabled(true);
        if (m8Addresses) m8Addresses->setEnabled(true);
        if (profileBar) profileBar->setEnabled(true);
        if (m8Invert) m8Invert->setEnabled(true);
        if (checkList) checkList->setEnabled(true);
        if (channelList) channelList->setEnabled(true);
        if (expectedState) expectedState->setEnabled(true);
        isdReady = false;
        if (currentStage >= 0) {
            if (!stageAlreadyRecorded) recordStage(result, message);
            finishScenario();
        }
        updateCaptureControls();
    }

    BrpTuWindow& window;
    const bool simulation;
    const QDir root;
    TestPage* page = nullptr;
    tu::ScenarioRunResult run;
    struct CoveragePoint {
        std::size_t check = 0;
        std::size_t channel = 0;
        CodeRange samples;
    };
    std::vector<CoveragePoint> coverage;
    std::vector<std::pair<std::size_t, std::size_t>> voltagePoints;
    std::vector<std::optional<double>> pointTargets;
    std::vector<int> digitalExpectations;
    int coverageGroups = 0;
    int coverageGapGroups = 0;
    int coverageLastOrdinal = -1;
    bool coverageActive = false;
    QElapsedTimer coverageClock;
    QVector<double> minimumTrace, maximumTrace;
    int currentStage = -1;
    bool waitingForPoint = false;
    bool pendingDecision = false;
    bool forcedOkActive = false;
    QString stageProgress;
    QString scenarioIsdHost;
    MetadataService metadata;
    QHash<QString, QString> monitorNames, activeMonitorNames;
    AddressSetModel profiles;
    QWidget* profileBar = nullptr;
    QComboBox* profileCombo = nullptr;
    QCheckBox *combineProfiles = nullptr, *plotEnabled = nullptr, *counterToCom = nullptr;
    brp::ui::HistoryPlot* monitorPlot = nullptr;
    QHash<QString, QVector<double>> monitorHistory;
    std::vector<FrameCheck> checks;
    QComboBox *interfaceMode = nullptr, *port = nullptr, *checkList = nullptr,
              *channelList = nullptr, *expectedState = nullptr;
    QLineEdit *isdHost = nullptr, *yasmCodes = nullptr, *m8Addresses = nullptr;
    QCheckBox* m8Invert = nullptr;
    QGroupBox* checksPanel = nullptr;
    QPushButton *bbPrButton = nullptr;
    bool bbPrBusy = false;
    QPushButton *start = nullptr, *stop = nullptr, *capture = nullptr, *prepare = nullptr,
                *yasmFile = nullptr, *purumk = nullptr;
    QString purumkProgram;
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
    QFutureWatcher<void>* stopWatcher = nullptr;
    bool stoppingStream = false;
    bool closeWhenStopped = false;
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
    if (impl_->stoppingStream) {
        impl_->closeWhenStopped = true;
        event->ignore();
        return;
    }
    if (impl_->currentStage >= 0 || impl_->bridge) {
        impl_->endSession(QStringLiteral("Окно закрыто во время испытания или сбора."), RunVerdict::Aborted);
        if (impl_->stoppingStream) {
            impl_->closeWhenStopped = true;
            event->ignore();
            return;
        }
    }
    QWidget::closeEvent(event);
}
