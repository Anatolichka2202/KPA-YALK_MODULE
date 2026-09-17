#include "orbita_stand/isd_http_transport.h"

#include <QCoreApplication>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>

#include <iostream>
#include <stdexcept>
#include <string>

using namespace orbita::stand;

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

class FakeHttpIsd final : public QTcpServer {
public:
    enum class Mode { Ack, Stall, Page };

    explicit FakeHttpIsd(Mode mode) : mode_(mode)
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto* socket = nextPendingConnection();
                ++connectionCount;
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    request += socket->readAll();
                    if (!request.contains("\r\n\r\n")) return;
                    ++requestCount;
                    if (mode_ == Mode::Stall) return;
                    const QByteArray body = mode_ == Mode::Ack ? QByteArray("OK")
                                                              : QByteArray("ISD page");
                    QByteArray reply = "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: ";
                    reply += QByteArray::number(body.size());
                    reply += "\r\n\r\n";
                    reply += body;
                    socket->write(reply);
                    socket->flush();
                    socket->disconnectFromHost();
                });
            }
        });
        if (!listen(QHostAddress::LocalHost, 0)) {
            throw std::runtime_error("Cannot start fake ISD HTTP server");
        }
    }

    unsigned connectionCount = 0;
    unsigned requestCount = 0;
    QByteArray request;

private:
    Mode mode_;
};

void acknowledgedCommandUsesOneRequest()
{
    FakeHttpIsd server(FakeHttpIsd::Mode::Ack);
    IsdHttpTransport transport({"127.0.0.1", server.serverPort(), 500});
    const auto response = transport.get("/type=2num=37val=1");
    require(response.status == 200 && response.body == "OK",
            "Acknowledged command returned wrong response");
    require(server.requestCount == 1 && server.connectionCount == 1,
            "Acknowledged command was sent more than once");
    require(server.request.contains("GET /type=2num=37val=1 "),
            "Wrong ISD command path reached the fake server");
}

void timedOutActiveCommandIsNotRetried()
{
    FakeHttpIsd server(FakeHttpIsd::Mode::Stall);
    IsdHttpTransport transport({"127.0.0.1", server.serverPort(), 60});
    bool timedOut = false;
    try {
        (void)transport.get("/type=2num=38val=1");
    } catch (const std::runtime_error&) {
        timedOut = true;
    }
    require(timedOut, "Stalled active command did not fail");
    require(server.requestCount == 1 && server.connectionCount == 1,
            "Timed-out active command was retried implicitly");
}

void probeOnlyChecksHttpConnectivity()
{
    FakeHttpIsd server(FakeHttpIsd::Mode::Page);
    IsdHttpTransport transport({"127.0.0.1", server.serverPort(), 500});
    const auto response = transport.get("/");
    require(response.body == "ISD page", "Probe page was not returned verbatim");
    require(server.requestCount == 1, "Probe was retried unexpectedly");
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    try {
        acknowledgedCommandUsesOneRequest();
        timedOutActiveCommandIsNotRetried();
        probeOnlyChecksHttpConnectivity();
        std::cout << "ISD HTTP transport tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ISD HTTP transport test failed: " << error.what() << '\n';
        return 1;
    }
}
