#include "orbita_stand/isd_http_transport.h"

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <stdexcept>
#include <utility>

namespace orbita::stand {
namespace {

void requireAck(const QByteArray& body)
{
    if (body.isEmpty()) throw std::runtime_error("ISD returned an empty response");
    const QByteArray trimmed = body.trimmed();
    const QString utf8 = QString::fromUtf8(body);
    const QString local = QString::fromLocal8Bit(body);
    if (trimmed.compare("OK", Qt::CaseInsensitive) != 0
        && !utf8.contains(QStringLiteral("успешно"), Qt::CaseInsensitive)
        && !local.contains(QStringLiteral("успешно"), Qt::CaseInsensitive)) {
        throw std::runtime_error("ISD did not confirm command: " + body.left(160).toStdString());
    }
}

} // namespace

struct IsdHttpTransport::Impl {
    explicit Impl(IsdHttpTransportConfig value) : config(std::move(value))
    {
        if (config.host.empty()) throw std::invalid_argument("ISD host is empty");
        if (!config.port) throw std::invalid_argument("ISD HTTP port is zero");
        if (!config.timeoutMilliseconds) throw std::invalid_argument("ISD HTTP timeout is zero");
    }

    IsdHttpTransportConfig config;

    std::string request(const std::string& path, bool expectAck)
    {
        QUrl url;
        url.setScheme(QStringLiteral("http"));
        url.setHost(QString::fromStdString(config.host));
        url.setPort(config.port);
        url.setPath(QString::fromStdString(path));

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
            throw std::runtime_error("ISD HTTP request failed: " + errorText.toStdString());
        }
        if (status < 200 || status >= 300) {
            throw std::runtime_error("ISD HTTP status " + std::to_string(status));
        }
        if (expectAck) requireAck(body);
        return body.toStdString();
    }
};

IsdHttpTransport::IsdHttpTransport(IsdHttpTransportConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

IsdHttpTransport::~IsdHttpTransport() = default;

std::string IsdHttpTransport::probe()
{
    auto body = impl_->request("/", false);
    if (body.size() > 200) body.resize(200);
    return body;
}

std::string IsdHttpTransport::command(const std::string& path)
{
    if (path.empty() || path.front() != '/') {
        throw std::invalid_argument("ISD HTTP command path must start with '/'");
    }
    return impl_->request(path, true);
}

} // namespace orbita::stand
