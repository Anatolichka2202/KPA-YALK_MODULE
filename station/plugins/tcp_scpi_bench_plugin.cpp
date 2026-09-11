#include "plugin_support.h"

#include <QTcpSocket>

#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace {
using namespace orbita::stand;

struct Instance {
    QString host;
    quint16 port = 5025;
    int timeoutMs = 1000;
};

QByteArray exchange(Instance& instance, const QByteArray& command)
{
    QTcpSocket socket;
    socket.connectToHost(instance.host, instance.port);
    if (!socket.waitForConnected(instance.timeoutMs))
        throw std::runtime_error(("SCPI connection failed: " + socket.errorString()).toStdString());
    socket.write(command + '\n');
    if (!socket.waitForBytesWritten(instance.timeoutMs))
        throw std::runtime_error("SCPI write timed out");
    if (!socket.waitForReadyRead(instance.timeoutMs))
        throw std::runtime_error("SCPI response timed out");
    QByteArray result = socket.readLine().trimmed();
    if (result.startsWith("ERR ")) throw std::runtime_error(result.mid(4).toStdString());
    return result;
}

double queryNumber(Instance& instance, const QByteArray& command)
{
    bool ok = false;
    const double value = exchange(instance, command).toDouble(&ok);
    if (!ok) throw std::runtime_error("SCPI instrument returned a non-numeric value");
    return value;
}

orbita_plugin_status_v1 create(const char*, const char* text, void** output,
                               orbita_plugin_buffer_v1* diagnostic)
{
    return plugin::guarded(diagnostic, [&] {
        const auto args = plugin::arguments(text);
        auto instance = std::make_unique<Instance>();
        instance->host = QString::fromStdString(plugin::required(args, "host"));
        instance->port = static_cast<quint16>(plugin::unsignedValue(args, "port", 5025));
        instance->timeoutMs = static_cast<int>(plugin::unsignedValue(args, "timeout_ms", 1000));
        *output = instance.release();
        return std::string("TCP SCPI bench created");
    });
}

void destroy(void* value) { delete static_cast<Instance*>(value); }

orbita_plugin_status_v1 invoke(void* value, const char* capability, const char* operation,
                              const char* request, orbita_plugin_buffer_v1* response)
{
    return plugin::guarded(response, [&] {
        auto& instance = *static_cast<Instance*>(value);
        const std::string cap = capability ? capability : "";
        const std::string action = operation ? operation : "";
        const auto args = plugin::arguments(request);
        std::ostringstream out;
        out << std::setprecision(15);

        if (action == "probe") {
            out << "status=ready\nidn=" << exchange(instance, "*IDN?").toStdString() << '\n';
            return out.str();
        }
        if (cap == "power.dc_supply") {
            if (action == "set_voltage")
                exchange(instance, "SOUR:VOLT " + QByteArray::number(plugin::doubleValue(args, "volts"), 'g', 12));
            else if (action == "set_current_limit")
                exchange(instance, "SOUR:CURR " + QByteArray::number(plugin::doubleValue(args, "amperes"), 'g', 12));
            else if (action == "output")
                exchange(instance, QByteArray("OUTP ") + (plugin::booleanValue(args, "enabled") ? "ON" : "OFF"));
            else if (action == "read_state") {
                out << "status=ready\nvolts=" << queryNumber(instance, "MEAS:VOLT?")
                    << "\namperes=" << queryNumber(instance, "MEAS:CURR?")
                    << "\noutput_enabled=" << (queryNumber(instance, "OUTP?") != 0.0 ? "true" : "false") << '\n';
                return out.str();
            } else throw std::invalid_argument("Unsupported power SCPI operation");
            return std::string("status=ok\n");
        }
        if (cap == "measure.reference_voltage" && action == "read_voltage")
            out << "volts=" << queryNumber(instance, "MEAS:VOLT:DC?") << '\n';
        else if (cap == "measure.dc_current" && action == "read_current")
            out << "amperes=" << queryNumber(instance, "MEAS:CURR:DC?") << '\n';
        else if (cap == "measure.reference_ac_voltage" && action == "read_ac_voltage")
            out << "volts=" << queryNumber(instance, "MEAS:VOLT:AC?") << '\n';
        else if (cap == "measure.reference_frequency" && action == "read_frequency")
            out << "hertz=" << queryNumber(instance, "MEAS:FREQ?") << '\n';
        else if (cap == "signal.generator" && action == "set_sine") {
            exchange(instance, "SOUR1:APPL:SIN "
                + QByteArray::number(plugin::doubleValue(args, "frequency_hz"), 'g', 12) + ','
                + QByteArray::number(plugin::doubleValue(args, "amplitude_vpp"), 'g', 12) + ','
                + QByteArray::number(plugin::doubleValue(args, "offset_v"), 'g', 12));
            out << "status=ok\n";
        } else if (cap == "signal.generator" && action == "output") {
            exchange(instance, QByteArray("OUTP1 ") + (plugin::booleanValue(args, "enabled") ? "ON" : "OFF"));
            out << "status=ok\n";
        } else throw std::invalid_argument("Unsupported TCP SCPI capability/operation");
        return out.str();
    });
}

void safeStop(void* value)
{
    if (!value) return;
    try { exchange(*static_cast<Instance*>(value), "OUTP OFF"); } catch (...) {}
    try { exchange(*static_cast<Instance*>(value), "OUTP1 OFF"); } catch (...) {}
}
void cancel(void* value) { safeStop(value); }

const orbita_equipment_api_v1 api{
    ORBITA_EQUIPMENT_ABI_V1, sizeof(orbita_equipment_api_v1),
    "orbita.tcp_scpi_bench", "Сетевые приборы SCPI",
    "power.dc_supply;measure.reference_voltage;measure.dc_current;"
    "measure.reference_ac_voltage;measure.reference_frequency;signal.generator",
    create, destroy, invoke, cancel, safeStop};
}

extern "C" ORBITA_PLUGIN_EXPORT const orbita_equipment_api_v1* orbita_plugin_get_api_v1(void)
{
    return &api;
}
