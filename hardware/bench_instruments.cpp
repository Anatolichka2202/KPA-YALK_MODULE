#include "hardware/bench_instruments.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace tu::hardware {
namespace {

std::string trim(std::string value)
{
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) ++first;
    return value.substr(first);
}

double parseNumber(const std::string& text, const char* what)
{
    const std::string value = trim(text);
    std::size_t parsed = 0;
    double result = 0.0;
    try { result = std::stod(value, &parsed); }
    catch (...) { throw std::runtime_error(std::string("Некорректный ответ ") + what + ": " + value); }
    if (parsed != value.size() || !std::isfinite(result))
        throw std::runtime_error(std::string("Некорректный ответ ") + what + ": " + value);
    return result;
}

std::string finite(double value)
{
    if (!std::isfinite(value)) throw std::invalid_argument("Значение прибора не finite");
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(6) << value;
    std::string text = out.str();
    while (!text.empty() && text.back() == '0') text.pop_back();
    if (!text.empty() && text.back() == '.') text.pop_back();
    return text;
}

} // namespace

V7Meter::V7Meter(V7Config config)
    : config_(std::move(config)),
      instrument_({config_.resourceExpressions, config_.timeoutMilliseconds}) {}
V7Meter::~V7Meter() = default;
std::string V7Meter::identity() { return trim(instrument_.query("*IDN?")); }
double V7Meter::readDcVoltage() { return parseNumber(instrument_.query(config_.dcVoltageCommand), "В7 DC voltage"); }
double V7Meter::readAcVoltage() { return parseNumber(instrument_.query(config_.acVoltageCommand), "В7 AC voltage"); }
double V7Meter::readFrequency() { return parseNumber(instrument_.query(config_.frequencyCommand), "В7 frequency"); }
const std::string& V7Meter::resourceName() const { return instrument_.resourceName(); }

RigolGenerator::RigolGenerator(RigolConfig config)
    : config_(std::move(config)),
      instrument_({config_.resourceExpressions, config_.timeoutMilliseconds}) {}
RigolGenerator::~RigolGenerator() { safeOff(); }
std::string RigolGenerator::identity() { return trim(instrument_.query("*IDN?")); }

void RigolGenerator::setSine(unsigned channel, double frequencyHz,
                             double amplitudeVpp, double offsetVolts)
{
    if (channel < 1 || channel > 2 || !(frequencyHz > 0.0) || amplitudeVpp < 0.0)
        throw std::invalid_argument("Некорректная синусоида Rigol");
    instrument_.write("VOLT:UNIT VPP");
    const std::string suffix = channel == 1 ? "" : ":CH2";
    instrument_.write("APPL:SIN" + suffix + " " + finite(frequencyHz) + ","
        + finite(amplitudeVpp) + "," + finite(offsetVolts));
    instrument_.write("PHAS 0");
}

void RigolGenerator::output(unsigned channel, bool enabled)
{
    if (channel < 1 || channel > 2) throw std::invalid_argument("Канал Rigol должен быть 1 или 2");
    const std::string prefix = channel == 1 ? "OUTP " : "OUTP:CH2 ";
    instrument_.write(prefix + (enabled ? "ON" : "OFF"));
}

void RigolGenerator::safeOff() noexcept
{
    try { output(1, false); } catch (...) {}
    try { output(2, false); } catch (...) {}
}

const std::string& RigolGenerator::resourceName() const { return instrument_.resourceName(); }

} // namespace tu::hardware
