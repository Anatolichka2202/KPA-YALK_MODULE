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
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace tu::hardware {
namespace {

class FrameTimeout final : public std::runtime_error {
public:
    FrameTimeout() : std::runtime_error("Тайм-аут ожидания кадра адаптера УБСИ") {}
};

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
    if (inet_pton(AF_INET, address.c_str(), &result.sin_addr) != 1)
        throw std::invalid_argument("Некорректный IPv4-адрес адаптера УБСИ: " + address);
    return result;
}

void sleepChecked(const YalkReferenceLink::Checkpoint& checkpoint,
                  std::chrono::milliseconds duration)
{
    constexpr auto slice = std::chrono::milliseconds(50);
    while (duration.count() > 0) {
        if (checkpoint) checkpoint();
        const auto part = std::min(slice, duration);
        std::this_thread::sleep_for(part);
        duration -= part;
    }
}

std::array<std::uint8_t, 128> command(std::uint8_t code)
{
    std::array<std::uint8_t, 128> value{};
    value[0] = 'R'; value[1] = 'O'; value[2] = 'K'; value[3] = 'T'; value[4] = code;
    return value;
}

std::vector<YalkChannelReading> decodeYalkFrame(const std::vector<std::uint8_t>& frame)
{
    if (frame.size() != 204)
        throw std::invalid_argument("reference204 имеет неверный размер");
    std::vector<YalkChannelReading> result(100);
    for (std::size_t index = 0; index < 100; ++index) {
        const std::size_t offset = 4 + index * 2;
        const std::uint16_t word = static_cast<std::uint16_t>(frame[offset])
            | (static_cast<std::uint16_t>(frame[offset + 1]) << 8);
        result[index].rawMean = word;
        result[index].codeMean = word & 0x03FFu;
        result[index].contact = (word & 0x0400u) != 0;
    }
    return result;
}

YtpSnapshot decodeYtpFrame(const std::vector<std::uint8_t>& frame)
{
    if (frame.size() != 68 || frame[0] != 0x01 || frame[1] != 0x00
        || frame[2] != 0x34 || frame[3] != 0x00)
        throw std::invalid_argument("Кадр ЯТП ROKT имеет неверный формат");
    YtpSnapshot result;
    for (std::size_t index = 0; index < 32; ++index) {
        const std::size_t offset = 4 + index * 2;
        const auto word = static_cast<std::uint16_t>(frame[offset])
            | (static_cast<std::uint16_t>(frame[offset + 1]) << 8);
        const double value = word == 0x8000 ? 32768.0 : static_cast<double>(word);
        if (word != 0x8000) ++result.validWordCount;
        if (index < 30) result.channels[index] = value;
        else if (index == 30) result.calibration31 = value;
        else result.calibration32 = value;
    }
    return result;
}

} // namespace

struct YalkReferenceLink::Impl
{
    struct QueuedFrame {
        std::uint64_t sequence = 0;
        std::vector<std::uint8_t> bytes;
    };

    explicit Impl(YalkUdpConfig value) : config(std::move(value))
    {
        if (!config.port) throw std::invalid_argument("UDP-порт адаптера УБСИ равен нулю");
#ifdef _WIN32
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
            throw std::runtime_error("Не удалось инициализировать WinSock2");
        winsockStarted = true;
#endif
        remote = endpoint(config.remoteHost, config.port);
        local = endpoint(config.localHost, config.port);
    }

    ~Impl()
    {
        stop();
#ifdef _WIN32
        if (winsockStarted) WSACleanup();
#endif
    }

    void setLiveSink(LiveYalkSink sink)
    {
        std::lock_guard<std::mutex> lock(frameMutex);
        liveSink = std::move(sink);
    }

    void setLiveYtpSink(LiveYtpSink sink)
    {
        std::lock_guard<std::mutex> lock(frameMutex);
        liveYtpSink = std::move(sink);
    }

    void stop() noexcept
    {
        stopping.store(true);
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
        frameCv.notify_all();
        if (receiverThread.joinable()
            && receiverThread.get_id() != std::this_thread::get_id()) {
            receiverThread.join();
        }
        {
            std::lock_guard<std::mutex> lock(frameMutex);
            frames.clear();
            sequence = 0;
            lastLivePublish = {};
            lastYtpLivePublish = {};
        }
    }

    void receiveLoop(Socket socket) noexcept
    {
        while (!stopping.load()) {
#ifdef _WIN32
            WSAPOLLFD descriptor{socket, POLLRDNORM, 0};
            const int ready = WSAPoll(&descriptor, 1, 100);
#else
            pollfd descriptor{socket, POLLIN, 0};
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
            const int count = recvfrom(socket, reinterpret_cast<char*>(bytes.data()),
                static_cast<int>(bytes.size()), 0,
                reinterpret_cast<sockaddr*>(&sender), &senderSize);
            if (count <= 0) continue;
            if (sender.sin_addr.s_addr != remote.sin_addr.s_addr
                || sender.sin_port != remote.sin_port) continue;
            if (count != 204 && count != 68) continue;

            QueuedFrame queued;
            queued.bytes.assign(bytes.begin(), bytes.begin() + count);
            LiveYalkSink sink;
            LiveYtpSink ytpSink;
            bool publishLive = false;
            bool publishYtpLive = false;
            {
                std::lock_guard<std::mutex> lock(frameMutex);
                queued.sequence = ++sequence;
                frames.push_back(queued);
                while (frames.size() > 1024) frames.pop_front();

                const auto now = std::chrono::steady_clock::now();
                if (count == 204 && liveSink
                    && (lastLivePublish.time_since_epoch().count() == 0
                        || now - lastLivePublish >= std::chrono::milliseconds(50))) {
                    sink = liveSink;
                    lastLivePublish = now;
                    publishLive = true;
                }
                if (count == 68 && liveYtpSink
                    && (lastYtpLivePublish.time_since_epoch().count() == 0
                        || now - lastYtpLivePublish >= std::chrono::milliseconds(50))) {
                    ytpSink = liveYtpSink;
                    lastYtpLivePublish = now;
                    publishYtpLive = true;
                }
            }
            frameCv.notify_all();

            if (publishLive) {
                try { sink(decodeYalkFrame(queued.bytes), queued.sequence); }
                catch (...) {}
            }
            if (publishYtpLive) {
                try { ytpSink(decodeYtpFrame(queued.bytes), queued.sequence); }
                catch (...) {}
            }
        }
    }

    void openReceiver()
    {
        stop();
        Socket value = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (value == InvalidSocket)
            throw std::runtime_error("Не удалось открыть UDP-сокет адаптера УБСИ");
        int reuse = 1;
        setsockopt(value, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&reuse), sizeof(reuse));
        if (::bind(value, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0) {
            closeSocket(value);
            throw std::runtime_error("Не удалось привязать UDP-сокет адаптера к "
                + config.localHost + ':' + std::to_string(config.port));
        }

        {
            std::lock_guard<std::mutex> lock(frameMutex);
            frames.clear();
            sequence = 0;
            lastLivePublish = {};
            lastYtpLivePublish = {};
        }
        receiver = value;
        stopping.store(false);
        receiverThread = std::thread([this, value] { receiveLoop(value); });
    }

    Socket openSender()
    {
        Socket sender = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sender == InvalidSocket)
            throw std::runtime_error("Не удалось открыть командный UDP-сокет");
        sockaddr_in source = endpoint(config.localHost, 0);
        if (::bind(sender, reinterpret_cast<const sockaddr*>(&source), sizeof(source)) != 0) {
            closeSocket(sender);
            throw std::runtime_error("Не удалось привязать командный UDP-сокет");
        }
        return sender;
    }

    void send(Socket sender, const std::array<std::uint8_t, 128>& bytes)
    {
        const int sent = ::sendto(sender, reinterpret_cast<const char*>(bytes.data()),
            static_cast<int>(bytes.size()), 0,
            reinterpret_cast<const sockaddr*>(&remote), sizeof(remote));
        if (sent != static_cast<int>(bytes.size()))
            throw std::runtime_error("Не удалось отправить ROKT-команду адаптеру УБСИ");
    }

    void prepare(Socket sender, const Checkpoint& checkpoint)
    {
        auto reset = command(0x16);
        auto addrYalk = command(0x14);
        addrYalk[5] = 0x01; addrYalk[6] = 0x2B; addrYalk[7] = 0x01; addrYalk[8] = 0x00;
        auto addrYtp = command(0x15);
        addrYtp[5] = 0x01; addrYtp[6] = 0x01; addrYtp[7] = 0x01; addrYtp[8] = 0x00;
        send(sender, reset);
        sleepChecked(checkpoint, std::chrono::milliseconds(50));
        send(sender, addrYalk);
        sleepChecked(checkpoint, std::chrono::milliseconds(50));
        send(sender, addrYtp);
    }

    void startYalk(std::chrono::milliseconds configureSettle, const Checkpoint& checkpoint)
    {
        openReceiver();
        Socket sender = openSender();
        try {
            prepare(sender, checkpoint);
            sleepChecked(checkpoint, configureSettle);
            auto start = command(0x0A);
            start[5] = 0x00; start[6] = 0x00; start[7] = 0x01; start[8] = 0x00;
            send(sender, start);
            closeSocket(sender);
        } catch (...) {
            closeSocket(sender);
            stop();
            throw;
        }
    }

    void startYtp(unsigned endpointNumber, std::chrono::milliseconds configureSettle,
                  const Checkpoint& checkpoint)
    {
        if (endpointNumber < 1 || endpointNumber > 255)
            throw std::invalid_argument("Номер интерфейса ЯТП должен быть 1..255");
        openReceiver();
        Socket sender = openSender();
        try {
            prepare(sender, checkpoint);
            sleepChecked(checkpoint, configureSettle);
            auto start = command(0x0A);
            start[5] = 0x02;
            start[7] = static_cast<std::uint8_t>(endpointNumber);
            send(sender, start);
            closeSocket(sender);
        } catch (...) {
            closeSocket(sender);
            stop();
            throw;
        }
    }

    std::uint64_t currentSequence()
    {
        std::lock_guard<std::mutex> lock(frameMutex);
        return sequence;
    }

    QueuedFrame waitPayloadAfter(std::size_t requiredSize, std::uint64_t afterSequence,
        std::chrono::milliseconds timeout, const Checkpoint& checkpoint)
    {
        if (receiver == InvalidSocket)
            throw std::runtime_error("Поток адаптера УБСИ не запущен");

        const auto deadline = std::chrono::steady_clock::now() + timeout;
        for (;;) {
            if (checkpoint) checkpoint();

            std::unique_lock<std::mutex> lock(frameMutex);
            const auto found = std::find_if(frames.begin(), frames.end(),
                [requiredSize, afterSequence](const QueuedFrame& frame) {
                    return frame.sequence > afterSequence && frame.bytes.size() == requiredSize;
                });
            if (found != frames.end()) {
                QueuedFrame result = *found;
                frames.erase(frames.begin(), std::next(found));
                return result;
            }
            if (stopping.load() || receiver == InvalidSocket)
                throw std::runtime_error("Поток адаптера УБСИ остановлен");

            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) break;
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
            frameCv.wait_for(lock, std::min(remaining, std::chrono::milliseconds(50)));
        }
        throw FrameTimeout{};
    }

    QueuedFrame waitPayload(std::size_t requiredSize,
        std::chrono::milliseconds timeout, const Checkpoint& checkpoint)
    {
        // Freshness barrier: frames already queued before the measurement call
        // are explicitly excluded. The receiver thread keeps draining UDP during
        // settle delays, so a measurement can no longer consume pre-switch data.
        return waitPayloadAfter(requiredSize, currentSequence(), timeout, checkpoint);
    }

    std::vector<YalkChannelReading> yalkSnapshot(unsigned samples,
        std::chrono::milliseconds timeout, const Checkpoint& checkpoint)
    {
        samples = std::max(1u, samples);
        std::vector<double> raw(100, 0.0), code(100, 0.0);
        std::vector<unsigned> contacts(100, 0);
        std::uint64_t after = currentSequence();
        for (unsigned sample = 0; sample < samples; ++sample) {
            const auto queued = waitPayloadAfter(204, after, timeout, checkpoint);
            after = queued.sequence;
            const auto& frame = queued.bytes;
            for (std::size_t index = 0; index < 100; ++index) {
                const std::size_t offset = 4 + index * 2;
                const std::uint16_t word = static_cast<std::uint16_t>(frame[offset])
                    | (static_cast<std::uint16_t>(frame[offset + 1]) << 8);
                raw[index] += word;
                code[index] += word & 0x03FFu;
                if ((word & 0x0400u) != 0) ++contacts[index];
            }
        }
        std::vector<YalkChannelReading> result(100);
        for (std::size_t index = 0; index < 100; ++index) {
            result[index].rawMean = raw[index] / samples;
            result[index].codeMean = code[index] / samples;
            result[index].contact = contacts[index] * 2 >= samples;
        }
        return result;
    }

    YtpSnapshot ytpSnapshot(unsigned samples, std::chrono::milliseconds timeout,
                            const Checkpoint& checkpoint)
    {
        samples = std::max(1u, samples);
        YtpSnapshot result;
        std::array<unsigned, 30> counts{};
        unsigned count31 = 0, count32 = 0;
        std::uint64_t after = currentSequence();
        for (unsigned sample = 0; sample < samples; ++sample) {
            const auto queued = waitPayloadAfter(68, after, timeout, checkpoint);
            after = queued.sequence;
            const auto& frame = queued.bytes;
            constexpr std::array<std::uint8_t, 4> header{0x01,0x00,0x34,0x00};
            if (!std::equal(header.begin(), header.end(), frame.begin()))
                throw std::runtime_error("Кадр ЯТП ROKT имеет неверный заголовок");
            const auto word = [&frame](std::size_t index) {
                const std::size_t offset = 4 + index * 2;
                return static_cast<std::uint16_t>(frame[offset])
                    | (static_cast<std::uint16_t>(frame[offset + 1]) << 8);
            };
            for (std::size_t index = 0; index < 30; ++index) {
                const auto value = word(index);
                if (value == 0x8000) continue;
                result.channels[index] += value;
                ++counts[index];
            }
            const auto v31 = word(30), v32 = word(31);
            if (v31 != 0x8000) { result.calibration31 += v31; ++count31; }
            if (v32 != 0x8000) { result.calibration32 += v32; ++count32; }
        }
        result.validWordCount = 0;
        for (std::size_t index = 0; index < 30; ++index) {
            if (counts[index]) { result.channels[index] /= counts[index]; ++result.validWordCount; }
            else result.channels[index] = 32768.0;
        }
        if (count31) { result.calibration31 /= count31; ++result.validWordCount; }
        else result.calibration31 = 32768.0;
        if (count32) { result.calibration32 /= count32; ++result.validWordCount; }
        else result.calibration32 = 32768.0;
        return result;
    }

    YtpSnapshot ytpSnapshotSince(std::uint64_t marker, unsigned samples,
                                 std::chrono::milliseconds timeout,
                                 const Checkpoint& checkpoint)
    {
        samples = std::max(1u, samples);
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        std::vector<QueuedFrame> selected;

        for (;;) {
            if (checkpoint) checkpoint();
            {
                std::unique_lock<std::mutex> lock(frameMutex);
                if (receiver == InvalidSocket || stopping.load())
                    throw std::runtime_error("Поток адаптера УБСИ остановлен");

                selected.clear();
                for (auto it = frames.rbegin(); it != frames.rend(); ++it) {
                    if (it->sequence <= marker) break;
                    if (it->bytes.size() != 68) continue;
                    selected.push_back(*it);
                    if (selected.size() == samples) break;
                }
                if (selected.size() == samples) break;

                const auto now = std::chrono::steady_clock::now();
                if (now >= deadline) break;
                const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - now);
                frameCv.wait_for(lock, std::min(remaining, std::chrono::milliseconds(50)));
            }
        }

        if (selected.size() != samples) throw FrameTimeout{};

        // Frames were collected newest-first.  Their order is immaterial for
        // averaging, but restoring chronological order makes the decoding path
        // identical to a regular consecutive snapshot.
        std::reverse(selected.begin(), selected.end());
        YtpSnapshot result;
        std::array<unsigned, 30> counts{};
        unsigned count31 = 0, count32 = 0;
        for (const auto& queued : selected) {
            const auto& frame = queued.bytes;
            constexpr std::array<std::uint8_t, 4> header{0x01,0x00,0x34,0x00};
            if (!std::equal(header.begin(), header.end(), frame.begin()))
                throw std::runtime_error("Кадр ЯТП ROKT имеет неверный заголовок");
            const auto word = [&frame](std::size_t index) {
                const std::size_t offset = 4 + index * 2;
                return static_cast<std::uint16_t>(frame[offset])
                    | (static_cast<std::uint16_t>(frame[offset + 1]) << 8);
            };
            for (std::size_t index = 0; index < 30; ++index) {
                const auto value = word(index);
                if (value == 0x8000) continue;
                result.channels[index] += value;
                ++counts[index];
            }
            const auto v31 = word(30), v32 = word(31);
            if (v31 != 0x8000) { result.calibration31 += v31; ++count31; }
            if (v32 != 0x8000) { result.calibration32 += v32; ++count32; }
        }
        result.validWordCount = 0;
        for (std::size_t index = 0; index < 30; ++index) {
            if (counts[index]) { result.channels[index] /= counts[index]; ++result.validWordCount; }
            else result.channels[index] = 32768.0;
        }
        if (count31) { result.calibration31 /= count31; ++result.validWordCount; }
        else result.calibration31 = 32768.0;
        if (count32) { result.calibration32 /= count32; ++result.validWordCount; }
        else result.calibration32 = 32768.0;
        return result;
    }

    YalkUdpConfig config;
    sockaddr_in remote{};
    sockaddr_in local{};
    Socket receiver = InvalidSocket;
    std::atomic<bool> stopping{true};
    std::thread receiverThread;
    std::mutex frameMutex;
    std::condition_variable frameCv;
    std::deque<QueuedFrame> frames;
    std::uint64_t sequence = 0;
    LiveYalkSink liveSink;
    LiveYtpSink liveYtpSink;
    std::chrono::steady_clock::time_point lastLivePublish{};
    std::chrono::steady_clock::time_point lastYtpLivePublish{};
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
        try {
            impl_->startYalk(std::chrono::milliseconds(0), checkpoint);
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());
            if (remaining.count() <= 0) break;
            impl_->waitPayload(204, std::min(remaining, std::chrono::milliseconds(750)), checkpoint);
            return true;
        } catch (const FrameTimeout&) {
            impl_->stop();
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) break;
        sleepChecked(checkpoint, std::min(remaining, std::chrono::milliseconds(250)));
    }
    impl_->stop();
    return false;
}

bool YalkReferenceLink::waitNextReference(std::chrono::milliseconds timeout,
                                          const Checkpoint& checkpoint)
{
    try {
        impl_->waitPayload(204, timeout, checkpoint);
        return true;
    } catch (const FrameTimeout&) {
        return false;
    }
}

bool YalkReferenceLink::startYalk(std::chrono::milliseconds configureSettle,
                                  std::chrono::milliseconds timeout,
                                  const Checkpoint& checkpoint)
{
    impl_->startYalk(configureSettle, checkpoint);
    try { impl_->waitPayload(204, timeout, checkpoint); return true; }
    catch (...) { impl_->stop(); throw; }
}

std::vector<YalkChannelReading> YalkReferenceLink::readYalkSnapshot(
    unsigned sampleCount, std::chrono::milliseconds timeout, const Checkpoint& checkpoint)
{
    return impl_->yalkSnapshot(sampleCount, timeout, checkpoint);
}

bool YalkReferenceLink::startYtp(unsigned endpoint,
                                 std::chrono::milliseconds configureSettle,
                                 std::chrono::milliseconds streamSettle,
                                 std::chrono::milliseconds timeout,
                                 const Checkpoint& checkpoint)
{
    impl_->startYtp(endpoint, configureSettle, checkpoint);
    sleepChecked(checkpoint, streamSettle);
    try { impl_->waitPayload(68, timeout, checkpoint); return true; }
    catch (...) { impl_->stop(); throw; }
}

YtpSnapshot YalkReferenceLink::readYtpSnapshot(
    unsigned sampleCount, std::chrono::milliseconds timeout, const Checkpoint& checkpoint)
{
    return impl_->ytpSnapshot(sampleCount, timeout, checkpoint);
}

std::uint64_t YalkReferenceLink::markYtpFrames() const
{
    return impl_->currentSequence();
}

YtpSnapshot YalkReferenceLink::readYtpSnapshotSince(
    std::uint64_t marker, unsigned sampleCount, std::chrono::milliseconds timeout,
    const Checkpoint& checkpoint)
{
    return impl_->ytpSnapshotSince(marker, sampleCount, timeout, checkpoint);
}

void YalkReferenceLink::setLiveYalkSink(LiveYalkSink sink)
{
    impl_->setLiveSink(std::move(sink));
}

void YalkReferenceLink::setLiveYtpSink(LiveYtpSink sink)
{
    impl_->setLiveYtpSink(std::move(sink));
}

void YalkReferenceLink::stop() noexcept { impl_->stop(); }

} // namespace tu::hardware
