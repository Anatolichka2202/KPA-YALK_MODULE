#include "hardware/isd_router.h"
#include "hardware/isd_dac_table.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <iomanip>
#include <locale>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace tu::hardware {
namespace {

class IsdNoResponse final : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

bool isSuccessResponse(const QByteArray& body)
{
    if (body.isEmpty()) return false;
    const auto trimmed = body.trimmed();
    const QString utf8 = QString::fromUtf8(body);
    const QString local = QString::fromLocal8Bit(body);
    return trimmed.compare("OK", Qt::CaseInsensitive) == 0
        || utf8.contains(QStringLiteral("успешно"), Qt::CaseInsensitive)
        || local.contains(QStringLiteral("успешно"), Qt::CaseInsensitive);
}

bool isRecoverableFirmwareFailure(const QByteArray& body)
{
    const QByteArray ascii = body.toLower();
    if (ascii.contains("modul ne otvechaet")
        || ascii.contains("sboi obshey shiny")) return true;
    const QString utf8 = QString::fromUtf8(body);
    const QString local = QString::fromLocal8Bit(body);
    const auto recoverable = [](const QString& text) {
        return (text.contains(QStringLiteral("модул"), Qt::CaseInsensitive)
                && text.contains(QStringLiteral("не отвечает"), Qt::CaseInsensitive))
            || text.contains(QStringLiteral("сбой общей шины"), Qt::CaseInsensitive);
    };
    return recoverable(utf8) || recoverable(local);
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
    unsigned value = 0;
    double volts = 0.0;
};

bool sameAction(const ActiveAction& left, const ActiveAction& right)
{
    return left.kind == right.kind && left.type == right.type
        && left.channel == right.channel;
}

unsigned pathNumber(const QString& path, const QString& marker,
                    const QString& nextMarker = {})
{
    const qsizetype beginMarker = path.indexOf(marker);
    if (beginMarker < 0) return 0;
    const qsizetype begin = beginMarker + marker.size();
    const qsizetype end = nextMarker.isEmpty() ? path.size() : path.indexOf(nextMarker, begin);
    bool ok = false;
    const unsigned value = path.mid(begin, end < 0 ? -1 : end - begin).toUInt(&ok);
    return ok ? value : 0;
}

struct HttpAttempt {
    QByteArray body;
    QString errorText;
    QNetworkReply::NetworkError error = QNetworkReply::NoError;
    int status = 0;
    bool timedOut = false;
    std::uint64_t latencyMilliseconds = 0;
};

} // namespace

struct IsdRouter::Impl
{
    explicit Impl(IsdConfig value) : config(std::move(value))
    {
        if (config.host.empty()) throw std::invalid_argument("ИСД host пуст");
        if (!config.port || !config.timeoutMilliseconds || !config.serviceTimeoutMilliseconds)
            throw std::invalid_argument("Некорректная конфигурация ИСД");

        networkContext = new QObject;
        networkContext->moveToThread(&networkThread);
        QObject::connect(&networkThread, &QThread::finished,
                         networkContext, &QObject::deleteLater);
        networkThread.start();
        const bool initialized = QMetaObject::invokeMethod(networkContext, [this] {
            networkManager = new QNetworkAccessManager(networkContext);
        }, Qt::BlockingQueuedConnection);
        if (!initialized || !networkManager) {
            networkThread.quit();
            networkThread.wait();
            throw std::runtime_error("Не удалось запустить сетевой транспорт ИСД");
        }
    }

    ~Impl()
    {
        networkThread.quit();
        networkThread.wait();
    }

    HttpAttempt requestOnNetworkThread(const QUrl& url, unsigned timeout)
    {
        HttpAttempt attempt;
        QElapsedTimer elapsed;
        elapsed.start();
        QNetworkReply* reply = networkManager->get(QNetworkRequest(url));
        QTimer timer;
        timer.setSingleShot(true);
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QObject::connect(&timer, &QTimer::timeout, reply, [&attempt, reply] {
            attempt.timedOut = true;
            reply->abort();
        });
        timer.start(static_cast<int>(timeout));
        loop.exec();
        timer.stop();

        attempt.error = reply->error();
        attempt.errorText = reply->errorString();
        attempt.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        attempt.body = reply->readAll();
        attempt.latencyMilliseconds = static_cast<std::uint64_t>(elapsed.elapsed());
        reply->deleteLater();
        return attempt;
    }

    QByteArray getOnce(const QString& path, bool requireAck,
                       unsigned timeoutMilliseconds = 0)
    {
        const unsigned timeout = timeoutMilliseconds ? timeoutMilliseconds
                                                     : config.timeoutMilliseconds;
        QUrl url;
        url.setScheme(QStringLiteral("http"));
        url.setHost(QString::fromStdString(config.host));
        url.setPort(config.port);
        url.setPath(path);

        HttpAttempt attempt;
        std::exception_ptr transportError;
        const auto request = [this, &attempt, &transportError, url, timeout] {
            try {
                attempt = requestOnNetworkThread(url, timeout);
            } catch (...) {
                transportError = std::current_exception();
            }
        };
        if (QThread::currentThread() == networkContext->thread()) {
            request();
        } else if (!QMetaObject::invokeMethod(networkContext, request,
                                              Qt::BlockingQueuedConnection)) {
            throw std::runtime_error("Не удалось выполнить запрос ИСД в сетевом потоке");
        }
        if (transportError) std::rethrow_exception(transportError);

        const bool httpOk = attempt.status >= 200 && attempt.status < 300;
        const bool accepted = attempt.error == QNetworkReply::NoError && httpOk
            && (!requireAck || isSuccessResponse(attempt.body));
        const std::uint64_t sequence = ++requestSequence;
        if (traceSink) {
            traceSink({sequence,
                pathNumber(path, QStringLiteral("type="), QStringLiteral("num=")),
                pathNumber(path, QStringLiteral("num="), QStringLiteral("val=")),
                attempt.latencyMilliseconds, attempt.status, attempt.timedOut,
                accepted, path.toStdString(), attempt.body.left(160).toStdString()});
        }

        // Автоматически активную команду не повторяем: timeout/обрыв ACK не
        // доказывает, что коммутация не произошла. Повтор допустим только после
        // подтверждённого оператором перезапуска и восстановления состояния.
        if (attempt.error != QNetworkReply::NoError) {
            const std::string message = "ИСД не отвечает на " + path.toStdString()
                + ": " + attempt.errorText.toUtf8().toStdString();
            if (attempt.status == 0 || attempt.status == 408
                || attempt.status == 429 || attempt.status >= 500)
                throw IsdNoResponse(message);
            throw std::runtime_error(message);
        }
        if (attempt.status == 408 || attempt.status == 429 || attempt.status >= 500)
            throw IsdNoResponse("ИСД HTTP status " + std::to_string(attempt.status)
                + " для " + path.toStdString());
        if (!httpOk)
            throw std::runtime_error("ИСД HTTP status " + std::to_string(attempt.status)
                + " для " + path.toStdString());
        if (requireAck && attempt.body.isEmpty())
            throw IsdNoResponse("ИСД не вернул подтверждение для " + path.toStdString());
        if (requireAck && isRecoverableFirmwareFailure(attempt.body))
            throw IsdNoResponse("ИСД сообщает о восстанавливаемой ошибке для "
                + path.toStdString() + ": " + attempt.body.left(160).toStdString());
        if (requireAck && !isSuccessResponse(attempt.body))
            throw std::runtime_error("ИСД не подтвердил команду: "
                + attempt.body.left(160).toStdString());
        return attempt.body;
    }

    QByteArray getWithRetries(const QString& path, bool requireAck,
                              unsigned timeoutMilliseconds = 0)
    {
        for (unsigned attempt = 1;; ++attempt) {
            try {
                return getOnce(path, requireAck, timeoutMilliseconds);
            } catch (const IsdNoResponse&) {
                if (attempt >= config.requestAttempts) throw;
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(config.retryDelayMilliseconds));
            }
        }
    }

    void restoreConfirmedState()
    {
        for (const auto& action : active) {
            switch (action.kind) {
            case ActionKind::Switch:
                getWithRetries(QStringLiteral("/type=%1num=%2val=1")
                    .arg(action.type).arg(action.channel), true);
                break;
            case ActionKind::Analog:
                getWithRetries(QStringLiteral("/type=1num=%1val=%2work=1")
                    .arg(action.channel).arg(action.value), true);
                break;
            case ActionKind::YalkOutput:
                getWithRetries(QStringLiteral("/type=1num=%1val=%2work=1bus=1")
                    .arg(action.channel).arg(isdDacCode(action.volts)), true);
                break;
            }
        }
    }

    QByteArray get(const QString& path, bool requireAck,
                   unsigned timeoutMilliseconds = 0)
    {
        const std::lock_guard<std::recursive_mutex> lock(requestMutex);
        try {
            auto response = getWithRetries(path, requireAck, timeoutMilliseconds);
            recoveryEnabled = true;
            return response;
        } catch (const IsdNoResponse& firstError) {
            if (!recoveryHandler || !recoveryEnabled) throw;
            std::string reason = firstError.what();
            for (;;) {
                if (!recoveryHandler(reason)) {
                    recoveryEnabled = false;
                    throw std::runtime_error("Восстановление ИСД отменено оператором");
                }
                try {
                    getWithRetries(QStringLiteral("/"), false, config.serviceTimeoutMilliseconds);
                    restoreConfirmedState();
                    auto response = getWithRetries(path, requireAck, timeoutMilliseconds);
                    recoveryEnabled = true;
                    if (recoveryRestoredSink) recoveryRestoredSink();
                    return response;
                } catch (const IsdNoResponse& retryError) {
                    reason = retryError.what();
                }
            }
        }
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
        get(QStringLiteral("/type=1num=%1val=%2work=1bus=1")
            .arg(channel).arg(isdDacCode(volts)), true);
    }

    void yalkOffRaw(unsigned channel)
    {
        // Код 0 соответствует -2 В по таблице ИСД; перед OFF не подаём его
        // на рабочий вход. Сначала разрываем измерительную шину при 0 В.
        get(QStringLiteral("/type=1num=%1val=%2work=1bus=0")
            .arg(channel).arg(isdDacCode(0.0)), true);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        get(QStringLiteral("/type=1num=%1val=%2work=0")
            .arg(channel).arg(isdDacCode(0.0)), true);
    }

    void safeStop() noexcept
    {
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
                // Оставляем запись: повторный внешний safeStop может ещё раз
                // адресно попытаться снять неопределённое воздействие.
            }
        }
    }

    IsdConfig config;
    QThread networkThread;
    QObject* networkContext = nullptr;
    QNetworkAccessManager* networkManager = nullptr;
    std::recursive_mutex requestMutex;
    std::uint64_t requestSequence = 0;
    std::vector<ActiveAction> active;
    TraceSink traceSink;
    RecoveryHandler recoveryHandler;
    RecoveryRestoredSink recoveryRestoredSink;
    bool recoveryEnabled = true;
};

IsdRouter::IsdRouter(IsdConfig config) : impl_(std::make_unique<Impl>(std::move(config))) {}
IsdRouter::~IsdRouter() = default;

std::string IsdRouter::probe()
{
    const std::lock_guard<std::recursive_mutex> lock(impl_->requestMutex);
    return impl_->get(QStringLiteral("/"), false).left(200).toStdString();
}

void IsdRouter::setSwitch(unsigned type, unsigned channel, bool enabled)
{
    const std::lock_guard<std::recursive_mutex> lock(impl_->requestMutex);
    if (!type || !channel) throw std::invalid_argument("ИСД type/channel начинаются с 1");
    const ActiveAction action{ActionKind::Switch, type, channel, 0, 0.0};
    try {
        impl_->switchRaw(type, channel, enabled);
        if (enabled) impl_->remember(action);
        else impl_->forget(action);
    } catch (...) {
        // Потерянный ACK не доказывает, что включение физически не произошло.
        // Запоминаем такое воздействие для последующего адресного safeStop.
        if (enabled) impl_->remember(action);
        throw;
    }
}

void IsdRouter::setAnalog(unsigned channel, unsigned value, bool enabled)
{
    const std::lock_guard<std::recursive_mutex> lock(impl_->requestMutex);
    if (!channel) throw std::invalid_argument("Канал ИСД начинается с 1");
    const ActiveAction action{ActionKind::Analog, 1, channel, value, 0.0};
    try {
        impl_->analogRaw(channel, value, enabled);
        if (enabled) impl_->remember(action);
        else impl_->forget(action);
    } catch (...) {
        if (enabled) impl_->remember(action);
        throw;
    }
}

void IsdRouter::setYalkVoltage(unsigned channel, double volts)
{
    const std::lock_guard<std::recursive_mutex> lock(impl_->requestMutex);
    if (!channel) throw std::invalid_argument("Канал ЯЛК начинается с 1");
    if (!std::isfinite(volts) || volts < 0.0 || volts > 6.2)
        throw std::invalid_argument("Напряжение ЯЛК должно быть 0.00..6.20 В");
    const ActiveAction action{ActionKind::YalkOutput, 1, channel, 0, volts};
    try {
        impl_->yalkOnRaw(channel, volts);
        impl_->remember(action);
    } catch (...) {
        impl_->remember(action);
        throw;
    }
}

void IsdRouter::disableYalkOutput(unsigned channel)
{
    const std::lock_guard<std::recursive_mutex> lock(impl_->requestMutex);
    if (!channel) throw std::invalid_argument("Канал ЯЛК начинается с 1");
    const ActiveAction action{ActionKind::YalkOutput, 1, channel, 0, 0.0};
    impl_->yalkOffRaw(channel);
    impl_->forget(action);
}

void IsdRouter::setTraceSink(TraceSink sink)
{
    const std::lock_guard<std::recursive_mutex> lock(impl_->requestMutex);
    impl_->traceSink = std::move(sink);
}

void IsdRouter::setRecoveryHandler(RecoveryHandler handler)
{
    const std::lock_guard<std::recursive_mutex> lock(impl_->requestMutex);
    impl_->recoveryHandler = std::move(handler);
    impl_->recoveryEnabled = true;
}

void IsdRouter::setRecoveryRestoredSink(RecoveryRestoredSink sink)
{
    const std::lock_guard<std::recursive_mutex> lock(impl_->requestMutex);
    impl_->recoveryRestoredSink = std::move(sink);
}

void IsdRouter::safeStop() noexcept
{
    const std::lock_guard<std::recursive_mutex> lock(impl_->requestMutex);
    impl_->safeStop();
}

} // namespace tu::hardware
