#include "hardware/isd_router.h"

#include <QByteArray>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace tu::hardware {
namespace {

void requireSuccess(const QByteArray& body)
{
    if (body.isEmpty()) throw std::runtime_error("ИСД вернул пустой ответ");
    const auto trimmed = body.trimmed();
    const QString utf8 = QString::fromUtf8(body);
    const QString local = QString::fromLocal8Bit(body);
    if (trimmed.compare("OK", Qt::CaseInsensitive) != 0
        && !utf8.contains(QStringLiteral("успешно"), Qt::CaseInsensitive)
        && !local.contains(QStringLiteral("успешно"), Qt::CaseInsensitive)) {
        throw std::runtime_error("ИСД не подтвердил команду: "
            + body.left(160).toStdString());
    }
}

std::string fixed2(double value)
{
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text << std::fixed << std::setprecision(2) << value;
    return text.str();
}

enum class ActionKind { Switch, Analog, YalkOutput };

struct ActiveAction {
    ActionKind kind = ActionKind::Switch;
    unsigned type = 0;
    unsigned channel = 0;
};

bool sameAction(const ActiveAction& left, const ActiveAction& right)
{
    return left.kind == right.kind && left.type == right.type
        && left.channel == right.channel;
}

} // namespace

struct IsdRouter::Impl
{
    explicit Impl(IsdConfig value) : config(std::move(value))
    {
        if (config.host.empty()) throw std::invalid_argument("ИСД host пуст");
        if (!config.port || !config.timeoutMilliseconds || !config.serviceTimeoutMilliseconds)
            throw std::invalid_argument("Некорректная конфигурация ИСД");
    }

    QByteArray get(const QString& path, bool requireAck,
                   unsigned timeoutMilliseconds = 0, unsigned attempts = 3)
    {
        const unsigned timeout = timeoutMilliseconds ? timeoutMilliseconds
                                                     : config.timeoutMilliseconds;
        attempts = std::max(1u, attempts);
        for (unsigned attempt = 1; attempt <= attempts; ++attempt) {
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
            timer.start(static_cast<int>(timeout));
            loop.exec();

            const auto error = reply->error();
            const QString errorText = reply->errorString();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray body = reply->readAll();
            reply->deleteLater();

            if (error == QNetworkReply::NoError) {
                if (status < 200 || status >= 300)
                    throw std::runtime_error("ИСД HTTP status " + std::to_string(status));
                if (requireAck) requireSuccess(body);
                return body;
            }

            const bool transient = error == QNetworkReply::OperationCanceledError
                || error == QNetworkReply::TimeoutError
                || error == QNetworkReply::TemporaryNetworkFailureError
                || error == QNetworkReply::RemoteHostClosedError;
            if (!transient || attempt == attempts)
                throw std::runtime_error("ИСД HTTP: " + errorText.toUtf8().toStdString());
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
        throw std::runtime_error("ИСД HTTP request failed");
    }

    void remember(ActiveAction action)
    {
        active.erase(std::remove_if(active.begin(), active.end(),
            [&action](const ActiveAction& value) { return sameAction(value, action); }), active.end());
        active.push_back(action);
    }

    void forget(const ActiveAction& action)
    {
        active.erase(std::remove_if(active.begin(), active.end(),
            [&action](const ActiveAction& value) { return sameAction(value, action); }), active.end());
    }

    void switchRaw(unsigned type, unsigned channel, bool enabled)
    {
        get(QStringLiteral("/type=%1num=%2val=%3")
            .arg(type).arg(channel).arg(enabled ? 1 : 0), true);
    }

    void analogRaw(unsigned channel, unsigned value, bool enabled)
    {
        get(QStringLiteral("/type=1num=%1val=%2work=%3")
            .arg(channel).arg(value).arg(enabled ? 1 : 0), true);
    }

    void yalkOnRaw(unsigned channel, double volts)
    {
        get(QString::fromStdString("/type=5num=" + std::to_string(channel)
            + "val=" + fixed2(volts) + "work=1bus=1"), true);
    }

    void yalkOffRaw(unsigned channel)
    {
        get(QString::fromStdString("/type=1num=" + std::to_string(channel)
            + "val=0work=1bus=0"), true);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        get(QString::fromStdString("/type=1num=" + std::to_string(channel)
            + "val=0work=0"), true);
    }

    void safeStop() noexcept
    {
        // Тот же принцип, что у финального stateful ISD driver: никакого
        // глобального type=4 в lifecycle cleanup. Снимаем только воздействия,
        // которые этот процесс сам мог включить, в обратном порядке.
        const auto snapshot = active;
        for (auto it = snapshot.rbegin(); it != snapshot.rend(); ++it) {
            try {
                switch (it->kind) {
                case ActionKind::Switch:
                    switchRaw(it->type, it->channel, false);
                    break;
                case ActionKind::Analog:
                    analogRaw(it->channel, 0, false);
                    break;
                case ActionKind::YalkOutput:
                    yalkOffRaw(it->channel);
                    break;
                }
                forget(*it);
            } catch (...) {
                // Оставляем действие в active: повторный safeStop сможет
                // предпринять ещё одну адресную попытку, не переходя к type=4.
            }
        }
    }

    IsdConfig config;
    std::vector<ActiveAction> active;
};

IsdRouter::IsdRouter(IsdConfig config) : impl_(std::make_unique<Impl>(std::move(config))) {}
IsdRouter::~IsdRouter() = default;

std::string IsdRouter::probe()
{
    return impl_->get(QStringLiteral("/"), false).left(200).toStdString();
}

void IsdRouter::serviceFullReset()
{
    impl_->get(QStringLiteral("/type=4num=1"), false,
               impl_->config.serviceTimeoutMilliseconds, 1);
    impl_->active.clear();
}

void IsdRouter::setSwitch(unsigned type, unsigned channel, bool enabled)
{
    if (!type || !channel) throw std::invalid_argument("ИСД type/channel начинаются с 1");
    const ActiveAction action{ActionKind::Switch, type, channel};
    if (enabled) impl_->remember(action);
    impl_->switchRaw(type, channel, enabled);
    if (!enabled) impl_->forget(action);
}

void IsdRouter::setAnalog(unsigned channel, unsigned value, bool enabled)
{
    if (!channel) throw std::invalid_argument("Канал ИСД начинается с 1");
    const ActiveAction action{ActionKind::Analog, 1, channel};
    if (enabled) impl_->remember(action);
    impl_->analogRaw(channel, value, enabled);
    if (!enabled) impl_->forget(action);
}

void IsdRouter::setYalkVoltage(unsigned channel, double volts)
{
    if (!channel) throw std::invalid_argument("Канал ЯЛК начинается с 1");
    if (!std::isfinite(volts) || volts < 0.0 || volts > 6.2)
        throw std::invalid_argument("Напряжение ЯЛК должно быть 0.00..6.20 В");
    const ActiveAction action{ActionKind::YalkOutput, 5, channel};
    impl_->remember(action);
    impl_->yalkOnRaw(channel, volts);
}

void IsdRouter::disableYalkOutput(unsigned channel)
{
    if (!channel) throw std::invalid_argument("Канал ЯЛК начинается с 1");
    const ActiveAction action{ActionKind::YalkOutput, 5, channel};
    impl_->yalkOffRaw(channel);
    impl_->forget(action);
}

void IsdRouter::safeStop() noexcept
{
    impl_->safeStop();
}

} // namespace tu::hardware
