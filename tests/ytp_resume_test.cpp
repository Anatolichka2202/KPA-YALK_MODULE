#include "hardware/yalk_reference_link.h"

#include <QHostAddress>
#include <QUdpSocket>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {

void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

class ThreadJoiner final {
public:
    explicit ThreadJoiner(std::thread& thread) : thread_(thread) {}
    ~ThreadJoiner() { if (thread_.joinable()) thread_.join(); }
private:
    std::thread& thread_;
};

QByteArray ytpFrame(std::uint16_t sample)
{
    QByteArray frame(68, '\0');
    frame[0] = 0x01;
    frame[2] = 0x34;
    const auto put = [&frame](unsigned index, std::uint16_t value) {
        const unsigned offset = 4 + index * 2;
        frame[static_cast<qsizetype>(offset)] = static_cast<char>(value & 0xffu);
        frame[static_cast<qsizetype>(offset + 1)] = static_cast<char>(value >> 8u);
    };
    for (unsigned channel = 0; channel < 30; ++channel) put(channel, sample);
    put(30, 4000);
    put(31, 330);
    return frame;
}

} // namespace

int main()
{
    try {
        constexpr std::uint16_t port = 18113;
        std::atomic<bool> senderReady{false};
        std::atomic<bool> senderBound{false};
        std::thread sender([&] {
            QUdpSocket socket;
            senderBound.store(socket.bind(QHostAddress("127.0.0.2"), port));
            senderReady.store(true);
            if (!senderBound.load()) return;
            const QByteArray frame = ytpFrame(2171);
            for (unsigned index = 0; index < 80; ++index) {
                socket.writeDatagram(frame, QHostAddress::LocalHost, port);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });
        const ThreadJoiner joinSender(sender);

        while (!senderReady.load()) std::this_thread::yield();
        if (!senderBound.load()) {
            throw std::runtime_error("cannot bind simulated YTP adapter");
        }
        tu::hardware::YalkUdpConfig config;
        config.remoteHost = "127.0.0.2";
        config.localHost = "127.0.0.1";
        config.port = port;
        tu::hardware::YalkReferenceLink link(config);
        std::atomic<unsigned> liveFrames{0};
        std::atomic<double> liveChannel1{0.0};
        link.setLiveYtpSink([&](const tu::hardware::YtpSnapshot& frame, std::uint64_t) {
            liveChannel1.store(frame.channels[0]);
            ++liveFrames;
        });
        require(link.startYtp(1, std::chrono::milliseconds(0), std::chrono::milliseconds(0),
                              std::chrono::milliseconds(300), {}),
                "simulated YTP stream did not start");

        // The sender begins before the marker, just as a real YTP stream is
        // already running before the operator dialog opens.
        const auto marker = link.markYtpFrames();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        sender.join(); // No frame will arrive after the simulated "OK".
        require(liveFrames.load() > 0 && liveChannel1.load() == 2171.0,
                "YTP live sink must receive frames while operator dialog is open");

        const auto snapshot = link.readYtpSnapshotSince(
            marker, 16, std::chrono::milliseconds(100), {});
        require(snapshot.validWordCount == 32, "YTP buffered snapshot is incomplete");
        require(snapshot.channels[0] == 2171.0, "YTP buffered point was not decoded");
        require(snapshot.calibration31 == 4000.0 && snapshot.calibration32 == 330.0,
                "YTP calibration words were not decoded");
        link.stop();
        std::cout << "ytp_resume_test: OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ytp_resume_test: " << error.what() << '\n';
        return 1;
    }
}
