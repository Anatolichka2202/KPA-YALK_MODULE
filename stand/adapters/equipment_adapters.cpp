#include "orbita_stand/equipment_adapters.h"
#include "orbita_stand/visa_instrument.h"

#include <QEventLoop>
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
    const QString utf8 = QString::fromUtf8(body);
    const QString local = QString::fromLocal8Bit(body);
    if (!utf8.contains(QStringLiteral("успешно"), Qt::CaseInsensitive)
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
    if (impl_->config.resetChannels.empty()) {
        throw std::runtime_error("ISD reset channel list is not configured");
    }
    for (unsigned channel : impl_->config.resetChannels) disconnectChannel(channel);
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
                                            : requiredAddress(config.localAddress, "UBSI local address")) {}
    UbsiUdpConfig config;
    QHostAddress adapter;
    QHostAddress local;
};
UbsiUdpAdapter::UbsiUdpAdapter(UbsiUdpConfig config) : impl_(std::make_unique<Impl>(std::move(config))) {}
UbsiUdpAdapter::~UbsiUdpAdapter() = default;
bool UbsiUdpAdapter::waitForPassivePacket()
{
    QUdpSocket socket;
    if (!socket.bind(impl_->local, impl_->config.commandAndDataPort,
                     QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        throw qtError("UBSI data port bind failed", socket.errorString());
    }
    if (!socket.waitForReadyRead(impl_->config.timeoutMilliseconds)) return false;
    while (socket.hasPendingDatagrams()) {
        const auto packet = socket.receiveDatagram();
        if (packet.senderAddress() == impl_->adapter) return true;
    }
    return false;
}
void UbsiUdpAdapter::selectMode(std::uint8_t mode, bool single)
{
    QUdpSocket acknowledgement;
    if (!acknowledgement.bind(impl_->local, impl_->config.acknowledgementPort,
                              QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        throw qtError("UBSI acknowledgement port bind failed", acknowledgement.errorString());
    }
    const auto bytes = modeCommand(mode, single);
    const QByteArray command(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    QUdpSocket sender;
    if (sender.writeDatagram(command, impl_->adapter, impl_->config.commandAndDataPort) != command.size()) {
        throw qtError("UBSI mode command send failed", sender.errorString());
    }
    if (!acknowledgement.waitForReadyRead(impl_->config.timeoutMilliseconds)) {
        throw std::runtime_error("UBSI adapter did not acknowledge mode command");
    }
    while (acknowledgement.hasPendingDatagrams()) {
        const auto packet = acknowledgement.receiveDatagram();
        if (packet.senderAddress() == impl_->adapter && packet.data() == command) return;
    }
    throw std::runtime_error("UBSI adapter returned an invalid mode acknowledgement");
}
void UbsiUdpAdapter::sendRawCommand(const std::vector<std::uint8_t>& bytes)
{
    if (bytes.empty()) throw std::invalid_argument("UBSI raw command must not be empty");
    const QByteArray command(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    QUdpSocket sender;
    if (sender.writeDatagram(command, impl_->adapter, impl_->config.commandAndDataPort)
        != command.size()) {
        throw qtError("UBSI raw command send failed", sender.errorString());
    }
}
std::vector<std::uint8_t> UbsiUdpAdapter::modeCommand(std::uint8_t mode, bool single)
{
    return {0x44, static_cast<std::uint8_t>(0x01 | (single ? 0x02 : 0x00)), mode};
}
std::vector<std::uint16_t> UbsiUdpAdapter::decodeYalkPacket(
    const std::vector<std::uint8_t>& packet, std::uint16_t mask)
{
    if (packet.size() != 200) {
        throw std::invalid_argument("YALK packet must contain exactly 200 bytes");
    }
    if (!mask) throw std::invalid_argument("YALK word mask must not be zero");
    std::vector<std::uint16_t> result;
    result.reserve(100);
    for (std::size_t index = 0; index < packet.size(); index += 2) {
        const std::uint16_t word = static_cast<std::uint16_t>(packet[index])
            | static_cast<std::uint16_t>(packet[index + 1]) << 8;
        result.push_back(word & mask);
    }
    return result;
}
std::vector<std::uint8_t> UbsiUdpAdapter::receiveRawPacket()
{
    QUdpSocket socket;
    if (!socket.bind(impl_->local, impl_->config.commandAndDataPort,
                     QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        throw qtError("UBSI data port bind failed", socket.errorString());
    }
    if (!socket.waitForReadyRead(impl_->config.timeoutMilliseconds)) {
        throw std::runtime_error("UBSI adapter data timeout");
    }
    while (socket.hasPendingDatagrams()) {
        const auto packet = socket.receiveDatagram();
        if (packet.senderAddress() != impl_->adapter) continue;
        const QByteArray data = packet.data();
        return {reinterpret_cast<const std::uint8_t*>(data.constData()),
                reinterpret_cast<const std::uint8_t*>(data.constData()) + data.size()};
    }
    throw std::runtime_error("UBSI packet came from an unexpected address");
}

} // namespace orbita::stand
