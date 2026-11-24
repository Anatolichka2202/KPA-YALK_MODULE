#include "brp_tu_window.h"

#include "frame_checks.h"
#include "hardware/akip1160.h"
#include "hardware/isd_router.h"
#include "metadata_service.h"
#include "miltech/orbita_sample_bridge.h"
#include "miltech/orbita_sample_simulator.h"
#include "orbita_stand/sample_source.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QSpinBox>
#include <QTimer>
#include <QtConcurrentRun>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <mutex>
#include <stdexcept>

namespace {
using namespace miltech::brp;
using namespace orbita::stand;

constexpr int groupsPerSecond = 128;
constexpr int counterTimeoutMs = 5000;
constexpr int captureTimeoutMs = 3000;

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
        auto* interfaceBox = new QGroupBox(QStringLiteral("1. Интерфейс RS-485 → Орбита"), &window);
        auto* interfaceLayout = new QVBoxLayout(interfaceBox);
        auto* interfaceControls = new QHBoxLayout;
        port = new QComboBox(interfaceBox);
        port->setEditable(true);
        port->lineEdit()->setPlaceholderText(QStringLiteral("COM-порт RS-485"));
        for (const auto& entry : QSerialPortInfo::availablePorts()) port->addItem(entry.portName());
        port->setCurrentIndex(-1);
        port->setVisible(!simulation);
        start = new QPushButton(QStringLiteral("Проверить интерфейс"), interfaceBox);
        stop = new QPushButton(QStringLiteral("Остановить сбор"), interfaceBox);
        stop->setEnabled(false);
        interfaceControls->addWidget(port);
        interfaceControls->addWidget(start);
        interfaceControls->addWidget(stop);
        interfaceControls->addStretch();
        interfaceLayout->addLayout(interfaceControls);

        status = new QLabel(simulation
            ? QStringLiteral("Синтетические кадры M16. COM-порт и стендовые приборы отключены.")
            : QStringLiteral("Подготовьте питание и подключите RS-485 и вход декодера Орбита."), interfaceBox);
        status->setWordWrap(true);
        interfaceLayout->addWidget(status);
        graph = new CounterPlot(interfaceBox);
        interfaceLayout->addWidget(graph);
        layout->addWidget(interfaceBox, 1);

        auto* checksBox = new QGroupBox(QStringLiteral("2. Кадровой контроль, раздел 3.2"), &window);
        auto* checksLayout = new QFormLayout(checksBox);
        checkList = new QComboBox(checksBox);
        channelList = new QComboBox(checksBox);
        details = new QLabel(checksBox);
        details->setWordWrap(true);
        expectedMin = new QSpinBox(checksBox);
        expectedMax = new QSpinBox(checksBox);
        expectedMin->setSpecialValueText(QStringLiteral("Задать"));
        expectedMax->setSpecialValueText(QStringLiteral("Задать"));
        auto* expected = new QHBoxLayout;
        expected->addWidget(expectedMin);
        expected->addWidget(new QLabel(QStringLiteral("–"), checksBox));
        expected->addWidget(expectedMax);
        expected->addStretch();
        capture = new QPushButton(QStringLiteral("Сравнить коды за 1 с кадра"), checksBox);
        capture->setEnabled(false);
        checksLayout->addRow(QStringLiteral("Строка таблицы 1"), checkList);
        checksLayout->addRow(QStringLiteral("Адрес Орбита"), channelList);
        checksLayout->addRow(details);
        checksLayout->addRow(QStringLiteral("Ожидаемый интервал кода"), expected);
        checksLayout->addRow(capture);
        layout->addWidget(checksBox);

        if (!simulation) {
            auto* instruments = new QGroupBox(QStringLiteral("Оборудование стенда"), &window);
            auto* instrumentsLayout = new QHBoxLayout(instruments);
            auto* isdHost = new QLineEdit(instruments);
            auto* akipPort = new QLineEdit(instruments);
            isdHost->setPlaceholderText(QStringLiteral("Адрес ИСД"));
            akipPort->setPlaceholderText(QStringLiteral("COM-порт АКИП"));
            auto* probe = new QPushButton(QStringLiteral("Проверить ИСД и АКИП"), instruments);
            instrumentsLayout->addWidget(isdHost);
            instrumentsLayout->addWidget(akipPort);
            instrumentsLayout->addWidget(probe);
            layout->addWidget(instruments);
            QObject::connect(probe, &QPushButton::clicked, &window, [this, probe, isdHost, akipPort] {
                const auto host = isdHost->text().trimmed();
                const auto supplyPort = akipPort->text().trimmed();
                if (host.isEmpty() && supplyPort.isEmpty()) return;
                if (!supplyPort.isEmpty() && supplyPort.compare(port->currentText().trimmed(), Qt::CaseInsensitive) == 0) {
                    log->appendPlainText(QStringLiteral("АКИП и RS-485 требуют разных COM-портов."));
                    return;
                }
                probe->setEnabled(false);
                auto* watcher = new QFutureWatcher<QString>(&this->window);
                QObject::connect(watcher, &QFutureWatcherBase::finished, &this->window, [this, watcher, probe] {
                    log->appendPlainText(watcher->result());
                    watcher->deleteLater();
                    probe->setEnabled(true);
                });
                watcher->setFuture(QtConcurrent::run(readEquipment, host, supplyPort));
            });
        }

        log = new QPlainTextEdit(&window);
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
            if (!name.isEmpty()) label += QStringLiteral(" · БД: %1").arg(name);
            channelList->addItem(label);
        }
        details->setText(QStringLiteral("%1. %2").arg(QString::fromStdString(check.requirement),
                                                         QString::fromStdString(check.note)));
        const int maximumCode = (1 << check.bits) - 1;
        expectedMin->setRange(-1, maximumCode);
        expectedMax->setRange(-1, maximumCode);
        expectedMin->setValue(simulation ? std::min(512, maximumCode) : -1);
        expectedMax->setValue(simulation ? std::min(512, maximumCode) : -1);
        capture->setEnabled(gatePassed && !check.addresses.empty());
    }

    void startInterface()
    {
        try {
            if (!simulation) {
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
                source = miltech::integration::createOrbitaSampleSimulator(simulatedWord);
                if (!source->open()) throw std::runtime_error("Имитатор Орбита не открылся");
            } else {
                QFile file(root.filePath(QStringLiteral("data/stand_brp.yaml")));
                if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("Профиль E20-10 не открыт");
                const auto config = YAML::Load(file.readAll().toStdString());
                if (config["schema"].as<int>() != 1 || !config["e2010"].IsMap())
                    throw std::runtime_error("Неверная схема профиля E20-10");
                source = createE2010SampleSource(config["e2010"].as<std::map<std::string, std::string>>());
            }

            // Состояние обнуляется до подключения callback. Перед любым следующим
            // запуском shutdown ждёт завершения потока, который изменяет эти поля.
            counter = {};
            observeCounter = simulation;
            nextCounter = 0;
            gatePassed = false;
            decoder = std::make_unique<orbita::Orbita>();
            decoder->setGroupCallback([this](const auto& words, int group, int cycle) {
                consume(words, group, cycle);
            });
            bridge = std::make_unique<miltech::integration::OrbitaSampleBridge>(*source, *decoder);
            if (!bridge->start()) throw std::runtime_error(bridge->lastError());
            start->setEnabled(false);
            stop->setEnabled(true);
            port->setEnabled(false);
            graph->setTrace({});
            status->setText(QStringLiteral("Ожидание двух непрерывных периодов счётчика 0–1023"));
            gateClock.start();
            refresh.start(60);
            if (!simulation) send.start(10);
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

        if (!gatePassed) {
            std::vector<unsigned> trace;
            {
                std::lock_guard<std::mutex> lock(dataMutex);
                gatePassed = counter.passed();
                trace = counter.trace();
            }
            graph->setTrace(std::move(trace));
            if (gatePassed) {
                send.stop();
                if (serial.isOpen()) serial.close();
                status->setText(simulation
                    ? QStringLiteral("Имитатор: приняты два непрерывных периода. Доступны кадровые сравнения.")
                    : QStringLiteral("Интерфейс: приняты два непрерывных периода. Доступны кадровые сравнения."));
                log->appendPlainText(status->text());
                displayCheck(checkList->currentIndex());
            } else if (gateClock.elapsed() >= counterTimeoutMs) {
                endSession(QStringLiteral("Интерфейс: за 5 с не получены два непрерывных периода."));
            }
            return;
        }

        std::lock_guard<std::mutex> lock(dataMutex);
        if (!reading) return;
        if (groupCount < groupsPerSecond && !groupGap && captureClock.elapsed() < captureTimeoutMs) return;
        reading = false;
        checkList->setEnabled(true);
        channelList->setEnabled(true);
        expectedMin->setEnabled(true);
        expectedMax->setEnabled(true);
        capture->setEnabled(true);
        const auto& check = checks.at(activeCheck);
        const auto name = QString::fromStdString(check.addresses.at(activeChannel));
        if (groupGap || groupCount < groupsPerSecond || !codes.count) {
            log->appendPlainText(QStringLiteral("Строка %1, %2: полный набор последовательных групп не получен.")
                .arg(check.number).arg(name));
            return;
        }
        log->appendPlainText(QStringLiteral("Строка %1, %2: %3 отсч., коды %4–%5, ожидание %6–%7. %8.")
            .arg(check.number).arg(name).arg(codes.count).arg(codes.minimum).arg(codes.maximum)
            .arg(low).arg(high).arg(codes.matches(low, high) ? QStringLiteral("Критерий выполнен")
                                                           : QStringLiteral("Критерий не выполнен")));
    }

    void startCapture()
    {
        if (!gatePassed || reading || checkList->currentIndex() < 0 || channelList->currentIndex() < 0) return;
        if (expectedMin->value() < 0 || expectedMax->value() < 0) {
            log->appendPlainText(QStringLiteral("Задайте нижнюю и верхнюю границы ожидаемого кода."));
            return;
        }
        if (expectedMin->value() > expectedMax->value()) {
            log->appendPlainText(QStringLiteral("Нижний предел превышает верхний."));
            return;
        }
        std::lock_guard<std::mutex> lock(dataMutex);
        activeCheck = checkList->currentIndex();
        activeChannel = channelList->currentIndex();
        low = expectedMin->value();
        high = expectedMax->value();
        codes = {};
        groupCount = 0;
        groupGap = false;
        captureClock.start();
        reading = true;
        checkList->setEnabled(false);
        channelList->setEnabled(false);
        expectedMin->setEnabled(false);
        expectedMax->setEnabled(false);
        capture->setEnabled(false);
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
        checkList->setEnabled(true);
        channelList->setEnabled(true);
        expectedMin->setEnabled(true);
        expectedMax->setEnabled(true);
        capture->setEnabled(false);
    }

    BrpTuWindow& window;
    const bool simulation;
    const QDir root;
    MetadataService metadata;
    std::vector<FrameCheck> checks;
    QComboBox *port = nullptr, *checkList = nullptr, *channelList = nullptr;
    QPushButton *start = nullptr, *stop = nullptr, *capture = nullptr;
    QLabel *status = nullptr, *details = nullptr;
    QSpinBox *expectedMin = nullptr, *expectedMax = nullptr;
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
    bool observeCounter = false;
    unsigned nextCounter = 0;
    bool gatePassed = false;
    bool reading = false, groupGap = false;
    int groupCount = 0, lastOrdinal = 0;
    std::size_t activeCheck = 0, activeChannel = 0;
    unsigned low = 0, high = 0;
    CodeRange codes;
};

BrpTuWindow::BrpTuWindow(bool simulation, QWidget* parent)
    : QWidget(parent), impl_(std::make_unique<Impl>(*this, simulation))
{}

BrpTuWindow::~BrpTuWindow() = default;
