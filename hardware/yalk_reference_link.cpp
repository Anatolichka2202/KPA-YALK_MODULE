#include "hardware/yalk_reference_link.h"

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <poll.h>
#  include <sys/socket.h>
#  include <unistd.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <thread>

namespace tu::hardware {
namespace {

#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket InvalidSocket = INVALID_SOCKET;
void closeSocket(Socket value) { if (value != InvalidSocket) closesocket(value); }
#else
using Socket = int;
constexpr Socket InvalidSocket = -1;
void closeSocket(Socket value) { if (value != InvalidSocket) ::close(value); }
#endif

sockaddr_in endpoint(const std::string& address, std::uint16_t port)
{
    sockaddr_in result{};
    result.sin_family = AF_INET;
    result.sin_port = htons(port);
    if (inet_pton(AF_INET, address.c_str(), &result.sin_addr) != 1) {
        throw std::invalid_argument("Некорректный IPv4-адрес адаптера ЯЛК: " + address);
    }
    return result;
}

void checkpointAndSleep(const YalkReferenceLink::Checkpoint& checkpoint,
                        std::chrono::milliseconds duration)
{
    constexpr auto slice = std::chrono::milliseconds(50);
    auto remaining = duration;
    while (remaining.count() > 0) {
        if (checkpoint) checkpoint();
        const auto part = std::min(slice, remaining);
        std::this_thread::sleep_for(part);
        remaining -= part;
    }
}

std::array<std::uint8_t, 128> command(std::uint8_t code)
{
    std::array<std::uint8_t, 128> result{};
    result[0] = 'R';
    result[1] = 'O';
    result[2] = 'K';
    result[3] = 'T';
    result[4] = code;
    return result;
}

} // namespace

struct YalkReferenceLink::Impl {
    explicit Impl(YalkUdpConfig value) : config(std::move(value))
    {
        if (!config.port) throw std::invalid_argument("UDP-порт адаптера ЯЛК равен нулю");
        remote = endpoint(config.remoteHost, config.port);
        local = endpoint(config.localHost, config.port);
#ifdef _WIN32
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
            throw std::runtime_error("Не удалось инициализировать WinSock2");
        winsockStarted = true;
#endif
    }

    ~Impl()
    {
        stop();
#ifdef _WIN32
        if (winsockStarted) WSACleanup();
#endif
    }

    void stop() noexcept
    {
        const Socket value = receiver;
        receiver = InvalidSocket;
        if (value != InvalidSocket) {
#ifdef _WIN32
            shutdown(value, SD_BOTH);
#else
            shutdown(value, SHUT_RDWR);
#endif
            closeSocket(value);
        }
    }

    void openReceiver()
    {
        stop();
        receiver = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (receiver == InvalidSocket)
            throw std::runtime_error("Не удалось открыть UDP-сокет адаптера ЯЛК");

        int reuse = 1;
        setsockopt(receiver, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&reuse), sizeof(reuse));
        if (::bind(receiver, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0) {
            closeSocket(receiver);
            receiver = InvalidSocket;
            throw std::runtime_error("Не удалось привязать UDP-сокет адаптера ЯЛК к "
                                     + config.localHost + ':' + std::to_string(config.port));
        }
    }

    void sendReferenceStart()
    {
        openReceiver();

        Socket sender = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sender == InvalidSocket) {
            stop();
            throw std::runtime_error("Не удалось открыть командный UDP-сокет адаптера ЯЛК");
        }

        sockaddr_in source = endpoint(config.localHost, 0);
        if (::bind(sender, reinterpret_cast<const sockaddr*>(&source), sizeof(source)) != 0) {
            closeSocket(sender);
            stop();
            throw std::runtime_error("Не удалось привязать командный UDP-сокет адаптера ЯЛК");
        }

        auto reset = command(0x16);
        auto addrYalk = command(0x14);
        addrYalk[5] = 0x01;
        addrYalk[6] = 0x2B;
        addrYalk[7] = 0x01;
        addrYalk[8] = 0x00;
        auto addrYtp = command(0x15);
        addrYtp[5] = 0x01;
        addrYtp[6] = 0x01;
        addrYtp[7] = 0x01;
        addrYtp[8] = 0x00;
        auto startYalk = command(0x0A);
        startYalk[5] = 0x00;
        startYalk[6] = 0x00;
        startYalk[7] = 0x01;
        startYalk[8] = 0x00;

        const auto send = [&](const std::array<std::uint8_t, 128>& bytes) {
            const int sent = ::sendto(
                sender, reinterpret_cast<const char*>(bytes.data()),
                static_cast<int>(bytes.size()), 0,
                reinterpret_cast<const sockaddr*>(&remote), sizeof(remote));
            if (sent != static_cast<int>(bytes.size())) {
                throw std::runtime_error("Не удалось отправить ROKT-команду адаптеру ЯЛК");
            }
        };

        try {
            // Последовательность и интервалы перенесены без изменения из
            // подтверждённого UlkUdpTransport старой поставки.
            send(reset);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            send(addrYalk);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            send(addrYtp);
            send(startYalk);
        } catch (...) {
            closeSocket(sender);
            stop();
            throw;
        }
        closeSocket(sender);
    }

    bool waitReference(std::chrono::milliseconds timeout,
                       const YalkReferenceLink::Checkpoint& checkpoint)
    {
        if (receiver == InvalidSocket)
            throw std::runtime_error("Поток ЯЛК не запущен");

        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            if (checkpoint) checkpoint();
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());
            const int pollMs = static_cast<int>(std::max<std::int64_t>(
                1, std::min<std::int64_t>(100, remaining.count())));
#ifdef _WIN32
            WSAPOLLFD descriptor{receiver, POLLRDNORM, 0};
            const int ready = WSAPoll(&descriptor, 1, pollMs);
#else
            pollfd descriptor{receiver, POLLIN, 0};
            const int ready = poll(&descriptor, 1, pollMs);
#endif
            if (ready <= 0) continue;

            std::array<std::uint8_t, 2048> bytes{};
            sockaddr_in sender{};
#ifdef _WIN32
            int senderSize = sizeof(sender);
#else
            socklen_t senderSize = sizeof(sender);
#endif
            const int count = recvfrom(
                receiver, reinterpret_cast<char*>(bytes.data()),
                static_cast<int>(bytes.size()), 0,
                reinterpret_cast<sockaddr*>(&sender), &senderSize);
            if (count <= 0) continue;
            if (sender.sin_addr.s_addr != remote.sin_addr.s_addr
                || sender.sin_port != remote.sin_port) continue;
            // Подтверждённый ROKT ЯЛК выдаёт reference204 кадры.
            if (count == 204) return true;
        }
        return false;
    }

    YalkUdpConfig config;
    sockaddr_in remote{};
    sockaddr_in local{};
    Socket receiver = InvalidSocket;
#ifdef _WIN32
    bool winsockStarted = false;
#endif
};

YalkReferenceLink::YalkReferenceLink(YalkUdpConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

YalkReferenceLink::~YalkReferenceLink() = default;

bool YalkReferenceLink::restartUntilReady(std::chrono::milliseconds timeout,
                                          const Checkpoint& checkpoint)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (checkpoint) checkpoint();
        impl_->sendReferenceStart();

        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) break;
        const auto attempt = std::min(remaining, std::chrono::milliseconds(750));
        if (impl_->waitReference(attempt, checkpoint)) return true;

        impl_->stop();
        const auto after = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (after.count() <= 0) break;
        checkpointAndSleep(checkpoint, std::min(after, std::chrono::milliseconds(250)));
    }
    impl_->stop();
    return false;
}

bool YalkReferenceLink::waitNextReference(std::chrono::milliseconds timeout,
                                          const Checkpoint& checkpoint)
{
    return impl_->waitReference(timeout, checkpoint);
}

void YalkReferenceLink::stop() noexcept
{
    impl_->stop();
}

} // namespace tu::hardware
