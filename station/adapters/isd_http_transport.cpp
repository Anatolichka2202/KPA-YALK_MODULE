#include "orbita_stand/isd_http_transport.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QString>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace orbita::stand {

struct IsdHttpTransport::Impl {
    explicit Impl(IsdHttpTransportConfig value) : config(std::move(value))
    {
        if (config.host.empty()) throw std::invalid_argument("ISD host is empty");
        if (!config.port) throw std::invalid_argument("ISD port is zero");
        if (!config.timeoutMilliseconds) throw std::invalid_argument("ISD timeout is zero");
    }

    IsdHttpTransportConfig config;
};

IsdHttpTransport::IsdHttpTransport(IsdHttpTransportConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

IsdHttpTransport::~IsdHttpTransport() = default;

IsdHttpResponse IsdHttpTransport::get(
    const std::string& path, unsigned timeoutOverrideMilliseconds)
{
    if (path.empty() || path.front() != '/') {
        throw std::invalid_argument("ISD HTTP path must start with '/'");
    }

    const unsigned timeoutMilliseconds = timeoutOverrideMilliseconds
        ? timeoutOverrideMilliseconds : impl_->config.timeoutMilliseconds;
    if (!timeoutMilliseconds) throw std::invalid_argument("ISD request timeout is zero");

    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(QString::fromStdString(impl_->config.host));
    url.setPort(impl_->config.port);
    url.setPath(QString::fromStdString(path));

    QNetworkAccessManager manager;
    QNetworkReply* reply = manager.get(QNetworkRequest(url));
    QTimer timer;
    timer.setSingleShot(true);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);

    QElapsedTimer elapsed;
    elapsed.start();
    timer.start(static_cast<int>(timeoutMilliseconds));
    loop.exec();
    timer.stop();

    const auto networkError = reply->error();
    const QString networkErrorText = reply->errorString();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    const unsigned elapsedMilliseconds = static_cast<unsigned>(
        std::max<qint64>(0, elapsed.elapsed()));
    reply->deleteLater();

    if (networkError != QNetworkReply::NoError) {
        // There is deliberately no retry here.  For an active ISD command a
        // timeout/connection close cannot prove that the physical mutation did
        // not happen; the stateful driver must mark the session indeterminate.
        throw std::runtime_error(
            "ISD HTTP request failed for " + path + ": "
            + networkErrorText.toUtf8().toStdString());
    }
    if (status < 200 || status >= 300) {
        throw std::runtime_error(
            "ISD HTTP status " + std::to_string(status) + " for " + path);
    }

    IsdHttpResponse response{status, body.toStdString(), elapsedMilliseconds};
    if (path != "/") requireCommandAck(response);
    return response;
}

void IsdHttpTransport::requireCommandAck(const IsdHttpResponse& response)
{
    const QByteArray body = QByteArray::fromStdString(response.body);
    if (body.isEmpty()) throw std::runtime_error("ISD returned an empty response");
    const QByteArray trimmed = body.trimmed();
    const QString utf8 = QString::fromUtf8(body);
    const QString local = QString::fromLocal8Bit(body);
    if (trimmed.compare("OK", Qt::CaseInsensitive) != 0
        && !utf8.contains(QStringLiteral("успешно"), Qt::CaseInsensitive)
        && !local.contains(QStringLiteral("успешно"), Qt::CaseInsensitive)) {
        throw std::runtime_error(
            "ISD did not acknowledge command: " + body.left(160).toStdString());
    }
}

} // namespace orbita::stand
