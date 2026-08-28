#include "orbita_stand/equipment_adapters.h"
#include "orbita_stand/yalk_frame.h"
#include "orbita_stand/visa_instrument.h"

#include <QEventLoop>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkDatagram>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSerialPort>
#include <QRegularExpression>
#include <QTimer>
#include <QUdpSocket>
#include <QUrl>

#include <cmath>
#include <algorithm>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace orbita::stand {
namespace {

std::runtime_error qtError(const std::string& prefix, const QString& detail)
{
    return std::runtime_error(prefix + ": " + detail.toUtf8().toStdString());
}

QHostAddress requiredAddress(const std::string& value, const char* name)
{
    QHostAddress address(QString::fromStdString(value));
    if (address.protocol() != QAbstractSocket::IPv4Protocol) {
        throw std::invalid_argument(std::string(name) + " must be an IPv4 address");
    }
    return address;
}

QByteArray httpGet(const IsdHttpConfig& config, const QString& path)
{
    if (config.host.empty()) throw std::invalid_argument("ISD host is empty");
    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(QString::fromStdString(config.host));
    url.setPort(config.port);
    url.setPath(path);
    QNetworkAccessManager manager;
    QNetworkReply* reply = manager.get(QNetworkRequest(url));
    QTimer timer;
    timer.setSingleShot(true);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    timer.start(static_cast<int>(config.timeoutMilliseconds));
    loop.exec();
    const auto error = reply->error();
    const QString errorText = reply->errorString();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    reply->deleteLater();
    if (error != QNetworkReply::NoError) throw qtError("ISD HTTP request failed", errorText);
    if (status < 200 || status >= 300) {
        throw std::runtime_error("ISD HTTP status " + std::to_string(status));
    }
    return body;
}

void requireIsdSuccess(const QByteArray& body)
{
    if (body.isEmpty()) throw std::runtime_error("ISD returned an empty response");
    const QByteArray trimmed = body.trimmed();
    const QString utf8 = QString::fromUtf8(body);
    const QString local = QString::fromLocal8Bit(body);
    // Фактическая прошивка стенда на 192.168.0.101 отвечает коротким "OK".
    // В старой Delphi-конфигурации встречается развёрнутое «успешно».
    if (trimmed.compare("OK", Qt::CaseInsensitive) != 0
        && !utf8.contains(QStringLiteral("успешно"), Qt::CaseInsensitive)
        && !local.contains(QStringLiteral("успешно"), Qt::CaseInsensitive)) {
        throw std::runtime_error("ISD did not confirm command: " + body.left(160).toStdString());
    }
}

QString finiteNumber(double value)
{
    if (!std::isfinite(value)) throw std::invalid_argument("equipment value must be finite");
    return QString::number(value, 'f', 6).remove(QRegularExpression(QStringLiteral("0+$")))
        .remove(QRegularExpression(QStringLiteral("\\.$")));
}

} // namespace

struct IsdHttpRouter::Impl { explicit Impl(IsdHttpConfig value) : config(std::move(value)) {} IsdHttpConfig config; };
IsdHttpRouter::IsdHttpRouter(IsdHttpConfig config) : impl_(std::make_unique<Impl>(std::move(config))) {}
IsdHttpRouter::~IsdHttpRouter() = default;
std::string IsdHttpRouter::probe() { return httpGet(impl_->config, QStringLiteral("/")).left(200).toStdString(); }
void IsdHttpRouter::reset()
{
    // Референсная Delphi-программа и KPA выполняют «сброс полный» одной
    // штатной командой type=4. Перебор каналов type=2 не эквивалентен ей.
    requireIsdSuccess(httpGet(impl_->config, QString::fromStdString(fullResetPath())));
}
void IsdHttpRouter::connectChannel(unsigned channel) { setSwitch(impl_->config.switchType, channel, true); }
void IsdHttpRouter::disconnectChannel(unsigned channel) { setSwitch(impl_->config.switchType, channel, false); }
void IsdHttpRouter::setSwitch(unsigned type, unsigned channel, bool enabled)
{
    requireIsdSuccess(httpGet(impl_->config, QString::fromStdString(switchPath(type, channel, enabled))));
}
void IsdHttpRouter::setAnalog(unsigned channel, unsigned value, bool enabled)
{
    requireIsdSuccess(httpGet(impl_->config, QString::fromStdString(analogPath(channel, value, enabled))));
}
std::string IsdHttpRouter::switchPath(unsigned type, unsigned channel, bool enabled)
{
    if (!type || !channel) throw std::invalid_argument("ISD type and channel start at one");
    return "/type=" + std::to_string(type) + "num=" + std::to_string(channel)
        + "val=" + (enabled ? "1" : "0");
}
std::string IsdHttpRouter::analogPath(unsigned channel, unsigned value, bool enabled)
{
    if (!channel) throw std::invalid_argument("ISD channel starts at one");
    return "/type=1num=" + std::to_string(channel) + "val=" + std::to_string(value)
        + "work=" + (enabled ? "1" : "0");
}
std::string IsdHttpRouter::fullResetPath()
{
    return "/type=4num=1";
}

struct RigolVisaGenerator::Impl {
    explicit Impl(RigolVisaConfig config)
        : instrument({std::move(config.resourceExpressions), config.timeoutMilliseconds}) {}
    VisaInstrument instrument;
};
RigolVisaGenerator::RigolVisaGenerator(RigolVisaConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}
RigolVisaGenerator::~RigolVisaGenerator() = default;
std::string RigolVisaGenerator::identity() { return impl_->instrument.query("*IDN?"); }
void RigolVisaGenerator::setSine(unsigned channel, double frequencyHz, double amplitudeVpp, double offsetVolts)
{
    if (channel < 1 || channel > 2 || frequencyHz <= 0.0 || amplitudeVpp < 0.0) {
        throw std::invalid_argument("invalid Rigol sine configuration");
    }
    impl_->instrument.write("VOLT:UNIT VPP");
    const std::string suffix = channel == 1 ? "" : ":CH2";
    impl_->instrument.write("APPL:SIN" + suffix + " "
        + finiteNumber(frequencyHz).toStdString() + ","
        + finiteNumber(amplitudeVpp).toStdString() + ","
        + finiteNumber(offsetVolts).toStdString());
    impl_->instrument.write("PHAS 0");
}
void RigolVisaGenerator::output(unsigned channel, bool enabled)
{
    if (channel < 1 || channel > 2) throw std::invalid_argument("Rigol channel must be 1 or 2");
    const std::string command = channel == 1 ? "OUTP " : "OUTP:CH2 ";
    impl_->instrument.write(command + (enabled ? "ON" : "OFF"));
}
const std::string& RigolVisaGenerator::resourceName() const { return impl_->instrument.resourceName(); }

struct R4831SerialAdapter::Impl { explicit Impl(R4831SerialConfig value) : config(std::move(value)) {} R4831SerialConfig config; };
R4831SerialAdapter::R4831SerialAdapter(R4831SerialConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}
R4831SerialAdapter::~R4831SerialAdapter() = default;
std::string R4831SerialAdapter::probe()
{
    if (impl_->config.portName.empty()) throw std::invalid_argument("R4831 COM port is empty");
    QSerialPort port(QString::fromStdString(impl_->config.portName));
    port.setBaudRate(impl_->config.baudRate);
    if (!port.open(QIODevice::ReadWrite)) throw qtError("R4831 COM open failed", port.errorString());
    port.close();
    return impl_->config.portName + " opened";
}
void R4831SerialAdapter::setResistance(double ohms)
{
    QSerialPort port(QString::fromStdString(impl_->config.portName));
    port.setBaudRate(impl_->config.baudRate);
    if (!port.open(QIODevice::WriteOnly)) throw qtError("R4831 COM open failed", port.errorString());
    const QByteArray command = QByteArray::fromStdString(
        resistanceCommand(ohms, impl_->config.decimalComma));
    if (port.write(command) != command.size() || !port.waitForBytesWritten(impl_->config.timeoutMilliseconds)) {
        throw qtError("R4831 write failed", port.errorString());
    }
    port.close();
}
std::string R4831SerialAdapter::resistanceCommand(double ohms, bool decimalComma)
{
    if (!std::isfinite(ohms) || ohms < 0.0) throw std::invalid_argument("R4831 resistance is invalid");
    std::string command = finiteNumber(ohms).toStdString();
    if (decimalComma) std::replace(command.begin(), command.end(), '.', ',');
    return command + "\r\n";
}

struct LegacyUdpPowerSupply::Impl {
    explicit Impl(LegacyUdpPowerSupplyConfig value) : config(std::move(value)), host(requiredAddress(config.host, "power supply host")) {}
    LegacyUdpPowerSupplyConfig config;
    QHostAddress host;
    void send(const QByteArray& command)
    {
        if (!config.allowLegacyCommands && command != "GETD\r") {
            throw std::runtime_error("legacy power-supply commands are not enabled in stand.ini");
        }
        QUdpSocket socket;
        if (socket.writeDatagram(command, host, config.commandPort) != command.size()) {
            throw qtError("power-supply UDP send failed", socket.errorString());
        }
    }
};
LegacyUdpPowerSupply::LegacyUdpPowerSupply(LegacyUdpPowerSupplyConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}
LegacyUdpPowerSupply::~LegacyUdpPowerSupply() = default;
std::string LegacyUdpPowerSupply::probe()
{
    QUdpSocket socket;
    if (!socket.bind(QHostAddress::AnyIPv4, impl_->config.localReplyPort)) {
        throw qtError("power-supply reply port bind failed", socket.errorString());
    }
    if (socket.writeDatagram("GETD\r", impl_->host, impl_->config.commandPort) != 5) {
        throw qtError("power-supply GETD send failed", socket.errorString());
    }
    if (!socket.waitForReadyRead(impl_->config.timeoutMilliseconds)) {
        throw std::runtime_error("power supply did not answer GETD");
    }
    while (socket.hasPendingDatagrams()) {
        QNetworkDatagram packet = socket.receiveDatagram();
        if (packet.senderAddress() == impl_->host) return packet.data().left(200).toStdString();
    }
    throw std::runtime_error("power supply reply came from an unexpected address");
}
void LegacyUdpPowerSupply::setVoltage(double volts)
{
    impl_->send(QByteArray::fromStdString(voltageCommand(volts)));
}
void LegacyUdpPowerSupply::setCurrent(double amperes)
{
    impl_->send(QByteArray::fromStdString(currentCommand(amperes)));
}
void LegacyUdpPowerSupply::outputOn() { impl_->send("SOUT 1\r"); }
void LegacyUdpPowerSupply::outputOff() { impl_->send("SOUT 0\r"); }
std::string LegacyUdpPowerSupply::voltageCommand(double volts)
{
    if (!std::isfinite(volts) || volts < 0.0 || volts > 99.99) throw std::invalid_argument("power-supply voltage is outside 0..99.99 V");
    const int value = static_cast<int>(std::lround(volts * 100.0));
    return QStringLiteral("VOLT 0%1\r").arg(value, 4, 10, QLatin1Char('0')).toStdString();
}
std::string LegacyUdpPowerSupply::currentCommand(double amperes)
{
    if (!std::isfinite(amperes) || amperes < 0.0 || amperes > 99.99) throw std::invalid_argument("power-supply current is outside 0..99.99 A");
    const int value = static_cast<int>(std::lround(amperes * 100.0));
    return QStringLiteral("CURR 0%1\r").arg(value, 4, 10, QLatin1Char('0')).toStdString();
}

struct UbsiUdpAdapter::Impl {
    explicit Impl(UbsiUdpConfig value)
        : config(std::move(value)), adapter(requiredAddress(config.adapterHost, "UBSI adapter host")),
          local(config.localAddress.empty() ? QHostAddress::AnyIPv4
                                            : requiredAddress(config.localAddress, "UBSI local address"))
    {
        // Порт должен быть открыт до команды: рабочий адаптер начинает отдавать
        // короткую серию кадров сразу после ROKT select и позднее открытие их теряет.
        if (!dataSocket.bind(local, config.commandAndDataPort,
                             QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
            throw qtError("UBSI data port bind failed", dataSocket.errorString());
        }
    }
    UbsiUdpConfig config;
    QHostAddress adapter;
    QHostAddress local;
    QUdpSocket dataSocket;
    bool accepts(const QHostAddress& sender) const
    {
        return config.acceptAnySender || sender == adapter;
    }
};
UbsiUdpAdapter::UbsiUdpAdapter(UbsiUdpConfig config) : impl_(std::make_unique<Impl>(std::move(config))) {}
UbsiUdpAdapter::~UbsiUdpAdapter() = default;
bool UbsiUdpAdapter::waitForPassivePacket()
{
    if (!impl_->dataSocket.hasPendingDatagrams()
        && !impl_->dataSocket.waitForReadyRead(impl_->config.timeoutMilliseconds)) return false;
    while (impl_->dataSocket.hasPendingDatagrams()) {
        const auto packet = impl_->dataSocket.receiveDatagram();
        if (impl_->accepts(packet.senderAddress())) return true;
    }
    return false;
}
void UbsiUdpAdapter::selectMode(std::uint8_t mode, bool single)
{
    if (impl_->config.acceptAnySender) {
        throw std::runtime_error("Нельзя отправлять команду режиму адаптера без подтверждённого IP источника");
    }
    QUdpSocket separateAcknowledgement;
    QUdpSocket* acknowledgement = &impl_->dataSocket;
    if (impl_->config.acknowledgementPort != impl_->config.commandAndDataPort) {
        if (!separateAcknowledgement.bind(impl_->local, impl_->config.acknowledgementPort,
                                          QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
            throw qtError("UBSI acknowledgement port bind failed",
                          separateAcknowledgement.errorString());
        }
        acknowledgement = &separateAcknowledgement;
    }
    const auto bytes = modeCommand(mode, single);
    const QByteArray command(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (impl_->dataSocket.writeDatagram(command, impl_->adapter,
                                        impl_->config.commandAndDataPort) != command.size()) {
        throw qtError("UBSI mode command send failed", impl_->dataSocket.errorString());
    }
    // В рабочем варианте порт подтверждения совпадает с портом непрерывного
    // потока (1113). Игнорируем кадры данных 200/204 байта и ждём именно echo
    // трёхбайтовой команды до общего тайм-аута.
    QElapsedTimer elapsed;
    elapsed.start();
    while (elapsed.elapsed() < static_cast<qint64>(impl_->config.timeoutMilliseconds)) {
        const qint64 remaining = static_cast<qint64>(impl_->config.timeoutMilliseconds) - elapsed.elapsed();
        if (!acknowledgement->hasPendingDatagrams()
            && !acknowledgement->waitForReadyRead(
                static_cast<int>(std::max<qint64>(1, remaining)))) {
            break;
        }
        while (acknowledgement->hasPendingDatagrams()) {
            const auto packet = acknowledgement->receiveDatagram();
            if (packet.senderAddress() == impl_->adapter && packet.data() == command) return;
        }
    }
    throw std::runtime_error("UBSI adapter did not acknowledge mode command");
}
void UbsiUdpAdapter::sendRawCommand(const std::vector<std::uint8_t>& bytes)
{
    if (impl_->config.acceptAnySender) {
        throw std::runtime_error("Нельзя отправлять UDP-команду адаптеру без подтверждённого IP источника");
    }
    if (bytes.empty()) throw std::invalid_argument("UBSI raw command must not be empty");
    const QByteArray command(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (impl_->dataSocket.writeDatagram(command, impl_->adapter, impl_->config.commandAndDataPort)
        != command.size()) {
        throw qtError("UBSI raw command send failed", impl_->dataSocket.errorString());
    }
}
std::vector<std::uint8_t> UbsiUdpAdapter::modeCommand(std::uint8_t mode, bool single)
{
    return {0x44, static_cast<std::uint8_t>(0x01 | (single ? 0x02 : 0x00)), mode};
}

namespace {
std::vector<std::uint8_t> rokotPacket(std::uint8_t command)
{
    std::vector<std::uint8_t> packet(128, 0);
    packet[0] = 'R';
    packet[1] = 'O';
    packet[2] = 'K';
    packet[3] = 'T';
    packet[4] = command;
    return packet;
}
}

std::vector<std::uint8_t> UbsiUdpAdapter::rokotResetCommand()
{
    return rokotPacket(0x16);
}

std::vector<std::uint8_t> UbsiUdpAdapter::rokotConfigureYalkCommand(
    std::uint8_t adapterChannel, std::uint8_t addressCount,
    bool slowParameters, bool fastParameters)
{
    if (!adapterChannel || !addressCount) {
        throw std::invalid_argument("ROKOT YALK adapter channel and address count start at one");
    }
    auto packet = rokotPacket(0x14);
    packet[5] = adapterChannel;
    packet[6] = addressCount;
    packet[7] = slowParameters ? 1 : 0;
    packet[8] = fastParameters ? 1 : 0;
    return packet;
}

std::vector<std::uint8_t> UbsiUdpAdapter::rokotConfigureYtpCommand(
    std::uint8_t adapterChannel, std::uint8_t firstAddress, std::uint8_t addressCount)
{
    if (!adapterChannel || !firstAddress || !addressCount) {
        throw std::invalid_argument("ROKOT YTP values start at one");
    }
    auto packet = rokotPacket(0x15);
    packet[5] = adapterChannel;
    packet[6] = firstAddress;
    packet[7] = addressCount;
    return packet;
}

std::vector<std::uint8_t> UbsiUdpAdapter::rokotSelectYalkCommand(std::uint8_t yalkNumber)
{
    if (!yalkNumber) throw std::invalid_argument("ROKOT YALK number starts at one");
    auto packet = rokotPacket(0x0A);
    // Сценарий KPA «ЯЛК -я1 -кад_вх -п3» даёт 0A 00 00 01.
    packet[7] = yalkNumber;
    return packet;
}
unsigned UbsiUdpAdapter::wordIndexForUlkAddress(unsigned address)
{
    if (address < 1 || address > 100) {
        throw std::invalid_argument("ULK address must be in range 1..100");
    }
    return address - 1;
}
std::vector<std::uint16_t> UbsiUdpAdapter::decodeYalkPacket(
    const std::vector<std::uint8_t>& packet, std::uint16_t mask)
{
    // Архивная прошивка передаёт 100 слов (200 байт). Работающий вариант
    // KPA/Rokot на UDP 1113 добавляет перед ними четырёхбайтный заголовок.
    // Заголовок относится к транспорту, а не к данным ЯЛК.
    if (!mask) throw std::invalid_argument("YALK word mask must not be zero");
    const auto samples = decodeYalkSlowFrame(packet);
    std::vector<std::uint16_t> result;
    result.reserve(100);
    for (const auto& sample : samples) result.push_back(sample.rawWord & mask);
    return result;
}
std::vector<std::uint8_t> UbsiUdpAdapter::receiveRawPacket()
{
    if (!impl_->dataSocket.hasPendingDatagrams()
        && !impl_->dataSocket.waitForReadyRead(impl_->config.timeoutMilliseconds)) {
        throw std::runtime_error("UBSI adapter data timeout");
    }
    while (impl_->dataSocket.hasPendingDatagrams()) {
        const auto packet = impl_->dataSocket.receiveDatagram();
        if (!impl_->accepts(packet.senderAddress())) continue;
        const QByteArray data = packet.data();
        return {reinterpret_cast<const std::uint8_t*>(data.constData()),
                reinterpret_cast<const std::uint8_t*>(data.constData()) + data.size()};
    }
    throw std::runtime_error("UBSI packet came from an unexpected address");
}

} // namespace orbita::stand
