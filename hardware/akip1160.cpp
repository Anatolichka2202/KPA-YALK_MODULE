#include "hardware/akip1160.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QSerialPort>
#include <QString>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace tu::hardware {
namespace {

std::runtime_error serialError(const std::string& prefix, const QString& detail)
{
    return std::runtime_error(prefix + ": " + detail.toUtf8().toStdString());
}

std::string fixed(double value)
{
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text << std::fixed << std::setprecision(3) << value;
    return text.str();
}

std::string uppercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char item) { return static_cast<char>(std::toupper(item)); });
    return value;
}

} // namespace

class Akip1160::Serial final {
public:
    explicit Serial(AkipConfig config) : config_(std::move(config)) {}

    std::string exchange(const std::string& command, bool expectReply) const
    {
        QSerialPort port(QString::fromStdString(config_.portName));
        port.setBaudRate(config_.baudRate);
        port.setDataBits(QSerialPort::Data8);
        port.setParity(QSerialPort::NoParity);
        port.setStopBits(QSerialPort::OneStop);
        port.setFlowControl(QSerialPort::NoFlowControl);
        if (!port.open(QIODevice::ReadWrite)) {
            throw serialError("AKIP-1160/6 COM open failed", port.errorString());
        }

        // Подтверждено старым драйвером стенда: CH340 нужен короткий интервал
        // после открытия порта перед первым байтом.
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        port.clear(QSerialPort::AllDirections);

        QByteArray wire = QByteArray::fromStdString(command);
        if (!wire.endsWith('\n')) wire.append('\n');
        if (port.write(wire) != wire.size()) {
            throw serialError("AKIP-1160/6 COM write failed", port.errorString());
        }
        if (port.bytesToWrite() > 0
            && !port.waitForBytesWritten(static_cast<int>(config_.timeoutMilliseconds))) {
            throw serialError("AKIP-1160/6 COM write timeout", port.errorString());
        }

        if (!expectReply) {
            // Прибор применяет команду не мгновенно; немедленное закрытие CH340
            // перед последующим query ранее приводило к потере ответа.
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            return {};
        }

        QElapsedTimer timer;
        timer.start();
        QByteArray reply;
        while (!reply.contains('\n')) {
            const int remaining = static_cast<int>(config_.timeoutMilliseconds)
                - static_cast<int>(timer.elapsed());
            if (remaining <= 0 || !port.waitForReadyRead(remaining)) break;
            reply += port.readAll();
        }
        if (reply.isEmpty()) {
            throw std::runtime_error("AKIP-1160/6 did not answer on " + config_.portName);
        }
        const auto newline = reply.indexOf('\n');
        if (newline >= 0) reply.truncate(newline);
        return reply.trimmed().toStdString();
    }

    double number(const std::string& command, const char* valueName) const
    {
        std::string lastFailure;
        for (unsigned attempt = 0; attempt < 3; ++attempt) {
            try {
                const std::string reply = exchange(command, true);
                std::size_t parsed = 0;
                const double value = std::stod(reply, &parsed);
                if (parsed != reply.size() || !std::isfinite(value)) {
                    throw std::runtime_error(std::string("AKIP-1160/6 returned invalid ")
                        + valueName + ": " + reply);
                }
                return value;
            } catch (const std::exception& error) {
                lastFailure = error.what();
                if (attempt < 2)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        throw std::runtime_error(lastFailure);
    }

    std::string identity() const { return exchange("*IDN?", true); }
    double voltageSetpoint() const { return number("VOLT?", "voltage setpoint"); }
    double overvoltageLimit() const { return number("VOLT:LIM?", "overvoltage limit"); }
    double currentSetpoint() const { return number("CURR?", "current setpoint"); }
    double measuredVoltage() const { return number("MEAS:VOLT?", "measured voltage"); }
    double measuredCurrent() const { return number("MEAS:CURR?", "measured current"); }

    bool outputEnabled() const
    {
        std::string reply = exchange("OUTP?", true);
        reply = uppercase(std::move(reply));
        if (reply == "ON" || reply == "1") return true;
        if (reply == "OFF" || reply == "0") return false;
        throw std::runtime_error("AKIP-1160/6 returned invalid output state: " + reply);
    }

    void setVoltage(double volts) const { exchange("VOLT " + fixed(volts), false); }
    void setOvervoltageLimit(double volts) const { exchange("VOLT:LIM " + fixed(volts), false); }
    void setCurrentLimit(double amperes) const { exchange("CURR " + fixed(amperes), false); }
    void setOutput(bool enabled) const { exchange(std::string("OUTP ") + (enabled ? "ON" : "OFF"), false); }

private:
    AkipConfig config_;
};

Akip1160::Akip1160(AkipConfig config)
    : config_(std::move(config)), serial_(std::make_unique<Serial>(config_))
{
    if (config_.portName.empty()) throw std::invalid_argument("AKIP-1160/6 COM port is empty");
    if (config_.baudRate <= 0) throw std::invalid_argument("AKIP-1160/6 baud rate is invalid");
    if (!config_.timeoutMilliseconds) throw std::invalid_argument("AKIP-1160/6 timeout is zero");
}

Akip1160::~Akip1160() = default;

std::string Akip1160::probe() const
{
    const std::string identity = serial_->identity();
    if (!config_.expectedIdentity.empty()
        && uppercase(identity).find(uppercase(config_.expectedIdentity)) == std::string::npos) {
        throw std::runtime_error("На " + config_.portName
            + " идентифицирован не ожидаемый АКИП-1160/6: " + identity);
    }
    return identity;
}

PowerState Akip1160::readState() const
{
    PowerState state;
    state.measuredVoltageV = serial_->measuredVoltage();
    state.measuredCurrentA = serial_->measuredCurrent();
    state.voltageSetpointV = serial_->voltageSetpoint();
    state.overvoltageLimitV = serial_->overvoltageLimit();
    state.currentSetpointA = serial_->currentSetpoint();
    state.outputEnabled = serial_->outputEnabled();
    return state;
}

void Akip1160::verifyNear(double actual, double expected, double tolerance,
                          const std::string& what) const
{
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
        throw std::runtime_error("АКИП не подтвердил " + what + ": задано "
            + std::to_string(expected) + ", прочитано " + std::to_string(actual));
    }
}

void Akip1160::setVoltage(double volts)
{
    if (!std::isfinite(volts) || volts < 0.0 || volts > config_.maximumVoltageV) {
        throw std::invalid_argument("Напряжение АКИП вне разрешённого профилем диапазона");
    }
    if (config_.overvoltageLimitV < volts
        || config_.overvoltageLimitV > config_.maximumVoltageV) {
        throw std::invalid_argument("OVP АКИП должен быть не ниже уставки и не выше предела профиля");
    }

    try {
        // АКИП/SPE может молча удержать VOLT на границе OVP, поэтому сначала
        // выставляем и проверяем OVP, затем саму уставку напряжения.
        serial_->setOvervoltageLimit(config_.overvoltageLimitV);
        verifyNear(serial_->overvoltageLimit(), config_.overvoltageLimitV,
                   config_.voltageSetpointToleranceV, "предел OVP");
        serial_->setVoltage(volts);
        verifyNear(serial_->voltageSetpoint(), volts,
                   config_.voltageSetpointToleranceV, "напряжение");
        voltageArmed_ = true;
    } catch (...) {
        safeOff();
        throw;
    }
}

void Akip1160::setCurrentLimit(double amperes)
{
    if (!std::isfinite(amperes) || amperes < 0.005 || amperes > config_.maximumCurrentA) {
        throw std::invalid_argument("Ограничение тока АКИП вне разрешённого профилем диапазона");
    }
    try {
        serial_->setCurrentLimit(amperes);
        verifyNear(serial_->currentSetpoint(), amperes,
                   config_.currentSetpointToleranceA, "ограничение тока");
        currentArmed_ = true;
    } catch (...) {
        safeOff();
        throw;
    }
}

void Akip1160::confirmOutputOff()
{
    const unsigned attempts = std::max(1u, config_.outputConfirmAttempts);
    std::string lastFailure;
    for (unsigned attempt = 0; attempt < attempts; ++attempt) {
        if (config_.outputConfirmSettleMilliseconds) {
            std::this_thread::sleep_for(std::chrono::milliseconds(
                config_.outputConfirmSettleMilliseconds));
        }
        try {
            if (!serial_->outputEnabled()) return;
            lastFailure = "АКИП не подтвердил отключение выхода";
        } catch (const std::exception& error) {
            lastFailure = error.what();
        }
    }
    throw std::runtime_error(lastFailure.empty()
        ? "АКИП не подтвердил отключение выхода" : lastFailure);
}

void Akip1160::setOutput(bool enabled)
{
    if (!enabled) {
        serial_->setOutput(false);
        confirmOutputOff();
        voltageArmed_ = false;
        currentArmed_ = false;
        return;
    }
    if (!voltageArmed_ || !currentArmed_) {
        throw std::runtime_error(
            "Включение АКИП запрещено: текущий запуск должен сначала задать напряжение и ограничение тока");
    }
    try {
        serial_->setOutput(true);
        if (!serial_->outputEnabled())
            throw std::runtime_error("АКИП не подтвердил включение выхода");
    } catch (...) {
        safeOff();
        throw;
    }
}

void Akip1160::safeOff() noexcept
{
    try { serial_->setOutput(false); } catch (...) {}
    voltageArmed_ = false;
    currentArmed_ = false;
}

} // namespace tu::hardware
