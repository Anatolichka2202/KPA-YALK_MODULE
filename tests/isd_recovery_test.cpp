#include "hardware/isd_router.h"
#include "hardware/stand_config.h"

#include <QCoreApplication>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

class FakeIsdServer final : public QThread
{
public:
    ~FakeIsdServer() override
    {
        stopping_.store(true);
        wait();
    }

    quint16 waitUntilListening()
    {
        start();
        std::unique_lock<std::mutex> lock(mutex_);
        ready_.wait(lock, [this] { return listening_; });
        return port_;
    }

    void failNextRequests(int count) noexcept { failNext_.store(count); }
    void moduleNoResponseNextRequests(int count) noexcept
    {
        moduleNoResponseNext_.store(count);
    }
    void serverErrorNextRequests(int count) noexcept { serverErrorNext_.store(count); }

    std::vector<std::string> paths() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return paths_;
    }

    int connectionCount() const noexcept { return connectionCount_.load(); }

protected:
    void run() override
    {
        QTcpServer server;
        const bool listening = server.listen(QHostAddress::LocalHost, 0);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            port_ = listening ? server.serverPort() : 0;
            listening_ = true;
        }
        ready_.notify_all();
        if (!listening) return;

        std::vector<std::unique_ptr<QTcpSocket>> unansweredSockets;

        while (!stopping_.load()) {
            if (!server.waitForNewConnection(50)) continue;
            while (server.hasPendingConnections()) {
                std::unique_ptr<QTcpSocket> socket(server.nextPendingConnection());
                if (!socket) continue;
                ++connectionCount_;
                while (!stopping_.load()
                       && socket->state() == QAbstractSocket::ConnectedState) {
                    if (!socket->waitForReadyRead(100)) continue;
                    QByteArray request = socket->readAll();
                    while (!request.contains("\r\n\r\n") && socket->waitForReadyRead(100))
                        request += socket->readAll();

                    const auto firstSpace = request.indexOf(' ');
                    const auto secondSpace = request.indexOf(' ', firstSpace + 1);
                    const QByteArray path = firstSpace >= 0 && secondSpace > firstSpace
                        ? request.mid(firstSpace + 1, secondSpace - firstSpace - 1)
                        : QByteArray{};
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        paths_.push_back(path.toStdString());
                    }

                    if (failNext_.load() > 0) {
                        --failNext_;
                        unansweredSockets.push_back(std::move(socket));
                        break;
                    }

                    if (serverErrorNext_.load() > 0) {
                        --serverErrorNext_;
                        static const QByteArray unavailable(
                            "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\n"
                            "Connection: keep-alive\r\n\r\n");
                        socket->write(unavailable);
                        socket->waitForBytesWritten(1000);
                        continue;
                    }

                    QByteArray body("OK");
                    if (moduleNoResponseNext_.load() > 0) {
                        --moduleNoResponseNext_;
                        body = "Komanda ne vypolnena! Modul ne otvechaet 8!";
                    }
                    const QByteArray response = "HTTP/1.1 200 OK\r\nContent-Length: "
                        + QByteArray::number(body.size())
                        + "\r\nConnection: keep-alive\r\n\r\n" + body;
                    socket->write(response);
                    socket->waitForBytesWritten(1000);
                }
            }
        }
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::vector<std::string> paths_;
    std::atomic_bool stopping_{false};
    std::atomic_int failNext_{0};
    std::atomic_int moduleNoResponseNext_{0};
    std::atomic_int serverErrorNext_{0};
    std::atomic_int connectionCount_{0};
    bool listening_ = false;
    quint16 port_ = 0;
};

int countPath(const std::vector<std::string>& paths, const std::string& expected)
{
    return static_cast<int>(std::count(paths.begin(), paths.end(), expected));
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    try {
        const auto productionConfig = tu::hardware::loadStandConfig(
            std::filesystem::path(TU_SOURCE_DIR) / "data" / "stand_ktma.yaml");
        require(productionConfig.isd.requestAttempts == 3,
                "Production-профиль ИСД должен выполнять не более трёх попыток");
        require(productionConfig.isd.retryDelayMilliseconds == 100,
                "Production-профиль ИСД должен выдерживать 100 мс между попытками");
        require(productionConfig.v7.timeoutMilliseconds >= 25000,
                "Первое AC-чтение В7 на ЯВП требует тайм-аут не менее 25 с");

        FakeIsdServer server;
        const quint16 port = server.waitUntilListening();
        require(port != 0, "Не удалось запустить локальный имитатор ИСД");

        tu::hardware::IsdConfig config;
        config.host = "127.0.0.1";
        config.port = port;
        config.timeoutMilliseconds = 100;
        config.serviceTimeoutMilliseconds = 1000;
        config.requestAttempts = 3;
        config.retryDelayMilliseconds = 10;
        tu::hardware::IsdRouter isd(config);

        int pauseCount = 0;
        int restoredCount = 0;
        std::vector<tu::hardware::IsdRequestTrace> traces;
        isd.setTraceSink([&traces](const tu::hardware::IsdRequestTrace& trace) {
            traces.push_back(trace);
        });
        isd.setRecoveryHandler([&pauseCount](const std::string&) {
            ++pauseCount;
            return true;
        });
        isd.setRecoveryRestoredSink([&restoredCount] { ++restoredCount; });

        isd.setAnalog(7, 321, true);
        isd.setYalkVoltage(25, 4.0);
        isd.disableYalkOutput(25);
        require(countPath(server.paths(), "/type=1num=25val=2457work=1bus=1") == 1,
                "ЯЛК должен включаться кодом type=1 из экспериментальной таблицы, не type=5");
        require(countPath(server.paths(), "/type=1num=25val=819work=0") == 1,
                "ЯЛК должен адресно отключаться после измерения");
        isd.setSwitch(2, 1, false);
        isd.setSwitch(2, 2, false);
        require(server.connectionCount() < 3,
                "Долгоживущий QNetworkAccessManager должен повторно использовать хотя бы одно HTTP-соединение");

        server.failNextRequests(3);
        isd.setSwitch(3, 44, true);

        auto afterRecovery = server.paths();
        require(pauseCount == 1, "При потере ответа ИСД должна быть одна пауза; фактически "
                + std::to_string(pauseCount));
        require(restoredCount == 1, "После recovery должно быть уведомление о продолжении");
        require(countPath(afterRecovery, "/type=1num=7val=321work=1") == 2,
                "После перезапуска должно восстановиться подтверждённое аналоговое состояние");
        require(countPath(afterRecovery, "/type=3num=44val=1") == 4,
                "До паузы должны быть ровно три попытки, после восстановления — продолжение команды");
        require(countPath(afterRecovery, "/") == 1,
                "Перед восстановлением нужен один пассивный probe ИСД");

        server.moduleNoResponseNextRequests(2);
        isd.setSwitch(3, 45, true);
        require(pauseCount == 1,
                "Успешная третья попытка после двух ответов модуля не должна открывать окно");
        afterRecovery = server.paths();
        require(countPath(afterRecovery, "/type=3num=45val=1") == 3,
                "Команда должна завершиться на третьей попытке");

        server.moduleNoResponseNextRequests(3);
        isd.setSwitch(3, 46, true);
        require(pauseCount == 2,
                "Три ответа 'модуль не отвечает' должны открыть окно перезапуска ИСД");
        afterRecovery = server.paths();
        require(countPath(afterRecovery, "/type=3num=46val=1") == 4,
                "После трёх ошибок и перезапуска текущая команда должна продолжиться");

        server.serverErrorNextRequests(3);
        isd.setSwitch(3, 47, true);
        require(pauseCount == 3,
                "Три HTTP 503 должны открыть окно перезапуска ИСД");
        afterRecovery = server.paths();
        require(countPath(afterRecovery, "/type=3num=47val=1") == 4,
                "После трёх HTTP-ошибок и перезапуска текущая команда должна продолжиться");
        require(!traces.empty() && traces.back().sequence == traces.size(),
                "Журнал ИСД должен содержать непрерывные sequence number");
        require(std::any_of(traces.begin(), traces.end(), [](const auto& trace) {
                    return trace.timeout && trace.httpStatus == 0
                        && trace.type == 3 && trace.channel == 44;
                }), "Журнал ИСД должен фиксировать timeout, type, num и HTTP status");
        require(std::any_of(traces.begin(), traces.end(), [](const auto& trace) {
                    return !trace.accepted && trace.httpStatus == 200
                        && trace.response.find("Modul ne otvechaet 8") != std::string::npos;
                }), "HTTP 200 с ответом 'модуль не отвечает' должен быть ошибкой команды");
        require(std::any_of(traces.begin(), traces.end(), [](const auto& trace) {
                    return !trace.accepted && trace.httpStatus == 503;
                }), "Журнал ИСД должен фиксировать HTTP 503 как ошибку команды");
        for (const auto& path : afterRecovery)
            require(path.find("type=4") == std::string::npos,
                    "Recovery не должен использовать type=4");

        isd.safeStop();
        const auto afterCleanup = server.paths();
        require(countPath(afterCleanup, "/type=3num=44val=0") == 1,
                "Cleanup должен адресно снять type=3");
        require(countPath(afterCleanup, "/type=1num=7val=0work=0") == 1,
                "Cleanup должен адресно снять analog type=1");

        std::cout << "isd_recovery_test: OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "isd_recovery_test: FAIL: " << error.what() << '\n';
        return 1;
    }
}
