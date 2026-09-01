#include "orbita_stand/equipment_adapters.h"
#include "orbita_stand/visa_instrument.h"

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#endif

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
#include <cctype>
#include <chrono>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <thread>
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

    constexpr unsigned maxAttempts = 3;
    constexpr auto retryDelay = std::chrono::milliseconds(250);

    for(unsigned attempt = 1; attempt <=maxAttempts; ++attempt)
    {

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

    if (error != QNetworkReply::NoError) {
    if (status < 200 || status >= 300) {
        throw std::runtime_error("ISD HTTP status " + std::to_string(status));
            }
        return body;
    }
    const bool transient =
        error == QNetworkReply::OperationCanceledError
          ||error == QNetworkReply::TimeoutError
          ||error == QNetworkReply::TemporaryNetworkFailureError
          ||error == QNetworkReply::RemoteHostClosedError;
    if (!transient || attempt == maxAttempts)
        {
        throw qtError("ISD HTTP request failed", errorText);
        }
    std::this_thread::sleep_for(retryDelay);
    }
    throw std::runtime_error("ISD HTTP request failed");
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
void IsdHttpRouter::prepareYalk()
{
    reset();
    // Рабочая KPA выдерживает около 400 мс между type=4 и type=7.
    // ИСД не всегда принимает следующую HTTP-команду без этой паузы.
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    requireIsdSuccess(httpGet(impl_->config, QString::fromStdString(yalkPreparePath())));
}
void IsdHttpRouter::setYalkVoltage(unsigned channel, double volts)
{
    requireIsdSuccess(httpGet(impl_->config,
        QString::fromStdString(yalkVoltagePath(channel, volts))));
}
void IsdHttpRouter::disableYalkOutput(unsigned channel)
{
    requireIsdSuccess(httpGet(impl_->config,
        QString::fromStdString(yalkOutputBusOffPath(channel))));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    requireIsdSuccess(httpGet(impl_->config,
        QString::fromStdString(yalkOutputOffPath(channel))));
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
std::string IsdHttpRouter::yalkPreparePath()
{
    return "/type=7num=1";
}
std::string IsdHttpRouter::yalkVoltagePath(unsigned channel, double volts)
{
    if (!channel) throw std::invalid_argument("ISD channel starts at one");
    if (!std::isfinite(volts) || volts < 0.0 || volts > 6.2) {
        throw std::invalid_argument("YALK voltage must be in range 0.00..6.20 V");
    }
    std::ostringstream value;
    value.imbue(std::locale::classic());
    value << std::fixed << std::setprecision(2) << volts;
    return "/type=5num=" + std::to_string(channel) + "val=" + value.str()
        + "work=1bus=1";
}
std::string IsdHttpRouter::yalkOutputBusOffPath(unsigned channel)
{
    if (!channel) throw std::invalid_argument("ISD channel starts at one");
    return "/type=1num=" + std::to_string(channel) + "val=0work=1bus=0";
}
std::string IsdHttpRouter::yalkOutputOffPath(unsigned channel)
{
    if (!channel) throw std::invalid_argument("ISD channel starts at one");
    return "/type=1num=" + std::to_string(channel) + "val=0work=0";
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

struct Akip1160Serial::Impl {
    explicit Impl(Akip1160SerialConfig value) : config(std::move(value))
    {
        if (config.portName.empty()) throw std::invalid_argument("AKIP-1160/6 COM port is empty");
        if (config.baudRate <= 0) throw std::invalid_argument("AKIP-1160/6 baud rate is invalid");
        if (!config.timeoutMilliseconds) throw std::invalid_argument("AKIP-1160/6 timeout is zero");
    }

    Akip1160SerialConfig config;

    std::string exchange(const std::string& command, bool expectReply) const
    {
        QSerialPort port(QString::fromStdString(config.portName));
        port.setBaudRate(config.baudRate);
        port.setDataBits(QSerialPort::Data8);
        port.setParity(QSerialPort::NoParity);
        port.setStopBits(QSerialPort::OneStop);
        port.setFlowControl(QSerialPort::NoFlowControl);
        if (!port.open(QIODevice::ReadWrite)) {
            throw qtError("AKIP-1160/6 COM open failed", port.errorString());
        }

        // CH340 may need a short interval after opening before the first byte.
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        port.clear(QSerialPort::AllDirections);
        QByteArray wire = QByteArray::fromStdString(command);
        if (!wire.endsWith('\n')) wire.append('\n');
        if (port.write(wire) != wire.size()) {
            throw qtError("AKIP-1160/6 COM write failed", port.errorString());
        }
        if (port.bytesToWrite() > 0
            && !port.waitForBytesWritten(static_cast<int>(config.timeoutMilliseconds))) {
            throw qtError("AKIP-1160/6 COM write timeout", port.errorString());
        }
        if (!expectReply) return {};

        QElapsedTimer timer;
        timer.start();
        QByteArray reply;
        while (!reply.contains('\n')) {
            const auto remaining = static_cast<int>(config.timeoutMilliseconds)
                - static_cast<int>(timer.elapsed());
            if (remaining <= 0 || !port.waitForReadyRead(remaining)) break;
            reply += port.readAll();
        }
        if (reply.isEmpty()) throw std::runtime_error("AKIP-1160/6 did not answer on " + config.portName);
        const auto newline = reply.indexOf('\n');
        if (newline >= 0) reply.truncate(newline);
        return reply.trimmed().toStdString();
    }

    double number(const std::string& command, const char* valueName) const
    {
        const auto reply = exchange(command, true);
        std::size_t parsed = 0;
        const double value = std::stod(reply, &parsed);
        if (parsed != reply.size() || !std::isfinite(value)) {
            throw std::runtime_error(std::string("AKIP-1160/6 returned invalid ")
                + valueName + ": " + reply);
        }
        return value;
    }
};

namespace {
std::string akipFixed(double value)
{
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text << std::fixed << std::setprecision(3) << value;
    return text.str();
}
}

Akip1160Serial::Akip1160Serial(Akip1160SerialConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}
Akip1160Serial::~Akip1160Serial() = default;
std::string Akip1160Serial::identity() const { return impl_->exchange("*IDN?", true); }
double Akip1160Serial::voltageSetpoint() const { return impl_->number("VOLT?", "voltage setpoint"); }
double Akip1160Serial::currentSetpoint() const { return impl_->number("CURR?", "current setpoint"); }
double Akip1160Serial::measuredVoltage() const { return impl_->number("MEAS:VOLT?", "measured voltage"); }
double Akip1160Serial::measuredCurrent() const { return impl_->number("MEAS:CURR?", "measured current"); }
bool Akip1160Serial::outputEnabled() const
{
    auto reply = impl_->exchange("OUTP?", true);
    std::transform(reply.begin(), reply.end(), reply.begin(),
        [](unsigned char value) { return static_cast<char>(std::toupper(value)); });
    if (reply == "ON" || reply == "1") return true;
    if (reply == "OFF" || reply == "0") return false;
    throw std::runtime_error("AKIP-1160/6 returned invalid output state: " + reply);
}
void Akip1160Serial::setVoltage(double volts) const
{
    impl_->exchange(voltageCommand(volts), false);
}
void Akip1160Serial::setCurrentLimit(double amperes) const
{
    impl_->exchange(currentCommand(amperes), false);
}
void Akip1160Serial::setOutput(bool enabled) const
{
    impl_->exchange(outputCommand(enabled), false);
}
const std::string& Akip1160Serial::portName() const { return impl_->config.portName; }
std::string Akip1160Serial::voltageCommand(double volts)
{
    if (!std::isfinite(volts) || volts < 0.0 || volts > 60.0) {
        throw std::invalid_argument("AKIP-1160/6 voltage must be within 0..60 V");
    }
    return "VOLT " + akipFixed(volts) + "\n";
}
std::string Akip1160Serial::currentCommand(double amperes)
{
    if (!std::isfinite(amperes) || amperes < 0.005 || amperes > 10.0) {
        throw std::invalid_argument("AKIP-1160/6 current must be within 0.005..10 A");
    }
    return "CURR " + akipFixed(amperes) + "\n";
}
std::string Akip1160Serial::outputCommand(bool enabled)
{
    return std::string("OUTP ") + (enabled ? "ON\n" : "OFF\n");
}

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

} // namespace orbita::stand
