#include "orbita_stand/ulk_udp_transport.h"

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
#include <atomic>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace orbita::stand {
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

std::int64_t unixNanoseconds()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

sockaddr_in endpoint(const std::string& address, std::uint16_t port)
{
    sockaddr_in result{};
    result.sin_family = AF_INET;
    result.sin_port = htons(port);
    if (inet_pton(AF_INET, address.c_str(), &result.sin_addr) != 1) {
        throw std::invalid_argument("Некорректный IPv4-адрес адаптера УЛК: " + address);
    }
    return result;
}

} // namespace

struct UlkUdpTransport::Impl {
    explicit Impl(KtmaUlkUdpConfig value) : config(std::move(value))
    {
        if (!config.port || !config.queueCapacity) {
            throw std::invalid_argument("Порт и ёмкость очереди адаптера УЛК должны быть ненулевыми");
        }
#ifdef _WIN32
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            throw std::runtime_error("Не удалось инициализировать WinSock2");
        }
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

    void openSocket()
    {
        socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socket == InvalidSocket)
            throw std::runtime_error("Cannot open ULK UDP socket");

        int reuse = 1;
        setsockopt(socket,
                   SOL_SOCKET,
                   SO_REUSEADDR,
                   reinterpret_cast<const char*>(&reuse),
                   sizeof(reuse));

        sockaddr_in local{};
        local.sin_family = AF_INET;
        local.sin_port = htons(config.port);
        local.sin_addr.s_addr = htonl(INADDR_ANY);

        if (::bind(socket,
                   reinterpret_cast<const sockaddr*>(&local),
                   sizeof(local)) != 0) {
            closeSocket(socket);
            socket = InvalidSocket;
            throw std::runtime_error(
                "Cannot bind ULK UDP receive socket");
        }
    }

    void start(std::uint8_t mode)
    {
        stop();

        {
            std::lock_guard<std::mutex> lock(mutex);
            queue.clear();
            counters = {};
        }

        openSocket();
        stopping.store(false);

        {
            std::lock_guard<std::mutex> lock(mutex);
            counters.running = true;
        }

        receiver = std::thread([this] { receiveLoop(); });

        const auto bytes =
            UlkUdpTransport::modeCommand(mode);

        const auto remote =
            endpoint(config.remoteHost, config.port);

        Socket sender =
            ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

        if (sender == InvalidSocket) {
            stop();
            throw std::runtime_error(
                "Cannot open ULK command socket");
        }

        const auto source =
            endpoint(config.localHost, 0);

        if (::bind(
                sender,
                reinterpret_cast<const sockaddr*>(&source),
                sizeof(source)) != 0) {

            closeSocket(sender);
            stop();

            throw std::runtime_error(
                "Cannot bind ULK command socket");
        }

        const int sent = ::sendto(
            sender,
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<int>(bytes.size()),
            0,
            reinterpret_cast<const sockaddr*>(&remote),
            sizeof(remote));

        closeSocket(sender);

        if (sent != static_cast<int>(bytes.size())) {
            stop();
            throw std::runtime_error(
                "Cannot send ULK mode command");
        }
    }

    void prepareYalkReference()
    {
        stop();

        {
            std::lock_guard<std::mutex> lock(mutex);
            queue.clear();
            counters = {};
        }

        openSocket();
        stopping.store(false);

        {
            std::lock_guard<std::mutex> lock(mutex);
            counters.running = true;
        }

        receiver = std::thread([this] { receiveLoop(); });

        const auto remote =
            endpoint(config.remoteHost, config.port);

        referenceSender =
            ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

        if (referenceSender == InvalidSocket) {
            stop();
            throw std::runtime_error(
                "Cannot open ULK ROKT command socket");
        }

        const auto source =
            endpoint(config.localHost, 0);

        if (::bind(
                referenceSender,
                reinterpret_cast<const sockaddr*>(&source),
                sizeof(source)) != 0) {

            closeSocket(referenceSender);
            referenceSender = InvalidSocket;
            stop();

            throw std::runtime_error(
                "Cannot bind ULK ROKT command socket");
        }

        std::array<std::uint8_t, 128> reset{};
        reset[0] = 'R';
        reset[1] = 'O';
        reset[2] = 'K';
        reset[3] = 'T';
        reset[4] = 0x16;

        std::array<std::uint8_t, 128> addrYalk{};
        addrYalk[0] = 'R';
        addrYalk[1] = 'O';
        addrYalk[2] = 'K';
        addrYalk[3] = 'T';
        addrYalk[4] = 0x14;
        addrYalk[5] = 0x01;
        addrYalk[6] = 0x2B;
        addrYalk[7] = 0x01;
        addrYalk[8] = 0x00;

        std::array<std::uint8_t, 128> addrYtp{};
        addrYtp[0] = 'R';
        addrYtp[1] = 'O';
        addrYtp[2] = 'K';
        addrYtp[3] = 'T';
        addrYtp[4] = 0x15;
        addrYtp[5] = 0x01;
        addrYtp[6] = 0x01;
        addrYtp[7] = 0x01;
        addrYtp[8] = 0x00;

        const auto sendCommand =
            [&](const std::array<std::uint8_t, 128>& command) {

                const int sent = ::sendto(
                    referenceSender,
                    reinterpret_cast<const char*>(
                        command.data()),
                    static_cast<int>(command.size()),
                    0,
                    reinterpret_cast<const sockaddr*>(&remote),
                    sizeof(remote));

                if (sent != static_cast<int>(command.size())) {
                    closeSocket(referenceSender);
                    referenceSender = InvalidSocket;
                    stop();

                    throw std::runtime_error(
                        "Cannot send ULK ROKT command");
                }
            };

        sendCommand(reset);
        std::this_thread::sleep_for(
            std::chrono::milliseconds(50));

        sendCommand(addrYalk);
        std::this_thread::sleep_for(
            std::chrono::milliseconds(50));

        sendCommand(addrYtp);
    }

    void startPreparedYalkReference()
    {
        if (referenceSender == InvalidSocket) {
            throw std::runtime_error(
                "ULK ROKT reference mode is not prepared");
        }

        const auto remote =
            endpoint(config.remoteHost, config.port);

        std::array<std::uint8_t, 128> startYalk{};
        startYalk[0] = 'R';
        startYalk[1] = 'O';
        startYalk[2] = 'K';
        startYalk[3] = 'T';
        startYalk[4] = 0x0A;
        startYalk[5] = 0x00;
        startYalk[6] = 0x00;
        startYalk[7] = 0x01;
        startYalk[8] = 0x00;

        const int sent = ::sendto(
            referenceSender,
            reinterpret_cast<const char*>(startYalk.data()),
            static_cast<int>(startYalk.size()),
            0,
            reinterpret_cast<const sockaddr*>(&remote),
            sizeof(remote));

        closeSocket(referenceSender);
        referenceSender = InvalidSocket;

        if (sent != static_cast<int>(startYalk.size())) {
            stop();
            throw std::runtime_error(
                "Cannot send ULK ROKT start command");
        }
    }

    void startYalkReference()
    {
        prepareYalkReference();
        std::this_thread::sleep_for(
            std::chrono::milliseconds(50));
        startPreparedYalkReference();
    }

    void stop() noexcept
    {
        stopping.store(true);
        const Socket command = referenceSender;
        referenceSender = InvalidSocket;
        closeSocket(command);
        const Socket active = socket;
        socket = InvalidSocket;
        if (active != InvalidSocket) {
#ifdef _WIN32
            shutdown(active, SD_BOTH);
#else
            shutdown(active, SHUT_RDWR);
#endif
            closeSocket(active);
        }
        condition.notify_all();
        if (receiver.joinable()) receiver.join();
        std::lock_guard<std::mutex> lock(mutex);
        counters.running = false;
        counters.streaming = false;
    }

    void startRecord(const std::string& path)
    {
        std::lock_guard<std::mutex> lock(mutex);
        record.close();
        record.clear();
        record.open(path, std::ios::binary | std::ios::trunc);
        if (!record) throw std::runtime_error("Не удалось открыть raw-файл УЛК: " + path);
        const char magic[4]{'U', 'L', 'K', 'R'};
        const std::uint16_t schema = 1;
        record.write(magic, sizeof(magic));
        record.write(reinterpret_cast<const char*>(&schema), sizeof(schema));
    }

    void stopRecord() noexcept
    {
        std::lock_guard<std::mutex> lock(mutex);
        record.close();
    }

    void receiveLoop() noexcept
    {
        const auto expected = endpoint(config.remoteHost, config.port);
        while (!stopping.load()) {
            const Socket active = socket;
            if (active == InvalidSocket) break;
#ifdef _WIN32
            WSAPOLLFD descriptor{active, POLLRDNORM, 0};
            const int ready = WSAPoll(&descriptor, 1, 100);
#else
            pollfd descriptor{active, POLLIN, 0};
            const int ready = poll(&descriptor, 1, 100);
#endif
            if (ready <= 0) continue;
            std::array<std::uint8_t, 2048> bytes{};
            sockaddr_in sender{};
#ifdef _WIN32
            int senderSize = sizeof(sender);
#else
            socklen_t senderSize = sizeof(sender);
#endif
            const int count = recvfrom(active, reinterpret_cast<char*>(bytes.data()),
                                       static_cast<int>(bytes.size()), 0,
                                       reinterpret_cast<sockaddr*>(&sender), &senderSize);
            if (count <= 0) continue;
            if (sender.sin_addr.s_addr != expected.sin_addr.s_addr
                || sender.sin_port != expected.sin_port) continue;

            UlkFrame frame;
            frame.receivedUnixNs = unixNanoseconds();
            frame.kind = UlkUdpTransport::classify(static_cast<std::size_t>(count));
            frame.payload.assign(bytes.begin(), bytes.begin() + count);
            {
                std::lock_guard<std::mutex> lock(mutex);
                frame.sequence = ++counters.lastSequence;
                switch (frame.kind) {
                case UlkFrameKind::Service4:
                    ++counters.service4;
                    break;

                case UlkFrameKind::Fast120:
                    ++counters.fast120;
                    break;

                case UlkFrameKind::Slow200:
                    ++counters.slow200;
                    break;

                case UlkFrameKind::Reference204:
                    ++counters.reference204;
                    break;

                case UlkFrameKind::Unknown:
                    ++counters.unknown;
                    break;
                }
                if (frame.kind == UlkFrameKind::Fast120
                    || frame.kind == UlkFrameKind::Slow200
                    || frame.kind == UlkFrameKind::Reference204)
                    counters.streaming = true;
                if (queue.size() == config.queueCapacity) {
                    queue.pop_front();
                    ++counters.dropped;
                }
                if (record) {
                    const auto payloadSize = static_cast<std::uint16_t>(frame.payload.size());
                    const auto kind = static_cast<std::uint8_t>(frame.kind);
                    record.write(reinterpret_cast<const char*>(&frame.sequence), sizeof(frame.sequence));
                    record.write(reinterpret_cast<const char*>(&frame.receivedUnixNs), sizeof(frame.receivedUnixNs));
                    record.write(reinterpret_cast<const char*>(&payloadSize), sizeof(payloadSize));
                    record.write(reinterpret_cast<const char*>(&kind), sizeof(kind));
                    record.write(reinterpret_cast<const char*>(frame.payload.data()), payloadSize);
                    record.flush();
                }
                queue.push_back(std::move(frame));
            }
            condition.notify_all();
        }
    }

    UlkFrame waitFrame(UlkFrameKind kind, std::uint64_t afterSequence,
                       std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(mutex);
        const auto findFrame = [&]() {
            return std::find_if(queue.begin(), queue.end(), [&](const UlkFrame& frame) {
                return frame.sequence > afterSequence && frame.kind == kind;
            });
        };
        if (!condition.wait_for(lock, timeout, [&] {
                return stopping.load() || findFrame() != queue.end();
            })) {
            throw std::runtime_error("Тайм-аут ожидания кадра адаптера УЛК");
        }
        const auto item = findFrame();
        if (item == queue.end()) throw std::runtime_error("Приём адаптера УЛК остановлен");
        return *item;
    }

    KtmaUlkUdpConfig config;
    Socket socket = InvalidSocket;
    Socket referenceSender = InvalidSocket;
    std::thread receiver;
    std::atomic_bool stopping{true};
    mutable std::mutex mutex;
    std::condition_variable condition;
    std::deque<UlkFrame> queue;
    UlkStreamStats counters;
    std::ofstream record;
#ifdef _WIN32
    bool winsockStarted = false;
#endif
};

UlkUdpTransport::UlkUdpTransport(KtmaUlkUdpConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}
UlkUdpTransport::~UlkUdpTransport() = default;
void UlkUdpTransport::start(std::uint8_t mode) { impl_->start(mode); }
void UlkUdpTransport::prepareYalkReference()
{
    impl_->prepareYalkReference();
}
void UlkUdpTransport::startPreparedYalkReference()
{
    impl_->startPreparedYalkReference();
}
void UlkUdpTransport::startYalkReference()
{
    impl_->startYalkReference();
}
void UlkUdpTransport::stop() noexcept { impl_->stop(); }

UlkFrame UlkUdpTransport::waitFrame(UlkFrameKind kind, std::uint64_t afterSequence,
                                    std::chrono::milliseconds timeout)
{
    return impl_->waitFrame(kind, afterSequence, timeout);
}

std::vector<UlkFrame> UlkUdpTransport::takeFrames()
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::vector<UlkFrame> result;
    result.reserve(impl_->queue.size());
    while (!impl_->queue.empty()) {
        result.push_back(std::move(impl_->queue.front()));
        impl_->queue.pop_front();
    }
    return result;
}

UlkStreamStats UlkUdpTransport::stats() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->counters;
}

void UlkUdpTransport::startRecord(const std::string& path) { impl_->startRecord(path); }
void UlkUdpTransport::stopRecord() noexcept { impl_->stopRecord(); }

std::vector<std::uint8_t> UlkUdpTransport::modeCommand(std::uint8_t mode)
{
    return {0x44, 0x01, mode};
}

UlkFrameKind UlkUdpTransport::classify(std::size_t size) noexcept
{
    if (size == 4) return UlkFrameKind::Service4;
    if (size == 120) return UlkFrameKind::Fast120;
    if (size == 200) return UlkFrameKind::Slow200;
    if (size == 204) return UlkFrameKind::Reference204;
    return UlkFrameKind::Unknown;
}

const char* toString(UlkFrameKind kind) noexcept
{
    switch (kind) {
    case UlkFrameKind::Service4: return "service4";
    case UlkFrameKind::Fast120: return "fast120";
    case UlkFrameKind::Slow200: return "slow200";
    case UlkFrameKind::Reference204: return "reference204";
    case UlkFrameKind::Unknown: return "unknown";
    }
    return "unknown";
}

} // namespace orbita::stand
