#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace orbita::stand {

struct KtmaUlkUdpConfig {
    std::string remoteHost = "192.168.0.115";
    std::string localHost = "192.168.0.50";
    std::uint16_t port = 1113;
    unsigned receiveTimeoutMs = 800;
    std::size_t queueCapacity = 4096;
};

enum class UlkFrameKind : std::uint8_t {
    Service4,
    Fast120,
    Slow200,
    Unknown,
};

struct UlkFrame {
    std::uint64_t sequence = 0;
    std::int64_t receivedUnixNs = 0;
    UlkFrameKind kind = UlkFrameKind::Unknown;
    std::vector<std::uint8_t> payload;
};

struct UlkStreamStats {
    bool running = false;
    bool streaming = false;
    std::uint64_t lastSequence = 0;
    std::uint64_t service4 = 0;
    std::uint64_t fast120 = 0;
    std::uint64_t slow200 = 0;
    std::uint64_t unknown = 0;
    std::uint64_t dropped = 0;
};

class UlkUdpTransport final {
public:
    explicit UlkUdpTransport(KtmaUlkUdpConfig config);
    ~UlkUdpTransport();
    UlkUdpTransport(const UlkUdpTransport&) = delete;
    UlkUdpTransport& operator=(const UlkUdpTransport&) = delete;

    void start(std::uint8_t mode);
    void stop() noexcept;
    UlkFrame waitFrame(UlkFrameKind kind, std::uint64_t afterSequence,
                       std::chrono::milliseconds timeout);
    std::vector<UlkFrame> takeFrames();
    UlkStreamStats stats() const;
    void startRecord(const std::string& path);
    void stopRecord() noexcept;

    static std::vector<std::uint8_t> modeCommand(std::uint8_t mode);
    static UlkFrameKind classify(std::size_t payloadSize) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

const char* toString(UlkFrameKind kind) noexcept;

} // namespace orbita::stand
