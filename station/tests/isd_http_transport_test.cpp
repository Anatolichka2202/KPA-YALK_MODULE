#include "orbita_stand/isd_http_transport.h"

#include <QCoreApplication>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace orbita::stand;

namespace {

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

}

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    try {
        QTcpServer server;
        require(server.listen(QHostAddress::LocalHost, 0), "Cannot start fake ISD server");

        unsigned requests = 0;
        bool replyEnabled = true;
        QByteArray lastRequest;
        QObject::connect(&server, &QTcpServer::newConnection, [&] {
            while (server.hasPendingConnections()) {
                auto* socket = server.nextPendingConnection();
                QObject::connect(socket, &QTcpSocket::readyRead, [&, socket] {
                    lastRequest += socket->readAll();
                    if (!lastRequest.contains("\r\n\r\n")) return;
                    ++requests;
                    if (replyEnabled) {
                        socket->write("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nOK");
                        socket->flush();
                        socket->disconnectFromHost();
                    }
                });
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });

        IsdHttpTransport transport({
            "127.0.0.1",
            static_cast<std::uint16_t>(server.serverPort()),
            150});

        transport.command("/type=2num=37val=1");
        require(requests == 1, "Successful command was sent more than once");
        require(lastRequest.startsWith("GET /type=2num=37val=1 HTTP/1.1"),
                "Unexpected HTTP request line");

        replyEnabled = false;
        lastRequest.clear();
        bool timeoutObserved = false;
        try { transport.command("/type=4num=1"); }
        catch (const std::runtime_error&) { timeoutObserved = true; }
        require(timeoutObserved, "Fake timeout was not propagated");
        require(requests == 2,
                "One-shot transport retried a timed-out command implicitly");

        std::cout << "ISD HTTP transport tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "ISD HTTP transport test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
