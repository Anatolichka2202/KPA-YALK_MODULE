#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHostAddress>
#include <QLabel>
#include <QPalette>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>
#include <QNetworkDatagram>
#include <QRegularExpression>
#include <QVBoxLayout>

#include <array>
#include <algorithm>
#include <cmath>

namespace {

class SimulatorWindow final : public QWidget {
public:
    SimulatorWindow()
    {
        setWindowTitle(QStringLiteral("MilTech Station — имитатор оборудования"));
        resize(1180, 780);

        auto* root = new QVBoxLayout(this);
        auto* title = new QLabel(QStringLiteral(
            "<b>Локальный стенд</b> · Адаптер 127.0.0.2:1113 · ИСД 127.0.0.1:18080 · SCPI 127.0.0.1:15025"));
        root->addWidget(title);

        auto* actions = new QHBoxLayout;
        auto* launch = new QPushButton(QStringLiteral("Запустить MilTech Station"));
        auto* normal = new QPushButton(QStringLiteral("Все в норме"));
        streamEnabled_ = new QCheckBox(QStringLiteral("Поток Адаптера"));
        streamEnabled_->setChecked(true);
        actions->addWidget(launch);
        actions->addWidget(normal);
        actions->addWidget(streamEnabled_);
        actions->addStretch();
        root->addLayout(actions);

        auto* tabs = new QTabWidget;
        tabs->addTab(makeCommonTab(), QStringLiteral("Оборудование"));
        tabs->addTab(makeYalkTab(), QStringLiteral("ЯЛК · 100 слов"));
        tabs->addTab(makeYtpTab(), QStringLiteral("ЯТП · 30 каналов"));
        root->addWidget(tabs, 1);

        log_ = new QPlainTextEdit;
        log_->setReadOnly(true);
        log_->setMaximumBlockCount(500);
        log_->setMaximumHeight(150);
        root->addWidget(log_);

        connect(launch, &QPushButton::clicked, this, [this] { launchStation(); });
        connect(normal, &QPushButton::clicked, this, [this] { setNormal(); });

        connect(&udp_, &QUdpSocket::readyRead, this, [this] { receiveAdapterCommands(); });
        if (!udp_.bind(QHostAddress(QStringLiteral("127.0.0.2")), 1113,
                       QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
            appendLog(QStringLiteral("ОШИБКА UDP: %1").arg(udp_.errorString()));
        }
        connect(&frameTimer_, &QTimer::timeout, this, [this] { sendFrame(); });
        frameTimer_.start(2);

        startTcpServer(isdServer_, 18080, [this](QTcpSocket* socket, const QByteArray& request) {
            handleIsd(socket, request);
        });
        startTcpServer(scpiServer_, 15025, [this](QTcpSocket* socket, const QByteArray& request) {
            handleScpi(socket, request);
        });
        setNormal();
    }

private:
    QWidget* makeCommonTab()
    {
        auto* page = new QWidget;
        auto* columns = new QHBoxLayout(page);
        auto* supply = new QGroupBox(QStringLiteral("Питание УБСИ"));
        auto* sf = new QFormLayout(supply);
        current_ = number(0.24, 0.0, 2.0, 3, QStringLiteral(" А"));
        voltageError_ = number(0.0, -5.0, 5.0, 3, QStringLiteral(" В"));
        sf->addRow(QStringLiteral("Потребление"), current_);
        sf->addRow(QStringLiteral("Ошибка напряжения"), voltageError_);
        supplyVoltage_ = new QLabel(QStringLiteral("0 В"));
        outputState_ = new QLabel(QStringLiteral("ВЫКЛ"));
        sf->addRow(QStringLiteral("Команда источника"), supplyVoltage_);
        sf->addRow(QStringLiteral("Выход"), outputState_);
        columns->addWidget(supply, 0, Qt::AlignTop);

        auto* signal = new QGroupBox(QStringLiteral("Общие воздействия"));
        auto* vf = new QFormLayout(signal);
        referenceError_ = number(0.0, -1.0, 1.0, 4, QStringLiteral(" В"));
        acVoltage_ = number(0.137, 0.0, 20.0, 4, QStringLiteral(" В RMS"));
        frequencyError_ = number(0.0, -100.0, 100.0, 3, QStringLiteral(" Гц"));
        vf->addRow(QStringLiteral("Ошибка В7 DC"), referenceError_);
        vf->addRow(QStringLiteral("В7 AC"), acVoltage_);
        vf->addRow(QStringLiteral("Ошибка частоты"), frequencyError_);
        columns->addWidget(signal, 0, Qt::AlignTop);

        auto* faults = new QGroupBox(QStringLiteral("Доступность"));
        auto* ff = new QVBoxLayout(faults);
        isdOnline_ = check(QStringLiteral("ИСД HTTP"), true, ff);
        scpiOnline_ = check(QStringLiteral("АКИП / В7 / генератор SCPI"), true, ff);
        columns->addWidget(faults, 0, Qt::AlignTop);
        columns->addStretch();
        return page;
    }

    QWidget* makeYalkTab()
    {
        auto* page = new QWidget;
        auto* layout = new QVBoxLayout(page);
        auto* row = new QHBoxLayout;
        yalkNoise_ = number(1.0, 0.0, 50.0, 1, QStringLiteral(" код"));
        row->addWidget(new QLabel(QStringLiteral("Колебание каждого слова ±")));
        row->addWidget(yalkNoise_);
        row->addWidget(new QLabel(QStringLiteral(
            "Смещение применяется поверх реального воздействия ИСД. Контакт: авто / 0 / 1.")));
        row->addStretch();
        layout->addLayout(row);
        yalkTable_ = new QTableWidget(100, 3);
        yalkTable_->setHorizontalHeaderLabels({QStringLiteral("Адрес"), QStringLiteral("Смещение, код"), QStringLiteral("Контакт")});
        yalkTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        for (int i = 0; i < 100; ++i) {
            auto* address = new QTableWidgetItem(QString::number(i + 1));
            address->setFlags(address->flags() & ~Qt::ItemIsEditable);
            yalkTable_->setItem(i, 0, address);
            yalkTable_->setItem(i, 1, new QTableWidgetItem(QStringLiteral("0")));
            yalkTable_->setItem(i, 2, new QTableWidgetItem(QStringLiteral("авто")));
        }
        layout->addWidget(yalkTable_);
        return page;
    }

    QWidget* makeYtpTab()
    {
        auto* page = new QWidget;
        auto* layout = new QVBoxLayout(page);
        auto* row = new QHBoxLayout;
        ytpResistance_ = number(0.0, 0.0, 240.0, 3, QStringLiteral(" Ом"));
        ytpNoise_ = number(1.0, 0.0, 100.0, 1, QStringLiteral(" raw"));
        row->addWidget(new QLabel(QStringLiteral("Фактический Р4831")));
        row->addWidget(ytpResistance_);
        row->addSpacing(30);
        row->addWidget(new QLabel(QStringLiteral("Колебание ±")));
        row->addWidget(ytpNoise_);
        row->addStretch();
        layout->addLayout(row);
        ytpTable_ = new QTableWidget(30, 2);
        ytpTable_->setHorizontalHeaderLabels({QStringLiteral("Канал"), QStringLiteral("Ошибка, Ом")});
        ytpTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        for (int i = 0; i < 30; ++i) {
            auto* channel = new QTableWidgetItem(QString::number(i + 1));
            channel->setFlags(channel->flags() & ~Qt::ItemIsEditable);
            ytpTable_->setItem(i, 0, channel);
            ytpTable_->setItem(i, 1, new QTableWidgetItem(QStringLiteral("0")));
        }
        layout->addWidget(ytpTable_);
        return page;
    }

    QDoubleSpinBox* number(double value, double low, double high, int decimals, const QString& suffix)
    {
        auto* box = new QDoubleSpinBox;
        box->setRange(low, high); box->setDecimals(decimals); box->setValue(value); box->setSuffix(suffix);
        return box;
    }

    QCheckBox* check(const QString& text, bool checked, QVBoxLayout* layout)
    {
        auto* box = new QCheckBox(text); box->setChecked(checked); layout->addWidget(box); return box;
    }

    template<class Handler>
    void startTcpServer(QTcpServer& server, quint16 port, Handler handler)
    {
        connect(&server, &QTcpServer::newConnection, this, [&server, handler] {
            while (auto* socket = server.nextPendingConnection()) {
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, handler] {
                    handler(socket, socket->readAll());
                });
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
        if (!server.listen(QHostAddress::LocalHost, port))
            appendLog(QStringLiteral("ОШИБКА TCP %1: %2").arg(port).arg(server.errorString()));
    }

    void launchStation()
    {
        const QString exe = QCoreApplication::applicationDirPath() + QStringLiteral("/MilTechStation.exe");
        auto* process = new QProcess(this);
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("MILTECH_STAND_PROFILE"), QStringLiteral("stand_ktma_simulator.yaml"));
        environment.insert(QStringLiteral("MILTECH_TIME_SCALE"), QStringLiteral("0.01"));
        process->setProcessEnvironment(environment);
        process->setProgram(exe);
        process->setWorkingDirectory(QCoreApplication::applicationDirPath());
        process->startDetached();
        appendLog(QStringLiteral("Запущена MilTech Station с локальным профилем, масштаб времени 1:100"));
    }

    void setNormal()
    {
        streamEnabled_->setChecked(true); isdOnline_->setChecked(true); scpiOnline_->setChecked(true);
        current_->setValue(0.24); voltageError_->setValue(0); referenceError_->setValue(0);
        yalkNoise_->setValue(1); ytpNoise_->setValue(1); acVoltage_->setValue(0.137); frequencyError_->setValue(0);
        for (int i = 0; i < 100; ++i) { yalkTable_->item(i, 1)->setText("0"); yalkTable_->item(i, 2)->setText(QStringLiteral("авто")); }
        for (int i = 0; i < 30; ++i) ytpTable_->item(i, 1)->setText("0");
        appendLog(QStringLiteral("Установлен профиль «Все в норме»"));
    }

    void receiveAdapterCommands()
    {
        while (udp_.hasPendingDatagrams()) {
            const auto datagram = udp_.receiveDatagram();
            const QByteArray bytes = datagram.data();
            if (bytes.size() == 128 && bytes.startsWith("ROKT") && quint8(bytes[4]) == 0x0A) {
                adapterMode_ = quint8(bytes[5]) == 0x02 ? 2 : quint8(bytes[5]) == 0x00 ? 1 : 3;
                appendLog(adapterMode_ == 1 ? QStringLiteral("Адаптер: поток ЯЛК 204")
                    : adapterMode_ == 2 ? QStringLiteral("Адаптер: поток ЯТП 68")
                    : QStringLiteral("Адаптер: команда ЯВП (сырой формат пока не декодируется Station)"));
            } else if (bytes.size() == 3 && quint8(bytes[0]) == 0x44 && quint8(bytes[1]) == 0x01) {
                adapterMode_ = quint8(bytes[2]) == 2 ? 4 : 1;
            }
        }
    }

    void sendFrame()
    {
        if (!streamEnabled_->isChecked() || adapterMode_ == 0) return;
        QByteArray frame;
        if (adapterMode_ == 2) frame = ytpFrame();
        else if (adapterMode_ == 4) frame = ytpLegacyFrame();
        else if (adapterMode_ == 1) frame = yalkFrame();
        else if (adapterMode_ == 3) frame = QByteArray(128, 0);
        else return;
        udp_.writeDatagram(frame, QHostAddress(QStringLiteral("127.0.0.1")), 1113);
    }

    QByteArray yalkFrame()
    {
        QByteArray frame(204, 0);
        const int phase = frameCounter_++ % 5 - 2;
        for (int i = 0; i < 100; ++i) {
            double code = 35.0;
            bool contact = true;
            if (i == 96) { code = 160; contact = false; }
            else if (i == 97 || i == 98) { code = 960; contact = true; }
            else if (i < 80 && yalkEnabled_[i]) {
                code = 160.0 + yalkVoltage_[i] / 6.2 * 800.0;
                contact = yalkVoltage_[i] >= 2.0;
            }
            bool ok = false;
            const double offset = yalkTable_->item(i, 1)->text().toDouble(&ok);
            if (ok) code += offset;
            code += phase * yalkNoise_->value() / 2.0;
            const QString override = yalkTable_->item(i, 2)->text().trimmed();
            if (override == "0") contact = false; else if (override == "1") contact = true;
            const quint16 raw = quint16(std::clamp(qRound(code), 0, 1023)) | (contact ? 0x0400 : 0);
            frame[4 + 2 * i] = char(raw & 0xff); frame[5 + 2 * i] = char(raw >> 8);
        }
        return frame;
    }

    QByteArray ytpFrame()
    {
        QByteArray frame(68, 0); frame[0]=1; frame[2]=0x34;
        const int phase = frameCounter_++ % 5 - 2;
        for (int i = 0; i < 32; ++i) {
            double raw = i == 30 ? 60000.0 : i == 31 ? 1000.0
                : 1000.0 + ytpResistance_->value() / 240.0 * 59000.0;
            if (i < 30) {
                bool ok = false; const double error = ytpTable_->item(i, 1)->text().toDouble(&ok);
                if (ok) raw += error / 240.0 * 59000.0;
                raw += phase * ytpNoise_->value() / 2.0;
            }
            const quint16 word = quint16(std::clamp(qRound(raw), 0, 65535));
            frame[4 + 2 * i] = char(word & 0xff); frame[5 + 2 * i] = char(word >> 8);
        }
        return frame;
    }

    QByteArray ytpLegacyFrame()
    {
        QByteArray modern = ytpFrame(), legacy(65, 0);
        for (int i=0;i<64;++i) legacy[i]=modern[i+4];
        return legacy;
    }

    void handleIsd(QTcpSocket* socket, const QByteArray& request)
    {
        if (!isdOnline_->isChecked()) { socket->disconnectFromHost(); return; }
        const int firstSpace = request.indexOf(' '), secondSpace = request.indexOf(' ', firstSpace + 1);
        const QString path = QString::fromLatin1(request.mid(firstSpace + 1, secondSpace - firstSpace - 1));
        if (path.contains("type=4")) {
            yalkEnabled_.fill(false); yalkVoltage_.fill(0.0);
        } else {
            QRegularExpression re(QStringLiteral("type=(\\d+)num=(\\d+)(?:val=([0-9.+-]+))?(?:work=(\\d))?"));
            const auto match = re.match(path);
            if (match.hasMatch()) {
                const int type=match.captured(1).toInt(), channel=match.captured(2).toInt();
                static constexpr std::array<int, 80> yalkAddress{
                    1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,
                    21,22,23,24,25,26,27,28,32,33,34,35,36,37,38,39,40,41,42,43,
                    45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,
                    65,66,67,68,69,70,74,75,76,77,78,79,80,81,82,83,84,85,86,87};
                const int address = channel >= 1 && channel <= int(yalkAddress.size())
                    ? yalkAddress[channel - 1] : channel;
                if (address >= 1 && address <= 100 && type == 5) {
                    yalkVoltage_[address-1]=match.captured(3).toDouble(); yalkEnabled_[address-1]=true;
                } else if (address >= 1 && address <= 100 && type == 1 && match.captured(4)=="0") {
                    yalkVoltage_[address-1]=0; yalkEnabled_[address-1]=false;
                }
            }
        }
        socket->write("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nOK");
        socket->disconnectFromHost();
    }

    void handleScpi(QTcpSocket* socket, const QByteArray& request)
    {
        if (!scpiOnline_->isChecked()) { socket->disconnectFromHost(); return; }
        const QByteArray cmd=request.trimmed(); QByteArray answer="OK";
        if (cmd=="*IDN?") answer="MILTECH,PROTOCOL-SIMULATOR,LOCAL,1.0";
        else if (cmd.startsWith("SOUR:VOLT ")) { supplySetVoltage_=cmd.mid(10).toDouble(); supplyVoltage_->setText(QString::number(supplySetVoltage_) + " В"); }
        else if (cmd.startsWith("SOUR:CURR ")) supplyCurrentLimit_=cmd.mid(10).toDouble();
        else if (cmd=="OUTP ON") { supplyOutput_=true; outputState_->setText(QStringLiteral("ВКЛ")); }
        else if (cmd=="OUTP OFF") { supplyOutput_=false; outputState_->setText(QStringLiteral("ВЫКЛ")); }
        else if (cmd=="OUTP?") answer=supplyOutput_ ? "1" : "0";
        else if (cmd=="MEAS:VOLT?") answer=QByteArray::number(supplyOutput_ ? supplySetVoltage_+voltageError_->value() : 0.0, 'g', 12);
        else if (cmd=="MEAS:CURR?" || cmd=="MEAS:CURR:DC?") answer=QByteArray::number(supplyOutput_ ? current_->value() : 0.0, 'g', 12);
        else if (cmd=="MEAS:VOLT:DC?") answer=QByteArray::number(referenceVoltage()+referenceError_->value(), 'g', 12);
        else if (cmd=="MEAS:VOLT:AC?") answer=QByteArray::number(acVoltage_->value(), 'g', 12);
        else if (cmd=="MEAS:FREQ?") answer=QByteArray::number(generatorFrequency_+frequencyError_->value(), 'g', 12);
        else if (cmd.startsWith("SOUR1:APPL:SIN ")) generatorFrequency_=cmd.mid(15).split(',').value(0).toDouble();
        else if (cmd=="OUTP1 ON") generatorOutput_=true;
        else if (cmd=="OUTP1 OFF") generatorOutput_=false;
        else answer="ERR unsupported command";
        socket->write(answer+'\n'); socket->disconnectFromHost();
    }

    double referenceVoltage() const
    {
        for (int i=0;i<80;++i) if (yalkEnabled_[i]) return yalkVoltage_[i];
        return 0.0;
    }

    void appendLog(const QString& text)
    {
        if (log_) log_->appendPlainText(QDateTime::currentDateTime().toString("HH:mm:ss.zzz  ") + text);
    }

    QUdpSocket udp_; QTcpServer isdServer_, scpiServer_; QTimer frameTimer_;
    int adapterMode_=0, frameCounter_=0;
    std::array<double,100> yalkVoltage_{}; std::array<bool,100> yalkEnabled_{};
    double supplySetVoltage_=0, supplyCurrentLimit_=0, generatorFrequency_=1000;
    bool supplyOutput_=false, generatorOutput_=false;
    QCheckBox *streamEnabled_=nullptr,*isdOnline_=nullptr,*scpiOnline_=nullptr;
    QDoubleSpinBox *current_=nullptr,*voltageError_=nullptr,*referenceError_=nullptr,*acVoltage_=nullptr,*frequencyError_=nullptr;
    QDoubleSpinBox *yalkNoise_=nullptr,*ytpResistance_=nullptr,*ytpNoise_=nullptr;
    QLabel *supplyVoltage_=nullptr,*outputState_=nullptr;
    QTableWidget *yalkTable_=nullptr,*ytpTable_=nullptr;
    QPlainTextEdit* log_=nullptr;
};

}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setStyle(QStringLiteral("Fusion"));
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#14171c"));
    palette.setColor(QPalette::WindowText, QColor("#e6eaf0"));
    palette.setColor(QPalette::Base, QColor("#0e1115"));
    palette.setColor(QPalette::AlternateBase, QColor("#1c2128"));
    palette.setColor(QPalette::Text, QColor("#e6eaf0"));
    palette.setColor(QPalette::Button, QColor("#1b2129"));
    palette.setColor(QPalette::ButtonText, QColor("#d7dee8"));
    palette.setColor(QPalette::Highlight, QColor("#2f80ed"));
    palette.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    app.setPalette(palette);
    app.setStyleSheet(QStringLiteral(
        "QWidget{font-size:13px} QGroupBox{border:1px solid #343c47;border-radius:6px;"
        "margin-top:12px;padding:12px} QGroupBox::title{subcontrol-origin:margin;left:10px}"
        "QPushButton{padding:7px 12px;border:1px solid #3c4653;border-radius:5px}"
        "QPushButton:hover{background:#263548} QHeaderView::section{background:#1b2129;padding:6px}"
        "QTabBar::tab{padding:8px 15px} QPlainTextEdit{border:1px solid #343c47}"));
    SimulatorWindow window; window.show();
    return app.exec();
}
