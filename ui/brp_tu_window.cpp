#include "brp_tu_window.h"

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
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QTimer>
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

MeterReading readMeter(const QDir& root)
{
    try {
        const auto v7 = loadStand(root)["v7"];
        const tu::hardware::V7Config config{
            {v7["resources"].as<std::vector<std::string>>(), v7["timeout_ms"].as<unsigned>()},
            v7["voltage_command"].as<std::string>()};
        if (config.visa.resources.empty() || !config.visa.timeoutMilliseconds || config.dcVoltageCommand.empty())
            throw std::runtime_error("Не настроены адрес и команда В7");
        return {tu::hardware::readV7Voltage(config), {}};
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

class CounterPlot final : public QWidget {
public:
    explicit CounterPlot(QWidget* parent) : QWidget(parent) { setMinimumHeight(220); }

    void setTrace(std::vector<unsigned> trace)
    {
        trace_ = std::move(trace);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), palette().base());
        const QRectF area = QRectF(rect()).adjusted(42, 16, -12, -31);
        if (area.isEmpty()) return;
        painter.setPen(palette().mid().color());
        painter.drawRect(area);
        painter.setPen(palette().text().color());
        painter.drawText(QPointF(6, area.top() + 6), QStringLiteral("1023"));
        painter.drawText(QPointF(20, area.bottom() + 4), QStringLiteral("0"));
        painter.drawText(QRectF(area.left(), area.bottom(), area.width(), 30),
                         Qt::AlignCenter, QStringLiteral("Номер принятого отсчёта"));
        QPolygonF line;
        line.reserve(trace_.size());
        for (std::size_t i = 0; i < trace_.size(); ++i) {
            line.append(QPointF(area.left() + area.width() * i / (2 * (counterLimit + 1)),
                area.bottom() - area.height() * trace_[i] / counterLimit));
        }
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor(QStringLiteral("#227fc1")), 2));
        painter.drawPolyline(line);
    }

private:
    std::vector<unsigned> trace_;
};

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
        window.setWindowTitle(simulation ? QStringLiteral("БРП · имитация кадровой проверки")
                                         : QStringLiteral("БРП · кадровые проверки"));
        auto* layout = new QVBoxLayout(&window);
        auto* interfaceBox = new QGroupBox(QStringLiteral("1. Интерфейс → Орбита"), &window);
        auto* interfaceLayout = new QVBoxLayout(interfaceBox);
        auto* interfaceControls = new QHBoxLayout;
        interfaceMode = new QComboBox(interfaceBox);
        interfaceMode->setObjectName(QStringLiteral("brpInterfaceMode"));
        interfaceMode->addItems({QStringLiteral("RS-485 · счётчик"), QStringLiteral("ЯСМ · внешний тест"),
                                 QStringLiteral("M8 · приём кадра")});
        port = new QComboBox(interfaceBox);
        port->setEditable(true);
        port->lineEdit()->setPlaceholderText(QStringLiteral("COM-порт RS-485"));
        for (const auto& entry : QSerialPortInfo::availablePorts()) port->addItem(entry.portName());
        port->setCurrentIndex(-1);
        port->setVisible(!simulation);
        start = new QPushButton(QStringLiteral("Проверить интерфейс"), interfaceBox);
        start->setObjectName(QStringLiteral("brpInterfaceStart"));
        stop = new QPushButton(QStringLiteral("Остановить сбор"), interfaceBox);
        stop->setObjectName(QStringLiteral("brpInterfaceStop"));
        stop->setEnabled(false);
        interfaceControls->addWidget(interfaceMode);
        interfaceControls->addWidget(port);
        interfaceControls->addWidget(start);
        interfaceControls->addWidget(stop);
        interfaceControls->addStretch();
        interfaceLayout->addLayout(interfaceControls);
        yasmCodes = new QLineEdit(interfaceBox);
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
        status->setObjectName(QStringLiteral("brpInterfaceStatus"));
        interfaceLayout->addWidget(status);
        graph = new CounterPlot(interfaceBox);
        interfaceLayout->addWidget(graph);
        QObject::connect(interfaceMode, &QComboBox::currentIndexChanged, &window, [this](int index) {
            port->setVisible(!this->simulation && index == 0);
            yasmCodes->setVisible(index == 1);
            yasmFile->setVisible(index == 1);
            graph->setVisible(index == 0);
            m8Addresses->setVisible(index == 2);
            m8Invert->setVisible(index == 2);
            checksPanel->setVisible(index != 2);
            start->setText(index == 2 ? QStringLiteral("Начать приём M8")
                                     : QStringLiteral("Проверить интерфейс"));
            if (index == 2)
                status->setText(QStringLiteral("Приём M8 через вход АЦП. Укажите адреса M08."));
            else
                status->setText(QStringLiteral("Готово к запуску проверки M16."));
        });
        layout->addWidget(interfaceBox, 1);

        auto* checksBox = new QGroupBox(QStringLiteral("2. Кадровой контроль, раздел 3.2"), &window);
        checksPanel = checksBox;
        auto* checksLayout = new QFormLayout(checksBox);
        checkList = new QComboBox(checksBox);
        checkList->setObjectName(QStringLiteral("brpCheckList"));
        channelList = new QComboBox(checksBox);
        details = new QLabel(checksBox);
        details->setWordWrap(true);
        expectedState = new QComboBox(checksBox);
        expectedState->addItems({QStringLiteral("Выбрать состояние"), QStringLiteral("0"), QStringLiteral("1")});
        capture = new QPushButton(QStringLiteral("Сравнить с В7"), checksBox);
        capture->setObjectName(QStringLiteral("brpCapture"));
        capture->setEnabled(false);
        checksLayout->addRow(QStringLiteral("Строка таблицы 1"), checkList);
        checksLayout->addRow(QStringLiteral("Адрес Орбита"), channelList);
        checksLayout->addRow(details);
        checksLayout->addRow(QStringLiteral("Ожидаемый бит"), expectedState);
        checksLayout->setRowVisible(expectedState, false);
        checkForm = checksLayout;
        checksLayout->addRow(capture);
        layout->addWidget(checksBox);

        if (!simulation) {
            auto* instruments = new QGroupBox(QStringLiteral("Оборудование стенда"), &window);
            auto* instrumentsLayout = new QHBoxLayout(instruments);
            isdHost = new QLineEdit(instruments);
            auto* akipPort = new QLineEdit(instruments);
            isdHost->setPlaceholderText(QStringLiteral("Адрес ИСД"));
            akipPort->setPlaceholderText(QStringLiteral("COM-порт АКИП"));
            auto* probe = new QPushButton(QStringLiteral("Проверить ИСД и АКИП"), instruments);
            prepare = new QPushButton(QStringLiteral("Подготовить ИСД · ЦАП OFF"), instruments);
            instrumentsLayout->addWidget(isdHost);
            instrumentsLayout->addWidget(akipPort);
            instrumentsLayout->addWidget(probe);
            instrumentsLayout->addWidget(prepare);
            layout->addWidget(instruments);
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
        log->setMaximumHeight(125);
        layout->addWidget(log);

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
            log->appendPlainText(QString::fromUtf8(error.what()));
        }
    }

    ~Impl() { shutdown(); }

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
    }

    void startIsdPreparation()
    {
        const auto host = isdHost->text().trimmed();
        if (preparingIsd || pointActive || probing || host.isEmpty()) return;
        isdReady = false;
        preparingIsd = true;
        updateCaptureControls();
        log->appendPlainText(QStringLiteral("ИСД: выполняются адресные OFF для реле и ЦАП."));
        auto* watcher = new QFutureWatcher<IsdPreparation>(&window);
        QObject::connect(watcher, &QFutureWatcherBase::finished, &window, [this, watcher] {
            const auto result = watcher->result();
            watcher->deleteLater();
            preparingIsd = false;
            isdReady = result.error.isEmpty() && result.acknowledged > 0;
            log->appendPlainText(isdReady
                ? QStringLiteral("ИСД: подтверждены %1 адресных OFF, включая ЦАП.").arg(result.acknowledged)
                : QStringLiteral("ИСД не подготовлен. %1").arg(result.error));
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
            graph->setTrace({});
            status->setText(m8Mode ? QStringLiteral("M8: поиск маркеров фразы и цикла") : yasmMode
                ? QStringLiteral("Ожидание кодов внешнего теста ЯСМ в M16P1A22")
                : QStringLiteral("Ожидание двух непрерывных периодов счётчика 0–1023"));
            gateClock.start();
            refresh.start(60);
            if (!simulation && !yasmMode && !m8Mode) send.start(10);
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
        codes.consume(words, checks.at(activeCheck), activeChannel);
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
                if (!yasmMode) trace = counter.trace();
            }
            if (!yasmMode) graph->setTrace(std::move(trace));
            if (gatePassed) {
                send.stop();
                if (serial.isOpen()) serial.close();
                const auto result = yasmMode
                    ? QStringLiteral("ЯСМ: ожидаемая последовательность получена в кадре.")
                    : QStringLiteral("RS-485: получены два непрерывных периода счётчика.");
                status->setText(simulation ? QStringLiteral("Имитатор. ") + result : result);
                log->appendPlainText(status->text());
                updateCaptureControls();
            } else if (gateClock.elapsed() >= counterTimeoutMs) {
                endSession(yasmMode ? QStringLiteral("Последовательность ЯСМ за 5 с не найдена.")
                                    : QStringLiteral("RS-485: за 5 с не получены два непрерывных периода."));
            }
            return;
        }

        bool complete;
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            if (!reading) return;
            if (groupCount < groupsPerSecond && !groupGap && captureClock.elapsed() < captureTimeoutMs) return;
            reading = false;
            complete = !groupGap && groupCount == groupsPerSecond && codes.count;
        }
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
        activeCheck = checkList->currentIndex();
        activeChannel = channelList->currentIndex();
        pointActive = true;
        ++pointId;
        checkList->setEnabled(false);
        channelList->setEnabled(false);
        expectedState->setEnabled(false);
        updateCaptureControls();
        if (checks.at(activeCheck).scale) {
            requestMeter(false);
        } else {
            if (checks.at(activeCheck).bits == 1)
                state = static_cast<unsigned>(expectedState->currentIndex() - 1);
            beginFrameCapture();
        }
    }

    void beginFrameCapture()
    {
        std::lock_guard<std::mutex> lock(dataMutex);
        codes = {};
        groupCount = 0;
        groupGap = false;
        captureClock.start();
        reading = true;
    }

    QString pointName() const
    {
        const auto& check = checks.at(activeCheck);
        return QStringLiteral("Строка %1 · %2 · %3.")
            .arg(check.number).arg(QString::fromStdString(check.requirement),
                                  QString::fromStdString(check.addresses.at(activeChannel)));
    }

    void finishPoint(const QString& message)
    {
        log->appendPlainText(message);
        pointActive = false;
        checkList->setEnabled(true);
        channelList->setEnabled(true);
        expectedState->setEnabled(true);
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
            beginFrameCapture();
            return;
        }
        const auto& scale = *checks.at(activeCheck).scale;
        const auto compared = compareVoltage(scale, codes, referenceBefore, result.volts);
        const auto format = [](double volts) { return QString::number(volts, 'f', 3); };
        QString detail = QStringLiteral(" В7 до/после %1 / %2 В. Код %3–%4 (%5 отсч.), ЯГР %6–%7 В. Макс. |Δ| %8 В.")
            .arg(format(referenceBefore), format(result.volts)).arg(codes.minimum).arg(codes.maximum).arg(codes.count)
            .arg(format(compared.minimumVolts), format(compared.maximumVolts), format(compared.maximumErrorVolts));
        if (compared.toleranceVolts) {
            const auto limit = *compared.toleranceVolts;
            if (std::abs(referenceBefore - result.volts) > limit) {
                detail += QStringLiteral(" Уровень В7 изменился сильнее допуска %1 В. Точка не оценена.").arg(format(limit));
            } else {
                detail += QStringLiteral(" Допуск точки ±%1 В. %2.").arg(format(limit),
                    compared.maximumErrorVolts <= limit ? QStringLiteral("Точка в допуске")
                                                        : QStringLiteral("Точка вне допуска"));
            }
        } else {
            detail += QStringLiteral(" Диагностическое сравнение.");
        }
        finishPoint(pointName() + detail);
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
        reading = gatePassed = false;
    }

    void endSession(const QString& message)
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
        updateCaptureControls();
    }

    BrpTuWindow& window;
    const bool simulation;
    const QDir root;
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
    CounterPlot* graph = nullptr;
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
